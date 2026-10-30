#include "infinite/Piloting.hpp"
#include "infinite/Planetology.hpp"
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <sstream>
#include <stdexcept>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void near(glm::dvec3 a, glm::dvec3 b, double tolerance, const char* message) {
    check(glm::length(a - b) <= tolerance * std::max(1., glm::length(b)), message);
}
void place(Navigation& nav, Address a, double log, glm::dvec3 eye, glm::dquat q = {1, 0, 0, 0}) {
    std::ostringstream s;
    s.precision(17);
    auto offset = eye - q * glm::dvec3(0, 0, 1);
    s << a.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
      << ' ' << offset.y << ' ' << offset.z;
    check(nav.restore(s.str()), "Invalid physics camera");
}
struct Rig {
    Navigation nav;
    RegionCache cache;
    FlightController spatial;
    Pilot pilot;
    Rig(const Universe& u) : nav(u), cache(u), spatial(u, nav, cache), pilot(u, nav, spatial) {
        place(nav, Address{}, 0, {0, 0, 100});
        pilot.resetMotion();
    }
    void fly(HelmInput h, double seconds, double dt = 1. / 60) {
        for (unsigned i = 0; i < unsigned(std::round(seconds / dt)); i++)
            pilot.update(h, dt);
    }
};
} // namespace
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 3);
        const auto p = shipProperties();
        check(p.massKg > 0, "Missing physical hull mass");
        glm::dquat q(1, 0, 0, 0);
        glm::dvec3 w(.2, -.35, .6);
        const auto h0 = q * (p.inertia * w);
        const double e0 = .5 * glm::dot(w, p.inertia * w);
        double maxEnergy = 0;
        for (unsigned i = 0; i < 10000; i++) {
            integrateShipRotation(q, w, p.inertia, {}, .002);
            near(q * (p.inertia * w), h0, 1e-10, "Torque-free angular momentum was lost");
            maxEnergy = std::max(maxEnergy, std::abs(.5 * glm::dot(w, p.inertia * w) / e0 - 1));
        }
        check(maxEnergy < 1e-6, "Torque-free rotational energy drifted");
        q = {1, 0, 0, 0};
        w = {};
        for (unsigned i = 0; i < 1000; i++)
            integrateShipRotation(q, w, p.inertia, {0, 0, 25000}, .001);
        near(w, {0, 0, 1}, 1e-10, "Angular acceleration does not follow torque/inertia");
        for (unsigned i = 0; i < 2000; i++) {
            Random r{57 + i};
            glm::dvec3 f(r.signedUnit(0), r.signedUnit(1), r.signedUnit(2));
            auto t = glm::dvec3(r.signedUnit(3), r.signedUnit(4), r.signedUnit(5));
            const auto a = allocateShipWrench(p, {0, p.massKg * 9.8, 0}, f * 1e9, t * 1e9);
            const double load = a.linear.x * a.linear.x + a.linear.y * a.linear.y +
                                std::pow(std::max(0., a.linear.z), 2) + glm::dot(a.angular, a.angular);
            check(load <= 1.000000001 && std::abs(a.linear.z) <= 1,
                  "Actuator allocation exceeds shared force/torque limits");
        }
        for (double scale : {1., .01, 1e-6, 1e-12}) {
            const auto m = shipProperties(scale);
            check(std::abs(m.massKg / (p.massKg * std::pow(scale, 3)) - 1) < 1e-12,
                  "Shrink-field mass scaling inconsistent");
            check(std::abs(m.forceCaps.z / m.massKg / (52 * scale) - 1) < 1e-12,
                  "Shrink-field thrust scaling inconsistent");
        }
        for (glm::dvec3 v : {glm::dvec3(0), glm::dvec3(100, 20, -300), glm::dvec3(1e8, 0, 0)}) {
            near(atmosphericDrag(p, {1, 0, 0, 0}, v, 0, .01), {}, 0, "Vacuum generated aerodynamic drag");
            auto force = atmosphericDrag(p, {1, 0, 0, 0}, v, 1.2, .01), after = v + force / p.massKg * .01;
            check(glm::dot(after, after) <= glm::dot(v, v) * (1 + 1e-12) && glm::dot(after, v) >= -1e-10,
                  "Atmospheric drag added energy or reversed velocity");
        }
        const auto windForce = atmosphericDrag(p, {1, 0, 0, 0}, {-20, 0, 0}, 1.2, .01);
        check(windForce.x > 0, "Wind has no physical force");
        near(pointGravity({0, -20, 0}, 400, 10), {0, -1, 0}, 1e-12,
             "Point gravity violates inverse-square law");
        near(pointGravity({0, -5, 0}, 400, 10), {0, -2, 0}, 1e-12,
             "Gravity interior is singular or discontinuous");
        Universe u;
        Rig a(u), b(u);
        a.pilot.toggleAssist();
        b.pilot.toggleAssist();
        b.pilot.setVelocity({10000, 0, 0});
        HelmInput input;
        input.throttle = 1;
        a.fly(input, 3);
        b.fly(input, 3);
        near(b.pilot.ship().velocity - a.pilot.ship().velocity, {10000, 0, 0}, 1e-12,
             "Engine force depends on initial velocity");
        const double acceleration = -a.pilot.ship().velocity.z / 3;
        check(acceleration > 15 && acceleration <= 52, "Conventional engine exceeds physical force budget");
        Rig coast(u);
        coast.pilot.toggleAssist();
        coast.pilot.setVelocity({2e6, -1e6, 30});
        const auto velocity = coast.pilot.ship().velocity;
        coast.fly({}, 2);
        near(coast.pilot.ship().velocity, velocity, 1e-13,
             "Unassisted coasting has hidden damping or speed clamp");
        coast.pilot.toggleAssist();
        coast.fly({}, 2);
        check(glm::length(coast.pilot.ship().velocity - velocity) < 120,
              "Assist used field braking for ordinary overspeed");
        check(!coast.pilot.instruments().fieldActive,
              "Conventional braking silently engaged a fictional drive");
        // Gravity remains present beyond the terrain-contact range and on gas giants.
        for (unsigned id : {0u, u.establishedPlanetCount(
                                    u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 0)))}) {
            auto planet =
                u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 0).child(Kind::Planet, id));
            const auto fingerprint = descriptorFingerprint(planet);
            Navigation nav(u);
            RegionCache cache(u);
            FlightController spatial(u, nav, cache);
            FlightEnvironment env;
            place(nav, planet.address, planet.frame.logRadius, {0, 3.001, 0});
            env.refresh(u, nav, spatial);
            auto first = env.sample(nav.cameraPosition() * std::pow(10., nav.logDistance), 12);
            check(first.gravity.y < 0, "Gravity is absent beyond 3R or on a non-landable giant");
            nav.setRenderScale(planet.frame.logRadius + 6);
            env.refresh(u, nav, spatial);
            near(env.sample(nav.cameraPosition() * std::pow(10., nav.logDistance), 12).gravity, first.gravity,
                 1e-10, "Render-unit change modified gravitational acceleration");
            place(nav, planet.address, planet.frame.logRadius, {0, 2.999, 0});
            env.refresh(u, nav, spatial);
            auto second = env.sample(nav.cameraPosition() * std::pow(10., nav.logDistance), 12);
            check(glm::length(second.gravity - first.gravity) < glm::length(first.gravity) * .01,
                  "Gravity jumps at the surface-query boundary");
            check(descriptorFingerprint(u.describe(planet.address)) == fingerprint,
                  "Force extraction changed generation");
        }
        Rig dock(u);
        const auto rendezvousPlanet =
            u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 0).child(Kind::Planet, 0));
        const double rendezvousRadius = std::pow(10., rendezvousPlanet.frame.logRadius);
        place(dock.nav, rendezvousPlanet.address, rendezvousPlanet.frame.logRadius, {0, 0, 3.5});
        dock.pilot.resetMotion();
        FlightWaypoint waypoint;
        waypoint.reference = rendezvousPlanet.address;
        waypoint.metres = {0, 0, 3.5 * rendezvousRadius - 100};
        waypoint.velocity = {3, 0, 0};
        waypoint.standOff = 10;
        waypoint.label = "MOVING PHYSICS FIXTURE";
        dock.pilot.navigateWaypoint(waypoint);
        for (unsigned i = 0; i < 2400; i++) {
            waypoint.metres += waypoint.velocity / 60.;
            dock.pilot.trackWaypoint(waypoint);
            dock.pilot.update({}, 1. / 60);
        }
        std::cout << "moving_error=" << dock.pilot.instruments().approachError
                  << " relative_speed=" << dock.pilot.instruments().relativeSpeed
                  << " phase=" << dock.pilot.instruments().phase << std::endl;
        check(dock.pilot.instruments().approachError < 6 && dock.pilot.instruments().relativeSpeed < .5,
              "Moving-target rendezvous did not match velocity and stand-off");
        const auto save = dock.pilot.serialize();
        check(save.starts_with("INFINITE_SHIP 4 ") && dock.pilot.restore(save) &&
                  save == dock.pilot.serialize(),
              "Physics actuator state failed persistence round trip");
        auto legacy = save.substr(0, save.rfind(' '));
        for (unsigned i = 0; i < 3; ++i)
            legacy = legacy.substr(0, legacy.rfind(' '));
        legacy.replace(14, 1, "3");
        const auto retainedVelocity = dock.pilot.ship().velocity;
        const auto retainedSize = dock.pilot.ship().logSize;
        check(dock.pilot.restore(legacy) && dock.pilot.ship().velocity == retainedVelocity &&
                  dock.pilot.ship().logSize == retainedSize,
              "Version-three save migration altered velocity or hull scale");
        check(dock.pilot.ship().angularThrusters == glm::dvec3(0),
              "Legacy save acquired an uncommanded torque");
        auto fields = [](const std::string& text) {
            std::istringstream in(text);
            std::vector<std::string> words;
            for (std::string word; in >> word;)
                words.push_back(word);
            return words;
        };
        auto joined = [](const std::vector<std::string>& words) {
            std::ostringstream out;
            for (const auto& word : words)
                out << word << ' ';
            return out.str();
        };
        for (int version : {1, 2}) {
            auto words = fields(legacy);
            words.resize(words.size() - 6);
            words[1] = std::to_string(version);
            if (version == 1)
                words.erase(words.begin() + 12, words.begin() + 15);
            check(dock.pilot.restore(joined(words)) && dock.pilot.ship().velocity == retainedVelocity,
                  "Legacy force-state restore lost SI velocity");
        }
        auto rotating = fields(legacy);
        rotating[8] = "150";
        rotating[18] = "0";
        check(dock.pilot.restore(joined(rotating)) && dock.pilot.ship().angularVelocity.z == 150,
              "Finite inertial spin failed save migration");
        check(dock.pilot.restore(save), "Could not return to saved physical state");
        auto excess = fields(save);
        excess[9] = ".8";
        excess[10] = excess[11] = "0";
        excess[34] = ".8";
        excess[35] = excess[36] = "0";
        check(!dock.pilot.restore(joined(excess)) && dock.pilot.serialize() == save,
              "Corrupt shared actuator budget was accepted or mutated the live ship");
        std::cout << "rotation_max_relative_energy_error=" << maxEnergy
                  << " burn_mean_accel_m_s2=" << acceleration << '\n';
        std::cout << "PASS: " << checks
                  << " physical force, torque, gravity, atmosphere, coasting, moving-target and persistence "
                     "checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
