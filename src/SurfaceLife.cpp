#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace infinite {
namespace {
constexpr double pi = std::numbers::pi;
bool cancelled(const std::atomic<bool>* cancel) {
    return cancel && cancel->load();
}
glm::dvec3 sphere(const Random& r, uint64_t i) {
    const double y = r.signedUnit(i), a = r.unit(i + 1) * 2 * pi;
    return {std::sqrt(1 - y * y) * std::cos(a), y, std::sqrt(1 - y * y) * std::sin(a)};
}
glm::dvec3 surfacePoint(const Universe& u, const Node& p, glm::dvec3 d, double extra = 0) {
    d = glm::normalize(d);
    return d * (std::pow(10., p.frame.logRadius) + u.terrainHeight(p, d) + extra);
}
bool dry(const Node& p, double height) {
    return p.geology->liquidWater < .08 || height > 2;
}
std::string_view kindName(SettlementKind kind) {
    switch (kind) {
    case SettlementKind::City:
        return "CITY";
    case SettlementKind::Colony:
        return "COLONY";
    case SettlementKind::Mining:
        return "MINING";
    default:
        return "RESEARCH";
    }
}
} // namespace
size_t SurfaceWorld::residentBytes() const {
    size_t bytes = sizeof(*this) + planet.path.capacity() * sizeof(Segment) +
                   settlements.capacity() * sizeof(SurfaceSettlement);
    for (const auto& s : settlements)
        bytes += s.name.capacity() + s.buildings.capacity() * sizeof(SurfaceBuilding) +
                 s.roads.capacity() * sizeof(SurfaceRoad) + s.junctions.capacity() * sizeof(CityJunction) +
                 s.streets.capacity() * sizeof(CityStreet) + s.districts.capacity() * sizeof(CityDistrict) +
                 s.parcels.capacity() * sizeof(CityParcel) + s.props.capacity() * sizeof(CityProp) +
                 s.pedestrianPoints.capacity() * sizeof(glm::vec3);
    return bytes;
}
std::shared_ptr<SurfaceWorld> generateSurfaceLife(const Universe& u, const Node& p,
                                                  const std::atomic<bool>* cancel) {
    auto world = std::make_shared<SurfaceWorld>();
    world->planet = p.address;
    world->seed = featureSeed(p.seed, Feature::SurfaceSociety, societyRecipeVersion);
    world->descriptor = descriptorFingerprint(p);
    world->climate = planetClimate(p);
    const auto& climate = world->climate;
    const double radius = std::pow(10., p.frame.logRadius);
    world->radiusMetres = radius;
    if (!climate.solid || radius < 90000 || climate.gravity > 32 || climate.temperatureK < 85 ||
        climate.temperatureK > 650 || cancelled(cancel))
        return world;
    const Random r{world->seed};
    // Many worlds remain wilderness. A separate society stream never feeds
    // back into the planet's descriptor, history, terrain or child addresses.
    if (r.unit(0) > (climate.temperate ? .96 : .72))
        return world;
    const bool colonyClimate = climate.temperatureK > 190 && climate.temperatureK < 370;
    const unsigned count = climate.temperate ? 7 + unsigned(r.unit(1) * 8)
                           : colonyClimate   ? 3 + unsigned(r.unit(1) * 4)
                                             : 1 + unsigned(r.unit(1) * 3);
    for (unsigned site = 0; site < count; ++site) {
        if (cancelled(cancel))
            return {};
        SurfaceSettlement s;
        s.slot = site;
        s.seed = mix64(world->seed ^ mix64(site));
        const Random random{s.seed};
        s.kind = climate.temperate                       ? SettlementKind::City
                 : colonyClimate && random.unit(0) < .65 ? SettlementKind::Colony
                 : p.environment.chemistry.metal > .22   ? SettlementKind::Mining
                                                         : SettlementKind::Research;
        s.sealed = !climate.temperate;
        bool found = false;
        for (unsigned attempt = 0; attempt < 192; ++attempt) {
            if (cancelled(cancel))
                return {};
            auto d = sphere(random, 10 + attempt * 4);
            if (site < 2) {
                // Port-served development near the EXISTING orbital harbour's
                // subpoint. This is fixed in the planet, never placed at the eye.
                const auto hub = glm::normalize(glm::dvec3(.3, 1, .4));
                const auto frame = tangentFrame(hub);
                const double a = random.unit(12 + attempt * 4) * 2 * pi;
                const double reach = 500 + attempt * 850 + site * 5000;
                d = glm::normalize(hub + frame * glm::dvec3(std::cos(a), 0, std::sin(a)) * (reach / radius));
            }
            const auto normal = u.terrainNormal(p, d);
            const double h = u.terrainHeight(p, d);
            const auto localClimate = sampleWeather(climate, d, h, 0);
            const double localT = localClimate.temperatureK;
            if (!dry(p, h) || glm::dot(normal, d) < .994 ||
                (climate.temperate && (localT < 265 || localT > 315 || localClimate.pressureBar < .5)))
                continue;
            const auto frame = tangentFrame(d);
            double low = h, high = h;
            bool landable = true;
            for (int x : {-1, 1})
                for (int z : {-1, 1}) {
                    const auto corner = glm::normalize(d + frame * glm::dvec3(x * 34., 0, z * 34.) / radius);
                    const double height = u.terrainHeight(p, corner);
                    low = std::min(low, height);
                    high = std::max(high, height);
                    landable &= dry(p, height) && glm::dot(corner, u.terrainNormal(p, corner)) > .99;
                }
            if (!landable || high - low > 7)
                continue;
            bool separated = true;
            for (const auto& other : world->settlements)
                separated &= glm::length(other.direction - d) * radius > other.radius + 2500;
            if (!separated)
                continue;
            s.direction = d;
            s.landingPoint = d * (radius + h);
            s.orientation = frame * glm::angleAxis(random.unit(2) * 2 * pi, glm::dvec3(0, 1, 0));
            found = true;
            break;
        }
        if (!found)
            continue;
        const unsigned side = s.kind == SettlementKind::City ? 15 : s.kind == SettlementKind::Colony ? 9 : 5;
        const double spacing = s.kind == SettlementKind::City ? 80 : 65;
        const double half = side / 2;
        s.radius = (half + 1) * spacing * 1.5;
        const auto pointAt = [&](glm::dvec3 local, double extra = 0) {
            return surfacePoint(u, p, s.direction + s.orientation * local / radius, extra);
        };
        for (unsigned row = 0; row < side; row++)
            for (unsigned column = 0; column < side; column++) {
                if (cancelled(cancel))
                    return {};
                const unsigned parcel = row * side + column;
                const double x = (column - half) * spacing, z = (row - half) * spacing;
                if (std::abs(x) < spacing * .7 || std::abs(z) < spacing * .7)
                    continue; // Clear cross streets, pad and public concourse.
                const Random plot{mix64(s.seed ^ mix64(parcel + 100))};
                if (plot.unit(0) > (s.kind == SettlementKind::City ? .90 : .78))
                    continue;
                SurfaceBuilding b;
                b.id = mix64(s.seed ^ mix64(parcel + 100));
                const auto local = glm::dvec3(x + plot.signedUnit(1) * 4, 0, z + plot.signedUnit(2) * 4);
                const auto d = glm::normalize(s.direction + s.orientation * local / radius);
                if (glm::dot(d, u.terrainNormal(p, d)) < .98)
                    continue;
                b.orientation =
                    tangentFrame(d) * glm::angleAxis(random.unit(2) * 2 * pi, glm::dvec3(0, 1, 0));
                const double distance = glm::length(glm::dvec2(x, z));
                if (s.kind == SettlementKind::City) {
                    b.kind = distance < spacing * 4 && plot.unit(3) > .25 ? SurfaceMesh::Tower
                                                                          : SurfaceMesh::Block;
                    b.dimensions = {22 + plot.unit(4) * 18,
                                    b.kind == SurfaceMesh::Tower ? 32 + plot.unit(5) * 90
                                                                 : 8 + plot.unit(5) * 19,
                                    22 + plot.unit(6) * 18};
                } else {
                    b.kind = plot.unit(3) < .55 ? SurfaceMesh::Dome : SurfaceMesh::Industry;
                    b.dimensions = {22 + plot.unit(4) * 14, 10 + plot.unit(5) * 12, 22 + plot.unit(6) * 14};
                }
                if (parcel % 11 == 0)
                    b.kind = SurfaceMesh::Utility;
                if (parcel % 13 == 0 && climate.moisture > .25)
                    b.kind = SurfaceMesh::Greenhouse;
                double low = u.terrainHeight(p, d), high = low;
                bool suitable = dry(p, low);
                for (int a : {-1, 1})
                    for (int c : {-1, 1}) {
                        const auto corner = glm::normalize(
                            d + b.orientation *
                                    glm::dvec3(a * b.dimensions.x * .5, 0, c * b.dimensions.z * .5) / radius);
                        const double h = u.terrainHeight(p, corner);
                        low = std::min(low, h);
                        high = std::max(high, h);
                        suitable &= dry(p, h);
                    }
                if (!suitable || high - low > 12)
                    continue;
                // Raised foundations accommodate terrain; the terrain itself is immutable.
                b.position = d * (radius + high + .5);
                b.foundationMetres = high - low + 1.5;
                b.tint = glm::mix(glm::vec3(.56, .67, .70), glm::vec3(.78, .55, .38), float(plot.unit(7)));
                if (b.kind == SurfaceMesh::Block || b.kind == SurfaceMesh::Tower ||
                    b.kind == SurfaceMesh::Dome)
                    b.inhabitants =
                        unsigned(b.dimensions.x * b.dimensions.z *
                                 std::max(1., std::floor(b.dimensions.y / 3)) / (s.sealed ? 45 : 30));
                else
                    b.inhabitants = 4 + unsigned(plot.unit(8) * 18);
                s.population += b.inhabitants;
                s.buildings.push_back(b);
            }
        if (s.buildings.size() < 4)
            continue;
        // Roads follow the same spherical terrain at 12 m intervals, avoiding
        // straight floating chords and giving each parcel a street frontage.
        for (int axis = 0; axis < 2; axis++)
            for (int lane = -int(half); lane <= int(half); lane += 2) {
                const double cross = lane == 0 ? 0 : (lane + (lane > 0 ? -.5 : .5)) * spacing;
                const double reach = (half + .3) * spacing;
                for (double step = -reach; step < reach; step += 12) {
                    const auto a = axis ? glm::dvec3(cross, 0, step) : glm::dvec3(step, 0, cross);
                    const auto b = axis ? glm::dvec3(cross, 0, std::min(reach, step + 12))
                                        : glm::dvec3(std::min(reach, step + 12), 0, cross);
                    const auto pa = pointAt(a, .22), pb = pointAt(b, .22);
                    if (dry(p, glm::length(pa) - radius) && dry(p, glm::length(pb) - radius))
                        s.roads.push_back({pa, pb, lane == 0 ? 10. : 6.});
                }
            }
        for (size_t i = 0; i < s.walkway.size(); i++) {
            const double a = 2 * pi * i / s.walkway.size();
            s.walkway[i] = pointAt({std::cos(a) * 47, 0, std::sin(a) * 47}, .05);
        }
        s.name = std::string(kindName(s.kind)) + " " + std::to_string(s.seed % 100000);
        s.buildings.shrink_to_fit();
        s.roads.shrink_to_fit();
        world->settlements.push_back(std::move(s));
    }
    // Retain the original site-selection recipe first: later city refinement
    // cannot shift another settlement's anchor or change an existing port ID.
    for (auto& settlement : world->settlements) {
        if (cancelled(cancel))
            return {};
        refineCity(u, p, settlement, cancel);
    }
    world->settlements.shrink_to_fit();
    return world;
}
SurfaceResident sampleResident(const SurfaceWorld& world, const SurfaceSettlement& s, unsigned id,
                               double seconds) {
    if (id >= s.population || !std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::invalid_argument("Invalid surface resident");
    SurfaceResident person;
    person.id = mix64(s.seed ^ mix64(uint64_t(id) + 0x70656f706c65ULL));
    auto resident = id;
    const SurfaceBuilding* home = nullptr;
    for (const auto& building : s.buildings) {
        if (resident < building.inhabitants) {
            person.homeBuilding = building.id;
            home = &building;
            break;
        }
        resident -= building.inhabitants;
    }
    person.suited = s.sealed;
    const Random r{person.id};
    const auto weather =
        sampleWeather(world.climate, s.direction, glm::length(s.landingPoint) - world.radiusMetres, seconds);
    // The public route is deliberately empty during severe storms. Everyone
    // still has a stable identity and home; culling does not remove population.
    const double routine = std::fmod(seconds / 900 + r.unit(0), 1.);
    person.outside = routine < .72 && weather.storm < .58;
    if (home && home->pathCount >= 2) {
        const auto begin = home->pathBegin, end = begin + home->pathCount;
        double total = 0;
        for (size_t i = begin + 1; i < end; i++)
            total += glm::length(glm::dvec3(s.pedestrianPoints[i] - s.pedestrianPoints[i - 1]));
        double travel = std::fmod(seconds * (.75 + r.unit(1) * .55) + r.unit(2) * total * 2, total * 2);
        const bool returning = travel > total;
        if (returning)
            travel = 2 * total - travel;
        for (size_t i = begin + 1; i < end; i++) {
            const auto a = glm::dvec3(s.pedestrianPoints[i - 1]), b = glm::dvec3(s.pedestrianPoints[i]);
            const double length = glm::length(b - a);
            if (travel <= length || i + 1 == end) {
                person.position =
                    s.landingPoint +
                    s.orientation * glm::mix(a, b, std::clamp(travel / std::max(1e-12, length), 0., 1.));
                const auto forward = s.orientation * glm::normalize((b - a) * (returning ? -1. : 1.));
                person.orientation = glm::quatLookAtRH(forward, glm::normalize(person.position));
                return person;
            }
            travel -= length;
        }
    }
    const double travel = std::fmod(seconds * (.75 + r.unit(1) * .55) / 36 + r.unit(2) * 8, 8.);
    const auto segment = size_t(travel);
    const auto a = s.walkway[segment], b = s.walkway[(segment + 1) % s.walkway.size()];
    person.position = glm::mix(a, b, travel - segment);
    const auto up = glm::normalize(person.position), forward = glm::normalize(b - a);
    person.orientation = glm::quatLookAtRH(forward, up);
    return person;
}
ActivityBody surfaceContact(const SurfaceWorld& world, const SurfaceSettlement& s, double seconds) {
    ActivityBody body;
    body.id = {world.planet, surfaceActivitySlot + s.slot};
    body.kind = ActivityKind::SurfacePort;
    body.position = s.landingPoint;
    body.orientation = s.orientation;
    body.radius = s.radius;
    body.seed = s.seed;
    body.name = s.name;
    body.tint = s.sealed ? glm::vec3(.95, .68, .38) : glm::vec3(.44, 1, .72);
    const auto weather = sampleWeather(world.climate, s.direction, 0, seconds);
    body.status = std::to_string(s.population) + " RESIDENTS / " + (s.sealed ? "SEALED / " : "OPEN / ") +
                  std::string(weatherName(weather.type)) + " / J APPROACH / L LAND";
    if (s.layoutRecipe)
        body.status += " / " + std::string(cityPatternName(s.pattern));
    return body;
}
std::optional<double> surfaceCollision(const SurfaceWorld& world, glm::dvec3 from, glm::dvec3 to,
                                       double radius) {
    std::optional<double> hit;
    for (const auto& s : world.settlements) {
        const auto centre = (from + to) * .5;
        if (glm::length(centre - s.landingPoint) > s.radius + glm::length(to - from) * .5 + radius + 200)
            continue;
        for (const auto& b : s.buildings) {
            const auto inv = glm::inverse(b.orientation);
            const auto a = inv * (from - b.position), delta = inv * (to - from);
            const glm::dvec3 lo{-b.dimensions.x * .5 - radius, -b.foundationMetres - radius,
                                -b.dimensions.z * .5 - radius};
            const double roof =
                (b.kind == SurfaceMesh::Industry || b.kind == SurfaceMesh::Utility) ? 1.35 : 1.10;
            const glm::dvec3 hi{b.dimensions.x * .5 + radius, b.dimensions.y * roof + radius,
                                b.dimensions.z * .5 + radius};
            double near = 0, far = 1;
            for (int axis = 0; axis < 3; axis++) {
                if (std::abs(delta[axis]) < 1e-20) {
                    if (a[axis] < lo[axis] || a[axis] > hi[axis]) {
                        near = 2;
                        break;
                    }
                } else {
                    double t0 = (lo[axis] - a[axis]) / delta[axis], t1 = (hi[axis] - a[axis]) / delta[axis];
                    if (t0 > t1)
                        std::swap(t0, t1);
                    near = std::max(near, t0);
                    far = std::min(far, t1);
                }
            }
            if (near <= far && near <= 1 && (!hit || near < *hit))
                hit = near;
        }
    }
    return hit;
}
} // namespace infinite
