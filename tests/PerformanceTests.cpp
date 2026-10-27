#include "infinite/World.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <oneapi/tbb/global_control.h>
#include <set>
#include <stdexcept>

using namespace infinite;
// The pre-optimisation recipe-3 kernel, retained as an independent equivalence
// oracle and an identically sampled microbenchmark (not a simulated old GPU).
static WebSample referenceWeb(glm::dvec3 p, uint64_t seed) {
    auto level = [](glm::dvec3 p, uint64_t seed) {
        auto base = glm::floor(p);
        std::array<double, 4> nearest{100, 100, 100, 100};
        for (int z = -2; z <= 2; ++z)
            for (int y = -2; y <= 2; ++y)
                for (int x = -2; x <= 2; ++x) {
                    auto c = base + glm::dvec3(x, y, z);
                    uint64_t key = seed;
                    for (int axis = 0; axis < 3; ++axis)
                        key = mix64(key ^ mix64(static_cast<uint64_t>(static_cast<int64_t>(c[axis]))));
                    Random r{key};
                    auto q = c + .5 + glm::dvec3(r.signedUnit(0), r.signedUnit(1), r.signedUnit(2)) * .28;
                    double distance = glm::length(q - p);
                    for (auto& best : nearest)
                        if (distance < best)
                            std::swap(distance, best);
                }
        double a = nearest[1] - nearest[0], b = nearest[2] - nearest[0], c = nearest[3] - nearest[0];
        WebSample out;
        out.sheet = std::exp(-a * a / .0036);
        out.filament = std::exp(-(a * a + b * b) / .009);
        out.knot = std::exp(-(a * a + b * b + c * c) / .0225);
        out.voidDistance = nearest[0];
        out.density = std::clamp(.004 + .12 * out.sheet + .57 * out.filament + .8 * out.knot, 0.0, 1.0);
        return out;
    };
    auto a = level(p, featureSeed(seed, Feature::CosmicWeb, 0));
    auto b = level(p * 2.7 + glm::dvec3(3.1, -1.7, 4.3), featureSeed(seed, Feature::CosmicWeb, 1));
    a.density = std::clamp(a.density * (.72 + .55 * b.density), 0.0, 1.0);
    return a;
}
static size_t checks = 0;
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static std::vector<Address> path(const Universe& u) {
    std::vector<Address> out{Address{}.child(Kind::Galaxy, 0)};
    while (out.back().kind() != Kind::Lattice)
        out.push_back(u.children(u.describe(out.back()), 1).front());
    return out;
}
int main(int argc, char**) {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        Random webRandom{0x12434};
        if (argc > 1) {
            std::cout << "variant,run,samples,elapsed_ms,checksum\n";
            for (unsigned run = 0; run < 5; ++run)
                for (unsigned variant = 0; variant < 2; ++variant) {
                    auto begin = std::chrono::steady_clock::now();
                    double sum = 0;
                    for (unsigned i = 0; i < 12000; ++i) {
                        auto p = glm::dvec3(webRandom.signedUnit(i * 3), webRandom.signedUnit(i * 3 + 1),
                                            webRandom.signedUnit(i * 3 + 2)) *
                                 4.0;
                        sum += (variant ? cosmicStructure(p, 410) : referenceWeb(p, 410)).density;
                    }
                    auto elapsed =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                            .count();
                    std::cout << (variant ? "optimised" : "recipe3_reference") << ',' << run << ",12000,"
                              << elapsed << ',' << sum << '\n';
                }
            return 0;
        }
        for (unsigned i = 0; i < 1000; ++i) {
            auto p = glm::dvec3(webRandom.signedUnit(i * 3), webRandom.signedUnit(i * 3 + 1),
                                webRandom.signedUnit(i * 3 + 2)) *
                     12.0;
            auto a = referenceWeb(p, 410), b = cosmicStructure(p, 410);
            check(a.density == b.density && a.sheet == b.sheet && a.filament == b.filament &&
                      a.knot == b.knot,
                  "bounded cellular search changed the cosmic density recipe");
        }
        auto addresses = path(u);
        GeometryGenerator generator(u);
        // A prepared particle transform must cancel the addressed star before
        // the 30-decade scale multiplication, just like the authoritative path.
        auto galaxy = addresses[0], system = addresses[1];
        auto frames = u.frames(galaxy), target = u.frames(system);
        auto transform = pointTransform(frames, galaxy, target, system, -10);
        check(transform(target.back().position) == glm::dvec3(0),
              "batch transform lost distant-star cancellation");
        for (const auto& source : addresses)
            for (const auto& anchor : addresses) {
                double log = u.describe(anchor).frame.logRadius;
                auto sourceFrames = u.frames(source), anchorFrames = u.frames(anchor);
                auto mapping = pointTransform(sourceFrames, source, anchorFrames, anchor, log);
                for (auto p : {glm::dvec3(0), glm::dvec3(.2, -.3, .1), glm::dvec3(-.8, .5, .7)}) {
                    auto exact = relativePoint(sourceFrames, source, p, anchorFrames, anchor, log),
                         fast = mapping(p);
                    check(glm::length(exact - fast) < 1e-12 * std::max(1.0, glm::length(exact)),
                          "prepared transform diverges from hierarchical coordinates");
                }
            }
        auto rock = u.describe(addresses[5]), grain = u.describe(addresses[6]);
        Random random{0x3344};
        for (unsigned type = 0; type < 7; ++type) {
            auto n = rock;
            n.rock.type = static_cast<RockType>(type);
            auto field = densityField(u, n);
            for (unsigned i = 0; i < 200; ++i) {
                auto p = glm::dvec3(random.signedUnit(i * 3), random.signedUnit(i * 3 + 1),
                                    random.signedUnit(i * 3 + 2)) *
                         1.2;
                check(std::abs(field(p) - u.rockSdf(n, p)) < 1e-13,
                      "prepared lithology changed its physical density");
            }
        }
        auto grainField = densityField(u, grain);
        for (unsigned i = 0; i < 300; ++i) {
            auto p = glm::dvec3(random.signedUnit(i * 3), random.signedUnit(i * 3 + 1),
                                random.signedUnit(i * 3 + 2));
            check(std::abs(grainField(p) - u.grainSdf(grain, p)) < 1e-12, "prepared grain lost host density");
        }
        for (auto kind : {Kind::Cosmic, Kind::Galaxy, Kind::Planet, Kind::Surface, Kind::Mountain, Kind::Rock,
                          Kind::Grain, Kind::Lattice}) {
            unsigned last = 0;
            for (double pixels = 1; pixels < 20000; pixels *= 1.1) {
                unsigned detail = screenDetail(kind, pixels, last);
                check(detail >= last && detail == geometryDetail(kind, detail),
                      "screen-error refinement is not monotonic/bounded");

                last = detail;
            }
        }
        for (unsigned level = 0; level < 3; ++level) {
            double boundary = 80 * std::pow(2.0, level);
            check(screenDetail(Kind::Rock, boundary * 1.01, level) == level &&
                      screenDetail(Kind::Rock, boundary * .99, level + 1) == level + 1,
                  "screen-error threshold chatters");
        }
        check(screenDetail(Kind::Rock, 900, 0) == 3 && screenDetail(Kind::Surface, 900, 0) == 4,
              "near views do not expose the additional resolution");
        for (unsigned face = 0; face < 6; ++face) {
            auto direction = tileDirection({static_cast<int>(face), 0, 0, 0});
            check(terrainView(direction) == face + 1, "terrain view key chose the wrong visible hemisphere");
            auto n = u.describe(addresses[2]);
            auto g = generator.generate(n, 1, nullptr, face + 1);
            check(g->viewKey == face + 1 && g->address == n.address &&
                      g->descriptor == descriptorFingerprint(n),
                  "camera-adaptive terrain changed the planet identity");
        }
        auto mesh = generator.generate(rock, 2);
        auto surface = generator.generate(u.describe(addresses[3]), 4);
        check(surface->solid.vertices.size() > 37000 && surface->solid.bytes() < 1100 * 1024,
              "fine surface resolution did not increase within its compact memory budget");
        auto region = u.describe(addresses[3]);
        auto section = generator.section(u.describe(addresses[2]), region.address,
                                         sectionBucket(region.frame.logRadius + .5));
        check(section->coveredHalfExtent.x > 0 && section->coveredHalfExtent.y > 0,
              "coarse terrain context did not exclude its finer patch");
        for (size_t i = 0; i < section->solid.indexCount(); i += 3) {
            glm::dvec3 centre(0);
            for (unsigned j = 0; j < 3; ++j) {
                auto v = section->solid.vertices[section->solid.index(i + j)];
                centre += glm::dvec3(v.x, v.y, v.z) / 3.0;
            }
            check(std::abs(centre.x) >= section->coveredHalfExtent.x - 1e-6 ||
                      std::abs(centre.z) >= section->coveredHalfExtent.y - 1e-6,
                  "coarse context still overlaps the fine terrain interior");
        }
        check(mesh->solid.vertices.size() < mesh->solid.indexCount() / 2,
              "SDF mesh still duplicates triangle vertices");
        std::map<std::pair<uint32_t, uint32_t>, unsigned> edges;
        for (size_t i = 0; i < mesh->solid.indexCount(); i += 3)
            for (unsigned j = 0; j < 3; ++j) {
                auto a = mesh->solid.index(i + j), b = mesh->solid.index(i + (j + 1) % 3);
                check(a < mesh->solid.vertices.size() && b < mesh->solid.vertices.size(),
                      "indexed SDF edge out of bounds");
                if (a > b)
                    std::swap(a, b);
                ++edges[{a, b}];
            }
        for (auto [edge, count] : edges)
            check(count == 2, "indexed SDF introduced a hole or non-manifold edge");
        check(!mesh->solid.wideIndices(), "small mesh did not use compact indices");
        Mesh large;
        large.vertices.resize(65537);
        large.indices = {0, 65536, 1};
        large.compact();
        check(large.wideIndices() && large.index(1) == 65536, "large mesh index was truncated");
        auto lattice = u.describe(addresses.back());
        auto coarse = generator.generate(lattice, 0), fine = generator.generate(lattice, 2);
        check(fine->lattice.atoms.size() > coarse->lattice.atoms.size(),
              "larger lattice window adds no observable sites");
        std::map<uint64_t, Atom> sites;
        for (auto atom : fine->lattice.atoms)
            sites.emplace(atom.siteId, atom);
        for (auto atom : coarse->lattice.atoms)
            check(sites.at(atom.siteId).position == atom.position, "atomic LOD replaced an existing site");
        check(sizeof(Geometry::Instance) == 80 && fine->atomInstances.size() == fine->lattice.atoms.size(),
              "GPU atom instance packing mismatch");
        for (size_t i = 0; i < fine->atomInstances.size(); ++i) {
            auto& atom = fine->lattice.atoms[i];
            auto& instance = fine->atomInstances[i];
            check(glm::vec3(instance.transform[3]) == atom.position &&
                      instance.transform[0][0] == atom.radius,
                  "cached GPU instance diverges from its atomic site");
        }
        std::atomic<bool> cancelled{true};
        for (auto a : addresses)
            check(!generator.generate(u.describe(a), 4, &cancelled), "cancelled mesh was published");
        check(generateLattice(lattice, {-8, 8}, &cancelled).atoms.empty(),
              "cancelled lattice still generated a specimen");
        {
            RegionCache cache(u, 4 * 1024 * 1024, 1);
            cache.beginFrame();
            cache.request(u.describe(Address{}), 2, 8);
            cache.finishFrame();
            cache.beginFrame();
            cache.request(rock, 0, 0);
            cache.finishFrame();
            cache.beginFrame();
            cache.request(rock, 0, 0);
            cache.finishFrame();
            cache.wait();
            cache.beginFrame();
            auto ready = cache.request(rock, 0, 0);
            check(ready && ready->address == rock.address, "obsolete job starved the new focus");
            check(cache.stats().superseded >= 1 && cache.stats().cancelled >= 1,
                  "stale running task was not cancelled");
            check(cache.stats().peakPending <= 1 && cache.stats().bytes <= 4 * 1024 * 1024,
                  "worker/memory admission exceeded its bound");
        }
        {
            RegionCache cache(u, 700 * 1024, 2);
            for (unsigned i = 0; i < 5; ++i) {
                cache.beginFrame();
                cache.request(u.describe(Address{}.child(Kind::Galaxy, i)), 0);
                cache.wait();
                check(cache.stats().bytes <= 700 * 1024, "cache eviction ignored resident capacity");
            }
            check(cache.stats().evicted > 0, "cache never evicted old geometry");
            cache.clear();
            cache.beginFrame();
            auto parent = u.describe(addresses[1]);
            check(!cache.catalog(parent), "catalog generated synchronously on first request");
            cache.wait();
            cache.beginFrame();
            auto catalog = cache.catalog(parent);
            check(catalog && catalog->size() == u.children(parent).size(), "asynchronous catalog incomplete");
            for (auto& node : *catalog)
                check(node.seed == seedOf(node.address, u.seed()), "catalog rerolled child identity");
        }
        std::cout << "PASS: " << checks
                  << " performance invariants; prepared fields/transforms, adaptive LOD, indexed topology, "
                     "instance identity, cancellation, admission and eviction.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
