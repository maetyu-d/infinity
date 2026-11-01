#include "infinite/NpcCombat.hpp"
#include "infinite/Piloting.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace infinite {
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
            for (auto* t : {&a.aggroUntil, &a.evadeUntil, &a.nextEvade})
                *t = std::max(0., *t + shift);
    }
    lastSeconds_ = player.seconds;
    damageFlash_ *= std::exp(-dt * 5);
    for (auto& s : shots_)
        s.age += dt;
    std::erase_if(shots_, [](const auto& s) { return s.age > .16; });
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
        const auto range = glm::length(
            body.position + (found == agents_.end() ? glm::dvec3(0) : found->second.offset) - f.position);
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
        local.push_back({raw, body, f.position, f.velocity, unit(f.forward), &a, range, safe});
    }
    for (auto& c : local) {
        auto& a = *c.agent;
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
        const bool hostile = c.body.kind == ActivityKind::Raider ||
                             (c.body.kind == ActivityKind::Patrol && a.aggroUntil > player.seconds);
        const bool engage =
            detectable && hostile && (!c.safe || c.body.kind == ActivityKind::Patrol) && !player.landed;
        const bool alarm = a.aggroUntil > player.seconds && detectable;
        if (c.body.disabled || c.body.hull <= 0) {
            a.mode = NpcMode::Disabled;
            a.drift = {};
            continue;
        }
        const bool aimed = glm::dot(c.forward, -toward) > .998 && distance < 4500;
        if (engage && aimed && player.seconds >= a.nextEvade && (fired || weapons.level() > 0)) {
            a.evadeUntil = player.seconds + 1.1;
            a.nextEvade = player.seconds + 3.4;
        }
        if ((engage || alarm) && c.body.hull <= 30)
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
            desired = limited(c.velocity - c.base.velocity, 250.) +
                      toward * std::clamp((distance - 1100.) * .55, -180., 360.);
            desired +=
                side * (a.mode == NpcMode::Evade ? 300. : 75.) * std::sin(player.seconds * 2.7 + phase);
            if (a.mode == NpcMode::Evade)
                desired += up * 95. * std::cos(player.seconds * 3.1 + phase);
        } else if (a.mode == NpcMode::Retreat)
            desired = -toward * (civilian(c.body.kind) ? 220. : 480.) + side * 70.;
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
        a.drift += limited(desired - a.drift, (a.mode == NpcMode::Retreat ? 70. : 45.) * dt);
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
        a.attitude = glm::normalize(glm::slerp(a.attitude, target, blend));
        a.cooldown = std::max(0., a.cooldown - dt);
        a.heat = std::max(0., a.heat - 28 * dt);
        if (detectable && hostile) {
            report_.hostiles++;
            if (report_.nearest < 0 || distance < report_.nearest)
                report_.nearest = distance;
        }
        if (!engage || a.mode == NpcMode::Retreat || a.cooldown > 0 || a.heat > 82 || distance < 70 ||
            distance > 2400)
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
        std::vector<ActivityBody> targets;
        targets.reserve(input.size() + 1);
        for (auto body : input)
            if (body.id != c.body.id) {
                apply(body);
                weapons.apply(body);
                targets.push_back(body);
            }
        targets.push_back(proxy);
        const auto trace = traceWeapon(u, ray, 2400, targets, geometry);
        const bool hit = trace.body && trace.body->id == proxy.id;
        if (hit) {
            report_.playerDamage += c.body.kind == ActivityKind::Raider ? 4. : 6.;
            damageFlash_ = 1;
        }
        if (shots_.size() >= 24)
            shots_.erase(shots_.begin());
        shots_.push_back({ray.reference, ray.origin + forward * 16., ray.origin + forward * trace.distance, 0,
                          1, trace.blocked, false, 1, true});
        ++a.shots;
        a.heat += 16;
        a.cooldown = c.body.kind == ActivityKind::Raider ? .9 : 1.1;
        report_.firing++;
    }
    std::erase_if(agents_, [&](const auto& entry) {
        const auto& a = entry.second;
        return a.mode == NpcMode::Patrol && a.aggroUntil < player.seconds && glm::length(a.offset) < .05 &&
               glm::length(a.drift) < .05;
    });
    return report_;
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
    if (a.mode == NpcMode::Patrol)
        return;
    b.hostile =
        b.kind == ActivityKind::Raider || (b.kind == ActivityKind::Patrol && a.aggroUntil > lastSeconds_);
    b.status = (b.hostile ? "HOSTILE / " : "DISTRESS / ") + std::string(npcModeName(a.mode));
    if (b.hostile)
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
}
size_t NpcCombat::residentBytes() const {
    return sizeof(*this) + agents_.size() * (sizeof(NpcAgent) + 64) + shots_.capacity() * sizeof(WeaponShot);
}
std::string NpcCombat::serialize(uint64_t seed) const {
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITE_NPC_COMBAT 1 " << seed << ' ' << lastSeconds_ << ' '
      << observedShots_ << ' ' << agents_.size();
    for (const auto& [_, a] : agents_) {
        o << ' ' << a.id.str() << ' ' << unsigned(a.mode);
        for (auto p : {a.offset, a.drift})
            o << ' ' << p.x << ' ' << p.y << ' ' << p.z;
        o << ' ' << a.attitude.w << ' ' << a.attitude.x << ' ' << a.attitude.y << ' ' << a.attitude.z;
        o << ' ' << a.aggroUntil << ' ' << a.evadeUntil << ' ' << a.nextEvade << ' ' << a.cooldown << ' '
          << a.heat << ' ' << a.lastHull << ' ' << a.shots;
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
            h != "INFINITE_NPC_COMBAT" || version != 1 || seed != u.seed() ||
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
            for (auto value : {a.aggroUntil, a.evadeUntil, a.nextEvade, a.cooldown, a.heat, a.lastHull})
                if (!std::isfinite(value) || value < 0 || value > 1e12)
                    return false;
            if (a.cooldown > 2 || a.heat > 100 || a.lastHull > 100)
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
} // namespace infinite
