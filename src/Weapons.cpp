#include "infinite/Weapons.hpp"
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
void WeaponSystem::advance(double dt) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(dt, 60.);
    cooldown_ = std::max(0., cooldown_ - dt);
    heat_ = std::max(0., heat_ - weaponSpec(level_).cooling * dt);
    if (heat_ <= 35)
        overheated_ = false;
    for (auto& s : shots_)
        s.age += dt;
    std::erase_if(shots_, [](const auto& s) { return s.age > .16; });
}
void WeaponSystem::apply(ActivityBody& body) const {
    const auto it =
        std::find_if(damage_.begin(), damage_.end(), [&](const auto& d) { return d.id == body.id; });
    if (it == damage_.end() || body.kind == ActivityKind::Harbour || body.kind == ActivityKind::SurfacePort)
        return;
    body.hull = it->hull;
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
bool WeaponSystem::fire(const Universe& u, const WeaponRay& input, std::span<const ActivityBody> targets,
                        const Geometry* geometry) {
    if (!level_ || cooldown_ > 1e-9 || overheated_ || !finite(input.origin) || !finite(input.direction) ||
        !finite(input.right) || !std::isfinite(input.scale) || input.scale < 1e-12 || input.scale > 1 ||
        !std::isfinite(input.seconds) || input.seconds < 0 || input.seconds > 1e12 ||
        glm::length(input.direction) < .99 || glm::length(input.direction) > 1.01 ||
        shotsFired_ >= 1000000000000ULL)
        return false;
    WeaponRay ray = input;
    ray.direction = glm::normalize(ray.direction);
    const auto& spec = weaponSpec(level_);
    if (heat_ + spec.heatPerShot > 100) {
        overheated_ = true;
        return false;
    }
    auto limit = spec.rangeMetres * ray.scale;
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
                std::abs(glm::length(local.origin) - radius) < spec.rangeMetres * ray.scale + radius * .05) {
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
    const bool surface = limit < spec.rangeMetres * ray.scale;
    std::optional<ActivityBody> chosen;
    for (auto b : targets) {
        apply(b);
        const auto& local = localRay(b.id.home);
        const auto distance = sphere(b.position - local.origin, local.direction, b.radius);
        if (distance >= ray.scale * 6 && distance < limit) {
            limit = distance;
            chosen = b;
        }
    }
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
            it->hull = std::max(0., it->hull - spec.damage);
            it->hitSeconds = ray.seconds;
            it->position = chosen->position;
            it->orientation = chosen->orientation;
            disabled = it->hull == 0;
        }
    }
    ++shotsFired_;
    heat_ += spec.heatPerShot;
    cooldown_ = spec.interval;
    if (shots_.size() >= 12)
        shots_.erase(shots_.begin());
    shots_.push_back({ray.reference,
                      ray.origin + ray.direction * (6 * ray.scale) +
                          ray.right * ((shotsFired_ & 1 ? 1.1 : -1.1) * ray.scale),
                      ray.origin + ray.direction * limit, 0, ray.scale, surface || bool(chosen), disabled,
                      level_});
    return true;
}
void WeaponSystem::annotate(FlightInstruments& t, bool inhibited) const {
    t.weapon = level_ ? std::string(weaponSpec(level_).name) : "NO WEAPON FITTED";
    t.weaponHeat = heat_;
    t.weaponStatus = !level_       ? "F2 WEAPONS / BUY A LASER"
                     : inhibited   ? "WEAPON INHIBITED"
                     : overheated_ ? "OVERHEAT / COOLING"
                                   : "SPACE FIRE";
}
std::string WeaponSystem::serialize() const {
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITE_COMBAT 1 " << heat_ << ' ' << cooldown_ << ' ' << overheated_
      << ' ' << shotsFired_ << ' ' << damage_.size();
    for (const auto& d : damage_)
        o << ' ' << d.id.str() << ' ' << d.hull << ' ' << d.hitSeconds << ' ' << d.position.x << ' '
          << d.position.y << ' ' << d.position.z << ' ' << d.orientation.w << ' ' << d.orientation.x << ' '
          << d.orientation.y << ' ' << d.orientation.z;
    return o.str();
}
bool WeaponSystem::restore(std::string_view text, const Universe& u) {
    try {
        WeaponSystem p;
        p.level_ = level_;
        std::istringstream in{std::string(text)};
        std::string h;
        unsigned v, hot;
        size_t count;
        if (!(in >> h >> v >> p.heat_ >> p.cooldown_ >> hot >> p.shotsFired_ >> count) ||
            h != "INFINITE_COMBAT" || v != 1 || !std::isfinite(p.heat_) || p.heat_ < 0 || p.heat_ > 100 ||
            !std::isfinite(p.cooldown_) || p.cooldown_ < 0 || p.cooldown_ > 1 || hot > 1 ||
            p.shotsFired_ > 1000000000000ULL || count > 2048)
            return false;
        p.overheated_ = hot;
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
            d.id = ActivityId::parse(id);
            const auto body = sampleActivity(u.describe(d.id.home), d.id.slot, d.hitSeconds);
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
