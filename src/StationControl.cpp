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
std::string_view helmOrderName(HelmOrder order) {
    switch (order) {
    case HelmOrder::Course:
        return "COURSE / APPROACH";
    case HelmOrder::HoldRange:
        return "HOLD RANGE";
    case HelmOrder::Circle:
        return "CIRCLE TARGET";
    case HelmOrder::Evade:
        return "EVASIVE BREAK";
    case HelmOrder::Retreat:
        return "RETREAT";
    }
    return "COURSE / APPROACH";
}
glm::dvec2 weaponTraverse(unsigned mount) {
    return mount == 1 ? glm::dvec2(-150, 30) : mount == 2 ? glm::dvec2(-30, 150) : glm::dvec2(-60, 60);
}
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
    order_ = HelmOrder::Course;
    response_ = evasion_ = 0;
    lastHull_ = pilot.ship().hull;
    target_.reset();
    tactical_.clear();
    status_ = "RELIEF WALKING TO BRIDGE / KEEP HELM / F10 CANCEL";
    return true;
}
void StationControl::returnToHelm(ShipCrew& crew, std::string reason) {
    crew.endGunRelief();
    mount_ = noWeaponMount;
    active_ = false;
    yaw_ = pitch_ = 0;
    status_ = std::move(reason);
    order_ = HelmOrder::Course;
    target_.reset();
    tactical_.clear();
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
        yaw_ = (mount_ == 1 ? -60. : mount_ == 2 ? 60. : 0.) * std::numbers::pi / 180.;
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
    const auto arc = weaponTraverse(mount_);
    yaw_ = std::clamp(yaw_ + std::clamp(yaw, -1., 1.) * dt * 80 * rad +
                          std::clamp(mouse.x, -2000., 2000.) * .0025,
                      arc.x * rad, arc.y * rad);
    pitch_ = std::clamp(pitch_ + std::clamp(pitch, -1., 1.) * dt * 65 * rad -
                            std::clamp(mouse.y, -2000., 2000.) * .0025,
                        -70 * rad, 70 * rad);
}
void StationControl::fly(Pilot& pilot, const ShipCrew& crew, double seconds,
                         const std::shared_ptr<const Geometry>& visible) {
    if (active())
        pilot.updateCrew(courseFrame_, course_, crew.reliefHelmReadiness(), seconds, visible);
}
bool StationControl::command(HelmOrder order) {
    if (!active() || unsigned(order) > unsigned(HelmOrder::Retreat))
        return false;
    order_ = order;
    target_.reset();
    response_ = evasion_ = 0;
    tactical_ = "ORDER / " + std::string(helmOrderName(order));
    return true;
}
void StationControl::fly(Pilot& pilot, const ShipCrew& crew, const Universe& u, const Navigation& nav,
                         const std::optional<ActivityBody>& target, double seconds,
                         const std::shared_ptr<const Geometry>& visible) {
    if (!active() || !std::isfinite(seconds) || seconds <= 0)
        return;
    const double dt = std::min(seconds, .25), quality = crew.reliefHelmReadiness();
    response_ += dt;
    evasion_ = std::max(0., evasion_ - dt);
    if (pilot.ship().hull < 30 && order_ != HelmOrder::Retreat) {
        order_ = HelmOrder::Retreat;
        tactical_ = "HULL CRITICAL / AUTOMATIC RETREAT";
    }
    if (pilot.ship().hull < lastHull_ && quality > .45)
        evasion_ = 1.2 + quality;
    lastHull_ = pilot.ship().hull;
    if (order_ == HelmOrder::Course) {
        tactical_ = "ORDER / COURSE OR EXISTING APPROACH";
        fly(pilot, crew, seconds, visible);
        return;
    }
    if (!target || (target->disabled && order_ != HelmOrder::Retreat) ||
        !std::isfinite(glm::length(target->position)) || !std::isfinite(glm::length(target->velocity))) {
        tactical_ = "NO LIVE TARGET / COURSE HOLD / SELECT CONTACT";
        fly(pilot, crew, seconds, visible);
        return;
    }
    if (!target_ || *target_ != target->id) {
        target_ = target->id;
        response_ = 0;
    }
    if (response_ < std::clamp(.65 / std::max(.2, quality), .25, 2.)) {
        tactical_ = "PILOT ACQUIRING / " + target->name;
        fly(pilot, crew, seconds, visible);
        return;
    }
    const auto eye = u.pointRelative(
        nav.anchor,
        nav.cameraPosition() * std::pow(10., nav.logDistance - u.describe(nav.anchor).frame.logRadius),
        target->id.home, 0);
    const auto delta = target->position - eye;
    const double range = glm::length(delta), size = pilot.sizeRatio();
    auto radial = range > 1e-20 ? delta / range : glm::dvec3(0, 0, -1);
    auto up = glm::length(eye) > 1 ? glm::normalize(eye) : glm::dvec3(0, 1, 0);
    auto tangent = glm::cross(radial, up);
    if (glm::length(tangent) < .01)
        tangent = glm::cross(radial, glm::dvec3(1, 0, 0));
    tangent = glm::normalize(tangent) * (mount_ == 1 ? -1. : 1.);
    const double desiredRange = std::max(target->radius * 6, 1200 * size);
    const double speed = (65 + 55 * std::clamp(quality, .2, 1.4)) * size;
    auto velocity = target->velocity + radial * std::clamp((range - desiredRange) * .18, -speed, speed);
    if (order_ == HelmOrder::Circle)
        velocity += tangent * speed * .65;
    if (order_ == HelmOrder::Evade || evasion_ > 0)
        velocity += tangent * speed + up * (speed * .35);
    if (order_ == HelmOrder::Retreat)
        velocity = target->velocity - radial * speed * 1.6 + up * (speed * .25);
    // A side station keeps its broadside toward the target. The captain's gun
    // remains independent of this physical hull attitude.
    auto heading = order_ == HelmOrder::Retreat                 ? -radial
                   : order_ == HelmOrder::Circle && mount_ != 0 ? -tangent
                                                                : radial;
    if (std::abs(glm::dot(up, heading)) > .98)
        up = glm::normalize(glm::cross(tangent, heading));
    const auto course = glm::quatLookAtRH(heading, up);
    pilot.updateCrewMotion(target->id.home, course, velocity, quality, seconds, visible);
    tactical_ = std::string(helmOrderName(order_)) + " / " + std::to_string(int(range)) + "M" +
                (evasion_ > 0 ? " / DODGING FIRE" : "");
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
    const auto arc = weaponTraverse(mount_);
    t.gunMinYaw = arc.x;
    t.gunMaxYaw = arc.y;
    t.helmQuality = crew.reliefHelmReadiness();
    t.phase = order_ == HelmOrder::Course ? (t.navigating ? "CREW / " + t.phase : "CREW / COURSE HOLD")
                                          : "CREW / " + std::string(helmOrderName(order_));
    t.helmOrder = tactical_;
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
