#include "infinite/Cockpit.hpp"
#include "infinite/FireControl.hpp"
#include "infinite/NpcCombat.hpp"
#include "infinite/Recovery.hpp"
#include <iostream>
#include <limits>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool x, const char* why) {
    ++checks;
    if (!x)
        throw std::runtime_error(why);
}
ShipCrew staffed() {
    ShipCrew c(52);
    c.toggleAutomatic();
    c.setWeaponLevels({1, 1, 1});
    c.reconcile({2, 0, 0});
    c.manWeapon(6, 1);
    c.manWeapon(7, 2);
    for (unsigned i = 0; i < 100; ++i)
        c.advance(.25);
    return c;
}
} // namespace
int main() {
    try {
        Universe u;
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        auto hostile = sampleRaider(u, home, 100);
        WeaponRay ray;
        ray.reference = home;
        ray.origin = hostile.position + glm::dvec3(1800, 0, 0);
        ray.seconds = 100;
        auto crew = staffed();
        WeaponSystem guns;
        guns.setLoadout({1, 1, 1});
        StationControl station;
        station.prepareWeapons(guns, crew);
        FireControl director;
        for (unsigned i = 0; i < 20; ++i) {
            guns.advance(.1);
            director.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, guns, std::span(&hostile, 1), .1,
                            false);
        }
        check(guns.shotsFired() == 0, "Crew fired without an order");
        director.order(FireOrder::Defend);
        for (unsigned i = 0; i < 30; ++i) {
            guns.advance(.1);
            director.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, guns, std::span(&hostile, 1), .1,
                            false);
        }
        check(guns.mount(1).shotsFired > 0 && guns.mount(0).shotsFired == 0 && guns.mount(2).shotsFired == 0,
              "Crew ignored occupied mount or physical firing arcs");
        const auto fired = guns.shotsFired();
        director.order(FireOrder::Focus);
        auto civilian = hostile;
        civilian.kind = ActivityKind::Ferry;
        civilian.hostile = false;
        for (unsigned i = 0; i < 30; ++i) {
            guns.advance(.1);
            director.update(u, ray, {1, 0, 0, 0}, civilian.id, 0, crew, guns, std::span(&civilian, 1), .1,
                            false);
        }
        check(guns.shotsFired() == fired, "Focus order attacked a civilian");
        WeaponSystem blocked;
        blocked.setLoadout({1, 1, 1});
        station.prepareWeapons(blocked, crew);
        civilian.id.slot = 200;
        civilian.position = glm::mix(ray.origin, hostile.position, .5);
        civilian.radius = 40;
        const std::array targets{hostile, civilian};
        for (unsigned i = 0; i < 40; ++i) {
            blocked.advance(.1);
            director.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, blocked, targets, .1, false);
        }
        check(blocked.shotsFired() == 0, "Autonomous gun fired through a friendly hull");
        // Acquisition is bounded, but every supplied hull still obstructs a shot.
        std::vector<ActivityBody> crowded(130, civilian);
        for (unsigned i = 0; i < crowded.size(); ++i) {
            crowded[i].id.slot = 1000 + i;
            crowded[i].position += glm::dvec3(0, 5000, 0);
        }
        crowded.front() = hostile;
        crowded.back() = civilian;
        for (unsigned i = 0; i < 40; ++i) {
            blocked.advance(.1);
            director.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, blocked, crowded, .1, false);
        }
        check(blocked.shotsFired() == 0, "Acquisition limit hid an intervening friendly hull");
        for (unsigned i = 0; i < 30; ++i) {
            blocked.advance(.1);
            director.update(u, ray, {1, 0, 0, 0}, hostile.id, 1, crew, blocked, std::span(&hostile, 1), .1,
                            false);
        }
        check(blocked.shotsFired() == 0, "Crew operated captain's gun");
        for (unsigned i = 0; i < 30; ++i)
            director.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, blocked, std::span(&hostile, 1), .1,
                            true);
        check(blocked.shotsFired() == 0, "Crew ignored flight safety inhibition");
        // The final muzzle trace must reject a friendly without consuming a shot.
        WeaponSystem guarded;
        guarded.setLoadout({1, 1, 1});
        station.prepareWeapons(guarded, crew);
        auto guardedRay = ray;
        guardedRay.direction = {-1, 0, 0};
        guardedRay.direct = true;
        check(!guarded.fireMount(1, u, guardedRay, targets, nullptr, &hostile.id) &&
                  guarded.mount(1).heat == 0 && guarded.mount(1).cooldown == 0 && guarded.damage().empty() &&
                  guarded.shotsFired() == 0,
              "Rejected autonomous shot consumed resources or damaged an intervening hull");
        check(guarded.fireMount(1, u, guardedRay, std::span(&hostile, 1), nullptr, &hostile.id) &&
                  guarded.damage().size() == 1 && guarded.damage()[0].id == hostile.id,
              "Safe intended-target shot failed after obstruction cleared");
        // Reframing the same physical eye and hull must preserve acquisition and shots.
        auto reframedRay = ray;
        reframedRay.reference = home.parent();
        reframedRay.origin = u.pointRelative(
            home, ray.origin / std::pow(10., u.describe(home).frame.logRadius), reframedRay.reference, 0);
        const auto rotation = u.relative(home, reframedRay.reference, 0).orientation;
        reframedRay.direction = rotation * ray.direction;
        reframedRay.right = rotation * ray.right;
        WeaponSystem localGuns, parentGuns;
        localGuns.setLoadout({1, 1, 1});
        parentGuns.setLoadout({1, 1, 1});
        station.prepareWeapons(localGuns, crew);
        station.prepareWeapons(parentGuns, crew);
        FireControl localControl, parentControl;
        localControl.order(FireOrder::Focus);
        parentControl.order(FireOrder::Focus);
        for (unsigned i = 0; i < 90; ++i) {
            localGuns.advance(1. / 60);
            parentGuns.advance(1. / 60);
            const auto localFired = localControl.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, localGuns,
                                                        targets, 1. / 60, false);
            const auto parentFired = parentControl.update(u, reframedRay, rotation, hostile.id, 0, crew,
                                                          parentGuns, targets, 1. / 60, false);
            check(localFired == parentFired, "Frame change altered friendly obstruction decisions");
        }
        check(localGuns.shotsFired() == 0 && parentGuns.shotsFired() == 0,
              "Mixed-frame acquisition bypassed an intervening friendly");
        for (unsigned i = 0; i < 90; ++i) {
            localGuns.advance(1. / 60);
            parentGuns.advance(1. / 60);
            const auto localFired = localControl.update(u, ray, {1, 0, 0, 0}, hostile.id, 0, crew, localGuns,
                                                        std::span(&hostile, 1), 1. / 60, false);
            const auto parentFired = parentControl.update(u, reframedRay, rotation, hostile.id, 0, crew,
                                                          parentGuns, std::span(&hostile, 1), 1. / 60, false);
            check(localFired == parentFired, "Frame change altered gunner firing cadence");
        }
        check(localGuns.shotsFired() > 0 && localGuns.damage()[0].hull == parentGuns.damage()[0].hull,
              "Mixed-frame acquisition changed damage or failed to fire");
        FireControl restored;
        check(restored.restore(director.serialize()) && restored.order() == FireOrder::Focus,
              "Fire order did not persist");
        check(!restored.restore("INFINITE_FIRE_CONTROL 1 8"), "Invalid order save accepted");
        WeaponSystem precision;
        auto craft = sampleRaider(u, home, 100);
        WeaponRay aimed;
        aimed.reference = home;
        aimed.seconds = 100;
        aimed.direct = true;
        auto shootComponent = [&](ShipSystem component) {
            const auto point = craft.position + craft.orientation * (systemOffset(component) * craft.radius);
            aimed.origin = point + glm::dvec3(0, 0, 600);
            aimed.direction = {0, 0, -1};
            aimed.targetSystem = component;
            for (unsigned i = 0; i < 4; ++i) {
                precision.advance(1);
                check(precision.fire(u, aimed, std::span(&craft, 1)), "Component shot failed");
            }
        };
        shootComponent(ShipSystem::Engines);
        auto crippled = craft;
        precision.apply(crippled);
        check(crippled.hull > 80 && crippled.systems[size_t(ShipSystem::Engines)] == 0 &&
                  glm::length(crippled.velocity) == 0,
              "Engine targeting destroyed hull or failed to stop propulsion");
        shootComponent(ShipSystem::NoseGun);
        auto disarmed = craft;
        precision.apply(disarmed);
        check(disarmed.hull > 60 && disarmed.systems[size_t(ShipSystem::NoseGun)] == 0,
              "Gun targeting failed independently of hull");
        WeaponSystem reloaded;
        check(reloaded.restore(precision.serialize(), u), "Subsystem damage did not persist");
        auto damaged = craft;
        reloaded.apply(damaged);
        check(damaged.systems == disarmed.systems, "Subsystem identities changed across restart");
        ShipCrew injured(52);
        injured.toggleAutomatic();
        injured.observeHull(100);
        injured.observeCombatHit(75, ShipSystem::NoseGun, 25);
        WeaponSystem shipGuns;
        StationControl{}.prepareWeapons(shipGuns, injured);
        check(injured.systemHealth(ShipSystem::NoseGun) == 0 &&
                  !shipGuns.fire(u, aimed, std::span(&craft, 1)),
              "Player fired a destroyed gun");
        ShipCrew repair(52);
        repair.toggleAutomatic();
        repair.observeHull(100);
        repair.observeCombatHit(99, ShipSystem::NoseGun, 1);
        const auto spareParts = repair.spares();
        repair.order(3, CrewOrder::Repair, CrewStation::Weapons);
        for (unsigned i = 0; i < 300; ++i)
            repair.advance(.25);
        check(repair.systemHealth(ShipSystem::NoseGun) == 100 && repair.spares() < spareParts,
              "Crew repair failed to restore component using spares");
        ShipCrew crewSave(52);
        check(crewSave.restore(injured.serialize()) && crewSave.systemHealth(ShipSystem::NoseGun) == 0,
              "Player component save lost a failed gun");
        struct Rig {
            Navigation nav;
            RegionCache cache;
            FlightController spatial;
            Pilot pilot;
            Rig(const Universe& u) : nav(u), cache(u), spatial(u, nav, cache), pilot(u, nav, spatial) {
                if (!nav.restore("0,0,0 24 1 0 0 0 0 0 3"))
                    throw std::runtime_error("Bad systems fixture");
                pilot.resetMotion();
            }
        } intact(u), failed(u);
        failed.pilot.setSystemAuthority(0, 0);
        HelmInput helm;
        helm.throttle = 1;
        helm.pitch = 1;
        for (unsigned i = 0; i < 60; ++i) {
            intact.pilot.update(helm, 1. / 60);
            failed.pilot.update(helm, 1. / 60);
        }
        check(intact.pilot.instruments().speed > failed.pilot.instruments().speed + 1,
              "Engine failure did not reduce real thrust");
        check(glm::length(failed.pilot.ship().angularVelocity) < 1e-12 &&
                  glm::length(intact.pilot.ship().angularVelocity) > .1,
              "Thruster failure did not remove steering torque");
        RecoveryController recovery;
        NpcPlayer ship;
        ship.reference = home;
        ship.position = craft.position + glm::dvec3(0, 0, 90);
        auto wreck = craft;
        wreck.disabled = true;
        wreck.velocity = {};
        for (unsigned i = 0; i < 70; ++i)
            recovery.update(u, ship, wreck, .1, true);
        check(recovery.progress() == 6, "Close slow physical recovery failed");
        ship.velocity = {4, 0, 0};
        check(!recovery.update(u, ship, wreck, .1, true) && recovery.progress() == 0,
              "Fast fly-by recovered a wreck");
        ship.velocity = {};
        ship.field = true;
        check(!recovery.update(u, ship, wreck, .1, true), "Travel field allowed recovery");
        ship.field = false;
        ship.size = .1;
        check(!recovery.update(u, ship, wreck, .1, true), "Shrunken ship recovered full-size cargo");
        ship.size = 1;
        ship.position += glm::dvec3(0, 0, 500);
        check(!recovery.update(u, ship, wreck, .1, true), "Remote ship recovered a wreck");
        ship.position = wreck.position + glm::dvec3(0, 0, 90);
        auto obstructing = wreck;
        obstructing.id.slot += 1;
        obstructing.position = glm::mix(ship.position, wreck.position, .5);
        obstructing.radius = 10;
        const std::array blockedRecovery{wreck, obstructing};
        for (unsigned i = 0; i < 70; ++i)
            check(!recovery.update(u, ship, wreck, .1, true, nullptr, blockedRecovery),
                  "Recovery passed through an intervening ship");
        check(recovery.progress() == 0 && recovery.status() == "RECOVERY / ACCESS OBSTRUCTED",
              "Obstructed recovery retained progress");
        for (unsigned i = 0; i < 70; ++i)
            recovery.update(u, ship, wreck, .1, true, nullptr, std::span(&wreck, 1));
        check(recovery.progress() == 6, "Clearing a ship obstruction did not permit recovery");
        check(!recovery.update(u, ship, wreck, .1, false, nullptr, std::span(&wreck, 1)) &&
                  recovery.progress() == 0,
              "Releasing recovery retained completed progress");
        for (glm::ivec2 size : {glm::ivec2(600, 1000), glm::ivec2(1440, 900), glm::ivec2(2160, 900)}) {
            CockpitViewport viewport(size.x, size.y, {0, 0, double(size.x), double(size.y)});
            FlightInstruments t;
            t.weapon = "PORT GUN / PULSE LASER III";
            t.weaponStatus = "GUN OFFLINE / F9 DAMAGE CONTROL";
            t.crewFire = "GUNNERS FOCUS HOSTILE / 6 HOLD 7 DEF 8 FOCUS";
            t.systemFault = "ENG 0 / RCS 24 / O2 65";
            t.systemTarget = "9 TARGET ENGINES 0%";
            t.recoveryStatus = "RECOVERING / 75% / HOLD 0";
            t.combatMission = "SURVIVOR ABOARD / RETURN TO PORT";
            t.localSystemTarget = {0, 0, -100};
            auto view = crew.view();
            view.flightOpen = true;
            auto hud = cockpitOverlay(t, viewport.aspect(), "", {}, {}, {}, false, viewport.projectionScale,
                                      nullptr, {1, 0, 0, 0}, &view);
            for (const auto& v : hud.lettering.vertices)
                check(std::isfinite(v.x) && std::isfinite(v.y) && v.x >= 0 &&
                          v.x <= 900 * viewport.aspect() && v.y >= 0 && v.y <= 900,
                      "Combat or recovery instruments exceed fitted cockpit");
        }
        std::cout
            << "PASS: " << checks
            << " autonomous gunner control, arcs, safety, friendly obstruction and persistence checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
