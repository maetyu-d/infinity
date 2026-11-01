#include "infinite/StationControl.hpp"
#include <numbers>
namespace infinite {
namespace {
bool available(const Pilot& pilot) {
    const auto& s = pilot.ship();
    return !s.landed && s.hull > 0 && !s.cruise && !pilot.instruments().fieldActive &&
           std::abs(s.logSize - s.targetLogSize) <= .01;
}
} // namespace
bool StationControl::request(unsigned mount, ShipCrew& crew, const Pilot& pilot, const Navigation&) {
    if (requested()) {
        status_ = "F10 RETURNS TO HELM BEFORE ANOTHER SWAP";
        return false;
    }
    if (!available(pilot)) {
        status_ = "STATION SWAP / FLIGHT ONLY / STABILISE SIZE AND DISENGAGE DRIVE";
        return false;
    }
    if (!crew.beginGunRelief(mount)) {
        status_ = "NEED TWO FITTED GUNS / A READY GUNNER / SAFE BRIDGE AND BAY";
        return false;
    }
    mount_ = mount;
    active_ = false;
    yaw_ = pitch_ = 0;
    status_ = "RELIEF WALKING TO BRIDGE / KEEP HELM / F10 CANCEL";
    return true;
}
void StationControl::returnToHelm(ShipCrew& crew, std::string reason) {
    crew.endGunRelief();
    mount_ = noWeaponMount;
    active_ = false;
    yaw_ = pitch_ = 0;
    status_ = std::move(reason);
}
void StationControl::synchronize(ShipCrew& crew, const Pilot& pilot, const Navigation& nav) {
    if (!requested())
        return;
    if (!available(pilot) || !crew.gunReliefValid()) {
        returnToHelm(crew, "CAPTAIN AT HELM / RELIEF OR GUN UNAVAILABLE");
        return;
    }
    if (!active_ && crew.reliefHelmReadiness() > 0) {
        courseFrame_ = nav.anchor;
        course_ = nav.orientation;
        active_ = true;
        status_ = "CAPTAIN AT " + std::string(weaponMountName(mount_)) + " / F10 HELM / B RECOVER";
    }
}
glm::dquat StationControl::aim() const {
    return active() ? glm::angleAxis(-yaw_, glm::dvec3(0, 1, 0)) * glm::angleAxis(pitch_, glm::dvec3(1, 0, 0))
                    : glm::dquat(1, 0, 0, 0);
}
void StationControl::aim(double pitch, double yaw, glm::dvec2 mouse, double seconds) {
    if (!active() || !std::isfinite(seconds) || seconds <= 0 || !std::isfinite(pitch) ||
        !std::isfinite(yaw) || !std::isfinite(mouse.x) || !std::isfinite(mouse.y))
        return;
    const double dt = std::min(seconds, .1), rad = std::numbers::pi / 180;
    yaw_ = std::clamp(yaw_ + std::clamp(yaw, -1., 1.) * dt * 80 * rad +
                          std::clamp(mouse.x, -2000., 2000.) * .0025,
                      -120 * rad, 120 * rad);
    pitch_ = std::clamp(pitch_ + std::clamp(pitch, -1., 1.) * dt * 65 * rad -
                            std::clamp(mouse.y, -2000., 2000.) * .0025,
                        -70 * rad, 70 * rad);
}
void StationControl::fly(Pilot& pilot, const ShipCrew& crew, double seconds,
                         const std::shared_ptr<const Geometry>& visible) {
    if (active())
        pilot.updateCrew(courseFrame_, course_, crew.reliefHelmReadiness(), seconds, visible);
}
void StationControl::prepareWeapons(WeaponSystem& weapons, const ShipCrew& crew) const {
    weapons.setCrewReadiness(crew.weaponReadiness());
    for (unsigned i = 1; i < maxWeaponMounts; ++i)
        weapons.setMountReadiness(i, crew.gunnerReadiness(i));
    if (active() && crew.gunReliefValid())
        weapons.setMountReadiness(mount_, 1.);
}
WeaponRay StationControl::ray(const Navigation& nav, const Pilot& pilot, double seconds) const {
    WeaponRay ray;
    ray.reference = nav.anchor;
    ray.origin = nav.cameraPosition() * std::pow(10., nav.logDistance);
    ray.direction = nav.orientation * aim() * glm::dvec3(0, 0, -1);
    ray.right = nav.orientation * glm::dvec3(1, 0, 0);
    ray.scale = pilot.sizeRatio();
    ray.seconds = seconds;
    ray.direct = active();
    return ray;
}
void StationControl::annotate(FlightInstruments& t, const ShipCrew& crew, const WeaponSystem& weapons) const {
    if (!requested())
        return;
    t.stationStatus = status_;
    if (!active())
        return;
    t.playerGun = mount_;
    t.gunAim = aim();
    t.gunYaw = yaw_ * 180 / std::numbers::pi;
    t.gunPitch = pitch_ * 180 / std::numbers::pi;
    t.helmQuality = crew.reliefHelmReadiness();
    t.phase = t.navigating ? "CREW / " + t.phase : "CREW / COURSE HOLD";
    for (const auto& m : crew.members())
        if (m.id == crew.reliefHelmId())
            t.reliefHelm = m.name;
    const auto& gun = weapons.mount(mount_);
    t.weapon = std::string(weaponMountName(mount_)) + " / " + std::string(weaponSpec(gun.level).name);
    t.weaponHeat = gun.heat;
    t.weaponBattery = false;
    t.weaponStatus = t.weaponStatus.find("INHIBITED") != std::string::npos ? "WEAPON INHIBITED"
                     : gun.overheated                                      ? "OVERHEAT / COOLING"
                     : gun.cooldown > 0                                    ? "RELOADING"
                                                                           : "SPACE FIRE THIS GUN / F10 HELM";
}
} // namespace infinite
