#include "infinite/Piloting.hpp"
#include <algorithm>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace infinite;
static size_t checks = 0;
static void check(bool v, const char* m) {
    ++checks;
    if (!v)
        throw std::runtime_error(m);
}
static double mass(const Node& n) {
    return 4.188790204786391 * material(n.environment.mineral).density * std::pow(10., 3 * n.frame.logRadius);
}
static void place(Navigation& n, Address a, double log, glm::dvec3 eye, glm::dquat q = {1, 0, 0, 0}) {
    const auto offset = eye - q * glm::dvec3(0, 0, 1);
    std::ostringstream out;
    out.precision(17);
    out << a.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
        << ' ' << offset.y << ' ' << offset.z;
    check(n.restore(out.str()), "Invalid population fixture");
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        size_t systems = 0, primaries = 0, satellites = 0;
        for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
            Universe u(seed);
            for (uint64_t i : {0, 3, 9, 17}) {
                const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, i));
                const auto bodies = u.children(system);
                const unsigned count = u.planetCount(system);
                check(count >= 13 && count <= 21, "Planet count did not increase to 13..21");
                check(u.children(system, 1).front() == system.address.child(Kind::Planet, 0),
                      "Expansion replaced the legacy prefix");
                std::set<uint64_t> identities;
                size_t moons = 0;
                const double extent = u.systemRadiusMetres(system),
                             units = std::pow(10., system.frame.logRadius);
                for (const auto& a : bodies) {
                    const auto body = u.describe(a);
                    check(identities.insert(a.id()).second, "Duplicate celestial identity");
                    check(body.seed == childSeed(system.seed, Kind::Planet, a.id()),
                          "Expansion changed the hierarchical seed recipe");
                    check(Address::parse(a.str()) == a, "Large moon IDs do not persist");
                    check(glm::length(body.frame.position) * units < extent,
                          "Body outside the physical system envelope");
                    check(body.geology && body.planetSeed == body.seed, "New world cannot refine to geology");
                    check(body.environment.temperature <= 1391 &&
                              body.environment.peakTemperature >= body.environment.temperature,
                          "Body violates inherited irradiation constraints");
                    if (isMoon(a)) {
                        moons++;
                        const auto primary = u.describe(system.address.child(Kind::Planet, body.satelliteOf));
                        check(body.satelliteOf == moonPrimary(a.id()), "Moon lost its host identity");
                        const auto expected = primary.frame.position + keplerPosition(body.orbit, 0) / units;
                        check(glm::length(expected - body.frame.position) < 1e-14,
                              "Moon does not orbit its actual primary");
                        const double hill = primary.orbit.semiMajorMetres * (1 - primary.orbit.eccentricity) *
                                            std::cbrt(mass(primary) / (3 * system.massSolar * 1.98847e30));
                        const double roche = 2.44 * std::pow(10., primary.frame.logRadius) *
                                             std::cbrt(material(primary.environment.mineral).density /
                                                       material(body.environment.mineral).density);
                        check(body.orbit.semiMajorMetres * (1 + body.orbit.eccentricity) < hill * .35,
                              "Moon outside stable Hill region");
                        check(body.orbit.semiMajorMetres * (1 - body.orbit.eccentricity) > roche,
                              "Moon inside its Roche limit");
                        check(mass(body) / mass(primary) < .004001,
                              "Satellite mass defeats the regular-moon spacing");
                        check(u.moons(body).empty() && u.children(body, 1).front().kind() == Kind::Surface,
                              "Moon replaced the surface hierarchy");
                    }
                }
                check(moons > 5, "System still lacks a genuine moon population");
                check(bodies.size() == count + moons, "A catalogue limit silently dropped moons");
                std::vector<uint64_t> before;
                for (auto a : bodies)
                    before.push_back(descriptorFingerprint(u.describe(a)));
                Universe replay(seed);
                std::vector<uint64_t> parallel(bodies.size());
                oneapi::tbb::parallel_for(size_t(0), bodies.size(), [&](size_t j) {
                    parallel[j] = descriptorFingerprint(replay.describe(bodies[bodies.size() - 1 - j]));
                });
                std::reverse(parallel.begin(), parallel.end());
                check(before == parallel, "Population depends on thread or query order");
                systems++;
                primaries += count;
                satellites += moons;
            }
        }
        Universe u;
        Navigation nav(u);
        RegionCache cache(u);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        pilot.startInSystem();
        const auto home = nav.anchor;
        const auto parent = u.describe(home);
        const auto moons = u.moons(parent);
        check(!moons.empty(), "Starting world has no moons");
        pilot.selectNextMoon();
        check(nav.selected == moons.front(), "Shift-Y cannot select a real moon");
        pilot.selectNextPlanet();
        check(nav.selected && !isMoon(*nav.selected), "Y stopped selecting primary planets");
        const auto moon = u.describe(moons.front());
        const auto system = u.describe(home.parent());
        place(nav, system.address, system.frame.logRadius,
              moon.frame.position +
                  glm::dvec3(std::pow(10., moon.frame.logRadius - system.frame.logRadius) * 2.4, 0, 0));
        for (int i = 0; i < 12; i++) {
            cache.beginFrame();
            spatial.update({}, 1. / 60);
            cache.finishFrame();
            cache.wait();
        }
        check(nav.anchor == moon.address, "Flying near a moon did not enter its real reference frame");
        auto a = moon.address;
        std::vector<std::pair<Address, uint64_t>> nested;
        GeometryGenerator generator(u);
        while (true) {
            const auto node = u.describe(a);
            const auto geometry = generator.generate(node, 0);
            check(node.planetSeed == moon.seed,
                  "Moon descendants inherit another planet's material identity");
            nested.emplace_back(a, geometryFingerprint(*geometry));
            if (a.kind() == Kind::Lattice) {
                check(geometry->lattice.atoms.size() > 100, "Moon has no atomic depth");
                break;
            }
            a = u.children(node, 1).front();
        }
        Universe fresh;
        GeometryGenerator returned(fresh);
        for (auto it = nested.rbegin(); it != nested.rend(); ++it)
            check(geometryFingerprint(*returned.generate(fresh.describe(it->first), 0)) == it->second,
                  "Moon-to-atoms round trip replaced its structures");
        glm::dvec3 direction = glm::normalize(glm::dvec3(.3, 1, .4));
        for (int i = 0; i < 100; i++) {
            const auto d = glm::normalize(glm::dvec3(std::sin(i * 1.7), 1, std::cos(i * 2.1)));
            if (glm::dot(d, u.terrainNormal(moon, d)) > .995) {
                direction = d;
                break;
            }
        }
        const auto normal = u.terrainNormal(moon, direction),
                   forward = glm::normalize(glm::cross(normal, glm::dvec3(1, 0, 0)));
        place(nav, moon.address, moon.frame.logRadius,
              direction * (1 + (u.terrainHeight(moon, direction) + 24) / std::pow(10., moon.frame.logRadius)),
              glm::quatLookAtRH(forward, normal));
        nav.changeFrame(FlightController::surfaceAt(moon.address, direction));
        nav.setRenderScale(2);
        pilot.resetMotion();
        check(pilot.land(), "Moon terrain cannot be targeted for landing");
        for (int i = 0; i < 2400 && !pilot.ship().landed; i++) {
            cache.beginFrame();
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        check(pilot.ship().landed && pilot.ship().hull == 100, "Moon landing did not preserve ship handling");
        cache.wait();
        std::cout << "PASS: " << checks
                  << " population, Hill/Roche, identity, deterministic replay, moon flight, landing and "
                     "atomic checks / "
                  << systems << " systems / " << primaries << " planets / " << satellites << " moons\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
