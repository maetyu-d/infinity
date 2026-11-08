#include "infinity/Geometry.hpp"
#include <algorithm>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace infinity;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static double residual(const Universe& u, const Node& n, const Geometry& g) {
    double maximum = 0;
    for (const auto& v : g.solid.vertices) {
        const double value = u.grainSdf(n, {v.x, v.y, v.z});
        check(std::isfinite(value), "Micro surface generated non-finite field samples");
        maximum = std::max(maximum, std::abs(value));
    }
    for (size_t i = 0; i < g.solid.indexCount(); ++i)
        check(g.solid.index(i) < g.solid.vertices.size(), "Micro surface has an invalid triangle index");
    return maximum;
}
int main() {
    try {
        Universe u;
        GeometryGenerator generator(u);
        const auto rock = Address::parse("0,0,0/1:0/2:9/3:0/4:61516819435380/5:4/6:4611686018427387905");
        const auto grain = u.describe(u.children(u.describe(rock), 1).front());
        const auto descriptor = descriptorFingerprint(grain);
        check(descriptor == 16381549922156961208ULL, "Micro fix changed the natural grain descriptor");
        check(geometryFingerprint(*generator.generate(grain, 0)) == 13767820425686152446ULL,
              "Micro fix changed the retained coarse grain geometry");
        for (unsigned lod : {1u, 2u, 3u}) {
            const auto mesh = generator.generate(grain, lod);
            check(mesh && !mesh->solid.vertices.empty(), "Micro grain mesh is empty");
            const double error = residual(u, grain, *mesh), cell = 2.6 / (8u << lod);
            check(error < cell * 5e-5, "Fine grain vertices leave the true crystal/host interface");
            const auto digest = geometryFingerprint(*mesh);
            check(digest == geometryFingerprint(*generator.generate(grain, lod)),
                  "Micro meshing depends on request order");
            std::cout << "MICRO_MESH lod=" << lod << " vertices=" << mesh->solid.vertices.size()
                      << " triangles=" << mesh->solid.indexCount() / 3 << " max_field_error=" << error
                      << " error_metres=" << error * std::pow(10., grain.frame.logRadius) << '\n';
        }
        // Intersecting habits for all five existing endmembers, evaluated against
        // their authoritative density rather than a second meshing implementation.
        for (auto mineral :
             {Mineral::Quartz, Mineral::Iron, Mineral::Olivine, Mineral::Ice, Mineral::Graphite}) {
            auto habit = grain;
            habit.hostRockSeed = 0;
            habit.environment.mineral = mineral;
            habit.crystal.axes = {.71, 1.13, .93};
            const auto mesh = generator.generate(habit, 2);
            check(mesh && !mesh->solid.vertices.empty(), "Crystal habit disappeared");
            check(residual(u, habit, *mesh) < (2.6 / 32) * 5e-5, "Crystal facet intersection is inaccurate");
        }
        auto concurrent = std::async(std::launch::async, [&] {
            Universe again;
            return geometryFingerprint(*GeometryGenerator(again).generate(again.describe(grain.address), 2));
        });
        check(concurrent.get() == geometryFingerprint(*generator.generate(grain, 2)),
              "Threaded micro generation changed geometry");
        std::atomic<bool> cancelled{true};
        check(!generator.generate(grain, 3, &cancelled), "Cancelled micro generation published a mesh");
        auto address = grain.address;
        while (address.kind() != Kind::Lattice)
            address = u.children(u.describe(address), 1).front();
        const auto atoms = geometryFingerprint(*generator.generate(u.describe(address), 1));
        Universe returned;
        check(atoms == geometryFingerprint(
                           *GeometryGenerator(returned).generate(returned.describe(address), 1)) &&
                  descriptorFingerprint(returned.describe(grain.address)) == descriptor,
              "Micro-to-atomic return changed the grain or its atoms");
        std::cout << "PASS micro facet precision, all mineral habits, retained coarse geometry, cancellation "
                     "and atomic identity\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
