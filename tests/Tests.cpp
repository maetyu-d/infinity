#include "infinite/Navigation.hpp"
#include "infinite/World.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <oneapi/tbb/parallel_for.h>
#include <stdexcept>

using namespace infinite;
static int checks = 0;
static void require(bool ok, const char* what) {
    ++checks;
    if (!ok)
        throw std::runtime_error(what);
}
static bool close(glm::dvec3 a, glm::dvec3 b, double eps = 1e-10) {
    return glm::length(a - b) < eps;
}
static std::vector<Address> chain(const Universe& u) {
    std::vector<Address> result{Address{}.child(Kind::Galaxy, 0)};
    while (result.back().kind() != Kind::Lattice) {
        auto c = u.children(u.describe(result.back()), 1);
        result.push_back(c.front());
    }
    return result;
}
int main(int argc, char** argv) {
    try {
        Universe u;
        auto path = chain(u);
        auto lattice = path.back();
        if (argc == 3) {
            Persistence db(argv[2], u.seed());
            if (std::string(argv[1]) == "--write") {
                db.mark(lattice, "restart verified");
                db.saveState(lattice.str());
            } else {
                require(db.marker(lattice) == std::optional<std::string>("restart verified"),
                        "process restart marker");
                require(db.loadState() == std::optional<std::string>(lattice.str()),
                        "process restart saved address");
            }
            return 0;
        }
        require(mix64(0) == 0xe220a8397b1dcdafULL, "counter hash golden vector");
        require(seedOf(lattice) == seedOf(Address::parse(lattice.str())), "address/seed round trip");
        Address negative{{INT64_MIN, INT64_MAX, -1}, {}};
        require(Address::parse(negative.str()) == negative, "signed integer cell extremes");
        require(lattice.parent().ancestorOf(lattice), "parent identity");
        require(seedOf(lattice) != seedOf(lattice.parent().child(Kind::Lattice, 1)),
                "hierarchical seed separation");
        bool invalid = false;
        try {
            Address::parse("0,0,0/99:1");
        } catch (...) {
            invalid = true;
        }
        require(invalid, "invalid kind rejected");
        std::vector<uint64_t> seeds;
        for (auto& a : path)
            seeds.push_back(u.describe(a).seed);
        std::reverse(path.begin(), path.end());
        for (size_t i = 0; i < path.size(); ++i)
            require(u.describe(path[i]).seed == seeds[seeds.size() - 1 - i], "generation order independence");
        std::reverse(path.begin(), path.end());
        for (auto& a : path) {
            auto first = u.describe(a), again = u.describe(a);
            require(first.frame.logRadius == again.frame.logRadius &&
                        first.frame.position == again.frame.position &&
                        first.environment.mineral == again.environment.mineral,
                    "descriptor determinism");
            require(first.seed == seedOf(a), "same addressing seed in generator");
        }
        auto planet = u.describe(path[2]);
        for (int i = 0; i <= 32; ++i) {
            double v = static_cast<double>(i) / 16 - 1;
            auto a = cubeDirection(0, -1, v), b = cubeDirection(4, 1, v);
            require(close(a, b), "cube face shared edge direction");
            require(u.terrainHeight(planet, a) == u.terrainHeight(planet, b), "cube face shared elevation");
        }
        Tile left{4, 5, 10, 12}, right{4, 5, 11, 12};
        for (int i = 0; i <= 20; ++i) {
            double v = static_cast<double>(i) / 20;
            auto a = tileDirection(left, 1, v), b = tileDirection(right, 0, v);
            require(close(a, b, 1e-14), "tile seam");
            require(u.terrainHeight(planet, a) == u.terrainHeight(planet, b), "continuous tile density");
        }
        auto parentTile = Tile{2, 3, 2, 4}, childTile = Tile{2, 4, 4, 8};
        require(close(tileDirection(parentTile, .25, .25), tileDirection(childTile, .5, .5), 1e-14),
                "LOD samples preserve field");
        auto pose = u.relative(lattice, lattice, -10);
        require(pose.position == glm::dvec3(0) && std::isfinite(pose.scale), "atomic local origin");
        auto leaf = u.describe(lattice);
        Address sib = lattice.parent().child(Kind::Lattice, 1);
        auto sibPose = u.relative(sib, lattice, -10);
        require(std::isfinite(glm::length(sibPose.position)), "atomic sibling frame");
        Address huge = lattice;
        huge.cell = {INT64_MAX, INT64_MIN, 1000000000000LL};
        auto hugePose = u.relative(huge, huge, -10);
        require(hugePose.position == glm::dvec3(0), "huge root integer cell does not lose local origin");
        // A synthetic chain with a 10^26 m ancestor and a 10^-10 m child.
        Address p = Address{}.child(Kind::Galaxy, 1), a = p.child(Kind::System, 0),
                b = p.child(Kind::System, 1);
        std::vector<LocalFrame> fa{
            {26, {}, {}}, {20, {.73, .2, .1}, {1, 0, 0, 0}}, {-10, {1e-30, 0, 0}, {1, 0, 0, 0}}};
        auto fb = fa;
        fb.back().position.x = 2e-30;
        auto tiny = relativePose(fb, b, fa, a, -10);
        require(std::abs(tiny.position.x - 1) < 1e-14,
                "shared ancestor translation excluded at 36-decade span");
        for (double log = -10; log <= 26; log += .5) {
            auto p = u.relative(lattice, lattice, log);
            require(std::isfinite(p.scale) && p.scale > 0, "finite logarithmic transforms");
        }
        auto system = u.describe(path[1]);
        auto orbit = planet.orbit;
        double period = 2 * std::numbers::pi *
                        std::sqrt(std::pow(orbit.semiMajorMetres, 3) / (1.32712440018e20 * system.massSolar));
        require(close(keplerPosition(orbit, 0) / orbit.semiMajorMetres,
                      keplerPosition(orbit, period) / orbit.semiMajorMetres, 1e-12),
                "Kepler period");
        auto grain = u.describe(path[6]);
        require(leaf.environment.mineral == grain.environment.mineral, "grain identity determines lattice");
        require(leaf.environment.chemistry.silicate == grain.environment.chemistry.silicate,
                "chemistry inherited into lattice");
        auto atoms = generateLattice(leaf), atoms2 = generateLattice(leaf);
        require(!atoms.atoms.empty() && !atoms.bonds.empty(), "atomic sites and bonds exist");
        require(atoms.atoms.size() == atoms2.atoms.size() && atoms.bonds.size() == atoms2.bonds.size(),
                "defect reproducibility");
        for (auto m : {Mineral::Quartz, Mineral::Iron, Mineral::Olivine, Mineral::Ice, Mineral::Graphite}) {
            auto n = leaf;
            n.environment.mineral = m;
            n.frame.logRadius = std::log10(material(m).cellMetres * 6);
            auto l = generateLattice(n);
            require(l.atoms.size() > 30 && l.bonds.size() > 10, "each material lattice has sites and edges");
        }
        GeometryGenerator gen(u);
        for (const auto& address : path) {
            auto geometry = gen.generate(u.describe(address), 0);
            bool finite = true;
            for (const auto* mesh : {&geometry->solid, &geometry->lines}) {
                for (const auto& v : mesh->vertices)
                    finite = finite && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
                for (size_t i = 0; i < mesh->indexCount(); ++i)
                    finite = finite && mesh->index(i) < mesh->vertices.size();
            }
            require(finite, "every scale has finite, valid mesh buffers");
        }
        std::array<uint64_t, 3> serial{}, parallel{};
        std::array<Address, 3> selected{path[0], path[4], path[5]};
        for (size_t i = 0; i < 3; ++i)
            serial[i] = geometryFingerprint(*gen.generate(u.describe(selected[i]), 1));
        oneapi::tbb::parallel_for(size_t(0), size_t(3), [&](size_t i) {
            parallel[i] = geometryFingerprint(*gen.generate(u.describe(selected[i]), 1));
        });
        require(serial == parallel, "thread scheduling independence of geometry");
        {
            RegionCache cache(u, 5 * 1024 * 1024, 1);
            cache.beginFrame();
            cache.request(u.describe(path[5]), 1);
            cache.wait();
            cache.beginFrame();
            auto mesh = cache.request(u.describe(path[5]), 1);
            require(mesh && geometryFingerprint(*mesh) == serial[2], "async result");
            cache.clear();
            cache.beginFrame();
            cache.request(u.describe(path[5]), 1);
            cache.wait();
            cache.beginFrame();
            mesh = cache.request(u.describe(path[5]), 1);
            require(mesh && geometryFingerprint(*mesh) == serial[2], "cache eviction/regeneration");
            require(cache.stats().bytes <= 5 * 1024 * 1024, "bounded cache");
        }
        {
            RegionCache cache(u, 64 * 1024, 1);
            cache.beginFrame();
            cache.request(u.describe(path[0]), 4);
            cache.wait();
            require(cache.stats().bytes <= 64 * 1024 && cache.stats().cancelled >= 1,
                    "oversize results rejected");
        }
        std::atomic<bool> cancelled{true};
        require(!gen.generate(u.describe(path[5]), 4, &cancelled), "obsolete task cancellation");
        Navigation nav(u);
        auto initial = nav.anchor;
        nav.focus();
        for (int i = 0; i < 360; ++i)
            nav.update(1.0 / 60);
        require(!nav.transitioning && nav.anchor == path[1], "smooth focus reaches selected child");
        nav.parent();
        for (int i = 0; i < 360; ++i)
            nav.update(1.0 / 60);
        require(nav.anchor == initial, "parent return identity");
        auto saved = nav.serialize();
        Navigation restored(u);
        require(restored.restore(saved) && restored.serialize() == saved, "camera persistence serialization");
        require(!restored.restore("bad state"), "corrupt saved state rejected");
        auto file = std::filesystem::temp_directory_path() / "infinite-regression.sqlite";
        std::filesystem::remove(file);
        {
            Persistence db(file.string(), u.seed());
            db.mark(lattice, "mineral specimen: SiO2");
            db.saveState(saved);
            require(db.markerCount() == 1, "sparse marker count");
            db.mark(lattice, "revised marker");
            require(db.markerCount() == 1, "marker upsert");
        }
        {
            Persistence db(file.string(), u.seed());
            require(db.marker(lattice) == std::optional<std::string>("revised marker"),
                    "compressed modification survives reopen");
            require(db.loadState() == std::optional<std::string>(saved), "saved camera survives reopen");
            Persistence other(file.string(), 123);
            require(!other.marker(lattice), "universe seed namespaces persistence");
        }
        std::filesystem::remove(file);
        require(LOD::detail(20) < LOD::detail(800), "screen error refines LOD");
        require(!LOD::visible({0, 0, 4}, .1, 1.6, 1), "behind-camera culling");
        std::cout << "PASS: " << checks
                  << " checks; determinism, order, boundaries, coordinates, identity, LOD, cache, "
                     "persistence, cancellation.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
