#include "infinite/Geometry.hpp"
#include "infinite/StellarEvolution.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace infinite;
static uint64_t legacyGeometryFingerprint(const Node& node, const std::shared_ptr<Geometry>& geometry) {
    if (!geometry)
        return 0;
    if (node.address.kind() == Kind::Galaxy)
        std::erase_if(geometry->particles,
                      [](const auto& p) { return p.objectId != UINT64_MAX && isDiverseStar(p.objectId); });
    return geometryFingerprint(*geometry);
}
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: infinite_population_snapshot --write|--check FILE");
        if (std::string_view(argv[1]) == "--write") {
            std::ofstream out(argv[2]);
            for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
                Universe u(seed);
                GeometryGenerator generator(u);
                auto record = [&](Address address, bool mesh) {
                    const auto n = u.describe(address);
                    out << seed << ' ' << address.str() << ' ' << descriptorFingerprint(n) << ' '
                        << (mesh ? legacyGeometryFingerprint(n, generator.generate(n, 0)) : 0) << '\n';
                };
                const auto galaxy = Address{}.child(Kind::Galaxy, 0);
                record(galaxy, true);
                for (uint64_t star : {0, 9, 42}) {
                    const auto system = galaxy.child(Kind::System, star);
                    record(system, false);
                    const auto host = u.describe(system);
                    for (uint64_t i = 0; i < 5 + host.seed % 5; i++)
                        record(system.child(Kind::Planet, i), i == 0);
                    auto a = system.child(Kind::Planet, 0);
                    while (a.kind() != Kind::Lattice) {
                        a = u.children(u.describe(a), 1).front();
                        record(a, true);
                    }
                }
            }
            if (!out)
                throw std::runtime_error("Snapshot write failed");
            std::cout << "Legacy world snapshot written\n";
            return 0;
        }
        std::ifstream in(argv[2]);
        uint64_t seed, descriptor, geometry;
        std::string address;
        size_t count = 0;
        while (in >> seed >> address >> descriptor >> geometry) {
            Universe u(seed);
            const auto node = u.describe(Address::parse(address));
            auto generated = geometry ? GeometryGenerator(u).generate(node, 0) : nullptr;
            if (descriptorFingerprint(node) != descriptor ||
                (geometry && legacyGeometryFingerprint(node, generated) != geometry))
                throw std::runtime_error("Legacy world changed: " + address + " seed " +
                                         std::to_string(seed));
            ++count;
        }
        if (!in.eof() || count < 90)
            throw std::runtime_error("Incomplete legacy population snapshot");
        std::cout << "PASS: " << count
                  << " pre-expansion descriptors and geometry fingerprints remain exact\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
