#include "infinite/Piloting.hpp"
#include <algorithm>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <sstream>
#include <stdexcept>
using namespace infinite;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static void place(Navigation& nav, Address a, double log, glm::dvec3 eye, glm::dquat q = {1, 0, 0, 0}) {
    std::ostringstream out;
    out.precision(17);
    const auto offset = eye - q * glm::dvec3(0, 0, 1);
    out << a.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
        << ' ' << offset.y << ' ' << offset.z;
    check(nav.restore(out.str()), "invalid operating-scale fixture");
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        Navigation nav(u);
        RegionCache cache(u);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        pilot.startInSystem();
        check(nav.anchor.kind() == Kind::Planet, "normal play did not begin among planets");
        check(!pilot.ship().cruise && !pilot.ship().shrinking && pilot.instruments().hullMetres == 12,
              "normal ship is not full size with cruise disengaged");
        const auto planet = nav.anchor;
        const auto p = u.describe(planet);
        const auto fingerprint = descriptorFingerprint(p);
        const auto initialView = nav.serialize();
        const auto direction = glm::normalize(glm::dvec3(.3, 1, .4));
        const double radius = std::pow(10., p.frame.logRadius), height = u.terrainHeight(p, direction);
        double low = 0;
        for (double altitude : {9999., 10001.}) {
            place(nav, planet, p.frame.logRadius, direction * (1 + (height + altitude) / radius));
            pilot.resetMotion();
            if (!low)
                low = pilot.instruments().driveSpeed;
            else
                check(pilot.instruments().driveSpeed / low < 1.01,
                      "planetary handling has a speed discontinuity at ten kilometres");
        }
        check(nav.restore(initialView), "failed to restore initial planetary orbit");
        pilot.resetMotion();
        pilot.selectNextPlanet();
        check(nav.selected && nav.selected->kind() == Kind::Planet &&
                  nav.selected->parent() == planet.parent(),
              "planet selector lost the current system");
        HelmInput throttle;
        throttle.throttle = 1;
        for (int i = 0; i < 600; i++) {
            cache.beginFrame();
            pilot.update(throttle, 1. / 60);
            cache.finishFrame();
        }
        check(pilot.instruments().driveSpeed <= 85000.00001,
              "ordinary flight silently engaged an astronomical drive");
        check(pilot.ship().logSize == 0 && !pilot.ship().shrinking,
              "approach automatically shrank the normal ship");
        const auto oldVelocity = pilot.ship().velocity, oldEye = nav.cameraPosition();
        pilot.toggleCruise();
        check(pilot.ship().cruise && pilot.ship().velocity == oldVelocity && nav.cameraPosition() == oldEye,
              "cruise activation jumped the ship");
        pilot.adjustDrive(3);
        check(pilot.ship().cruiseSpeed == 9.4607304725808e17 * 8,
              "cruise rate did not respond independently");
        pilot.toggleCruise();
        pilot.resetMotion();
        const auto beforeShrink = nav.cameraPosition();
        pilot.adjustSize(-6);
        check(nav.cameraPosition() == beforeShrink && pilot.ship().logSize == 0,
              "size request teleported or instantly rescaled the ship");
        for (int i = 0; i < 900 && pilot.ship().logSize > -6; i++)
            pilot.update({}, 1. / 60);
        check(pilot.ship().logSize == -6 && std::abs(pilot.instruments().hullMetres - 12e-6) < 1e-15,
              "micro-size ship never reached twelve micrometres");
        check(std::abs(pilot.collisionRadius() - 2e-6) < 1e-16,
              "shrinking did not change physical clearance");
        check(!pilot.ship().cruise, "shrinking left long-range cruise active");
        pilot.toggleCruise();
        check(!pilot.ship().cruise, "nano-capable hull engaged an incompatible cruise field");
        pilot.adjustSize(-6);
        for (int i = 0; i < 1200 && pilot.ship().logSize > -12; i++)
            pilot.update({}, 1. / 60);
        check(pilot.ship().logSize == -12 && pilot.instruments().hullMetres < 2e-11,
              "ship did not shrink below the nanoscale");
        const auto nanoState = pilot.serialize();
        check(pilot.restore(nanoState) && pilot.serialize() == nanoState,
              "size, target, drive or odometer did not persist");
        const auto velocity = pilot.ship().velocity;
        const auto physicalView = nav.serialize();
        for (double log : {26., 6., -6., -10.}) {
            nav.setRenderScale(log);
            pilot.synchronizeFrame();
            check(pilot.ship().logSize == -12 && pilot.ship().velocity == velocity,
                  "camera magnification changed actual hull size or velocity");
        }
        check(nav.restore(physicalView), "failed to restore orbital fixture after render-unit stress");
        pilot.synchronizeFrame();
        pilot.restoreFullSize();
        for (int i = 0; i < 1200 && pilot.ship().shrinking; i++)
            pilot.update({}, 1. / 60);
        check(!pilot.ship().shrinking && pilot.ship().logSize == 0, "regrowth in open space failed");
        check(descriptorFingerprint(u.describe(planet)) == fingerprint, "shrinking edited the planet");
        {
            Navigation n(u);
            RegionCache c(u);
            FlightController f(u, n, c);
            auto galaxy = planet;
            while (galaxy.kind() > Kind::Galaxy)
                galaxy = galaxy.parent();
            const auto destination = galaxy.child(Kind::System, 2999);
            const auto star = u.describe(destination);
            const auto g = u.describe(galaxy);
            place(n, galaxy, g.frame.logRadius, star.frame.position + glm::dvec3(.000001, 0, 0));
            n.changeFrame(galaxy.child(Kind::System, 0));
            f.observeGalaxy(GeometryGenerator(u).generate(g, 0));
            for (int i = 0; i < 8; i++) {
                c.beginFrame();
                f.update({}, 1. / 60);
                c.finishFrame();
                c.wait();
            }
            check(n.anchor == destination,
                  "planetary-start cruise could not enter an addressed background star");
        }
        {
            Navigation n(u);
            RegionCache c(u);
            FlightController f(u, n, c);
            Pilot ship(u, n, f);
            const auto q = glm::quatLookAtRH(glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0));
            place(n, Address{}, 26, {.9999999, .2, -.2}, q);
            ship.resetMotion();
            ship.toggleCruise();
            for (int i = 0; i < 1500 && n.anchor.cell[0] == 0; i++) {
                c.beginFrame();
                ship.update(throttle, 1. / 60);
                c.finishFrame();
                if (i % 60 == 0)
                    c.wait();
            }
            check(n.anchor.cell[0] == 1, "continuous long-range travel did not cross a root-cell boundary");
            check(ship.ship().flightSeconds > 1 && ship.ship().travelMetres > 1e19,
                  "voyage clock and travelled distance did not advance");
            const auto state = ship.serialize();
            check(ship.restore(state) && ship.serialize() == state,
                  "long voyage failed to resume coherently");
            check(!ship.ship().shrinking && ship.instruments().hullMetres == 12,
                  "deep cruise silently resized the ship");
            c.wait();
        }
        // Obtain an existing rock/grain/lattice branch without adding any objects.
        auto a = planet;
        while (a.kind() != Kind::Lattice)
            a = u.children(u.describe(a), 1).front();
        const auto node = u.describe(a);
        auto geometry = GeometryGenerator(u).generate(node, 0);
        const auto atom =
            *std::max_element(geometry->lattice.atoms.begin(), geometry->lattice.atoms.end(),
                              [](const Atom& x, const Atom& y) { return x.position.x < y.position.x; });
        const double units = std::pow(10., geometry->logUnits);
        const auto point = glm::dvec3(atom.position) + glm::dvec3(atom.radius + 1e-11 / units, 0, 0);
        place(nav, a, geometry->logUnits, point);
        pilot.resetMotion();
        pilot.fitToView();
        pilot.adjustSize(1);
        for (int i = 0; i < 90; i++)
            pilot.update({}, 1. / 60, geometry);
        check(pilot.instruments().growthBlocked, "growth inside a specimen was not blocked");
        check(pilot.instruments().hullMetres < 1e-9, "blocked regrowth expanded through material");
        pilot.adjustSize(-12);
        for (int i = 0; i < 600 && pilot.ship().logSize > -12; i++)
            pilot.update({}, 1. / 60, geometry);
        pilot.setVelocity({-1e-7, 0, 0});
        pilot.update({}, 1. / 60, geometry);
        const auto relative = u.relative(geometry->referenceFrame, nav.anchor, nav.logDistance);
        const auto local = glm::inverse(relative.orientation) * (nav.cameraPosition() - relative.position) *
                           std::pow(10., nav.logDistance - geometry->logUnits);
        check(glm::length(local - glm::dvec3(atom.position)) >=
                  atom.radius + pilot.collisionRadius() / units - 1e-8,
              "nano ship tunnelled through an existing atom");
        check(geometryFingerprint(*geometry) == geometryFingerprint(*GeometryGenerator(u).generate(node, 0)),
              "ship contact altered the atomic specimen");
        cache.wait();
        std::cout << "PASS: " << checks
                  << " planetary defaults, independent cruise, continuous voyages, real shrinking, safe "
                     "growth, nano contacts and persistence checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
