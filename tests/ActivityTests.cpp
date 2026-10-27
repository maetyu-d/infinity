#include "infinite/Activity.hpp"
#include "infinite/Piloting.hpp"
#include <algorithm>
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <set>
#include <stdexcept>
using namespace infinite;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
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
