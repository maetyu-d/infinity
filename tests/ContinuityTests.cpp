#include "infinite/Continuity.hpp"
#include "infinite/Navigation.hpp"
#include "infinite/World.hpp"
#include <algorithm>
#include <bit>
#include <filesystem>
#include <iostream>
#include <map>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static void near(glm::dvec3 a, glm::dvec3 b, double eps, const char* message) {
    check(glm::length(a - b) <= eps, message);
}
template <class F> static void rejects(F&& f, const char* message) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, message);
}
struct View {
    Address address;
    std::optional<Address> selected;
    double log;
    glm::dquat q;
    glm::dvec3 offset;
    explicit View(const Navigation& n)
        : address(n.anchor), selected(n.selected), log(n.logDistance), q(n.orientation), offset(n.offset) {}
    void compare(const Navigation& n) const {
        check(address == n.anchor && selected == n.selected, "round trip changed the selected branch");
        check(log == n.logDistance && q == n.orientation && offset == n.offset,
              "round trip did not restore exact local camera state");
    }
};
static void settle(Navigation& n) {
    double lo = std::min(n.logDistance, n.targetLogDistance),
           hi = std::max(n.logDistance, n.targetLogDistance);
    int steps = 0;
    while (n.transitioning && steps++ < 2400) {
        n.update(1.0 / 120);
        check(std::isfinite(glm::length(n.cameraPosition())) &&
                  std::abs(glm::length(n.orientation) - 1) < 1e-12,
              "non-finite camera during scale transition");
        check(n.logDistance >= lo - 1e-12 && n.logDistance <= hi + 1e-12,
              "focus overshot its logarithmic scale interval");
    }
    check(!n.transitioning, "scale transition failed to settle");
}
static void checkRebase(Navigation& n, const Universe& u, Address child) {
    auto old = n.anchor;
    auto oldEye = n.cameraPosition();
    auto oldQ = n.orientation;
    double log = n.logDistance;
    auto before = glm::inverse(oldQ) * (u.relative(child, old, log).position - oldEye);
    n.focus(child);
    auto pose = u.relative(old, child, log);
    near(n.cameraPosition(), pose.position + pose.orientation * oldEye, 2e-12,
         "reference-frame change teleported the camera");
    auto after = glm::inverse(n.orientation) * (u.relative(child, child, log).position - n.cameraPosition());
    near(before, after, 3e-12, "reference-frame change moved the focused object on screen");
    check(std::abs(glm::dot(n.orientation, glm::normalize(pose.orientation * oldQ))) > 1 - 1e-13,
          "reference-frame change rotated the camera");
    settle(n);
}
static void startAt(Navigation& nav, const Universe& u, const Address& address) {
    std::ostringstream state;
    state.precision(17);
    state << address.str() << ' ' << u.describe(address).frame.logRadius + .55 << " 1 0 0 0 .07 -.03 .04";
    check(nav.restore(state.str()), "legacy camera state failed to migrate");
    nav.rotate(21, -12);
    nav.move({.2, .1, -.3}, .15, false);
}
static std::vector<View> descend(Navigation& nav, const Universe& u) {
    std::vector<View> path;
    while (nav.anchor.kind() != Kind::Lattice) {
        auto children = u.children(u.describe(nav.anchor));
        check(!children.empty(), "missing refinement child");
        size_t index = children.size() == 1
                           ? 0
                           : (static_cast<size_t>(nav.anchor.kind()) * 7 + 3) % (children.size() - 1) + 1;
        nav.select(children[index]);
        path.emplace_back(nav);
        checkRebase(nav, u, *nav.selected);
    }
    path.emplace_back(nav);
    return path;
}
static uint64_t sectionHash(const Universe& u, const Navigation& nav) {
    if (nav.anchor.kind() < Kind::Surface)
        return 0;
    auto parent = u.describe(nav.anchor.parent());
    int bucket = sectionBucket(nav.logDistance);
    auto g = GeometryGenerator(u).section(parent, nav.anchor, bucket);
    check(g->section && g->address == parent.address && g->referenceFrame == nav.anchor &&
              g->seed == parent.seed && g->descriptor == descriptorFingerprint(parent),
          "local section replaced its enclosing object");
    check(!g->solid.vertices.empty(), "local material section is empty");
    check(g->inspectionRadius == 0 || g->inspectionRadius * std::pow(10.0, g->logUnits - nav.logDistance) >
                                          glm::length(nav.cameraPosition()),
          "inspection section occludes the focused specimen");
    return geometryFingerprint(*g);
}
static void restartMode(const char* mode, const std::string& file) {
    Universe u(0xdecafbad1234ULL);
    Navigation expected(u);
    Address galaxy{{INT64_MAX, -31, INT64_MIN}, {{Kind::Galaxy, 7}}};
    startAt(expected, u, galaxy);
    auto views = descend(expected, u);
    Persistence db(file, u.seed());
    if (std::string(mode) == "--write") {
        db.saveState(expected.serialize());
        for (const auto& v : views)
            db.mark(v.address, std::to_string(proveObject(u, v.address).digest()));
    } else {
        Navigation restored(u);
        auto state = db.loadState();
        check(state && restored.restore(*state), "restart lost camera or ancestry");
        for (size_t i = views.size(); i-- > 0;) {
            views[i].compare(restored);
            check(db.marker(restored.anchor) ==
                      std::optional<std::string>(std::to_string(proveObject(u, restored.anchor).digest())),
                  "restart changed descriptor, materials, atomic sites or geometry");
            if (i) {
                restored.parent();
                settle(restored);
            }
        }
    }
}
int main(int argc, char** argv) {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        if (argc == 3) {
            restartMode(argv[1], argv[2]);
            std::cout << "PASS: persisted full hierarchy " << argv[1] << "\n";
            return 0;
        }
        for (auto spelling :
             {"0,0,0/", "0,0,0/1:0/1:1", "00,0,0", "0,0,0/1:0/2", "0,0,0/1:0/2:0/3:0/4:18446744073709551615"})
            rejects([&] { Address::parse(spelling); }, "non-canonical address accepted");
        rejects([] { decodeTile(encodeTile({4, 8, 12, 13}) | (uint64_t(1) << 63)); }, "tile alias accepted");
        for (auto boundary : {20., 80., 250., 700.}) {
            unsigned low = LOD::detail(boundary - .01);
            check(LOD::stableDetail(boundary + .01, low) == low, "LOD threshold chatter");
            check(LOD::stableDetail(boundary - .01, low + 1) == low + 1, "reverse LOD threshold chatter");
        }
        Address ancestor = Address{}.child(Kind::Galaxy, 0), star = ancestor.child(Kind::System, 17);
        std::vector<LocalFrame> a{{26, {}, {}}, {20, {.7, .8, .9}, {1, 0, 0, 0}}}, b = a;
        b.push_back({12, {.123456789123, .22, .33}, {1, 0, 0, 0}});
        near(relativePoint(a, ancestor, b.back().position, b, star, -10), {}, 0,
             "addressed distant star failed to cancel before scaling");
        auto sibling = star.parent().child(Kind::System, 18);
        auto c = b;
        c.back().position.x += 1e-12;
        auto got = relativePose(c, sibling, b, star, 0);
        near(got.position, {(c.back().position.x - b.back().position.x) * 1e20, 0, 0}, 1e-8,
             "first divergent translations lost precision");
        Universe u;
        GeometryGenerator generator(u);
        constexpr std::array<uint64_t, 8> legacySeeds{
            0x8fc70fc430282e0aULL, 0xb4b6ffad680a814fULL, 0x8bae562380fa887cULL, 0x3da02065c550839eULL,
            0xd95de5e2544d639cULL, 0x00c78683bd08d6d9ULL, 0x35a084c442aaa31cULL, 0x8bbb59397da7e8d0ULL};
        auto legacyAddress = Address{};
        for (unsigned i = 0; i < legacySeeds.size(); ++i) {
            legacyAddress = legacyAddress.child(static_cast<Kind>(i + 1), i == 3 ? 115964125760ULL : 0);
            check(u.describe(legacyAddress).seed == legacySeeds[i],
                  "continuity changes rerolled the existing hierarchical seeds");
        }
        auto galaxy = u.describe(ancestor);
        auto stars = generator.generate(galaxy, 0), moreStars = generator.generate(galaxy, 4);
        for (size_t i = 0; i < 192; ++i) {
            auto sample = u.starSample(galaxy, i);
            auto node = u.describe(ancestor.child(Kind::System, i));
            check(stars->particles[i].objectId == i && stars->particles[i].position == sample.position &&
                      moreStars->particles[i].position == sample.position &&
                      node.frame.position == sample.position,
                  "galaxy sample is not the same addressed system");
            check(sample.color == stellarColor(node.temperatureK),
                  "stellar colour changed across representations");
        }
        // Verify frame transport across the polar tangent-basis seam.
        auto planetAddress = ancestor.child(Kind::System, 17).child(Kind::Planet, 0);
        auto surfaceAddress = planetAddress.child(Kind::Surface, encodeTile({2, 8, 169, 127}));
        auto surface = u.describe(surfaceAddress);
        for (uint64_t id = 0; id < 40; ++id) {
            auto mountain = u.describe(surfaceAddress.child(Kind::Mountain, id));
            check(std::abs(mountain.frame.orientation.w) > .999, "terrain frame flipped at a pole");
            near(mountain.terrainOrientation * glm::dvec3(0, 1, 0), mountain.geographicDirection, 1e-12,
                 "transported tangent frame lost its normal");
        }
        auto terrain = u.describe(planetAddress);
        for (auto address : {planetAddress, surfaceAddress, surfaceAddress.child(Kind::Mountain, 7)}) {
            auto node = u.describe(address);
            auto coarse = generator.generate(node, 0), fine = generator.generate(node, 1);
            using Position = std::array<uint32_t, 3>;
            std::set<Position> positions;
            for (auto v : fine->solid.vertices)
                positions.insert({std::bit_cast<uint32_t>(v.x), std::bit_cast<uint32_t>(v.y),
                                  std::bit_cast<uint32_t>(v.z)});
            for (auto v : coarse->solid.vertices)
                check(positions.contains({std::bit_cast<uint32_t>(v.x), std::bit_cast<uint32_t>(v.y),
                                          std::bit_cast<uint32_t>(v.z)}),
                      "terrain refinement replaced a coarse sample");
            for (auto [x, z] : {std::pair(0., 0.), std::pair(.3, -.4), std::pair(-.7, .5)})
                if (address.kind() != Kind::Planet) {
                    auto d = u.terrainDirection(node, x, z);
                    auto p = u.terrainPoint(node, x, z);
                    auto world = u.pointRelative(address, p, planetAddress, terrain.frame.logRadius);
                    near(world,
                         d * (1 + u.terrainHeight(terrain, d) / std::pow(10.0, terrain.frame.logRadius)),
                         2e-14, "terrain child left the planetary height field");
                    check(u.terrainAlbedo(node, d) == u.terrainAlbedo(terrain, d),
                          "terrain albedo changed at a frame boundary");
                    near(u.terrainNormal(node, d), u.terrainNormal(terrain, d), 1e-13,
                         "terrain lighting changed at a frame boundary");
                }
        }
        GeometryBlend blend;
        blend.update(stars, 0);
        blend.update(moreStars, 0);
        check(blend.previous == stars && blend.current == moreStars && blend.weight() == 0,
              "LOD refinement discarded the prior mesh");
        blend.update(nullptr, .075);
        check(std::abs(blend.weight() - .25) < 1e-6, "LOD crossfade is not time based");
        auto old = blend.weight();
        blend.update(stars, 0);
        check(std::abs(blend.weight() - (1 - old)) < 1e-6, "reversing LOD caused a discontinuity");
        for (int i = 0; i < 6; ++i)
            blend.update(stars, .1);
        check(!blend.active() && blend.current == stars, "LOD failed to finish");
        rejects([&] { blend.update(generator.generate(u.describe(planetAddress), 0), 0); },
                "LOD mixed unrelated object identities");
        for (uint64_t seed : {0x494e46494e495445ULL, 0x12345678ULL, 0xfedcba9876543210ULL}) {
            Universe world(seed);
            Navigation nav(world);
            Address root{{seed == 0x12345678ULL ? INT64_MAX : int64_t(0), -19, INT64_MIN},
                         {{Kind::Galaxy, seed % 9}}};
            startAt(nav, world, root);
            auto views = descend(nav, world);
            check(views.size() == 8, "descent did not cover every scale");
            std::vector<ObjectProof> originals;
            std::vector<uint64_t> sections;
            Navigation at(world);
            for (const auto& v : views) {
                originals.push_back(proveObject(world, v.address));
                at.anchor = v.address;
                at.logDistance = v.log;
                sections.push_back(sectionHash(world, at));
            }
            auto rock = world.describe(views[5].address), grain = world.describe(views[6].address),
                 lattice = world.describe(views[7].address);
            for (const auto& object : {rock, grain}) {
                auto coarse = GeometryGenerator(world).generate(object, 0),
                     fine = GeometryGenerator(world).generate(object, 2);
                auto residual = [&](const Geometry& geometry) {
                    double sum = 0;
                    for (auto vertex : geometry.solid.vertices) {
                        glm::dvec3 p(vertex.x, vertex.y, vertex.z);
                        sum += std::abs(object.address.kind() == Kind::Rock ? world.rockSdf(object, p)
                                                                            : world.grainSdf(object, p));
                    }
                    return sum / std::max(size_t(1), geometry.solid.vertices.size());
                };
                check(residual(*fine) < std::max(.02, residual(*coarse) * 1.05),
                      "SDF refinement stopped converging to the same solid");
            }
            check(std::abs(world.rockSdf(rock, grain.frame.position)) < 1e-10,
                  "grain does not lie on its host rock");
            check(world.grainSdf(grain, lattice.frame.position) < -.08,
                  "atomic specimen is outside the mineral");
            auto atoms = generateLattice(lattice);
            double ratio = std::pow(10.0, lattice.frame.logRadius - grain.frame.logRadius);
            for (const auto& atom : atoms.atoms)
                check(world.grainSdf(grain, lattice.frame.position + glm::dvec3(atom.position) * ratio) < 0,
                      "atomic site is outside its host grain");
            auto persisted = nav.serialize();
            {
                Navigation outward(world);
                check(outward.restore(persisted), "outward focus fixture restore");
                auto leaving = outward.anchor;
                outward.parent();
                for (int frame = 0; frame < 4; ++frame) {
                    outward.update(1.0 / 120);
                    auto p = world.relative(leaving, outward.anchor, outward.logDistance).position;
                    auto viewPoint = glm::inverse(outward.orientation) * (p - outward.cameraPosition());
                    near({viewPoint.x, viewPoint.y, 0}, {}, 1e-8,
                         "outward focus panned a macro translation at atomic scale");
                }
                settle(outward);
                views[views.size() - 2].compare(outward);
            }
            Navigation restored(world);
            check(restored.restore(persisted) && restored.serialize() == persisted,
                  "deep navigation breadcrumbs did not round-trip");
            RegionCache cache(world, 6 * 1024 * 1024, 2);
            for (size_t i = views.size(); i-- > 0;) {
                views[i].compare(restored);
                auto actual = proveObject(world, restored.anchor);
                check(actual == originals[i], "full round trip changed descriptor or an LOD recipe");
                check(sectionHash(world, restored) == sections[i], "local section changed after return");
                auto node = world.describe(restored.anchor);
                cache.beginFrame();
                cache.request(node, 2);
                cache.wait();
                cache.beginFrame();
                auto g = cache.request(node, 2);
                check(g && geometryFingerprint(*g) == originals[i].geometry[2],
                      "cache changed round-trip geometry");
                cache.clear();
                if (i) {
                    auto leaving = restored.anchor;
                    restored.parent();
                    check(restored.selected == leaving,
                          "parent chose child zero instead of the visited branch");
                    settle(restored);
                }
            }
            std::vector<ObjectProof> reordered(views.size());
            oneapi::tbb::parallel_for(size_t(0), views.size(), [&](size_t i) {
                auto index = views.size() - 1 - i;
                reordered[index] = proveObject(world, views[index].address);
            });
            check(reordered == originals, "generation order or worker scheduling changed full object proofs");
            // Outward wheel navigation changes reference frames without changing the
            // physical pan/pivot, and returns to an actual parent rather than trapping
            // the camera in a microscopic address forever.
            Navigation zoom(world);
            check(zoom.restore(persisted), "zoom fixture restore");
            zoom.zoom(-40);
            settle(zoom);
            check(zoom.anchor.path.size() < views.back().address.path.size(),
                  "logarithmic zoom out never promoted the frame");
            auto host = world.describe(views.back().address.parent());
            auto halo =
                GeometryGenerator(world).section(host, views.back().address, sectionBucket(views.back().log));
            rejects(
                [&] {
                    RoundTripVerifier verifier(world);
                    verifier.observe(lattice, halo);
                },
                "tour accepted an unrelated parent section as the selected object");
        }
        auto latticeAddress = planetAddress.child(Kind::Surface, encodeTile({4, 8, 128, 128}))
                                  .child(Kind::Mountain, 3)
                                  .child(Kind::Rock, 4)
                                  .child(Kind::Grain, 5)
                                  .child(Kind::Lattice, 0);
        auto specimen = u.describe(latticeAddress);
        for (auto mineral :
             {Mineral::Quartz, Mineral::Iron, Mineral::Olivine, Mineral::Ice, Mineral::Graphite}) {
            auto node = specimen;
            node.environment.mineral = mineral;
            node.frame.logRadius = std::log10(material(mineral).cellMetres * 6);
            auto small = generateLattice(node, {-3, 3}), large = generateLattice(node, {-4, 4});
            std::map<uint64_t, Atom> occupied;
            std::set<uint64_t> vacant(large.vacantSites.begin(), large.vacantSites.end());
            for (auto atom : large.atoms)
                check(occupied.emplace(atom.siteId, atom).second, "atomic site ID collision");
            for (auto atom : small.atoms) {
                auto found = occupied.find(atom.siteId);
                check(found != occupied.end() && found->second.position == atom.position &&
                          found->second.radius == atom.radius && found->second.element == atom.element,
                      "changing atomic query bounds replaced existing sites");
            }
            for (auto site : small.vacantSites)
                check(vacant.contains(site), "atomic vacancy moved when the query changed");
            check(small.vacancies == small.vacantSites.size(), "vacancy identities lost");
            std::set<uint64_t> shared;
            for (auto atom : small.atoms)
                shared.insert(atom.siteId);
            auto edges = [](const Lattice& lattice, const std::set<uint64_t>& subset) {
                std::set<std::pair<uint64_t, uint64_t>> out;
                for (auto bond : lattice.bonds) {
                    auto a = lattice.atoms[bond.a].siteId, b = lattice.atoms[bond.b].siteId;
                    if (subset.contains(a) && subset.contains(b))
                        out.emplace(std::min(a, b), std::max(a, b));
                }
                return out;
            };
            check(edges(small, shared) == edges(large, shared),
                  "changing atomic query bounds replaced existing bonds");
        }
        Universe foreign(1);
        RegionCache cache(u);
        rejects([&] { cache.request(foreign.describe(ancestor), 0); },
                "cache reused an address from another universe seed");
        std::cout << "PASS: " << checks
                  << " continuity checks; full non-default galaxy-to-lattice round trips, all LODs, caches, "
                     "local sections, persisted breadcrumbs, transported frames and atomic site stability.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " continuity checks: " << e.what() << '\n';
        return 1;
    }
}
