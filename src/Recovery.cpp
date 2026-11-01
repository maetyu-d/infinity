#include "infinite/Recovery.hpp"
namespace infinite {
bool RecoveryController::update(const Universe& u, const NpcPlayer& ship,
                                const std::optional<ActivityBody>& body, double dt, bool holding,
                                const Geometry* geometry, std::span<const ActivityBody> nearby) {
    if (!body || (!body->disabled && body->systems[size_t(ShipSystem::Engines)] > 0)) {
        reset();
        status_.clear();
        return false;
    }
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        return false;
    if (!target_ || *target_ != body->id) {
        reset();
        target_ = body->id;
    }
    auto origin = ship.position, velocity = ship.velocity;
    if (ship.reference != body->id.home) {
        origin = u.pointRelative(ship.reference,
                                 ship.position / std::pow(10., u.describe(ship.reference).frame.logRadius),
                                 body->id.home, 0);
        velocity = u.relative(ship.reference, body->id.home, 0).orientation * ship.velocity;
    }
    const auto delta = body->position - origin;
    range_ = glm::length(delta);
    speed_ = glm::length(velocity - body->velocity);
    if (ship.field || ship.landed || ship.size < .99 || ship.hull <= 0)
        status_ = "RECOVERY / FULL SIZE / DISENGAGE DRIVE / AIRBORNE";
    else if (range_ > body->radius + 120)
        status_ = "RECOVERY / APPROACH WITHIN 120M OF HULL";
    else if (speed_ > 3)
        status_ = "RECOVERY / MATCH SPEED BELOW 3M/S";
    else if (!holding) {
        // Keep range/speed instruments live, but trace only an actual access attempt.
        progress_ = 0;
        status_ = "HOLD 0 / RECOVER CARGO AND SURVIVORS";
        return false;
    } else {
        WeaponRay ray;
        ray.reference = body->id.home;
        ray.origin = origin;
        ray.direction = range_ > 1e-9 ? delta / range_ : glm::dvec3(0, 0, -1);
        const auto trace =
            traceWeapon(u, ray, body->radius + 150, nearby.empty() ? std::span(&*body, 1) : nearby, geometry);
        if (!trace.body || trace.body->id != body->id)
            status_ = "RECOVERY / ACCESS OBSTRUCTED";
        else {
            progress_ = std::min(6., progress_ + dt);
            status_ = "RECOVERING / " + std::to_string(int(progress_ / 6 * 100)) + "% / HOLD 0";
            return progress_ >= 6;
        }
    }
    progress_ = 0;
    return false;
}
} // namespace infinite
