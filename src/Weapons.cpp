#include "infinity/SaveTag.hpp"
#include "infinity/Weapons.hpp"
#include "infinity/CombatActors.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/StationControl.hpp"
#include <numbers>
#include "infinity/Planetology.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace infinity {
namespace {
const std::array<WeaponSpec, 6> specs{{{"PULSE LASER I", 10, 3000, .28, 17, 28, {.25, .9, 1}},
                                       {"PULSE LASER II", 16, 6000, .24, 16, 32, {.75, .45, 1}},
                                       {"PULSE LASER III", 24, 10000, .20, 15, 36, {1, .45, .65}},
                                       {"ION DISRUPTOR", 0, 1400, 3.5, 42, 12, {.33, 1., .55}},
                                       {"GUIDED MISSILE", 38, 3500, 6., 68, 13, {1, .64, .22}},
                                       {"POINT DEFENCE", 0, 780, .75, 25, 30, {.48, .85, 1}}}};
bool finite(glm::dvec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
double sphere(glm::dvec3 delta, glm::dvec3 direction, double radius) {
    const auto b = glm::dot(delta, direction), c = glm::dot(delta, delta) - radius * radius;
    if (c <= 0)
        return 0;
    const auto discriminant = b * b - c;
    if (discriminant < 0 || b < 0)
        return 1e100;
    return b - std::sqrt(discriminant);
}
double meshHit(const Universe& u, const WeaponRay& ray, const Geometry& g, double limit) {
    const auto home = u.describe(ray.reference);
    const auto origin =
        u.pointRelative(ray.reference, ray.origin / std::pow(10., home.frame.logRadius), g.referenceFrame, 0);
    const auto direction = u.relative(ray.reference, g.referenceFrame, 0).orientation * ray.direction;
    const double units = std::pow(10., g.logUnits);
    if (g.solid.indexCount() / 3 > 20000) {
        for (const auto& atom : g.atomInstances) {
            const auto centre = glm::dvec3(atom.transform[3]) * units;
            const double radius = glm::length(glm::dvec3(atom.transform[0])) * units;
            const auto distance = sphere(centre - origin, direction, radius);
            if (distance >= ray.scale * 6)
                limit = std::min(limit, distance);
        }
        return limit;
    }
    auto position = [&](unsigned index) {
        if (!g.precisePositions.empty())
            return g.precisePositions.at(index);
        const auto& v = g.solid.vertices.at(index);
        return glm::dvec3(v.x, v.y, v.z) * units;
    };
    for (size_t t = 0; t + 2 < g.solid.indexCount(); t += 3) {
        auto index = [&](size_t i) {
            return g.solid.wideIndices() ? g.solid.indices[i] : g.solid.indices16[i];
        };
        const auto a = position(index(t)), e1 = position(index(t + 1)) - a, e2 = position(index(t + 2)) - a;
        const auto p = glm::cross(direction, e2);
        const auto determinant = glm::dot(e1, p);
        if (std::abs(determinant) < 1e-14 * std::max(1e-40, glm::length(e1) * glm::length(e2)))
            continue;
        const auto s = origin - a;
        const auto x = glm::dot(s, p) / determinant;
        if (x < 0 || x > 1)
            continue;
        const auto q = glm::cross(s, e1);
        const auto y = glm::dot(direction, q) / determinant;
        if (y < 0 || x + y > 1)
            continue;
        const auto d = glm::dot(e2, q) / determinant;
        if (d >= ray.scale * 6 && d < limit)
            limit = d;
    }
    return limit;
}
} // namespace
const WeaponSpec& weaponSpec(unsigned level) {
    return specs.at(std::clamp(level, 1u, maxWeaponLevel) - 1);
}
void WeaponSystem::setLoadout(const std::array<unsigned, maxWeaponMounts>& levels) {
    for (unsigned i = 0; i < maxWeaponMounts; ++i)
        mounts_[i].level = std::min(levels[i], maxWeaponLevel);
}
void WeaponSystem::setMountIntegrity(unsigned mount, double integrity) {
    if (mount < maxWeaponMounts && std::isfinite(integrity))
        mounts_[mount].integrity = std::clamp(integrity, 0., 100.);
}
void WeaponSystem::setMountReadiness(unsigned mount, double readiness) {
    if (mount >= maxWeaponMounts || !std::isfinite(readiness))
        return;
    if (mount == 0) {
        setCrewReadiness(readiness);
        return;
    }
    mounts_[mount].crewReadiness = readiness <= 0 ? 0 : std::clamp(readiness, .2, 1.2);
}
unsigned WeaponSystem::firingMask() const {
    unsigned mask = 0;
    for (const auto& s : shots_)
        if (!s.enemy && s.age <= .16 && s.mount < maxWeaponMounts)
            mask |= 1u << s.mount;
    return mask;
}
void WeaponSystem::advance(double dt) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(dt, 60.);
    for (auto& outage : outages_) {
        outage.position += outage.velocity * std::min(dt, outage.remaining);
        outage.remaining -= dt;
    }
    std::erase_if(outages_, [](const auto& outage) { return outage.remaining <= 0; });
    for (auto& feedback : pulseFeedback_)
        feedback.remaining = std::max(0., feedback.remaining - dt);
    for (auto& m : mounts_) {
        m.cooldown = std::max(0., m.cooldown - dt);
        m.heat = std::max(0., m.heat - weaponSpec(m.level).cooling * std::max(.65, m.crewReadiness) * dt);
        if (m.heat <= 35)
            m.overheated = false;
    }
    for (auto& s : shots_)
        s.age += dt;
    std::erase_if(shots_, [](const auto& s) { return s.age > weaponShotLifetime(s); });
}
void WeaponSystem::utilityBeam(const WeaponRay& ray, glm::dvec3 endpoint, unsigned mount) {
    if(mount>=maxWeaponMounts || !finite(ray.origin) || !finite(endpoint))return;
    std::erase_if(shots_,[&](const auto& s){return !s.enemy && s.mount==mount;});
    if(shots_.size()>=12)shots_.erase(shots_.begin());
    shots_.push_back({ray.reference,ray.origin,endpoint,0,1,true,false,1,false,mount,true});
}
void WeaponSystem::receiveNpcDamage(const ActivityBody& body, double points, double seconds) {
    if ((body.kind != ActivityKind::Ferry && body.kind != ActivityKind::Freighter &&
         body.kind != ActivityKind::Survey) ||
        !std::isfinite(points) || points <= 0 || !std::isfinite(seconds) || seconds < 0 || seconds > 1e12 ||
        body.disabled)
        return;
    auto it = std::find_if(damage_.begin(), damage_.end(), [&](const auto& d) { return d.id == body.id; });
    if (it == damage_.end()) {
        if (damage_.size() >= 2048)
            return;
        damage_.push_back({body.id, body.hull, seconds, body.position, body.orientation});
        it = std::prev(damage_.end());
    }
    it->hull = std::max(0., std::min(it->hull, body.hull) - std::min(points, 100.));
    it->hitSeconds = seconds;
    it->position = body.position;
    it->orientation = body.orientation;
}
const WeaponDamage* WeaponSystem::hunterOffence(const Address& system, double seconds) const {
    const WeaponDamage* latest = nullptr;
    for (const auto& hit : damage_)
        if (isRaider(hit.id) && hit.id.slot != factionHunterSlot && hit.id.home.parent() == system &&
            seconds - hit.hitSeconds >= 0 && seconds - hit.hitSeconds < 600 &&
            (!latest || hit.hitSeconds > latest->hitSeconds))
            latest = &hit;
    return latest;
}
bool WeaponSystem::ionDisabled(const ActivityId& id) const {
    return std::any_of(outages_.begin(), outages_.end(), [&](const auto& outage) { return outage.id == id; });
}
bool WeaponSystem::secureIonTarget(const ActivityBody& body, double seconds) {
    if (body.kind != ActivityKind::Raider || body.disabled || !ionDisabled(body.id))
        return false;
    auto record = std::find_if(damage_.begin(), damage_.end(), [&](const auto& d) { return d.id == body.id; });
    if (record == damage_.end()) {
        if (damage_.size() >= 2048)
            return false;
        damage_.push_back({body.id, body.hull, seconds, body.position, body.orientation});
        record = std::prev(damage_.end());
    }
    record->hitSeconds = seconds;
    record->position = body.position;
    record->orientation = body.orientation;
    record->systems[size_t(ShipSystem::Engines)] = 0;
    std::erase_if(outages_, [&](const auto& outage) { return outage.id == body.id; });
    return true;
}
void WeaponSystem::apply(ActivityBody& body) const {
    const auto applyIon = [&] {
        if (body.disabled || body.hull <= 0 || body.systems[size_t(ShipSystem::Engines)] <= 0)
            return;
        const auto outage = std::find_if(outages_.begin(), outages_.end(),
                                         [&](const auto& item) { return item.id == body.id; });
        if (outage == outages_.end())
            return;
        body.position = outage->position;
        body.velocity = outage->velocity;
        body.systems[size_t(ShipSystem::Engines)] = 0;
        body.systems[size_t(ShipSystem::Thrusters)] = 0;
        body.status = "ION OFFLINE " + std::to_string(int(std::ceil(outage->remaining))) + "S / HOLD 0 TO SECURE";
        body.economicBrief.clear();
    };
    const auto it =
        std::find_if(damage_.begin(), damage_.end(), [&](const auto& d) { return d.id == body.id; });
    if (body.kind == ActivityKind::Harbour || body.kind == ActivityKind::SurfacePort)
        return;
    if (it == damage_.end()) {
        applyIon();
        return;
    }
    body.hull = it->hull;
    body.systems = it->systems;
    body.systems[0] = body.hull;
    if (it->systems[size_t(ShipSystem::Engines)] <= 0 && it->hull > 0) {
        body.position = it->position;
        body.velocity = {};
        body.status = "ADRIFT / ENGINES OFFLINE";
    }
    body.disabled = body.disabled || it->hull <= 0;
    if (body.disabled) {
        body.position = it->position;
        body.orientation = it->orientation;
        body.velocity = {};
        body.status = "DISABLED WRECK / HULL 0%";
        body.tint *= .3f;
    } else {
        body.status += " / HULL " + std::to_string(int(std::ceil(body.hull))) + "%";
        body.tint *= float(.45 + .55 * body.hull / 100);
    }
    applyIon();
}
WeaponTrace traceWeapon(const Universe& u, const WeaponRay& ray, double rangeMetres,
                        std::span<const ActivityBody> targets, const Geometry* geometry) {
    auto limit = rangeMetres;
    struct HomeRay {
        Address home;
        glm::dvec3 origin, direction;
    };
    std::vector<HomeRay> frames;
    const HomeRay identity{{}, ray.origin, ray.direction};
    std::optional<double> referenceUnits;
    auto localRay = [&](const Address& address) -> const HomeRay& {
        if (address == ray.reference)
            return identity;
        const auto it =
            std::find_if(frames.begin(), frames.end(), [&](const auto& f) { return f.home == address; });
        if (it != frames.end())
            return *it;
        if (!referenceUnits)
            referenceUnits = std::pow(10., u.describe(ray.reference).frame.logRadius);
        frames.push_back({address, u.pointRelative(ray.reference, ray.origin / *referenceUnits, address, 0),
                          u.relative(ray.reference, address, 0).orientation * ray.direction});
        return frames.back();
    };
    if (ray.scale >= .01) {
        std::vector<Address> planets;
        for (const auto& n : u.lineage(ray.reference))
            if (n.address.kind() == Kind::Planet)
                planets.push_back(n.address);
        for (const auto& b : targets)
            if (std::find(planets.begin(), planets.end(), b.id.home) == planets.end())
                planets.push_back(b.id.home);
        for (const auto& address : planets) {
            const auto p = u.describe(address);
            if (p.address.kind() != Kind::Planet)
                continue;
            const auto& local = localRay(address);
            const double radius = std::pow(10., p.frame.logRadius);
            limit = std::min(limit, sphere(-local.origin, local.direction, radius));
            if (hasSolidSurface(p) &&
                std::abs(glm::length(local.origin) - radius) < rangeMetres + radius * .05) {
                auto clearance = [&](double t) {
                    const auto point = local.origin + local.direction * t;
                    return glm::length(point) - radius - u.terrainHeight(p, glm::normalize(point));
                };
                double previous = 0;
                for (unsigned step = 0; step <= 32; ++step) {
                    const double t = limit * step / 32;
                    if (clearance(t) <= 0) {
                        double lo = previous, hi = t;
                        for (unsigned refine = 0; refine < 10; ++refine) {
                            const auto mid = (lo + hi) * .5;
                            if (clearance(mid) <= 0)
                                hi = mid;
                            else
                                lo = mid;
                        }
                        limit = hi;
                        break;
                    }
                    previous = t;
                }
            }
        }
    }
    if (geometry)
        limit = meshHit(u, ray, *geometry, limit);
    const bool surface = limit < rangeMetres;
    const ActivityBody* chosen = nullptr;
    for (const auto& b : targets) {
        const auto& local = localRay(b.id.home);
        const auto distance = sphere(b.position - local.origin, local.direction, b.radius);
        if (distance >= ray.scale * 6 && distance < limit) {
            limit = distance;
            chosen = &b;
        }
    }
    return {limit, surface || bool(chosen), chosen ? std::optional<ActivityBody>(*chosen) : std::nullopt};
}
bool WeaponSystem::fire(const Universe& u, const WeaponRay& input, std::span<const ActivityBody> targets,
                        const Geometry* geometry) {
    return fireMount(0, u, input, targets, geometry);
}
unsigned WeaponSystem::fireBattery(const Universe& u, const WeaponRay& ray,
                                   std::span<const ActivityBody> targets, const Geometry* geometry) {
    unsigned fired = 0;
    for (unsigned i = 0; i < maxWeaponMounts; ++i)
        if (fireMount(i, u, ray, targets, geometry))
            fired |= 1u << i;
    return fired;
}
bool WeaponSystem::fireMount(unsigned mount, const Universe& u, const WeaponRay& input,
                             std::span<const ActivityBody> targets, const Geometry* geometry,
                             const ActivityId* requiredTarget) {
    if (mount >= maxWeaponMounts)
        return false;
    auto& m = mounts_[mount];
    if (!m.level || m.level == pointDefence || m.integrity <= 0 || m.crewReadiness <= 0 || m.cooldown > 1e-9 || m.overheated ||
        !finite(input.origin) || !finite(input.direction) || !finite(input.right) ||
        !std::isfinite(input.scale) || input.scale < 1e-12 || input.scale > 1 ||
        !std::isfinite(input.seconds) || input.seconds < 0 || input.seconds > 1e12 ||
        glm::length(input.direction) < .99 || glm::length(input.direction) > 1.01 ||
        m.shotsFired >= 1000000000000ULL || (mount && std::abs(glm::length(input.right) - 1) > .01))
        return false;
    WeaponRay ray = input;
    if (mount)
        ray.origin += ray.right * ((mount == 1 ? -4. : 4.) * ray.scale);
    ray.direction = glm::normalize(ray.direction);
    const auto& spec = weaponSpec(m.level);
    if (m.heat + spec.heatPerShot > 100) {
        m.overheated = true;
        return false;
    }
    std::vector<ActivityBody> presented(targets.begin(), targets.end());
    for (auto& body : presented)
        apply(body);
    if (mount && !input.direct) {
        // A crewed console aligns its own muzzle with the captain's sight line.
        // Trace again from that muzzle so nearby cover can still block the shot.
        const auto sight = traceWeapon(u, input, spec.rangeMetres * ray.scale, presented, geometry);
        if (sight.distance > 6 * ray.scale)
            ray.direction =
                glm::normalize(input.origin + glm::normalize(input.direction) * sight.distance - ray.origin);
    }
    const auto trace = traceWeapon(u, ray, spec.rangeMetres * ray.scale, presented, geometry);
    const auto limit = trace.distance;
    const auto& chosen = trace.body;
    const bool surface = trace.blocked;
    if (requiredTarget && (!chosen || chosen->id != *requiredTarget || chosen->disabled))
        return false;
    if (m.level == missileLauncher) {
        if (!chosen || !chosen->hostile || chosen->disabled || ray.scale < .99 || missiles_.size() >= 24)
            return false;
        missiles_.push_back({ray.reference, chosen->id, ray.origin + ray.direction * 10., ray.direction});
        missileEffect(ray.reference, ray.origin + ray.direction * 5., missiles_.back().position,
                      false, false, missileLauncher);
        shots_.back().mount = mount;
        ++m.shotsFired;
        m.heat += spec.heatPerShot;
        m.cooldown = spec.interval;
        return true;
    }
    bool disabled = false;
    bool damageApplied = false;
    ShipSystem impacted = ShipSystem::Hull;
    if (m.level == ionDisruptor) {
        if (chosen && !chosen->disabled && chosen->kind == ActivityKind::Raider) {
            auto outage = std::find_if(outages_.begin(), outages_.end(),
                                       [&](const auto& item) { return item.id == chosen->id; });
            if (outage != outages_.end())
                outage->remaining = 22.;
            else if (chosen->systems[size_t(ShipSystem::Engines)] > 0 && outages_.size() < 64)
                outages_.push_back({chosen->id, chosen->position, chosen->velocity, 22.});
        }
    } else if (chosen && !chosen->disabled && chosen->kind != ActivityKind::Harbour &&
        chosen->kind != ActivityKind::SurfacePort) {
        auto it =
            std::find_if(damage_.begin(), damage_.end(), [&](const auto& d) { return d.id == chosen->id; });
        if (it == damage_.end() && damage_.size() < 2048) {
            damage_.push_back({chosen->id});
            it = damage_.end() - 1;
        }
        if (it != damage_.end()) {
            bool componentHit = false;
            if (input.targetSystem != ShipSystem::Hull && size_t(input.targetSystem) < shipSystemCount) {
                const auto origin = u.pointRelative(
                    ray.reference, ray.origin / std::pow(10., u.describe(ray.reference).frame.logRadius),
                    chosen->id.home, 0);
                const auto direction =
                    u.relative(ray.reference, chosen->id.home, 0).orientation * ray.direction;
                const auto point = chosen->position +
                                   chosen->orientation * (systemOffset(input.targetSystem) * chosen->radius);
                const auto along = glm::dot(point - origin, direction);
                componentHit =
                    along > 0 && glm::length(point - origin - direction * along) <= chosen->radius * .2;
                if (componentHit)
                    it->systems[size_t(input.targetSystem)] =
                        std::max(0., it->systems[size_t(input.targetSystem)] - spec.damage * 3.);
            }
            damageApplied = true;
            if (componentHit)
                impacted = input.targetSystem;
            it->hull = std::max(0., it->hull - spec.damage * (componentHit ? .35 : 1.));
            it->hitSeconds = ray.seconds;
            it->position = chosen->position;
            it->orientation = chosen->orientation;
            disabled = it->hull == 0;
            if (disabled || it->systems[size_t(ShipSystem::Engines)] <= 0)
                std::erase_if(outages_, [&](const auto& outage) { return outage.id == chosen->id; });
        }
    }
    ++m.shotsFired;
    m.heat += spec.heatPerShot;
    m.cooldown = spec.interval / (m.crewReadiness * std::clamp(m.integrity / 100., .25, 1.));
    if (shots_.size() >= 12)
        shots_.erase(shots_.begin());
    shots_.push_back({ray.reference,
                      ray.origin + ray.direction * (6 * ray.scale) +
                          ray.right * ((m.shotsFired & 1 ? 1.1 : -1.1) * ray.scale),
                      ray.origin + ray.direction * limit, 0, ray.scale, surface || bool(chosen), disabled,
                      m.level, false, mount, false, impacted});
    if (isPulseLaser(m.level)) {
        using Result = PulseFeedback::Result;
        auto& feedback = pulseFeedback_[mount];
        if (damageApplied || feedback.remaining <= 0 || feedback.result != Result::Hit)
            feedback = {impacted, damageApplied ? Result::Hit : surface ? Result::Blocked : Result::Miss, 1.15};
    }
    return true;
}
void WeaponSystem::missileEffect(const Address& frame, glm::dvec3 from, glm::dvec3 to, bool enemy,
                                 bool hit, unsigned level) {
    if (shots_.size() >= 24)
        shots_.erase(shots_.begin());
    shots_.push_back({frame, from, to, 0, 1, hit, false, level, enemy});
}
bool WeaponSystem::launchHostileMissile(const WeaponRay& ray) {
    if (missiles_.size() >= 24 || !finite(ray.origin) || !finite(ray.direction) ||
        std::abs(glm::length(ray.direction) - 1.) > .01)
        return false;
    const auto direction = glm::normalize(ray.direction);
    missiles_.push_back({ray.reference, {}, ray.origin + direction * 22., direction});
    missileEffect(ray.reference, ray.origin + direction * 14., missiles_.back().position,
                  true, false, missileLauncher);
    return true;
}
double WeaponSystem::updateMissiles(const Universe& u, const NpcPlayer& player,
                                    std::span<const ActivityBody> bodies, const Geometry* geometry,
                                    double dt, bool defend, unsigned captainMount) {
    missileThreat_ = {};
    if (missiles_.empty() || !std::isfinite(dt) || dt <= 0)
        return 0;
    dt = std::min(dt, .1);
    constexpr double speed = 360.;
    const double playerUnits = std::pow(10., u.describe(player.reference).frame.logRadius);
    double playerDamage = 0;
    for (auto it = missiles_.begin(); it != missiles_.end();) {
        auto& missile = *it;
        const bool enemy = !missile.target;
        missile.age += dt;
        if (missile.age > 14 || (enemy && (player.field || player.landed || player.hull <= 0))) {
            it = missiles_.erase(it);
            continue;
        }
        ActivityBody target;
        if (enemy) {
            target.id = {missile.reference, UINT32_MAX};
            target.position = u.pointRelative(player.reference, player.position / playerUnits,
                                              missile.reference, 0);
            target.radius = std::max(4., 9. * player.size);
        } else {
            auto contact = std::find_if(bodies.begin(), bodies.end(),
                                       [&](const auto& b) { return b.id == *missile.target; });
            if (contact == bodies.end() || contact->disabled) {
                it = missiles_.erase(it);
                continue;
            }
            target = *contact;
            apply(target);
        }
        const auto targetPoint = target.id.home == missile.reference ? target.position :
            u.pointRelative(target.id.home, target.position /
                std::pow(10., u.describe(target.id.home).frame.logRadius), missile.reference, 0);
        const auto delta = targetPoint - missile.position;
        const double range = glm::length(delta);
        bool intercepted = false;
        if (enemy && defend && player.size >= .99 && range < weaponSpec(pointDefence).rangeMetres) {
            const auto toHull = u.relative(missile.reference, player.reference, 0).orientation;
            const auto right = player.orientation * glm::dvec3(1, 0, 0);
            for (unsigned i = 1; i < maxWeaponMounts; ++i) {
                auto& mount = mounts_[i];
                const auto& spec = weaponSpec(pointDefence);
                if (i == captainMount || mount.level != pointDefence || mount.integrity <= 0 ||
                    mount.crewReadiness <= 0 || mount.cooldown > 0 || mount.overheated)
                    continue;
                if (mount.heat + spec.heatPerShot > 100) {
                    mount.overheated = true;
                    continue;
                }
                const auto muzzle = player.position + right * weaponMountLateralMetres(i);
                const auto aim = -(toHull * delta) - (muzzle - player.position);
                const double distance = glm::length(aim);
                if (distance < 1e-6 || distance > spec.rangeMetres)
                    continue;
                const auto local = glm::inverse(player.orientation) * (aim / distance);
                const auto arc = weaponTraverse(i);
                const double yaw = std::atan2(local.x, -local.z) * 180 / std::numbers::pi;
                if (yaw < arc.x || yaw > arc.y || std::abs(local.y) > .94)
                    continue;
                const WeaponRay ray{player.reference, muzzle, aim / distance, right};
                const auto obstruction = traceWeapon(u, ray, distance, bodies, geometry);
                if (obstruction.blocked || obstruction.body)
                    continue;
                missileEffect(player.reference, muzzle, muzzle + aim, false, true, pointDefence);
                ++mount.shotsFired;
                mount.heat += spec.heatPerShot;
                mount.cooldown = spec.interval / (mount.crewReadiness * std::max(.25, mount.integrity / 100.));
                intercepted = true;
                break;
            }
        }
        if (intercepted) {
            it = missiles_.erase(it);
            continue;
        }
        if (range > 1e-6) {
            const auto desired = delta / range;
            const double angle = std::acos(std::clamp(glm::dot(missile.direction, desired), -1., 1.));
            auto axis = glm::cross(missile.direction, desired);
            if (glm::length(axis) < 1e-8)
                axis = glm::cross(missile.direction, std::abs(missile.direction.y) < .9 ?
                                   glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0));
            missile.direction = glm::angleAxis(std::min(angle, dt * .57), glm::normalize(axis)) *
                                missile.direction;
        }
        const WeaponRay ray{missile.reference, missile.position, missile.direction};
        const double travel = speed * dt;
        const double contact = sphere(delta, missile.direction, target.radius);
        const auto obstacle = traceWeapon(u, ray, std::min(travel, contact), bodies, geometry);
        const bool targetHit = contact <= travel &&
            ((!obstacle.blocked && !obstacle.body) || (obstacle.body && obstacle.body->id == target.id));
        if (targetHit || obstacle.blocked || obstacle.body) {
            const auto impact = missile.position + missile.direction * obstacle.distance;
            missileEffect(missile.reference, missile.position - missile.direction * 24., impact,
                          enemy, true, missileLauncher);
            if (targetHit && enemy)
                playerDamage += 27.;
            else if (targetHit && !target.disabled) {
                auto record = std::find_if(damage_.begin(), damage_.end(),
                                           [&](const auto& d) { return d.id == target.id; });
                if (record == damage_.end() && damage_.size() < 2048) {
                    damage_.push_back({target.id, target.hull});
                    record = std::prev(damage_.end());
                }
                if (record != damage_.end()) {
                    record->hull = std::max(0., record->hull - weaponSpec(missileLauncher).damage);
                    record->position = target.position;
                    record->orientation = target.orientation;
                    record->hitSeconds = player.seconds;
                    if (record->hull == 0)
                        std::erase_if(outages_, [&](const auto& o) { return o.id == target.id; });
                }
            }
            it = missiles_.erase(it);
            continue;
        }
        missile.position += missile.direction * travel;
        if (enemy) {
            const auto toPlayer = u.relative(missile.reference, player.reference, 0).orientation;
            const auto toward = toPlayer * (missile.position - targetPoint);
            const double distance = glm::length(toward);
            if (distance > 1e-6) {
                const auto bearing = toward / distance;
                const double closing = glm::dot(player.velocity - toPlayer * missile.direction * speed, bearing);
                const double eta = closing > 5 ? distance / closing : -1.;
                auto& threat = missileThreat_;
                if (threat.count++ == 0 ||
                    (eta >= 0 && (threat.seconds < 0 || eta < threat.seconds)) ||
                    (eta < 0 && threat.seconds < 0 && distance < threat.range)) {
                    threat.localDirection = glm::inverse(player.orientation) * bearing;
                    threat.range = distance;
                    threat.seconds = eta;
                }
            }
        }
        ++it;
    }
    return playerDamage;
}
std::string WeaponSystem::pulseStatus(unsigned mount, double scale) const {
    const auto& gun = mounts_.at(mount);
    if (!isPulseLaser(gun.level))
        return {};
    const auto& feedback = pulseFeedback_[mount];
    std::string status = flightQuantity(weaponSpec(gun.level).rangeMetres * scale) + " REACH / ";
    if (gun.integrity <= 0)
        return status + "OFFLINE";
    if (gun.overheated)
        return status + "OVERHEAT / COOLING";
    if (feedback.remaining > 0) {
        using Result = PulseFeedback::Result;
        if (feedback.result == Result::Hit)
            return status + std::string(shipSystemName(feedback.component)) + " HIT";
        return status + (feedback.result == Result::Blocked ? "SHOT BLOCKED" : "NO CONTACT");
    }
    return status + (gun.cooldown > 0 ? "CYCLING" : "SPACE FIRE");
}
void WeaponSystem::annotate(FlightInstruments& t, bool inhibited) const {
    const auto& m = mounts_[0];
    t.weaponBattery = mounts_[1].level || mounts_[2].level;
    t.weapon = m.level ? std::string(weaponSpec(m.level).name) : "NO WEAPON FITTED";
    t.weaponHeat = m.heat;
    t.weaponStatus = m.integrity <= 0 ? "NOSE GUN OFFLINE / F9 REPAIR"
                     : !m.level       ? "F2 WEAPONS / BUY A WEAPON"
                     : inhibited      ? "WEAPON INHIBITED"
                     : m.overheated   ? "OVERHEAT / COOLING"
                                      : "SPACE FIRE";
    if (t.weaponBattery) {
        t.weapon = "HEAT ";
        t.weaponStatus.clear();
        constexpr std::array<std::string_view, 3> names{"N", "P", "S"};
        for (unsigned i = 0; i < maxWeaponMounts; ++i) {
            const auto& s = mounts_[i];
            if (i)
                t.weapon += "/";
            t.weapon += std::string(names[i]) + ":" + std::string(weaponGrade(s.level)) +
                        (s.level ? " " + std::to_string(int(s.heat)) : "");
            t.weaponHeat = std::max(t.weaponHeat, s.heat);
            if (i && s.level) {
                if (!t.weaponStatus.empty())
                    t.weaponStatus += " / ";
                t.weaponStatus += std::string(names[i]) + (s.integrity <= 0       ? " OFFLINE"
                                                           : s.crewReadiness <= 0 ? " UNMANNED"
                                                           : s.overheated         ? " HOT"
                                                           : s.cooldown > 0       ? " RELOAD"
                                                                                  : " READY");
            }
        }
        t.weaponStatus = inhibited ? "BATTERY INHIBITED" : t.weaponStatus + " / SPACE FIRE";
    }
}
std::string WeaponSystem::serialize() const {
    std::ostringstream o;
    o << std::setprecision(17) << (outages_.empty() ? "INFINITY_COMBAT 3" : "INFINITY_COMBAT 4");
    for (const auto& m : mounts_)
        o << ' ' << m.heat << ' ' << m.cooldown << ' ' << m.overheated << ' ' << m.shotsFired;
    o << ' ' << damage_.size();
    for (const auto& d : damage_) {
        o << ' ' << d.id.str() << ' ' << d.hull << ' ' << d.hitSeconds << ' ' << d.position.x << ' '
          << d.position.y << ' ' << d.position.z << ' ' << d.orientation.w << ' ' << d.orientation.x << ' '
          << d.orientation.y << ' ' << d.orientation.z;
        for (double health : d.systems)
            o << ' ' << health;
    }
    if (!outages_.empty()) {
        o << ' ' << outages_.size();
        for (const auto& outage : outages_)
            o << ' ' << outage.id.str() << ' ' << outage.remaining << ' '
              << outage.position.x << ' ' << outage.position.y << ' ' << outage.position.z << ' '
              << outage.velocity.x << ' ' << outage.velocity.y << ' ' << outage.velocity.z;
    }
    return o.str();
}
bool WeaponSystem::restore(std::string_view text, const Universe& u) {
    try {
        WeaponSystem p;
        for (unsigned i = 0; i < maxWeaponMounts; ++i)
            p.mounts_[i].level = mounts_[i].level;
        std::istringstream in{std::string(text)};
        std::string h;
        unsigned v, hot;
        size_t count;
        if (!(in >> h >> v) || !matchesSaveTag(h, "INFINITY_COMBAT") || (v < 1 || v > 4))
            return false;
        for (unsigned j = 0; j < (v == 1 ? 1u : maxWeaponMounts); ++j) {
            auto& m = p.mounts_[j];
            if (!(in >> m.heat >> m.cooldown >> hot >> m.shotsFired) || !std::isfinite(m.heat) ||
                m.heat < 0 || m.heat > 100 || !std::isfinite(m.cooldown) || m.cooldown < 0 ||
                m.cooldown > (v >= 3 ? 8. : 2.) || hot > 1 || m.shotsFired > 1000000000000ULL)
                return false;
            m.overheated = hot;
        }
        if (!(in >> count) || count > 2048)
            return false;
        for (size_t i = 0; i < count; ++i) {
            WeaponDamage d;
            std::string id;
            if (!(in >> id >> d.hull >> d.hitSeconds >> d.position.x >> d.position.y >> d.position.z >>
                  d.orientation.w >> d.orientation.x >> d.orientation.y >> d.orientation.z) ||
                !std::isfinite(d.hull) || d.hull < 0 || d.hull > 100 || !std::isfinite(d.hitSeconds) ||
                d.hitSeconds < 0 || d.hitSeconds > 1e12 || !finite(d.position) ||
                !std::isfinite(glm::length(d.position)) || glm::length(d.position) > 1e20 ||
                !std::isfinite(glm::length(d.orientation)) || std::abs(glm::length(d.orientation) - 1) > 1e-6)
                return false;
            if (v >= 3)
                for (auto& health : d.systems)
                    if (!(in >> health) || !std::isfinite(health) || health < 0 || health > 100)
                        return false;
            d.id = ActivityId::parse(id);
            const auto body = sampleCombatActivity(u, d.id, d.hitSeconds);
            if (body.kind == ActivityKind::Harbour || body.kind == ActivityKind::SurfacePort ||
                std::any_of(p.damage_.begin(), p.damage_.end(), [&](const auto& x) { return x.id == d.id; }))
                return false;
            p.damage_.push_back(d);
        }
        if (v == 4) {
            if (!(in >> count) || count > 64)
                return false;
            for (size_t i = 0; i < count; ++i) {
                IonOutage outage;
                std::string id;
                if (!(in >> id >> outage.remaining >> outage.position.x >> outage.position.y >>
                      outage.position.z >> outage.velocity.x >> outage.velocity.y >> outage.velocity.z) ||
                    !(outage.remaining > 0 && outage.remaining <= 22.) ||
                    !finite(outage.position) || !finite(outage.velocity) ||
                    glm::length(outage.position) > 1e20 || glm::length(outage.velocity) > 1e8)
                    return false;
                outage.id = ActivityId::parse(id);
                if (!isRaider(outage.id) || p.ionDisabled(outage.id))
                    return false;
                u.describe(outage.id.home);
                p.outages_.push_back(std::move(outage));
            }
        }
        in >> std::ws;
        if (!in.eof())
            return false;
        *this = std::move(p);
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace infinity
