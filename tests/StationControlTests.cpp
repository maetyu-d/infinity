#include "infinite/Cockpit.hpp"
#include "infinite/StationControl.hpp"
#include <iostream>
#include <limits>
#include <oneapi/tbb/global_control.h>
#include <sstream>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
struct Rig {
    Navigation nav;
    RegionCache cache;
    FlightController flight;
    Pilot pilot;
    Rig(const Universe& u) : nav(u), cache(u), flight(u, nav, cache), pilot(u, nav, flight) {
        check(nav.restore("0,0,0 24 1 0 0 0 0 0 3"), "Bad station fixture");
        pilot.resetMotion();
    }
};
void tick(ShipCrew& c, unsigned n = 80) {
    for (unsigned i = 0; i < n; ++i)
        c.advance(.25);
}
ShipCrew staffed() {
    ShipCrew c(50);
    c.toggleAutomatic();
    c.setWeaponLevels({1, 1, 2});
    c.reconcile({2, 0, 0});
    c.manWeapon(6, 1);
    c.manWeapon(7, 2);
    tick(c);
    return c;
}
double headingError(glm::dquat a, glm::dquat b) {
    return std::acos(std::clamp(glm::dot(a * glm::dvec3(0, 0, -1), b * glm::dvec3(0, 0, -1)), -1., 1.));
}
} // namespace
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 3);
        Universe u;
        Rig r(u);
        StationControl control;
        ShipCrew single(50);
        check(!control.request(0, single, r.pilot, r.nav), "Single gun allowed a swap");
        auto c = staffed();
        for (unsigned invalid : {3u, 100u})
            check(!control.request(invalid, c, r.pilot, r.nav), "Invalid mount allowed a swap");
        c.order(6, CrewOrder::Rest, CrewStation::Bunks);
        check(!control.request(1, c, r.pilot, r.nav), "Off-duty gunner allowed a swap");
        c.manWeapon(6, 1);
        tick(c);
        const auto camera = r.nav.serialize(), ship = r.pilot.serialize();
        const auto id = c.members()[6].id;
        const auto room = c.members()[6].room;
        check(control.request(1, c, r.pilot, r.nav), "Ready staffed extra gun rejected");
        check(control.requested() && !control.active(), "Handover skipped crew walking");
        check(r.nav.serialize() == camera && r.pilot.serialize() == ship,
              "Swap moved or reset the physical hull");
        check(c.members()[6].id == id && c.members()[6].room == room &&
                  c.members()[6].target == CrewStation::Bridge && c.members()[6].weaponMount == 1,
              "Swap replaced, teleported or unbound the selected gunner");
        check(c.gunnerReadiness(1) == 0 && c.gunnerReadiness(2) > 0, "Relief operated two consoles");
        check(!control.request(2, c, r.pilot, r.nav), "Nested swap allowed");
        c.advance(.25);
        check(c.members()[6].travel > 0, "Relief did not use existing corridors");
        ShipCrew pendingSave(50);
        pendingSave.setWeaponLevels({1, 1, 2});
        check(pendingSave.restore(c.serialize()) && !pendingSave.reliefHelmId() &&
                  pendingSave.members()[6].target == CrewStation::Weapons &&
                  pendingSave.members()[6].travel == c.members()[6].travel,
              "Pending save retained remote authority or changed a corridor leg");
        tick(c);
        control.synchronize(c, r.pilot, r.nav);
        check(control.active() && c.members()[6].room == CrewStation::Bridge && c.reliefHelmReadiness() > 0,
              "Captain did not receive gun after relief arrived");
        const auto xp = c.members()[6].experience;
        c.advance(1, 0, true, true, 2);
        check(c.members()[6].experience[0] > xp[0] && c.members()[6].experience[2] == xp[2],
              "Relief trained gunnery instead of piloting");
        const auto physical = r.nav.serialize(), physicalShip = r.pilot.serialize();
        control.aim(.5, 1, {45, -20}, .1);
        check(r.nav.serialize() == physical && r.pilot.serialize() == physicalShip,
              "Turret aiming rotated or accelerated the hull");
        check(headingError(control.aim(), glm::dquat(1, 0, 0, 0)) > .1, "Gun has no independent aim");
        const auto aimed = control.aim();
        control.aim(0, 0, {std::numeric_limits<double>::quiet_NaN(), 0}, .1);
        check(glm::length(aimed - control.aim()) == 0, "Invalid mouse corrupted gun attitude");
        WeaponSystem guns;
        guns.setLoadout({1, 1, 2});
        control.prepareWeapons(guns, c);
        check(guns.mount(1).crewReadiness == 1 && guns.mount(2).crewReadiness > 0,
              "Captain's console not ready or other gunner lost readiness");
        auto t = r.pilot.instruments();
        guns.annotate(t);
        control.annotate(t, c, guns);
        check(t.playerGun == 1 && t.reliefHelm == c.members()[6].name && t.helmQuality > 0 &&
                  t.weapon.find("PORT GUN") != std::string::npos && t.gunYaw > 0 &&
                  t.phase == "CREW / COURSE HOLD",
              "HUD omitted active station, pilot or traverse");
        for (unsigned i = 0; i < 80; ++i)
            control.aim(1, 1, {2000, -2000}, .1);
        t = r.pilot.instruments();
        control.annotate(t, c, guns);
        check(std::abs(t.gunYaw - 120) < 1e-8 && std::abs(t.gunPitch - 70) < 1e-8,
              "Turret exceeded physical traverse stops");
        for (glm::ivec2 size : {glm::ivec2(600, 1000), {960, 1280}, {900, 900}, {1440, 900}, {2160, 900}}) {
            const CockpitViewport viewport(size.x, size.y, {0, 0, double(size.x), double(size.y)});
            const double aspect = viewport.aspect();
            auto crewView = c.view(6);
            for (bool console : {false, true}) {
                crewView.flightOpen = console;
                const auto stationCard = weaponStationCard(aspect, console);
                const CockpitLayout layout(aspect);
                const CrewFlightLayout orders(aspect);
                check(!stationCard.overlaps(layout.life) && !stationCard.overlaps(layout.celestial) &&
                          !stationCard.overlaps(layout.systems) &&
                          (!console ||
                           (!stationCard.overlaps(orders.panel) && !stationCard.overlaps(orders.weapon))) &&
                          stationCard.x >= 0 && stationCard.x + stationCard.w <= 900 * aspect,
                      "Station card overlaps another instrument in the fitted viewport");
                auto hud = cockpitOverlay(t, aspect, "", {}, {}, {}, false, viewport.projectionScale, nullptr,
                                          t.gunAim, &crewView);
                check(std::any_of(hud.panels.vertices.begin(), hud.panels.vertices.end(),
                                  [&](const auto& v) {
                                      return std::abs(v.x - (450 * aspect - 1)) < .001 &&
                                             std::abs(v.y - 449) < .001;
                                  }),
                      "Turret reticle follows ship nose instead of gun sight");
                for (const auto& v : hud.lettering.vertices)
                    check(std::isfinite(v.x) && std::isfinite(v.y) && v.x >= 0 && v.x <= 900 * aspect &&
                              v.y >= 0 && v.y <= 900,
                          "Station HUD exceeds canvas");
            }
        }
        ShipCrew saved(50);
        saved.setWeaponLevels({1, 1, 2});
        check(saved.restore(c.serialize()) && !saved.reliefHelmId() && saved.playerGun() == noWeaponMount &&
                  saved.members()[6].target == CrewStation::Weapons,
              "Save left gun authority active after restart");
        tick(saved);
        check(saved.gunnerReadiness(1) > 0, "Saved relief failed to return to gun");
        const auto beforeReturn = r.pilot.serialize();
        control.returnToHelm(c);
        check(!control.requested() && !c.reliefHelmId() && r.pilot.serialize() == beforeReturn,
              "Returning to helm changed physical state");
        check(c.members()[6].target == CrewStation::Weapons && c.gunnerReadiness(1) == 0,
              "Gunner instantly remanned gun from bridge");
        tick(c);
        check(c.gunnerReadiness(1) > 0, "Gunner failed return walk");
        check(control.request(1, c, r.pilot, r.nav), "Second handover failed");
        tick(c);
        control.synchronize(c, r.pilot, r.nav);
        c.order(6, CrewOrder::Rest, CrewStation::Bunks);
        control.synchronize(c, r.pilot, r.nav);
        check(!control.requested() && c.members()[6].order == CrewOrder::Rest &&
                  c.members()[6].target == CrewStation::Bunks,
              "Emergency return overwrote crew order");
        for (unsigned failure = 0; failure < 5; ++failure) {
            auto crew = staffed();
            StationControl s;
            check(s.request(1, crew, r.pilot, r.nav), "Safety fixture request failed");
            tick(crew);
            s.synchronize(crew, r.pilot, r.nav);
            if (failure == 0)
                crew.setWeaponLevels({1, 0, 2});
            if (failure == 1)
                crew.dismiss(6);
            if (failure == 2)
                crew.assign(6, CrewStation::Navigation);
            if (failure == 3) {
                crew.vent(CrewStation::Bridge);
                tick(crew, 60);
            }
            if (failure == 4) {
                crew.vent(CrewStation::Weapons);
                tick(crew, 60);
            }
            s.synchronize(crew, r.pilot, r.nav);
            check(!s.requested() && !crew.reliefHelmId(), "Unsafe or unavailable relief retained control");
        }
        // Same ship, target heading and time: pilot quality changes correction,
        // while the existing assisted controller still provides every force.
        Rig novice(u), expert(u);
        const auto frame = novice.nav.anchor;
        const auto course = glm::angleAxis(.9, glm::dvec3(0, 1, 0));
        for (unsigned i = 0; i < 60; ++i) {
            novice.pilot.updateCrew(frame, course, .25, 1. / 60);
            expert.pilot.updateCrew(frame, course, 1.4, 1. / 60);
        }
        const double noviceError = headingError(novice.nav.orientation, course),
                     expertError = headingError(expert.nav.orientation, course);
        check(expertError + .08 < noviceError, "Pilot skill did not affect heading correction");
        for (unsigned i = 0; i < 600; ++i)
            novice.pilot.updateCrew(frame, course, .25, 1. / 60);
        check(headingError(novice.nav.orientation, course) < .01 &&
                  glm::length(novice.pilot.ship().angularVelocity) < .02,
              "Novice course hold never settled safely");
        novice.pilot.toggleAssist();
        novice.pilot.updateCrew(frame, course, .5, .1);
        check(!novice.pilot.ship().assist, "Crew overwrote captain's assist preference");
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        auto body = sampleActivity(u.describe(home), 300, 12);
        Rig slowApproach(u), fastApproach(u);
        for (auto* flight : {&slowApproach, &fastApproach}) {
            const auto offset = (body.position + glm::dvec3(0, 0, 650)) / 1000. - glm::dvec3(0, 0, 1);
            std::ostringstream text;
            text.precision(17);
            text << home.str() << " 3 1 0 0 0 " << offset.x << ' ' << offset.y << ' ' << offset.z;
            check(flight->nav.restore(text.str()), "Bad crew approach fixture");
            flight->pilot.resetMotion();
            FlightWaypoint destination;
            destination.reference = home;
            destination.metres = body.position + glm::dvec3(0, 0, 500);
            destination.standOff = 20;
            destination.label = "CREW APPROACH TEST";
            flight->pilot.navigateWaypoint(destination);
        }
        for (unsigned i = 0; i < 120; ++i) {
            slowApproach.pilot.updateCrew(home, glm::dquat(1, 0, 0, 0), .25, 1. / 60);
            fastApproach.pilot.updateCrew(home, glm::dquat(1, 0, 0, 0), 1.4, 1. / 60);
        }
        check(slowApproach.pilot.instruments().navigating && fastApproach.pilot.instruments().navigating &&
                  fastApproach.pilot.instruments().speed > slowApproach.pilot.instruments().speed * 1.2,
              "Crew skill did not affect active approach or cancelled navigation");
        WeaponRay ray;
        ray.reference = home;
        ray.origin = body.position + glm::dvec3(0, 0, 600);
        ray.seconds = 12;
        ray.direct = true;
        ray.direction = glm::normalize(body.position - (ray.origin - glm::dvec3(4, 0, 0)));
        WeaponSystem isolated;
        isolated.setLoadout({1, 1, 2});
        isolated.setMountReadiness(1, 1);
        check(isolated.fireMount(1, u, ray, std::span(&body, 1)), "Player gun did not fire");
        isolated.apply(body);
        check(body.hull == 90 && isolated.mount(0).shotsFired == 0 && isolated.mount(2).shotsFired == 0 &&
                  isolated.damage().size() == 1,
              "Player station fired other guns or missed shared damage ledger");
        // The request is allowed for two auxiliary guns even if the nose was sold.
        auto aux = staffed();
        aux.setWeaponLevels({0, 1, 2});
        StationControl a;
        check(a.request(2, aux, r.pilot, r.nav), "Two-gun ship required a nose gun");
        a.returnToHelm(aux);
        aux.setWeaponLevels({0, 0, 2});
        check(!a.request(2, aux, r.pilot, r.nav), "One auxiliary alone met multi-gun requirement");
        // Reference-frame and view-scale changes retain the same physical course.
        Rig reframed(u);
        auto frameCrew = staffed();
        StationControl frameStation;
        check(frameStation.request(2, frameCrew, reframed.pilot, reframed.nav),
              "Frame fixture request failed");
        tick(frameCrew);
        frameStation.synchronize(frameCrew, reframed.pilot, reframed.nav);
        frameStation.aim(0, 0, {25, -20}, .1);
        const auto gunAim = frameStation.aim();
        const auto oldFrame = reframed.nav.anchor;
        const auto oldCourse = reframed.nav.orientation;
        reframed.nav.changeFrame(home.parent());
        for (double log : {21., 12., 3., -9.}) {
            reframed.nav.setRenderScale(log);
            frameStation.synchronize(frameCrew, reframed.pilot, reframed.nav);
            frameStation.fly(reframed.pilot, frameCrew, .02);
            const auto globalCourse =
                u.relative(reframed.nav.anchor, oldFrame, 0).orientation * reframed.nav.orientation;
            check(frameStation.active() && glm::length(gunAim - frameStation.aim()) < 1e-12 &&
                      headingError(globalCourse, oldCourse) < 1e-5,
                  "Reference or render-scale change broke station or physical course");
        }
        Rig drive(u);
        drive.pilot.toggleCruise();
        check(!a.request(1, c, drive.pilot, drive.nav), "Crew swap allowed active travel drive");
        std::cout << "PASS: " << checks
                  << " station handover, independent aim, selected fire, real walking, safety, saves, HUD "
                     "and crew piloting checks\n";
        std::cout << "SKILL heading_error novice=" << noviceError << " expert=" << expertError << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
