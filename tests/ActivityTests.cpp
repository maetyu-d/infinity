#include "infinity/Activity.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/Planetology.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/SurfaceLife.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <oneapi/tbb/global_control.h>
#include <set>
#include <stdexcept>
using namespace infinity;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static void debrisTests(const Universe& u) {
    unsigned ringWorlds = 0, impactWorlds = 0, impactMoons = 0, empty = 0;
    double sampledDepth = 0, meanDepth = 0;
    std::optional<ActivityId> savedFragment;
    const auto galaxy = Address{}.child(Kind::Galaxy, 0);
    for (uint64_t star : {0, 1, 2, 9}) {
        const auto system = u.describe(galaxy.child(Kind::System, star));
        for (const auto& address : u.children(system)) {
            const auto planet = u.describe(address);
            const auto ring = planetTraits(planet).rings;
            const auto fragments = sampleOrbitalDebris(planet, 640);
            check(fragments.size() <= orbitalDebrisCapacity, "Debris exceeded its allocated ID range");
            if (fragments.empty()) {
                ++empty;
                check(!ring.present(), "Visible rings lost their physical fragments");
                continue;
            }
            ringWorlds += ring.present();
            impactWorlds += !ring.present();
            impactMoons += isMoon(address);
            const double radius = std::pow(10., planet.frame.logRadius);
            const double massSolar = 4.188790204786391 * planetDensity(planet) *
                                     radius * radius * radius / 1.98847e30;
            const double hill = planet.orbit.semiMajorMetres * (1 - planet.orbit.eccentricity) *
                                std::cbrt(massSolar / (3 * planet.orbit.centralMassSolar));
            const auto climate = planetClimate(planet);
            const double minimum = radius + (climate.pressureBar > 0 ? 7 * climate.scaleHeightMetres : 0);
            double uniform = 0;
            if (ring.present()) {
                for (unsigned i = 0; i < 64; ++i)
                    uniform += ringOpticalDepth(ring, glm::mix(ring.innerRadius, ring.outerRadius,
                                                                (i + .5) / 64));
                uniform /= 64;
            }
            std::set<ActivityId> ids;
            const auto old = activitySlots(planet);
            for (const auto& b : fragments) {
                check(isOrbitalDebris(b.id) && b.id.home == address && ids.insert(b.id).second &&
                          std::find(old.begin(), old.end(), b.id.slot) == old.end(),
                      "Orbital debris reused a legacy address");
                const auto repeat = sampleActivity(planet, b.id.slot, 640);
                check(repeat.position == b.position && repeat.orientation == b.orientation,
                      "Independent member and field sampling disagreed");
                const auto before = sampleActivity(planet, b.id.slot, 639.99),
                           after = sampleActivity(planet, b.id.slot, 640.01);
                check(glm::length((after.position - before.position) / .02 - b.velocity) < .025,
                      "Reported velocity does not follow the debris orbit");
                for (double time : {0., 60000., 1e12}) {
                    const auto body = sampleActivity(planet, b.id.slot, time);
                    const auto distance = glm::length(body.position);
                    check(distance > minimum && distance < hill * .24 && body.radius > 0 &&
                              std::abs(glm::length(body.orientation) - 1) < 1e-10,
                          "Debris left its atmosphere and Hill bounds");
                    if (ring.present())
                        check(distance > ring.innerRadius * radius && distance < ring.outerRadius * radius &&
                                  glm::dot(glm::normalize(glm::cross(body.position, body.velocity)),
                                           ring.axis) > .99999,
                              "Ring debris crossed a ring edge or left the ring plane");
                }
                if (ring.present()) {
                    sampledDepth += ringOpticalDepth(ring, glm::length(b.position) / radius);
                    meanDepth += uniform;
                    savedFragment = b.id;
                }
            }
        }
    }
    check(ringWorlds && impactWorlds && impactMoons && empty,
          "Orbital ensemble lacks ring fields, impact families, moon debris or quiet worlds");
    check(sampledDepth > meanDepth * 1.12, "Ring fragments ignored the dense bands");
    ActivityState clock;
    clock.selected = savedFragment;
    clock.advance(640);
    ActivityState restored;
    check(restored.restore(clock.serialize(), u) && restored.serialize() == clock.serialize(),
          "Ring fragment selection did not survive a save round trip");
    std::cout << "Debris fields ringed=" << ringWorlds << " impact=" << impactWorlds
              << " moons=" << impactMoons << " quiet=" << empty << '\n';
}
static void debrisLandingTest(const Universe& u) {
    const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    const auto home = u.describe(system.child(Kind::Planet, 13));
    constexpr double start = 640;
    const auto field = sampleOrbitalDebris(home, start);
    const auto chosen = std::find_if(field.begin(), field.end(), landableDebris);
    check(chosen != field.end(), "Landing fixture lacks a large ring fragment");
    const auto pad = debrisPad(*chosen, 1);
    const auto q = glm::quatLookAtRH(pad.forward, pad.up);
    const auto eye = pad.eye + pad.up * 38. + pad.forward * 19.;
    const auto offset = eye / 1000. - q * glm::dvec3(0, 0, 1);
    std::ostringstream view;
    view << std::setprecision(17) << home.address.str() << " 3 " << q.w << ' ' << q.x << ' '
         << q.y << ' ' << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
    Navigation nav(u);
    check(nav.restore(view.str()), "Invalid debris landing view");
    RegionCache cache(u);
    FlightController flight(u, nav, cache);
    Pilot pilot(u, nav, flight);
    pilot.resetMotion();
    pilot.setDebrisClock(start);
    pilot.setVelocity(pad.velocity);
    check(pilot.landOnDebris(*chosen), "Matched approach did not acquire the rotating face");
    double seconds = start;
    auto tick = [&] {
        seconds += 1. / 60;
        pilot.setDebrisClock(seconds);
        pilot.update({}, 1. / 60);
    };
    for (int i = 0; i < 2700 && !pilot.ship().landed; ++i)
        tick();
    if (!pilot.landedDebris() || pilot.ship().hull != 100) {
        const auto report = pilot.instruments();
        const auto moving = debrisPad(sampleActivity(home, chosen->id.slot, seconds), 1);
        const auto eyeNow = u.pointRelative(nav.anchor, nav.cameraPosition() *
            std::pow(10., nav.logDistance - u.describe(nav.anchor).frame.logRadius), home.address, 0);
        const auto offsetNow = eyeNow - moving.eye;
        const auto attitude = u.relative(nav.anchor, home.address, 0).orientation * nav.orientation;
        std::cerr << report.phase << " altitude=" << glm::dot(offsetNow, moving.up)
                  << " offset=" << glm::length(offsetNow) << " relative=" << report.relativeSpeed
                  << " up=" << glm::dot(attitude * glm::dvec3(0, 1, 0), moving.up)
                  << " forward=" << glm::dot(attitude * glm::dvec3(0, 0, -1), moving.forward)
                  << " hull=" << pilot.ship().hull << " frame=" << nav.anchor.str() << '\n';
    }
    check(pilot.landedDebris() && *pilot.landedDebris() == chosen->id && pilot.ship().hull == 100,
          "Landing controller did not settle safely on the fragment");
    auto checkPosition = [&] {
        const auto moving = debrisPad(sampleActivity(home, chosen->id.slot, seconds), 1);
        const auto point = u.pointRelative(nav.anchor, nav.cameraPosition() *
                std::pow(10., nav.logDistance - u.describe(nav.anchor).frame.logRadius), home.address, 0);
        check(glm::length(point - moving.eye) < 1e-3, "Landed ship slipped across the moving face");
    };
    checkPosition();
    const auto saved = pilot.serialize();
    check(pilot.restore(saved) && pilot.landedDebris() && *pilot.landedDebris() == chosen->id,
          "Debris landing did not round trip through ship persistence");
    for (int i = 0; i < 60; ++i)
        tick();
    checkPosition();
    pilot.launch();
    for (int i = 0; i < 145; ++i)
        tick();
    check(!pilot.ship().landed && pilot.instruments().altitude > 8, "Lift-off did not clear the moving face");
    std::cout << "DEBRIS_LANDING_TOUCHDOWN hull=" << pilot.ship().hull << " lift_agl="
              << pilot.instruments().altitude << '\n';
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        debrisTests(u);
        debrisLandingTest(u);
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto planet = u.describe(home);
        const auto fingerprint = descriptorFingerprint(planet);
        const auto geometry = geometryFingerprint(*GeometryGenerator(u).generate(planet, 0));
        std::set<ActivityId> identities;
        for (const auto& b : sampleActivity(planet, 12)) {
            check(identities.insert(b.id).second, "Duplicate activity ID");
            check(ActivityId::parse(b.id.str()) == b.id, "Activity identity did not round trip");
            check(b.id.home == home, "Activity replaced a universe address");
            const auto repeat = sampleActivity(planet, b.id.slot, 12);
            check(repeat.position == b.position && repeat.velocity == b.velocity && repeat.name == b.name,
                  "Activity depends on generation order");
            for (double t : {0., .001, 12., 241., 899., 3600., 1e8}) {
                const auto at = sampleActivity(planet, b.id.slot, t);
                check(glm::length(at.position) > std::pow(10., planet.frame.logRadius) * 1.05,
                      "Activity path penetrates its planet");
                check(std::isfinite(glm::length(at.position)) && std::isfinite(glm::length(at.velocity)),
                      "Non-finite activity pose");
                check(std::abs(glm::length(at.orientation) - 1) < 1e-12, "Invalid craft orientation");
                if (t > 0 && t < 4000) {
                    const double eps = 1e-3;
                    const auto a = sampleActivity(planet, b.id.slot, t - eps),
                               z = sampleActivity(planet, b.id.slot, t + eps);
                    check(glm::length((z.position - a.position) / (2 * eps) - at.velocity) < .02,
                          "Route velocity differs from its continuous trajectory");
                }
            }
            if (b.kind != ActivityKind::Harbour) {
                double travelled = 0;
                for (int i = 1; i <= 20; i++)
                    travelled =
                        std::max(travelled, glm::length(sampleActivity(planet, b.id.slot, i * 100.).position -
                                                        b.position));
                check(travelled > 1, "An active vessel never moves");
            }
        }
        check(identities.size() > 35 && identities.size() < 90, "Planetary activity density is not bounded");
        auto cold = planet, rich = planet;
        cold.environment.temperature = 90;
        cold.environment.waterActivity = 0;
        rich.environment.temperature = 285;
        rich.environment.waterActivity = 1;
        check(activitySlots(rich).size() > activitySlots(cold).size(),
              "Activity ignores inherited habitation constraints");
        for (auto slot : activitySlots(cold)) {
            const auto a = sampleActivity(cold, slot, 20), b = sampleActivity(rich, slot, 20);
            check(a.id == b.id && a.position == b.position,
                  "Higher density replaced existing actors or paths");
        }
        ActivityState clock;
        clock.selected = ActivityId{home, 200};
        clock.advance(120);
        clock.enabled = false;
        clock.advance(10);
        const auto saved = clock.serialize();
        ActivityState restart;
        check(restart.restore(saved, u) && restart.serialize() == saved,
              "Activity clock/identity did not persist");
        check(clock.seconds == 130, "Hidden activity stopped its clock");
        check(!restart.restore("INFINITE_ACTIVITY 1 nan 1 -", u) && restart.serialize() == saved,
              "Malformed state corrupted activity");
        check(!restart.restore("INFINITE_ACTIVITY 1 0 1 " + home.str() + "@activity1:4096", u),
              "Unknown activity slot accepted");
        size_t bytes = 0;
        for (unsigned kind = 0; kind < 6; kind++)
            for (unsigned detail = 0; detail < 3; detail++) {
                const auto mesh = activityMesh(ActivityKind(kind), detail);
                bytes += mesh.bytes();
                check(!mesh.vertices.empty() && mesh.indexCount() % 3 == 0, "Missing activity mesh");
                for (size_t i = 0; i < mesh.indexCount(); i++)
                    check(mesh.index(i) < mesh.vertices.size(), "Activity mesh index outside buffer");
                for (const auto& v : mesh.vertices)
                    check(std::isfinite(v.x + v.y + v.z), "Non-finite activity vertex");
            }
        check(bytes < 2 * 1024 * 1024, "Activity template GPU memory exceeded its bound");
        Navigation nav(u);
        RegionCache cache(u);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        pilot.startInSystem();
        const auto body = sampleActivity(u.describe(nav.anchor), 0, 12);
        const FlightWaypoint point{nav.anchor, body.position, body.velocity, body.radius * 4, body.name};
        const auto before = nav.serialize();
        const auto velocity = pilot.ship().velocity;
        pilot.navigateWaypoint(point);
        check(nav.serialize() == before && pilot.ship().velocity == velocity,
              "Rendezvous teleported or reset the ship");
        for (int i = 0; i < 5400; i++) {
            cache.beginFrame();
            pilot.trackWaypoint(point);
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        std::cout << "HARBOUR_APPROACH error=" << pilot.instruments().approachError
                  << " speed=" << pilot.instruments().speed << " phase=" << pilot.instruments().phase
                  << " anchor=" << nav.anchor.str() << '\n';
        check(pilot.instruments().approachError < 15 && pilot.instruments().speed < 3,
              "Harbour approach did not brake into a holding point");
        check(pilot.instruments().destination == body.name, "Moving waypoint lost its contact identity");
        const auto contactPoint = u.pointRelative(home, body.position / std::pow(10., planet.frame.logRadius),
                                                  nav.anchor, nav.logDistance);
        check(std::abs(pilot.instruments().range - glm::length(contactPoint - nav.cameraPosition()) *
                                                       std::pow(10., nav.logDistance)) < 1e-5,
              "Contact range displays an invisible holding point instead of the real vessel");
        check(glm::dot(nav.orientation * glm::dvec3(0, 0, -1),
                       glm::normalize(contactPoint - nav.cameraPosition())) > .99,
              "Holding camera turned away from the real harbour");
        for (int i = 0; i < 5400; i++) {
            const auto moving = sampleActivity(planet, 300, 12 + i / 60.);
            const FlightWaypoint intercept{home, moving.position, moving.velocity, 150, moving.name};
            pilot.trackWaypoint(intercept);
            if (i == 0)
                pilot.navigateWaypoint(intercept);
            cache.beginFrame();
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        const auto moving = sampleActivity(planet, 300, 12 + 5399 / 60.);
        const auto targetVelocity = u.relative(home, nav.anchor, 0).orientation * moving.velocity;
        std::cout << "MOVING_RENDEZVOUS error=" << pilot.instruments().approachError
                  << " relative_speed=" << glm::length(pilot.ship().velocity - targetVelocity) << '\n';
        check(pilot.instruments().approachError < 40 &&
                  glm::length(pilot.ship().velocity - targetVelocity) < 10,
              "Rendezvous failed to track the actual moving contact");
        const auto movingPoint = u.pointRelative(
            home, moving.position / std::pow(10., planet.frame.logRadius), nav.anchor, nav.logDistance);
        check(glm::dot(nav.orientation * glm::dvec3(0, 0, -1),
                       glm::normalize(movingPoint - nav.cameraPosition())) > .95,
              "Rendezvous camera lost the actual moving vessel");
        pilot.trackWaypoint(point);
        const auto beforeUnits = pilot.instruments().approachError;
        nav.setRenderScale(3);
        pilot.synchronizeFrame();
        pilot.trackWaypoint(point);
        check(std::abs(pilot.instruments().approachError - beforeUnits) < 1e-4,
              "Reference units moved the rendezvous target");
        check(descriptorFingerprint(u.describe(home)) == fingerprint &&
                  geometryFingerprint(*GeometryGenerator(u).generate(u.describe(home), 0)) == geometry,
              "Activity mutated the universe");
        cache.wait();
        std::cout << "PASS: " << checks
                  << " stable activity IDs, environment density, continuous routes, persistence, mesh "
                     "budgets and rendezvous checks / templates="
                  << bytes << " bytes\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
