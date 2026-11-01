#include "infinite/FireControl.hpp"
#include <numbers>
#include <sstream>
namespace infinite {
std::string_view fireOrderName(FireOrder o) {
    return o == FireOrder::Defend ? "DEFEND" : o == FireOrder::Focus ? "FOCUS HOSTILE" : "HOLD FIRE";
}
void FireControl::order(FireOrder o) {
    if (unsigned(o) > unsigned(FireOrder::Focus))
        return;
    order_ = o;
    status_.fill(std::string(fireOrderName(o)));
    tracking_.fill(0);
    targets_.fill({});
}
unsigned FireControl::update(const Universe& u, const WeaponRay& hullRay, glm::dquat attitude,
                             const std::optional<ActivityId>& selected, unsigned captainMount,
                             const ShipCrew& crew, WeaponSystem& guns, std::span<const ActivityBody> input,
                             double dt, bool inhibited, const Geometry* geometry) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1 || !std::isfinite(glm::length(attitude)) ||
        std::abs(glm::length(attitude) - 1) > .01)
        return 0;
    clock_ = std::fmod(clock_ + dt, 10000.);
    unsigned fired = 0;
    std::vector<ActivityBody> bodies;
    for (auto body : input.first(std::min<size_t>(128, input.size()))) {
        guns.apply(body);
        bodies.push_back(body);
    }
    for (unsigned mount = 0; mount < maxWeaponMounts; ++mount) {
        auto& text = status_[mount];
        const auto& gun = guns.mount(mount);
        const double skill = crew.gunnerReadiness(mount);
        if (inhibited || order_ == FireOrder::Hold || mount == captainMount || skill <= 0 || !gun.level ||
            gun.integrity <= 0) {
            text = gun.integrity <= 0      ? "GUN OFFLINE"
                   : inhibited             ? "INHIBITED"
                   : mount == captainMount ? "CAPTAIN"
                   : skill <= 0            ? "UNMANNED"
                                           : "HOLD FIRE";
            tracking_[mount] = 0;
            targets_[mount].reset();
            continue;
        }
        const ActivityBody* chosen = nullptr;
        if (gun.cooldown > 1e-9 || gun.overheated) {
            text = "TRACKING / RELOAD";
            continue;
        }
        glm::dvec3 aim{};
        double nearest = 1e100;
        for (const auto& b : bodies) {
            if (!b.hostile || b.disabled || (order_ == FireOrder::Focus && (!selected || b.id != *selected)))
                continue;
            const auto p = u.pointRelative(
                b.id.home,
                (b.position + b.orientation * (systemOffset(hullRay.targetSystem) * b.radius)) /
                    std::pow(10., u.describe(b.id.home).frame.logRadius),
                hullRay.reference, 0);
            auto delta = p - hullRay.origin -
                         attitude * glm::dvec3(weaponMountLateralMetres(mount) * hullRay.scale, 0, 0);
            const double range = glm::length(delta);
            if (range < 10 * hullRay.scale || range > weaponSpec(gun.level).rangeMetres * hullRay.scale ||
                (order_ == FireOrder::Defend && range > 3000 * hullRay.scale) || range >= nearest)
                continue;
            const auto local = glm::inverse(attitude) * (delta / range);
            const double yaw = std::atan2(local.x, -local.z) * 180 / std::numbers::pi,
                         pitch = std::asin(std::clamp(local.y, -1., 1.)) * 180 / std::numbers::pi;
            const auto arc = weaponTraverse(mount);
            if (yaw < arc.x || yaw > arc.y || std::abs(pitch) > 70)
                continue;
            chosen = &b;
            aim = delta / range;
            nearest = range;
        }
        if (!chosen) {
            text = "NO HOSTILE IN ARC";
            tracking_[mount] = 0;
            targets_[mount].reset();
            continue;
        }
        if (!targets_[mount] || *targets_[mount] != chosen->id) {
            targets_[mount] = chosen->id;
            tracking_[mount] = 0;
        }
        tracking_[mount] += dt;
        if (tracking_[mount] < .55 / std::clamp(skill, .2, 1.2)) {
            text = "ACQUIRING " + chosen->name;
            continue;
        }
        if (gun.heat + weaponSpec(gun.level).heatPerShot > 82) {
            text = "CONTROLLED COOLING";
            continue;
        }
        if (gun.cooldown > 0 || gun.overheated) {
            text = "TRACKING / RELOAD";
            continue;
        }
        auto ray = hullRay;
        ray.direct = true;
        const auto right = attitude * glm::dvec3(1, 0, 0), up = attitude * glm::dvec3(0, 1, 0);
        const double spread = std::max(0., 1.2 - skill) * .0018;
        ray.direction = glm::normalize(aim + right * (std::sin(clock_ * 3.1 + mount) * spread) +
                                       up * (std::cos(clock_ * 2.7 + mount) * spread));
        ray.right = right;
        auto checkRay = ray;
        checkRay.origin += right * (weaponMountLateralMetres(mount) * ray.scale);
        const auto trace =
            traceWeapon(u, checkRay, weaponSpec(gun.level).rangeMetres * ray.scale, bodies, geometry);
        if (!trace.body || trace.body->id != chosen->id) {
            text = "LINE OF FIRE BLOCKED";
            continue;
        }
        if (guns.fireMount(mount, u, ray, bodies, geometry)) {
            fired |= 1u << mount;
            text = "FIRING / " + chosen->name;
        }
    }
    return fired;
}
std::string FireControl::serialize() const {
    return "INFINITE_FIRE_CONTROL 1 " + std::to_string(unsigned(order_));
}
bool FireControl::restore(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string h;
    unsigned v, o;
    if (!(in >> h >> v >> o) || h != "INFINITE_FIRE_CONTROL" || v != 1 || o > 2)
        return false;
    in >> std::ws;
    if (!in.eof())
        return false;
    order(FireOrder(o));
    return true;
}
} // namespace infinite
