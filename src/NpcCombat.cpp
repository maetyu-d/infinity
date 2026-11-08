#include "infinity/SaveTag.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/Piloting.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace infinity {
namespace {
bool finite(glm::dvec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
glm::dvec3 limited(glm::dvec3 v, double maximum) {
    const auto n = glm::length(v);
    return n > maximum ? v * (maximum / n) : v;
}
bool civilian(ActivityKind k) {
    return k == ActivityKind::Freighter || k == ActivityKind::Ferry || k == ActivityKind::Survey;
}
bool flying(ActivityKind k) {
    return civilian(k) || k == ActivityKind::Patrol || k == ActivityKind::Raider;
}
glm::dvec3 unit(glm::dvec3 v, glm::dvec3 fallback = {0, 0, -1}) {
    return glm::length(v) > 1e-20 ? glm::normalize(v) : fallback;
}
glm::dquat heading(glm::dvec3 direction, glm::dvec3 up) {
    direction = unit(direction);
    up = unit(up, {0, 1, 0});
    if (std::abs(glm::dot(up, direction)) > .98)
        up = std::abs(direction.y) < .98 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
    return glm::quatLookAtRH(direction, up);
}
} // namespace
std::string_view npcModeName(NpcMode mode) {
    switch (mode) {
    case NpcMode::Patrol:
        return "PATROLLING";
    case NpcMode::Attack:
        return "ENGAGING";
    case NpcMode::Evade:
        return "EVADING FIRE";
    case NpcMode::Retreat:
        return "BREAKING OFF";
    case NpcMode::Return:
        return "RETURNING TO COURSE";
    case NpcMode::Disabled:
        return "DISABLED";
    }
    return "UNKNOWN";
}
NpcCombatReport NpcCombat::update(const Universe& u, const NpcPlayer& player,
                                  std::span<const ActivityBody> input, const WeaponSystem& weapons, double dt,
                                  const Geometry* geometry) {
    report_ = {};
    if (!std::isfinite(dt) || dt <= 0 || !finite(player.position) || !finite(player.velocity) ||
        !finite(player.forward) || !std::isfinite(player.seconds) || player.seconds < 0 ||
        player.seconds > 1e12 || !std::isfinite(player.size) || player.size <= 0 || player.size > 1)
        return report_;
    dt = std::min(dt, .25);
    if (lastSeconds_ >= 0 && player.seconds < lastSeconds_) {
        const auto shift = player.seconds - lastSeconds_;
        for (auto& [_, a] : agents_)
            for (auto* t : {&a.aggroUntil, &a.evadeUntil, &a.nextEvade, &a.demandUntil, &a.truceUntil, &a.signalAt, &a.pursuitUntil})
                *t = std::max(0., *t + shift);
    }
    lastSeconds_ = player.seconds;
    damageFlash_ *= std::exp(-dt * 5);
    for (auto& s : shots_)
        s.age += dt;
    std::erase_if(shots_, [](const auto& s) { return s.age > weaponShotLifetime(s); });
    const bool fired = observedShots_ != weapons.shotsFired();
    observedShots_ = weapons.shotsFired();
    struct Local {
        ActivityBody base, body;
        glm::dvec3 player, velocity, forward;
        NpcAgent* agent = nullptr;
        double range = 0;
        bool safe = false;
    };
    std::vector<Local> local;
    local.reserve(std::min<size_t>(input.size(), 128));
    const auto playerUnits = std::pow(10., u.describe(player.reference).frame.logRadius);
    // Cache frame conversion once per nearby planet, never once per ray/actor.
    struct Frame {
        Address home;
        glm::dvec3 position, velocity, forward;
    };
    std::vector<Frame> frames;
    auto frame = [&](const Address& home) -> const Frame& {
        auto it = std::find_if(frames.begin(), frames.end(), [&](const auto& f) { return f.home == home; });
        if (it != frames.end())
            return *it;
        const auto rotation = u.relative(player.reference, home, 0).orientation;
        frames.push_back({home, u.pointRelative(player.reference, player.position / playerUnits, home, 0),
                          rotation * player.velocity, rotation * player.forward});
        return frames.back();
    };
    struct Incident {
        Address home;
        glm::dvec3 position;
    };
    std::vector<Incident> incidents;
    for (const auto& raw : input) {
        if (!flying(raw.kind) || local.size() >= 128)
            continue;
        auto body = raw;
        weapons.apply(body);
        const auto f = frame(body.id.home);
        auto found = agents_.find(body.id);
        const bool jammed = weapons.ionDisabled(body.id);
        const auto range = glm::length(body.position +
            (jammed || found == agents_.end() ? glm::dvec3(0) : found->second.offset) - f.position);
        if (range > 25000)
            continue;
        if (found == agents_.end()) {
            if (agents_.size() >= 4096)
                continue;
            NpcAgent a;
            a.id = body.id;
            a.attitude = body.orientation;
            found = agents_.emplace(a.id, a).first;
        }
        auto& a = found->second;
        if (body.hull < a.lastHull) {
            const auto damage = std::find_if(weapons.damage().begin(), weapons.damage().end(),
                                             [&](const auto& d) { return d.id == body.id; });
            if (damage != weapons.damage().end() && std::abs(player.seconds - damage->hitSeconds) < 3) {
                a.aggroUntil = player.seconds + 60;
                a.evadeUntil = player.seconds + 1.5;
                a.truceUntil = 0;
                a.demandUntil = player.seconds;
                if (body.kind != ActivityKind::Raider)
                    incidents.push_back({body.id.home, body.position});
            }
        }
        a.lastHull = body.hull;
        bool safe = player.landed;
        for (const auto& port : input)
            if ((port.kind == ActivityKind::Harbour || port.kind == ActivityKind::SurfacePort) &&
                port.id.home == body.id.home &&
                glm::length(port.position - f.position) <
                    port.radius + (port.kind == ActivityKind::Harbour ? 4500. : 1800.))
                safe = true;
        if (isRaider(body.id) && raiderRole(body.id) != RaiderRole::Legacy)
            for (const auto& contact : input)
                if (contact.kind == ActivityKind::Patrol && contact.id.home == body.id.home &&
                    glm::length(contact.position - f.position) < 2200) {
                    auto patrol = contact;
                    weapons.apply(patrol);
                    safe |= !patrol.disabled && patrol.systems[size_t(ShipSystem::Engines)] > 0;
                }
        auto base = raw;
        if (jammed && !body.disabled) {
            a.signalAt = 0;
            a.offset = body.position - raw.position;
            a.drift = body.velocity - raw.velocity;
            a.attitude = raw.orientation;
            a.mode = NpcMode::Return;
            continue;
        }
        if (body.systems[size_t(ShipSystem::Engines)] <= 0) {
            base.position = body.position;
            base.velocity = {};
            a.offset = {};
            a.drift = {};
        }
        local.push_back({base, body, f.position, f.velocity, unit(f.forward), &a, range, safe});
    }
    for (auto& c : local) {
        auto& a = *c.agent;
        std::optional<ActivityId> victim;
        if (escort_ && c.body.kind == ActivityKind::Raider && raiderRole(c.body.id) == RaiderRole::Legacy &&
            a.aggroUntil <= player.seconds) {
            const auto it = std::find_if(input.begin(), input.end(), [&](const auto& b) {
                return b.id == *escort_ && b.id.home == c.body.id.home;
            });
            if (it != input.end()) {
                auto escorted = *it;
                apply(escorted);
                weapons.apply(escorted);
                if (!escorted.disabled && glm::length(escorted.position - c.player) < 8000) {
                    c.player = escorted.position;
                    c.velocity = escorted.velocity;
                    victim = escorted.id;
                }
            }
        }
        if (c.body.kind == ActivityKind::Patrol)
            for (const auto& incident : incidents)
                if (incident.home == c.body.id.home &&
                    glm::length(incident.position - c.body.position) < 8000)
                    a.aggroUntil = player.seconds + 60;
        const auto position = c.base.position + a.offset;
        const auto toPlayer = c.player - position;
        const auto distance = glm::length(toPlayer);
        const auto toward = unit(toPlayer);
        const bool detectable = !player.field && player.size >= .01 && player.hull > 0 &&
                                distance < 12000 * std::sqrt(player.size);
        const bool alarm = a.aggroUntil > player.seconds && detectable;
        if (c.body.disabled || c.body.hull <= 0) {
            a.mode = NpcMode::Disabled;
            a.signalAt = 0;
            a.drift = {};
            continue;
        }
        const auto role = raiderRole(c.body.id);
        const auto* offence = role == RaiderRole::Hunter
                                  ? weapons.hunterOffence(c.body.id.home.parent(), player.seconds)
                                  : nullptr;
        if (offence && offence->id.home != c.body.id.home)
            offence = nullptr;
        if (offence && offence->hitSeconds > a.truceUntil - 600)
            a.truceUntil = 0;
        const bool pair = role == RaiderRole::Cutoff || role == RaiderRole::MissileWing;
        bool partnerLost = false;
        if (pair) {
            const ActivityId partner{c.body.id.home, role == RaiderRole::Cutoff ? missileWingSlot : cutoffRaiderSlot};
            partnerLost = weapons.ionDisabled(partner) ||
                std::any_of(weapons.damage().begin(), weapons.damage().end(), [&](const auto& d) {
                    return d.id == partner && (d.hull <= 0 || d.systems[size_t(ShipSystem::Engines)] <= 0);
                }) || std::any_of(input.begin(), input.end(), [&](const auto& b) {
                    return b.id == partner && (b.disabled || b.hull <= 0);
                });
        }
        const bool scout = role == RaiderRole::Scout;
        const bool pursuit = role == RaiderRole::Pursuit;
        const bool activePursuit = (scout || pursuit) &&
                                   reinforcementActive(c.body.id.home, player.seconds);
        const bool interested = role == RaiderRole::Cargo ? player.valuableCargo :
                                role == RaiderRole::Hunter ? offence != nullptr :
                                role == RaiderRole::Scavenger ? player.hull <= 65 || player.driveDisabled :
                                pursuit ? activePursuit : true;
        const bool hostile = (c.body.kind == ActivityKind::Raider && (interested || alarm) &&
                              player.seconds >= a.truceUntil) ||
                             (c.body.kind == ActivityKind::Patrol && alarm);
        const bool cargoRaid = role == RaiderRole::Cargo && !alarm;
        const bool negotiation = cargoRaid || (offence && offence->hull >= 50 && !alarm);
        const bool engageable = detectable && hostile && (!c.safe || c.body.kind == ActivityKind::Patrol) &&
            !player.landed && (role == RaiderRole::Legacy || c.body.kind != ActivityKind::Raider ||
                               (c.body.systems[size_t(ShipSystem::Engines)] > 0 && !partnerLost));
        const bool disabledPrize = role == RaiderRole::Cargo && player.driveDisabled;
        if (engageable && negotiation && distance <= 3500 && c.body.hull > 30 && !disabledPrize) {
            if (a.demandUntil == 0)
                a.demandUntil = player.seconds + 12;
            if (a.demandUntil > player.seconds && (!report_.demand || distance < report_.demand->range)) {
                RaiderDemand demand{c.body.id, a.demandUntil, distance, 0, "CARGO DEMAND"};
                if (offence) {
                    demand.restitution = 1200 + int64_t((100 - offence->hull) * 80);
                    demand.reason = "ATTACK ON " + sampleCombatActivity(u, offence->id, player.seconds).name;
                }
                report_.demand = std::move(demand);
            }
        }
        const bool awaiting = negotiation && a.demandUntil > player.seconds;
        const bool engage = engageable && !awaiting && !disabledPrize;
        if (scout) {
            if (!engage || distance > 6000 || c.body.hull <= 30)
                a.signalAt = 0;
            else if (!activePursuit && (a.pursuitUntil == 0 || player.seconds >= a.pursuitUntil + 120)) {
                if (a.signalAt == 0)
                    a.signalAt = player.seconds + 10;
                if (player.seconds >= a.signalAt) {
                    a.signalAt = 0;
                    a.pursuitUntil = player.seconds + 90;
                } else {
                    const auto remaining = a.signalAt - player.seconds;
                    if (report_.signalRemaining == 0 || remaining < report_.signalRemaining)
                        report_.signalRemaining = remaining;
                }
            }
        }
        if (engage && pursuit)
            report_.pursuitIncoming = true;
        const bool skirmisher = role == RaiderRole::Skirmisher;
        const bool siege = role == RaiderRole::Siege;
        const bool saboteur = role == RaiderRole::Saboteur;
        const bool aimed = glm::dot(c.forward, -toward) > .998 && distance < 4500;
        if (engage && aimed && player.seconds >= a.nextEvade && (fired || weapons.level() > 0)) {
            a.evadeUntil = player.seconds + (skirmisher ? 1.5 : 1.1);
            a.nextEvade = player.seconds + (skirmisher ? 2.3 : 3.4);
        }
        const double retreatHull = role == RaiderRole::Scavenger ? 55. : siege ? 18. : 30.;
        if (partnerLost || (scout && engage) || ((engage || alarm) && c.body.hull <= retreatHull))
            a.mode = NpcMode::Retreat;
        else if (civilian(c.body.kind) && alarm)
            a.mode = player.seconds < a.evadeUntil ? NpcMode::Evade : NpcMode::Retreat;
        else if (engage)
            a.mode = player.seconds < a.evadeUntil ? NpcMode::Evade : NpcMode::Attack;
        else if (glm::length(a.offset) > .05 || glm::length(a.drift) > .05)
            a.mode = NpcMode::Return;
        else
            a.mode = NpcMode::Patrol;
        auto up = unit(c.base.position, {0, 1, 0});
        auto side = unit(glm::cross(toward, up), {1, 0, 0});
        const double phase = double(c.body.seed & 65535) / 65536 * 6.283185307179586;
        glm::dvec3 desired{};
        if (a.mode == NpcMode::Attack || a.mode == NpcMode::Evade) {
            const double standoff = siege ? 2750. : role == RaiderRole::MissileWing ? 1850. :
                                    skirmisher ? 420. : saboteur ? 780. : pursuit ? 650. : 1100.;
            desired = limited(c.velocity - c.base.velocity, 250.) +
                      toward * std::clamp((distance - standoff) * .55, -180., 360.);
            if (role == RaiderRole::Cutoff)
                desired += limited(c.velocity * .55, 230.) + side * 160.;
            desired +=
                side * (a.mode == NpcMode::Evade ? 300. : 75.) * std::sin(player.seconds * 2.7 + phase);
            if (skirmisher)
                desired += side * 155. * std::cos(player.seconds * 1.9 + phase);
            if (a.mode == NpcMode::Evade)
                desired += up * 95. * std::cos(player.seconds * 3.1 + phase);
        } else if (a.mode == NpcMode::Retreat)
            desired = -toward * (civilian(c.body.kind) ? 220. : scout ? 180. : 480.) + side * 70.;
        else if (a.mode == NpcMode::Return)
            desired = limited(-a.offset * .55, 280.);
        const bool manoeuvring =
            a.mode == NpcMode::Attack || a.mode == NpcMode::Evade || a.mode == NpcMode::Retreat;
        if (manoeuvring) {
            if (distance < 150)
                desired -= toward * (150 - distance) * 4.;
            for (const auto& port : input)
                if (port.id.home == c.body.id.home &&
                    (port.kind == ActivityKind::Harbour || port.kind == ActivityKind::SurfacePort)) {
                    const auto delta = position - port.position;
                    const auto d = glm::length(delta), clearance = port.radius + 220.;
                    if (d < clearance)
                        desired += unit(delta) * std::min(400., (clearance - d) * 2.);
                }
        }
        desired = limited(desired, 550.);
        const auto old = a.drift;
        const auto propulsion = c.body.systems[size_t(ShipSystem::Engines)] / 100.;
        const auto steering = c.body.systems[size_t(ShipSystem::Thrusters)] / 100.;
        const double acceleration = a.mode == NpcMode::Retreat ? 70. : skirmisher || pursuit ? 105. : siege ? 26. : 45.;
        a.drift += limited(desired * propulsion - a.drift, acceleration * dt * steering);
        a.offset += (old + a.drift) * (.5 * dt);
        a.offset = limited(a.offset, 20000.);
        const auto planet = u.describe(c.body.id.home);
        const double floor =
            std::min(glm::length(c.base.position), std::pow(10., planet.frame.logRadius) + 1000.);
        const auto next = c.base.position + a.offset;
        if (a.mode != NpcMode::Patrol && glm::length(next) < floor) {
            a.offset = unit(next) * floor - c.base.position;
            a.drift -= up * std::min(0., glm::dot(a.drift, up));
        }
        const auto aim =
            (engage && a.mode != NpcMode::Retreat)
                ? c.player + (c.velocity - c.base.velocity - a.drift) * .4 - (c.base.position + a.offset)
                : c.base.velocity + a.drift;
        const auto target = heading(aim, up);
        const double angle = 2 * std::acos(std::clamp(std::abs(glm::dot(a.attitude, target)), 0., 1.));
        const double blend = std::min(1 - std::exp(-dt * 2.5), angle > 1e-9 ? 1.8 * dt / angle : 1.);
        a.attitude = glm::normalize(glm::slerp(a.attitude, target, blend * steering));
        a.cooldown = std::max(0., a.cooldown - dt);
        a.heat = std::max(0., a.heat - 28 * dt);
        if (detectable && hostile && !partnerLost && !disabledPrize) {
            report_.hostiles++;
            if (report_.nearest < 0 || distance < report_.nearest)
                report_.nearest = distance;
        }
        const double fireRange = siege ? 3100. : skirmisher ? 950. : 2400.;
        if (!engage || scout || c.body.systems[size_t(ShipSystem::NoseGun)] <= 0 || a.mode == NpcMode::Retreat ||
            a.cooldown > 0 || a.heat > 82 || distance < 70 || distance > fireRange)
            continue;
        auto forward = a.attitude * glm::dvec3(0, 0, -1);
        if (glm::dot(forward, toward) < .9997)
            continue;
        const auto right = a.attitude * glm::dvec3(1, 0, 0);
        forward = unit(forward + right * (std::sin(double(a.shots) * 2.399 + phase) * .0012));
        WeaponRay ray;
        ray.reference = c.body.id.home;
        ray.origin = c.base.position + a.offset;
        ray.direction = forward;
        ray.right = right;
        ray.seconds = player.seconds;
        ActivityBody proxy;
        proxy.id = {c.body.id.home, UINT32_MAX};
        proxy.kind = ActivityKind::Patrol;
        proxy.position = c.player;
        proxy.radius = 2 * player.size;
        proxy.orientation = u.relative(player.reference, c.body.id.home, 0).orientation * player.orientation;
        constexpr std::array components{ShipSystem::Engines,      ShipSystem::Thrusters,
                                        ShipSystem::NoseGun,      ShipSystem::PortGun,
                                        ShipSystem::StarboardGun, ShipSystem::LifeSupport};
        const auto hitSystem = role == RaiderRole::Cargo ? ShipSystem::Engines :
            saboteur ? (a.shots % 2 == 0 ? ShipSystem::Thrusters : ShipSystem::NoseGun) :
            components[(a.shots + c.body.seed) % components.size()];
        const auto componentPoint =
            proxy.position + proxy.orientation * (systemOffset(hitSystem) * proxy.radius);
        if (siege)
            ray.direction = toward;
        else if (!victim)
            ray.direction = unit(unit(componentPoint - ray.origin) +
                                 right * (std::sin(double(a.shots) * 2.399 + phase) * .0012));
        std::vector<ActivityBody> targets;
        targets.reserve(input.size() + 1);
        for (auto body : input)
            if (body.id != c.body.id) {
                apply(body);
                weapons.apply(body);
                targets.push_back(body);
            }
        if (!victim)
            targets.push_back(proxy);
        const auto trace = traceWeapon(u, ray, fireRange, targets, geometry);
        const bool hit = !victim && trace.body && trace.body->id == proxy.id;
        const bool missilePass = siege || (role == RaiderRole::MissileWing && a.shots % 3 == 0) ||
                                 (role == RaiderRole::Legacy && a.shots % 6 == 5);
        if (hit && c.body.kind == ActivityKind::Raider && missilePass && distance > 500 &&
            report_.missileLaunches.size() < 4) {
            ray.direction = forward;
            report_.missileLaunches.push_back(ray);
            ++a.shots;
            a.cooldown = 3.;
            report_.firing++;
            continue;
        }
        if (siege) {
            a.cooldown = .35;
            continue;
        }
        if (trace.body && civilian(trace.body->kind) && report_.civilianHits.size() < 24)
            report_.civilianHits.push_back(*trace.body);
        if (hit) {
            const double damage = skirmisher ? 2. : c.body.kind == ActivityKind::Raider ? 4. : 6.;
            report_.playerDamage += damage;
            const auto componentDelta = componentPoint - ray.origin;
            const auto miss =
                glm::length(componentDelta - ray.direction * glm::dot(componentDelta, ray.direction));
            report_.playerHits.emplace_back(miss <= proxy.radius * .25 ? hitSystem : ShipSystem::Hull,
                                            damage);
            damageFlash_ = 1;
        }
        if (shots_.size() >= 24)
            shots_.erase(shots_.begin());
        shots_.push_back({ray.reference, ray.origin + forward * 16., ray.origin + ray.direction * trace.distance, 0,
                          1, hit || trace.blocked || trace.body.has_value(), false, 1, true});
        ++a.shots;
        a.heat += 16;
        a.cooldown = skirmisher ? (a.shots % 3 == 0 ? 1.15 : .20) :
                     c.body.kind == ActivityKind::Raider ? .9 : 1.1;
        report_.firing++;
    }
    for (auto& [_, agent] : agents_)
        if (agent.signalAt > 0 && std::none_of(local.begin(), local.end(), [&](const auto& c) {
                return c.agent == &agent;
            }))
            agent.signalAt = 0;
    std::erase_if(agents_, [&](const auto& entry) {
        const auto& a = entry.second;
        return a.mode == NpcMode::Patrol && a.aggroUntil < player.seconds &&
               a.signalAt == 0 && (a.pursuitUntil == 0 || a.pursuitUntil + 120 <= player.seconds) &&
               (a.demandUntil == 0 || a.demandUntil + 60 < player.seconds) && a.truceUntil <= player.seconds && glm::length(a.offset) < .05 &&
               glm::length(a.drift) < .05;
    });
    return report_;
}
void NpcCombat::acceptTruce(const ActivityId& id, double paidAt, double until) {
    if (!isRaider(id) || !std::isfinite(paidAt) || !std::isfinite(until) || until <= std::max(paidAt, lastSeconds_))
        return;
    auto it = agents_.find(id);
    if (it == agents_.end()) {
        if (agents_.size() >= 4096)
            return;
        it = agents_.try_emplace(id).first;
        it->second.id = id;
    }
    auto& agent = it->second;
    if (agent.truceUntil >= until || (agent.aggroUntil > 0 && agent.aggroUntil - 60 >= paidAt))
        return;
    agent.truceUntil = until;
    agent.demandUntil = 0;
    if (report_.demand && report_.demand->id == id)
        report_.demand.reset();
}
bool NpcCombat::reinforcementActive(const Address& home, double seconds) const {
    const auto scout = agents_.find({home, raiderScoutSlot});
    return scout != agents_.end() && scout->second.pursuitUntil > seconds;
}
void NpcCombat::apply(ActivityBody& b) const {
    const auto it = agents_.find(b.id);
    if (it == agents_.end())
        return;
    const auto& a = it->second;
    if (a.mode == NpcMode::Disabled)
        return; // authoritative damage overlay owns wreck position
    b.position += a.offset;
    b.velocity += a.drift;
    if (a.mode != NpcMode::Patrol)
        b.orientation = a.attitude;
    if (a.truceUntil > lastSeconds_) {
        b.hostile = false;
        b.status = "TRUCE / DISENGAGING";
        return;
    }
    if (a.demandUntil > lastSeconds_) {
        b.status = "HAIL / ALT-I ACCEPT DEMAND";
        return;
    }
    const auto role = raiderRole(b.id);
    if (role == RaiderRole::Scout && a.mode == NpcMode::Retreat) {
        b.hostile = true;
        b.status = a.signalAt > lastSeconds_ ? "TRANSMITTING / PURSUIT RELAY" :
            reinforcementActive(b.id.home, lastSeconds_) ? "PURSUIT SIGNAL SENT" : "RELAY SCOUT / FLEEING";
        return;
    }
    if (role != RaiderRole::Legacy && a.mode != NpcMode::Attack && a.mode != NpcMode::Evade) {
        b.hostile = false;
        if (a.mode == NpcMode::Retreat)
            b.status = "BREAKING OFF";
        return;
    }
    if (a.mode == NpcMode::Patrol)
        return;
    b.hostile =
        b.kind == ActivityKind::Raider || (b.kind == ActivityKind::Patrol && a.aggroUntil > lastSeconds_);
    b.status = (b.hostile ? "HOSTILE / " : "DISTRESS / ") + std::string(npcModeName(a.mode));
    if (b.hostile && role <= RaiderRole::Hunter)
        b.tint = {1, .25, .18};
    b.economicBrief = std::string(npcModeName(a.mode));
}
void NpcCombat::annotate(FlightInstruments& t) const {
    t.hostileContacts = report_.hostiles;
    t.incomingFire = damageFlash_;
    t.combatStatus = report_.hostiles ? "HOSTILES " + std::to_string(report_.hostiles) + " / H TARGET"
                                      : "H TARGET HOSTILE / LOCAL SPACE CLEAR";
    if (damageFlash_ > .2)
        t.combatStatus = "TAKING FIRE / EVADE / H TARGET";
    if (report_.pursuitIncoming)
        t.combatStatus = "RAIDER PURSUIT / H TARGET";
    if (report_.signalRemaining > 0)
        t.combatStatus = "RAIDER RELAY / " + std::to_string(int(std::ceil(report_.signalRemaining))) +
                         "S / H TARGET";
}
size_t NpcCombat::residentBytes() const {
    return sizeof(*this) + agents_.size() * (sizeof(NpcAgent) + 64) + shots_.capacity() * sizeof(WeaponShot);
}
std::string NpcCombat::serialize(uint64_t seed) const {
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITY_NPC_COMBAT 3 " << seed << ' ' << lastSeconds_ << ' '
      << observedShots_ << ' ' << agents_.size();
    for (const auto& [_, a] : agents_) {
        o << ' ' << a.id.str() << ' ' << unsigned(a.mode);
        for (auto p : {a.offset, a.drift})
            o << ' ' << p.x << ' ' << p.y << ' ' << p.z;
        o << ' ' << a.attitude.w << ' ' << a.attitude.x << ' ' << a.attitude.y << ' ' << a.attitude.z;
        o << ' ' << a.aggroUntil << ' ' << a.evadeUntil << ' ' << a.nextEvade << ' ' << a.cooldown << ' '
          << a.heat << ' ' << a.lastHull << ' ' << a.shots << ' ' << a.demandUntil << ' ' << a.truceUntil
          << ' ' << a.signalAt << ' ' << a.pursuitUntil;
    }
    return o.str();
}
bool NpcCombat::restore(std::string_view text, const Universe& u) {
    try {
        NpcCombat p;
        std::istringstream in{std::string(text)};
        std::string h, id;
        unsigned version;
        uint64_t seed;
        size_t count;
        if (!(in >> h >> version >> seed >> p.lastSeconds_ >> p.observedShots_ >> count) ||
            !matchesSaveTag(h, "INFINITY_NPC_COMBAT") || (version < 1 || version > 3) || seed != u.seed() ||
            !std::isfinite(p.lastSeconds_) || p.lastSeconds_ < -1 || p.lastSeconds_ > 1e12 ||
            p.observedShots_ > 1000000000000ULL || count > 4096)
            return false;
        for (size_t i = 0; i < count; ++i) {
            NpcAgent a;
            unsigned mode;
            if (!(in >> id >> mode >> a.offset.x >> a.offset.y >> a.offset.z >> a.drift.x >> a.drift.y >>
                  a.drift.z >> a.attitude.w >> a.attitude.x >> a.attitude.y >> a.attitude.z >> a.aggroUntil >>
                  a.evadeUntil >> a.nextEvade >> a.cooldown >> a.heat >> a.lastHull >> a.shots) ||
                mode > unsigned(NpcMode::Disabled) || !finite(a.offset) || glm::length(a.offset) > 50000 ||
                !finite(a.drift) || glm::length(a.drift) > 2000 || !std::isfinite(glm::length(a.attitude)) ||
                std::abs(glm::length(a.attitude) - 1) > 1e-6 || a.shots > 1000000000000ULL)
                return false;
            if (version >= 2 && !(in >> a.demandUntil >> a.truceUntil))
                return false;
            if (version >= 3 && !(in >> a.signalAt >> a.pursuitUntil))
                return false;
            for (auto value : {a.signalAt, a.pursuitUntil, a.demandUntil, a.truceUntil, a.aggroUntil, a.evadeUntil, a.nextEvade, a.cooldown, a.heat, a.lastHull})
                if (!std::isfinite(value) || value < 0 || value > 1e12)
                    return false;
            if (a.cooldown > 3 || a.heat > 100 || a.lastHull > 100)
                return false;
            a.id = ActivityId::parse(id);
            a.mode = NpcMode(mode);
            if (!flying(sampleCombatActivity(u, a.id, 0).kind) || p.agents_.contains(a.id))
                return false;
            p.agents_.emplace(a.id, a);
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
