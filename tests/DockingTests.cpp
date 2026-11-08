#include "infinity/Cockpit.hpp"
#include "infinity/Piloting.hpp"
#include <iostream>
#include <set>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
void place(const Universe& u, Navigation& n, const Address& home, glm::dvec3 eye, glm::dquat q) {
    const double log = u.describe(home).frame.logRadius;
    const auto offset = eye / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
    std::ostringstream s;
    s.precision(17);
    s << home.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
      << ' ' << offset.y << ' ' << offset.z;
    check(n.restore(s.str()), "Invalid docking fixture");
}
FlightWaypoint waypoint(const ActivityBody& b) {
    return {b.id.home, b.position, b.velocity, b.radius * 4, b.name, false, dockingBerth(b)};
}
} // namespace
int main() {
    try {
        Universe u;
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto planet = u.describe(home);
        const auto fingerprint = descriptorFingerprint(planet);
        auto body = sampleActivity(planet, 0, 12);
        const auto originalBody = body;
        check(bool(dockingBerth(body)), "Healthy harbour has no real berth");
        auto disabled = body;
        disabled.disabled = true;
        check(!dockingBerth(disabled), "Disabled station accepted a docking clearance");
        disabled = body;
        disabled.kind = ActivityKind::Freighter;
        check(!dockingBerth(disabled), "Freighter acquired a fictional station berth");
        const auto lane = dockingLane(*dockingBerth(body), 10, DockingPhase::Glide);
        check(lane.indexCount() < 512 && lane.bytes() < 16384 && lane.indexCount() % 2 == 0,
              "Arrival lane exceeded its bounded line budget");
        for (unsigned scenario = 0; scenario < 3; ++scenario) {
            body = scenario == 1 ? sampleActivity(planet, 1, 12) : originalBody;
            if (scenario == 2)
                body.velocity = body.orientation * glm::dvec3(2, .3, .5);
            Navigation nav(u);
            RegionCache cache(u, 4 * 1024 * 1024, 1);
            FlightController spatial(u, nav, cache);
            Pilot pilot(u, nav, spatial);
            const auto berth = *dockingBerth(body);
            const glm::dvec3 offset =
                scenario == 1 ? glm::dvec3(-600, 420, -2500) : glm::dvec3(150, 65, berth.laneLength() + 650);
            auto q = body.orientation * glm::angleAxis(scenario == 1 ? 1.0 : .3, glm::dvec3(0, 0, 1));
            place(u, nav, home, berth.metres + body.orientation * offset, q);
            pilot.resetMotion();
            pilot.setVelocity(body.velocity + body.orientation * glm::dvec3(6, -2, -8));
            const auto oldPosition = nav.serialize();
            const auto oldState = pilot.ship().velocity;
            pilot.navigateWaypoint(waypoint(body));
            check(nav.serialize() == oldPosition && pilot.ship().velocity == oldState,
                  "Docking request teleported the ship or erased momentum");
            std::set<DockingPhase> phases;
            double maxStep = 0;
            double hullClearance = 1e30;
            for (unsigned i = 0; i < 30000 && !pilot.instruments().docking.docked; ++i) {
                body.position += body.velocity / 60.;
                pilot.trackWaypoint(waypoint(body));
                auto before = u.pointRelative(
                    nav.anchor,
                    nav.cameraPosition() *
                        std::pow(10., nav.logDistance - u.describe(nav.anchor).frame.logRadius),
                    home, 0);
                pilot.update({}, 1. / 60);
                auto after = u.pointRelative(
                    nav.anchor,
                    nav.cameraPosition() *
                        std::pow(10., nav.logDistance - u.describe(nav.anchor).frame.logRadius),
                    home, 0);
                maxStep = std::max(maxStep, glm::length(after - before));
                hullClearance = std::min(hullClearance, glm::length(after - body.position) / body.radius);
                phases.insert(pilot.instruments().docking.phase);
                if (i == 120)
                    nav.setRenderScale(nav.logDistance + 2);
            }
            const auto& d = pilot.instruments().docking;
            std::cout << "DOCK scenario=" << scenario << " phase=" << int(d.phase) << " range=" << d.range
                      << " relative_speed=" << d.relativeSpeed << " max_step=" << maxStep << '\n';
            check(d.docked && d.range < .001 && d.relativeSpeed < .001,
                  "Staged docking failed to capture gently");
            check(.00005 * std::pow(10., nav.logDistance) < .1,
                  "Planet-sized render units clipped the nearby docking deck");
            check(phases.contains(DockingPhase::Align) && phases.contains(DockingPhase::Glide) &&
                      phases.contains(DockingPhase::Capture),
                  "Docking skipped its visible alignment/glide/capture stages");
            check(maxStep < 3, "Docking made an unbounded positional jump");
            check(hullClearance > 1.05, "Arrival cut through the retained station hull");
            const auto state = pilot.serialize();
            const auto dockedNav = nav.serialize();
            const auto capturedBody = body;
            check(state.starts_with("INFINITY_SHIP 5 "), "Docking rewrote the established ship-save format");
            for (auto size : {glm::ivec2(600, 1000), glm::ivec2(1440, 900)}) {
                CockpitViewport viewport(size.x, size.y, {0, 0, double(size.x), double(size.y)});
                auto ui = cockpitOverlay(pilot.instruments(), viewport.aspect(), "", {}, {}, {}, false,
                                         viewport.projectionScale);
                for (const auto& v : ui.lettering.vertices)
                    if (!std::isfinite(v.x + v.y) || v.x < 0 || v.x > 900 * viewport.aspect() || v.y < 0 ||
                        v.y > 900)
                        throw std::runtime_error("Docking instruments exceed the fitted viewport");
            }
            pilot.launch();
            check(pilot.instruments().docking.phase == DockingPhase::Departure,
                  "Enter did not release clamps into departure");
            for (unsigned i = 0; i < 18000 && pilot.instruments().docking.active; ++i) {
                body.position += body.velocity / 60.;
                pilot.trackWaypoint(waypoint(body));
                pilot.update({}, 1. / 60);
            }
            check(!pilot.instruments().docking.active &&
                      pilot.instruments().docking.offset.z > berth.laneLength(),
                  "Departure failed to turn and fly clear of the physical berth");
            pilot.navigateWaypoint(waypoint(body));
            pilot.update(HelmInput{.strafe = 1}, 1. / 60);
            check(!pilot.instruments().docking.active && !pilot.instruments().navigating,
                  "Manual input failed to take the helm during docking");
            check(nav.restore(dockedNav) && pilot.restore(state),
                  "Docked physical state failed the existing save round trip");
            pilot.trackWaypoint(waypoint(capturedBody));
            check(pilot.instruments().docking.docked, "Restored physical berth pose lost its magnetic clamp");
        }
        check(descriptorFingerprint(u.describe(home)) == fingerprint &&
                  sampleActivity(planet, 0, 12).position == originalBody.position,
              "Docking changed the underlying planet or harbour identity/orbit");
        {
            Navigation nav(u);
            RegionCache cache(u, 4 * 1024 * 1024, 1);
            FlightController spatial(u, nav, cache);
            Pilot pilot(u, nav, spatial);
            const auto berth = *dockingBerth(originalBody);
            place(u, nav, home, berth.metres + berth.orientation * glm::dvec3(.3, .1, 10), berth.orientation);
            pilot.resetMotion();
            pilot.navigateWaypoint(waypoint(originalBody));
            check(pilot.instruments().docking.phase == DockingPhase::Capture,
                  "A manually aligned final approach was sent back to the outer gate");
            for (unsigned i = 0; i < 5000 && !pilot.instruments().docking.docked; ++i)
                pilot.update({}, 1. / 60);
            check(pilot.instruments().docking.docked, "Manual final capture failed to latch softly");
            pilot.toggleCruise();
            check(!pilot.ship().cruise && pilot.instruments().docking.docked,
                  "Cruise engaged inside closed magnetic clamps");
        }
        std::cout << "PASS: " << checks
                  << " physical docking, roll alignment, moving berth, rebase, abort, departure and "
                     "fitted-HUD checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
