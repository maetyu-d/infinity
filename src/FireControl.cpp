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
    std::array<bool, maxWeaponMounts> ready{};
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
        if (gun.cooldown > 1e-9 || gun.overheated) {
            text = "TRACKING / RELOAD";
            continue;
        }
        ready[mount] = true;
    }
    if (std::none_of(ready.begin(), ready.end(), [](bool x) { return x; }))
        return 0;

    // Keep HUD/economic strings out of acquisition. Convert the eye once per home
    // frame, then work on the same SI positions used by the weapon collision test.
    struct Frame {
        const Address* home;
        glm::dvec3 eye;
        glm::dquat toReference;
    };
    struct Candidate {
        const ActivityBody* body;
        glm::dvec3 delta;
    };
    std::array<Frame, 128> frames;
    std::array<Candidate, 128> candidates;
    size_t frameCount = 0, candidateCount = 0;
    std::optional<double> referenceUnits;
    for (const auto& b : input.first(std::min<size_t>(128, input.size()))) {
        if (!b.hostile || b.disabled || (order_ == FireOrder::Focus && (!selected || b.id != *selected)))
            continue;
        auto position = b.position;
        const auto damage = std::find_if(guns.damage().begin(), guns.damage().end(),
                                         [&](const auto& d) { return d.id == b.id; });
        if (damage != guns.damage().end()) {
            if (damage->hull <= 0)
                continue;
            if (damage->systems[size_t(ShipSystem::Engines)] <= 0)
                position = damage->position;
        }
        position += b.orientation * (systemOffset(hullRay.targetSystem) * b.radius);
        auto delta = position - hullRay.origin;
        if (b.id.home != hullRay.reference) {
            auto frame = std::find_if(frames.begin(), frames.begin() + frameCount,
                                      [&](const auto& f) { return *f.home == b.id.home; });
            if (frame == frames.begin() + frameCount) {
                if (!referenceUnits)
                    referenceUnits = std::pow(10., u.describe(hullRay.reference).frame.logRadius);
                *frame = {&b.id.home,
                          u.pointRelative(hullRay.reference, hullRay.origin / *referenceUnits, b.id.home, 0),
                          u.relative(b.id.home, hullRay.reference, 0).orientation};
                ++frameCount;
            }
            delta = frame->toReference * (position - frame->eye);
        }
        candidates[candidateCount++] = {&b, delta};
    }
    const auto inverseAttitude = glm::inverse(attitude);
    const auto right = attitude * glm::dvec3(1, 0, 0), up = attitude * glm::dvec3(0, 1, 0);
    for (unsigned mount = 0; mount < maxWeaponMounts; ++mount) {
        if (!ready[mount])
            continue;
        auto& text = status_[mount];
        const auto& gun = guns.mount(mount);
        const double skill = crew.gunnerReadiness(mount);
        const ActivityBody* chosen = nullptr;
        glm::dvec3 aim{};
        double nearest = 1e100;
        for (size_t i = 0; i < candidateCount; ++i) {
            const auto& candidate = candidates[i];
            // Another mount may have destroyed this candidate earlier this update.
            if (fired) {
                const auto damage = std::find_if(guns.damage().begin(), guns.damage().end(),
                                                 [&](const auto& d) { return d.id == candidate.body->id; });
                if (damage != guns.damage().end() && damage->hull <= 0)
                    continue;
            }
            const auto delta = candidate.delta - right * (weaponMountLateralMetres(mount) * hullRay.scale);
            const double range = glm::length(delta);
            if (range < 10 * hullRay.scale || range > weaponSpec(gun.level).rangeMetres * hullRay.scale ||
                (order_ == FireOrder::Defend && range > 3000 * hullRay.scale) || range >= nearest)
                continue;
            const auto local = inverseAttitude * (delta / range);
            const double yaw = std::atan2(local.x, -local.z) * 180 / std::numbers::pi,
                         pitch = std::asin(std::clamp(local.y, -1., 1.)) * 180 / std::numbers::pi;
            const auto arc = weaponTraverse(mount);
            if (yaw < arc.x || yaw > arc.y || std::abs(pitch) > 70)
                continue;
            chosen = candidate.body;
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
        const double spread = std::max(0., 1.2 - skill) * .0018;
        ray.direction = glm::normalize(aim + right * (std::sin(clock_ * 3.1 + mount) * spread) +
                                       up * (std::cos(clock_ * 2.7 + mount) * spread));
        ray.right = right;
        // fireMount checks the intended target against its final muzzle trace
        // before consuming heat/cooldown or causing damage. No duplicate trace.
        if (guns.fireMount(mount, u, ray, input, geometry, &chosen->id)) {
            fired |= 1u << mount;
            text = "FIRING / " + chosen->name;
        } else
            text = "LINE OF FIRE BLOCKED";
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
