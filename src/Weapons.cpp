#include "infinite/Weapons.hpp"
#include "infinite/CombatActors.hpp"
#include "infinite/Piloting.hpp"
#include "infinite/Planetology.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace infinite {
namespace {
const std::array<WeaponSpec, 3> specs{{{"PULSE LASER I", 10, 3000, .28, 17, 28, {.25, .9, 1}},
                                       {"PULSE LASER II", 16, 6000, .24, 16, 32, {.75, .45, 1}},
                                       {"PULSE LASER III", 24, 10000, .20, 15, 36, {1, .45, .65}}}};
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
        if (!s.enemy && s.mount < maxWeaponMounts)
            mask |= 1u << s.mount;
    return mask;
}
void WeaponSystem::advance(double dt) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(dt, 60.);
    for (auto& m : mounts_) {
        m.cooldown = std::max(0., m.cooldown - dt);
        m.heat = std::max(0., m.heat - weaponSpec(m.level).cooling * std::max(.65, m.crewReadiness) * dt);
        if (m.heat <= 35)
            m.overheated = false;
    }
    for (auto& s : shots_)
        s.age += dt;
    std::erase_if(shots_, [](const auto& s) { return s.age > .16; });
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
void WeaponSystem::apply(ActivityBody& body) const {
    const auto it =
        std::find_if(damage_.begin(), damage_.end(), [&](const auto& d) { return d.id == body.id; });
    if (it == damage_.end() || body.kind == ActivityKind::Harbour || body.kind == ActivityKind::SurfacePort)
        return;
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
}
WeaponTrace traceWeapon(const Universe& u, const WeaponRay& ray, double rangeMetres,
                        std::span<const ActivityBody> targets, const Geometry* geometry) {
    auto limit = rangeMetres;
    const auto home = u.describe(ray.reference);
    struct HomeRay {
        Address home;
        glm::dvec3 origin, direction;
    };
    std::vector<HomeRay> frames;
    frames.reserve(8);
    auto localRay = [&](const Address& address) -> const HomeRay& {
        const auto it =
            std::find_if(frames.begin(), frames.end(), [&](const auto& f) { return f.home == address; });
        if (it != frames.end())
            return *it;
        frames.push_back(
            {address,
             u.pointRelative(ray.reference, ray.origin / std::pow(10., home.frame.logRadius), address, 0),
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
    std::optional<ActivityBody> chosen;
    for (auto b : targets) {
        const auto& local = localRay(b.id.home);
        const auto distance = sphere(b.position - local.origin, local.direction, b.radius);
        if (distance >= ray.scale * 6 && distance < limit) {
            limit = distance;
            chosen = b;
        }
    }
    return {limit, surface || bool(chosen), chosen};
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
                             std::span<const ActivityBody> targets, const Geometry* geometry) {
    if (mount >= maxWeaponMounts)
        return false;
    auto& m = mounts_[mount];
    if (!m.level || m.integrity <= 0 || m.crewReadiness <= 0 || m.cooldown > 1e-9 || m.overheated ||
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
    bool disabled = false;
    if (chosen && !chosen->disabled && chosen->kind != ActivityKind::Harbour &&
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
            it->hull = std::max(0., it->hull - spec.damage * (componentHit ? .35 : 1.));
            it->hitSeconds = ray.seconds;
            it->position = chosen->position;
            it->orientation = chosen->orientation;
            disabled = it->hull == 0;
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
                      m.level, false, mount});
    return true;
}
void WeaponSystem::annotate(FlightInstruments& t, bool inhibited) const {
    const auto& m = mounts_[0];
    t.weaponBattery = mounts_[1].level || mounts_[2].level;
    t.weapon = m.level ? std::string(weaponSpec(m.level).name) : "NO WEAPON FITTED";
    t.weaponHeat = m.heat;
    t.weaponStatus = m.integrity <= 0 ? "NOSE GUN OFFLINE / F9 REPAIR"
                     : !m.level       ? "F2 WEAPONS / BUY A LASER"
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
    o << std::setprecision(17) << "INFINITE_COMBAT 3";
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
        if (!(in >> h >> v) || h != "INFINITE_COMBAT" || (v < 1 || v > 3))
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
