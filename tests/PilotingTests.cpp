#include "infinite/Cockpit.hpp"
#include "infinite/Piloting.hpp"
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool b, const char* message) {
    ++checks;
    if (!b)
        throw std::runtime_error(message);
}
static void near(glm::dvec3 a, glm::dvec3 b, double tolerance, const char* message) {
    check(glm::length(a - b) <= tolerance * std::max(1e-30, glm::length(b)), message);
}
static void place(Navigation& n, Address address, double log, glm::dvec3 eye, glm::dquat q = {1, 0, 0, 0}) {
    std::ostringstream s;
    s.precision(17);
    auto offset = eye - q * glm::dvec3(0, 0, 1);
    s << address.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
      << offset.x << ' ' << offset.y << ' ' << offset.z;
    check(n.restore(s.str()), "invalid pilot fixture");
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        Navigation nav(u);
        RegionCache cache(u);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        const auto galaxy = nav.anchor;
        auto address = galaxy;
        std::vector<Address> route{Address{}, galaxy};
        while (address.kind() != Kind::Lattice) {
            address = u.children(u.describe(address), 1).front();
            route.push_back(address);
        }
        const glm::dvec3 velocity{123.5, -7.2, 81.9};
        pilot.setVelocity(velocity);
        // A render-unit change must never change physical ship speed or momentum.
        for (double log : {26., 15., 3., -6., -10., 21.}) {
            nav.setRenderScale(log);
            pilot.synchronizeFrame();
            near(pilot.ship().velocity, velocity, 1e-14, "magnification changed physical velocity");
        }
        glm::dvec3 expected = velocity;
        Address old = nav.anchor;
        for (const auto& next : route) {
            auto rotation = u.relative(old, next, 0).orientation;
            nav.changeFrame(next);
            expected = rotation * expected;
            pilot.synchronizeFrame();
            near(pilot.ship().velocity, expected, 2e-13, "inward frame rebase changed momentum");
            old = next;
        }
        for (auto it = route.rbegin(); it != route.rend(); ++it) {
            auto rotation = u.relative(old, *it, 0).orientation;
            nav.changeFrame(*it);
            expected = rotation * expected;
            pilot.synchronizeFrame();
            near(pilot.ship().velocity, expected, 2e-13, "outward frame rebase changed momentum");
            old = *it;
        }
        nav.changeFrame(galaxy);
        pilot.synchronizeFrame();
        near(pilot.ship().velocity, velocity, 2e-12, "galaxy-to-atom round trip lost velocity");
        auto state = pilot.serialize();
        check(pilot.restore(state) && pilot.serialize() == state, "ship persistence lost state");
        check(!pilot.restore("INFINITE_SHIP 1 nonsense"), "corrupt ship data accepted");
        check(pilot.serialize() == state, "failed restore mutated live ship");
        // Cruise disengaged: test local dynamics independently of scene scale.
        nav.reset();
        pilot.resetMotion();
        pilot.toggleCruise();
        pilot.setVelocity({100, 0, 0});
        pilot.toggleAssist();
        for (int i = 0; i < 60; i++)
            pilot.update({}, 1. / 60);
        near(pilot.ship().velocity, {100, 0, 0}, 1e-12, "assist-off coasting lost inertia");
        HelmInput spin;
        spin.roll = 1;
        for (int i = 0; i < 20; i++)
            pilot.update(spin, 1. / 60);
        auto rate = pilot.ship().angularVelocity;
        check(glm::length(rate) > 0, "roll input has no torque");
        for (int i = 0; i < 20; i++)
            pilot.update({}, 1. / 60);
        near(pilot.ship().angularVelocity, rate, 1e-12, "assist-off rotation was stopped instantly");
        pilot.resetMotion();
        pilot.setVelocity({0, 0, -100});
        pilot.brake();
        pilot.update({}, 1. / 60);
        check(glm::length(pilot.ship().velocity) > 90, "brake stopped the ship instantaneously");
        for (int i = 0; i < 360; i++)
            pilot.update({}, 1. / 60);
        check(glm::length(pilot.ship().velocity) < .05, "bounded braking did not stop the ship");
        pilot.resetMotion();
        HelmInput throttle;
        throttle.throttle = 1;
        for (int i = 0; i < 60; i++)
            pilot.update(throttle, 1. / 60);
        const auto held = pilot.ship().throttle;
        pilot.update({}, 1. / 60);
        check(std::abs(pilot.ship().throttle - held) < 1e-12, "releasing throttle key erased throttle");
        check(glm::length(pilot.ship().velocity) > 0, "thrusters failed to accelerate");
        auto planet = route[3];
        const auto p = u.describe(planet);
        const double radius = std::pow(10., p.frame.logRadius);
        glm::dvec3 direction = glm::normalize(glm::dvec3(.3, 1, .4));
        // Choose a naturally gentle site; never flatten or alter geology to land.
        for (int i = 0; i < 100; i++) {
            const auto d = glm::normalize(glm::dvec3(std::sin(i * 1.7), 1, std::cos(i * 2.1)));
            if (glm::dot(d, u.terrainNormal(p, d)) > .995) {
                direction = d;
                break;
            }
        }
        auto up = u.terrainNormal(p, direction);
        auto forward = glm::normalize(glm::cross(up, glm::dvec3(1, 0, 0)));
        auto q = glm::quatLookAtRH(forward, up);
        double height = u.terrainHeight(p, direction);
        place(nav, planet, p.frame.logRadius, direction * (1 + (height + 6000) / radius), q);
        pilot.resetMotion();
        pilot.navigate(planet);
        check(pilot.instruments().navigating, "navigation did not acquire the selected destination");
        bool sawTransit = false;
        for (int i = 0; i < 6000; i++) {
            pilot.update({}, 1. / 60);
            sawTransit = sawTransit || pilot.instruments().navigating;
            if (i > 1 && !pilot.instruments().navigating)
                break;
        }
        check(sawTransit && !pilot.instruments().navigating,
              "destination approach failed to arrive and brake");
        check(glm::length(pilot.ship().velocity) < 2, "arrival did not brake the ship");
        pilot.navigate(planet);
        HelmInput takeover;
        takeover.strafe = 1;
        pilot.update(takeover, 1. / 60);
        check(!pilot.instruments().navigating, "manual helm failed to cancel navigation");
        place(nav, planet, p.frame.logRadius, direction * (1 + (height + 42) / radius), q);
        nav.changeFrame(FlightController::surfaceAt(planet, direction));
        nav.setRenderScale(2);
        pilot.resetMotion();
        check(pilot.land(), "landing did not acquire the existing planet");
        const auto before = descriptorFingerprint(u.describe(planet));
        for (int i = 0; i < 3000 && !pilot.ship().landed; i++) {
            cache.beginFrame();
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        check(pilot.ship().landed, "automatic approach failed to settle on real terrain");
        check(std::abs(pilot.instruments().altitude) < .12,
              "landing gear did not contact the real geological surface");
        check(pilot.ship().hull == 100, "safe landing damaged hull");
        check(descriptorFingerprint(u.describe(planet)) == before, "landing modified procedural planet");
        auto landed = pilot.serialize();
        check(pilot.restore(landed) && pilot.ship().landed, "landed save did not restore");
        const auto physicalEye = nav.cameraPosition();
        pilot.navigate(planet.parent());
        check(pilot.instruments().navigating && !pilot.ship().landed,
              "navigation did not release landing clamps for departure");
        pilot.launch(); // Already departing; this must not teleport or restart it.
        check(nav.cameraPosition() == physicalEye, "launch teleported the ship");
        for (int i = 0; i < 300; i++)
            pilot.update({}, 1. / 60);
        check(!pilot.ship().landed && pilot.instruments().altitude > 10,
              "launch did not depart under thrust");
        const auto speedBefore = glm::length(pilot.ship().velocity);
        pilot.toggleSurvey();
        check(pilot.ship().survey && glm::length(pilot.ship().velocity) == speedBefore,
              "survey activation reset momentum");
        for (double aspect : {1., 4. / 3, 16. / 9, 2.4}) {
            auto cabin = cockpitMesh(aspect);
            auto hud = cockpitOverlay(pilot.instruments(), aspect, "HYPERION");
            check(cabin.vertices.size() > 300 && cabin.indexCount() > 300,
                  "cockpit lost its beveled geometry");
            check(!hud.lettering.vertices.empty() && !hud.strokes.vertices.empty(),
                  "flight instruments were not drawn");
            for (const auto& v : cabin.vertices)
                check(std::isfinite(v.x) && std::isfinite(v.y) && v.z < 0, "cockpit vertex is invalid");
        }
        {
            Navigation journey(u);
            RegionCache flightCache(u);
            FlightController frames(u, journey, flightCache);
            Pilot vessel(u, journey, frames);
            place(journey, Address{}, 26, {.1, .2, .8});
            vessel.resetMotion();
            vessel.toggleSurvey();
            std::set<Kind> visited{Kind::Cosmic};
            HelmInput drive;
            drive.throttle = 1;
            drive.boost = true;
            for (int frame = 0; frame < 9000 && journey.anchor.kind() != Kind::Lattice; frame++) {
                flightCache.beginFrame();
                auto visible = flightCache.request(u.describe(journey.anchor), 0, 0);
                auto target = frames.target();
                if (target) {
                    const auto delta = u.relative(*target, journey.anchor, journey.logDistance).position -
                                       journey.cameraPosition();
                    if (glm::length(delta) > 1e-20) {
                        auto eye = journey.cameraPosition();
                        auto direction = glm::normalize(delta);
                        auto up = std::abs(direction.y) > .95 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
                        journey.orientation = glm::quatLookAtRH(direction, up);
                        journey.offset = eye - journey.orientation * glm::dvec3(0, 0, 1);
                    }
                }
                vessel.update(drive, 1. / 30, visible);
                flightCache.finishFrame();
                flightCache.wait();
                if (visited.insert(journey.anchor.kind()).second)
                    std::cout << "PILOT_SCALE " << kindName(journey.anchor.kind()) << " frame=" << frame
                              << '\n';
            }
            check(visited.size() == 9, "ship propulsion failed the automatic cosmic-to-atomic journey");
            std::set<Kind> returned{Kind::Lattice};
            for (int frame = 0; frame < 9000 && journey.anchor.kind() != Kind::Cosmic; frame++) {
                flightCache.beginFrame();
                const auto child = u.describe(journey.anchor);
                const auto radial = glm::length(child.frame.position) > 1e-20
                                        ? glm::normalize(child.frame.position)
                                        : glm::dvec3(0, 1, 0);
                const auto goal = u.pointRelative(journey.anchor.parent(), radial * 2.5, journey.anchor,
                                                  journey.logDistance);
                const auto delta = goal - journey.cameraPosition();
                if (glm::length(delta) > 1e-30) {
                    auto eye = journey.cameraPosition();
                    auto direction = glm::normalize(delta);
                    auto up = std::abs(direction.y) > .95 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
                    journey.orientation = glm::quatLookAtRH(direction, up);
                    journey.offset = eye - journey.orientation * glm::dvec3(0, 0, 1);
                }
                auto visible = flightCache.request(child, 0, 0);
                vessel.update(drive, 1. / 30, visible);
                flightCache.finishFrame();
                flightCache.wait();
                if (returned.insert(journey.anchor.kind()).second)
                    std::cout << "PILOT_RETURN " << kindName(journey.anchor.kind()) << " frame=" << frame
                              << '\n';
            }
            check(returned.size() == 9, "ship propulsion failed the atomic-to-cosmic return");
        }
        cache.wait();
        std::cout << "PASS: " << checks
                  << " ship, SI momentum, galaxy/atomic round trip, braking, landing, launch and persistence "
                     "checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
