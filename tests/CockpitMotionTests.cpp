#include "infinite/Cockpit.hpp"
#include "infinite/CockpitMotion.hpp"
#include "infinite/Piloting.hpp"
#include <iostream>
#include <limits>
#include <oneapi/tbb/global_control.h>
#include <stdexcept>

using namespace infinite;
namespace {
size_t checks = 0;
void check(bool b, const char* message) {
    ++checks;
    if (!b)
        throw std::runtime_error(message);
}
void advance(CockpitMotion& motion, const CockpitMotionInput& input, double seconds, double dt = 1. / 60) {
    for (int i = 0; i < int(std::round(seconds / dt)); ++i)
        motion.update(input, dt);
}
void bounded(const CockpitMotionPose& pose) {
    check(std::isfinite(glm::length(pose.angles)) && glm::length(pose.angles) <= .010000001,
          "Cockpit camera angle escaped the small visual envelope");
    check(std::isfinite(glm::length(pose.translation)) && glm::length(pose.translation) <= .009000001,
          "Cockpit head translation escaped the cabin envelope");
    check(std::abs(glm::length(pose.orientation) - 1) < 1e-12,
          "Cockpit head rotation lost its unit quaternion");
}
} // namespace
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 3);
        CockpitMotion resting;
        CockpitMotionInput input;
        advance(resting, input, 2);
        check(resting.pose().angles == glm::dvec3(0) && resting.pose().translation == glm::dvec3(0),
              "Idle ship has perpetual artificial shake");
        CockpitMotionInput turn;
        turn.acceleration = {35, 16, -25};
        turn.angularRate = {.5, -.6, .8};
        turn.engine = .8;
        CockpitMotion loaded;
        loaded.update(turn, 1. / 60);
        const double firstLean = glm::length(loaded.pose().translation);
        advance(loaded, turn, 1);
        check(loaded.pose().translation.x < 0 && loaded.pose().translation.z > 0 &&
                  glm::length(loaded.pose().translation) > firstLean * 2,
              "Acceleration does not produce a gradual lean opposite the applied force");
        check(glm::length(loaded.pose().angles) > .001, "High-G turn has no visible cockpit response");
        const auto turnPose = loaded.pose();
        advance(loaded, input, 2);
        check(glm::length(loaded.pose().angles) < 1e-8 && glm::length(loaded.pose().translation) < 1e-8,
              "Cockpit inertia failed to settle after manoeuvring");
        // Recoil and collision/damage have distinct, finite, decaying impulses.
        double peakRecoil = 0, peakDamage = 0;
        for (auto type : {CockpitImpulse::Recoil, CockpitImpulse::Impact, CockpitImpulse::Damage}) {
            CockpitMotion motion;
            motion.update(input, 1. / 60);
            motion.impulse(type, .8, {1, .3, 0});
            double peak = 0;
            for (int i = 0; i < 60; ++i) {
                motion.update(input, 1. / 60);
                bounded(motion.pose());
                peak = std::max(peak, glm::length(motion.pose().angles));
            }
            check(peak > .0005 && peak < .01, "Feedback impulse is invisible or excessive");
            check(glm::length(motion.pose().angles) < 1e-8,
                  "One feedback impulse left continuous camera shake");
            if (type == CockpitImpulse::Recoil)
                peakRecoil = peak;
            if (type == CockpitImpulse::Damage)
                peakDamage = peak;
        }
        CockpitMotion damaged;
        damaged.update(input, 1. / 60);
        auto hit = input;
        hit.hull = 88;
        damaged.update(hit, 1. / 60);
        check(glm::length(damaged.pose().angles) > .0001, "Actual hull loss did not trigger damage feedback");
        advance(damaged, hit, 2);
        check(glm::length(damaged.pose().angles) < 1e-8, "The same hull loss retriggered every frame");
        CockpitMotion contact, explicitImpact;
        contact.update(input, 1. / 60);
        auto alreadyDamaged = input;
        alreadyDamaged.hull = 80;
        explicitImpact.update(alreadyDamaged, 1. / 60);
        auto impact = input;
        impact.hull = 80;
        impact.contactSequence = 1;
        impact.contactImpulse = {16, 0, 0};
        explicitImpact.impulse(CockpitImpulse::Impact, .8, impact.contactImpulse);
        contact.update(impact, 1. / 60);
        auto sameHull = input;
        sameHull.hull = 80;
        explicitImpact.update(sameHull, 1. / 60);
        check(glm::length(contact.pose().angles - explicitImpact.pose().angles) < 1e-12,
              "Collision damage was counted twice as an impact and a damage event");
        advance(contact, impact, 2);
        check(glm::length(contact.pose().angles) < 1e-8, "An old contact serial retriggered the impact");
        // Sustained automatic fire, damage spam, large dt and corrupted samples
        // cannot produce an unbounded view. None of these changes ship physics.
        CockpitMotion stress;
        stress.update(input, 1. / 60);
        for (int i = 0; i < 1800; ++i) {
            for (int k = 0; k < 8; ++k)
                stress.impulse(CockpitImpulse(i % 3), 1e9, {1, -1, 1});
            stress.update(turn, i % 5 ? 1. / 144 : .7);
            bounded(stress.pose());
        }
        const auto valid = stress.pose();
        auto bad = input;
        bad.acceleration.x = std::numeric_limits<double>::quiet_NaN();
        stress.update(bad, .1);
        stress.update(input, std::numeric_limits<double>::infinity());
        stress.impulse(CockpitImpulse::Damage, std::numeric_limits<double>::quiet_NaN());
        check(stress.pose().angles == valid.angles, "Invalid feedback sample poisoned the view");
        stress.reset();
        check(stress.pose().angles == glm::dvec3(0), "Disabling motion left a displaced head");
        // Remove vibration to compare the spring itself across display rates.
        auto leanOnly = turn;
        leanOnly.acceleration = {3, 2, -4};
        leanOnly.engine = 0;
        CockpitMotion reference;
        advance(reference, leanOnly, 1, 1. / 60);
        for (double dt : {1. / 15, 1. / 30, 1. / 144}) {
            CockpitMotion other;
            advance(other, leanOnly, 1, dt);
            check(glm::length(other.pose().angles - reference.pose().angles) < 1e-12 &&
                      glm::length(other.pose().translation - reference.pose().translation) < 1e-12,
                  "Cockpit spring response depends on display frame rate");
        }
        for (double size : {1., 1e-3, 1e-6, 1e-12}) {
            auto small = turn;
            small.hullMetres *= size;
            small.acceleration *= size;
            CockpitMotion motion;
            motion.update(small, 1. / 60);
            advance(motion, small, 1);
            check(glm::length(motion.pose().angles - turnPose.angles) < 1e-12 &&
                      glm::length(motion.pose().translation - turnPose.translation) < 1e-12,
                  "Magnification or shrinking changed the dimensionless cockpit response");
        }
        // A rotated visual head retains the physical eye and address, and the
        // projected physical aim agrees with the collimated cockpit reticle.
        Universe universe;
        Navigation nav(universe);
        RegionCache cache(universe);
        FlightController spatial(universe, nav, cache);
        Pilot pilot(universe, nav, spatial);
        pilot.startInSystem();
        const auto camera = nav.serialize(), ship = pilot.serialize();
        const auto system = universe.describe(nav.anchor.parent());
        std::vector<Node> planets;
        for (const auto& a : universe.children(system))
            planets.push_back(universe.describe(a));
        const auto bodies = extractCelestialContacts(universe, nav, system, planets);
        const auto headBodies =
            extractCelestialContacts(universe, nav, system, planets, turnPose.orientation);
        for (size_t i = 0; i < bodies.size(); ++i) {
            check(bodies[i].address == headBodies[i].address &&
                      bodies[i].distanceMetres == headBodies[i].distanceMetres &&
                      glm::length(glm::inverse(turnPose.orientation) * bodies[i].localDirection -
                                  headBodies[i].localDirection) < 1e-12,
                  "Visual head motion changed object identity, range or projected bearing");
        }
        const auto star = extractStellarImage(universe, nav, bodies);
        const auto headStar = extractStellarImage(universe, nav, headBodies, turnPose.orientation);
        check(glm::length(headStar.direction - glm::inverse(turnPose.orientation) * star.direction) < 1e-12,
              "Stellar composite and geometry disagree about the visual camera");
        for (double aspect : {1., 16. / 9, 2.4}) {
            const auto aim =
                cockpitTargetMarker(glm::inverse(turnPose.orientation) * glm::dvec3(0, 0, -1), aspect).point;
            const auto hud = cockpitOverlay(pilot.instruments(), aspect, "HYPERION", {}, headBodies, {},
                                            false, 1, nullptr, turnPose.orientation);
            bool foundAim = false;
            for (const auto& vertex : hud.panels.vertices)
                foundAim |= glm::length(glm::dvec2(vertex.x, vertex.y) - (aim - glm::dvec2(1))) < 1e-4;
            check(foundAim, "Visual camera shake moved the reticle away from the physical aim");
        }
        check(nav.serialize() == camera && pilot.serialize() == ship,
              "Cockpit feedback changed physical camera, ship state or the saved flight");
        std::cout << "peak_recoil_degrees=" << peakRecoil * 57.2957795
                  << " peak_damage_degrees=" << peakDamage * 57.2957795
                  << " turn_lean_degrees=" << glm::length(turnPose.angles) * 57.2957795
                  << " turn_slide_mm=" << glm::length(turnPose.translation) * 1000 << '\n';
        std::cout << "PASS: " << checks << " cockpit inertia, bounded shake, event, scale and aim checks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
