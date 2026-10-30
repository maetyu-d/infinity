#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <numbers>
#include <queue>
#include <set>

namespace infinite {
namespace {
constexpr double pi = std::numbers::pi;
using V = glm::dvec2;
double cross(V a, V b) {
    return a.x * b.y - a.y * b.x;
}
double segmentDistance(V p, V a, V b) {
    const auto d = b - a;
    const double t = glm::dot(d, d) > 1e-12 ? std::clamp(glm::dot(p - a, d) / glm::dot(d, d), 0., 1.) : 0;
    return glm::length(p - a - d * t);
}
bool inside(V p, const std::array<V, 4>& q) {
    double sign = 0;
    for (unsigned i = 0; i < 4; i++) {
        const double c = cross(q[(i + 1) % 4] - q[i], p - q[i]);
        if (std::abs(c) < 1e-8)
            continue;
        if (sign && sign * c < 0)
            return false;
        sign = c;
    }
    return true;
}
bool overlaps(const std::array<V, 4>& a, const std::array<V, 4>& b, double gap = 1.5) {
    for (const auto* q : {&a, &b})
        for (unsigned i = 0; i < 2; i++) {
            const auto d = (*q)[i + 1] - (*q)[i];
            const auto n = glm::normalize(V(-d.y, d.x));
            double loA = 1e30, hiA = -1e30, loB = 1e30, hiB = -1e30;
            for (auto p : a) {
                loA = std::min(loA, glm::dot(p, n));
                hiA = std::max(hiA, glm::dot(p, n));
            }
            for (auto p : b) {
                loB = std::min(loB, glm::dot(p, n));
                hiB = std::max(hiB, glm::dot(p, n));
            }
            if (hiA + gap < loB || hiB + gap < loA)
                return false;
        }
    return true;
}
struct Face {
    uint64_t id;
    std::array<uint16_t, 4> nodes;
    std::array<std::vector<int>, 4> streets;
};
struct Lot {
    uint64_t id;
    V centre, along, inward;
    double width, depth, priority;
    uint16_t frontage, district;
};
struct Planner {
    const Universe& u;
    const Node& planet;
    SurfaceSettlement& s;
    const std::atomic<bool>* cancel;
    double radius, extent;
    Random random;
    std::vector<Face> faces;
    std::map<std::pair<uint16_t, uint16_t>, int> links;
    std::vector<std::vector<std::pair<uint16_t, double>>> adjacency;
    std::vector<double> distance;
    std::vector<int> parent;
    std::vector<bool> connected;
    Planner(const Universe& u, const Node& p, SurfaceSettlement& s, const std::atomic<bool>* cancel)
        : u(u), planet(p), s(s), cancel(cancel), radius(std::pow(10., p.frame.logRadius)),
          extent(s.radius * .68), random{s.layoutSeed} {}
    bool stopped() const {
        return cancel && cancel->load();
    }
    glm::dvec3 point(V p, double extra = .22) const {
        const auto d = glm::normalize(s.direction + s.orientation * glm::dvec3(p.x, 0, p.y) / radius);
        return d * (radius + u.terrainHeight(planet, d) + extra);
    }
    V local(glm::dvec3 p) const {
        const auto q = glm::inverse(s.orientation) * (p - s.landingPoint);
        return {q.x, q.z};
    }
    bool dry(glm::dvec3 p) const {
        return planet.geology->liquidWater < .08 || glm::length(p) - radius > 2.2;
    }
    uint16_t node(V p, uint64_t key) {
        const auto id = uint16_t(s.junctions.size());
        s.junctions.push_back({mix64(s.layoutSeed ^ key), p, point(p)});
        return id;
    }
    int edge(uint16_t a, uint16_t b, StreetClass kind, bool circular = false) {
        const auto key = std::minmax(a, b);
        if (auto it = links.find(key); it != links.end())
            return it->second;
        const auto x = s.junctions[a].local, y = s.junctions[b].local;
        const double length = glm::length(y - x);
        if (length < 1)
            return -1;
        const auto sideways = V(-(y - x).y, (y - x).x) / length;
        double best = 1e30;
        std::vector<glm::dvec3> chosen;
        for (double bend : {0., 9., -9., 18., -18.}) {
            std::vector<glm::dvec3> route;
            const unsigned steps = std::max(2u, unsigned(std::ceil(length / 10.)));
            double cost = 0;
            bool valid = true;
            for (unsigned i = 0; i <= steps; i++) {
                const double t = double(i) / steps;
                auto q = glm::mix(x, y, t);
                if (circular) {
                    const double angle =
                        std::atan2(x.y, x.x) +
                        std::remainder(std::atan2(y.y, y.x) - std::atan2(x.y, x.x), 2 * pi) * t;
                    const double r = glm::mix(glm::length(x), glm::length(y), t);
                    q = {std::cos(angle) * r, std::sin(angle) * r};
                }
                q += sideways * (std::sin(pi * t) * bend);
                const auto p = point(q);
                if (!dry(p)) {
                    valid = false;
                    break;
                }
                if (!route.empty()) {
                    const double grade =
                        std::abs(glm::length(p) - glm::length(route.back())) / glm::length(p - route.back());
                    if (grade > .18) {
                        valid = false;
                        break;
                    }
                    cost += glm::length(p - route.back()) * (1 + grade * grade * 35);
                }
                route.push_back(p);
            }
            if (valid && cost < best) {
                best = cost;
                chosen = std::move(route);
            }
        }
        if (chosen.empty())
            return -1;
        // Retain height/curvature accuracy without storing a 10 m segment for
        // every straight road. Grade and water were checked on the dense path.
        std::vector<size_t> keep{0};
        const auto simplify = [&](auto&& self, size_t a, size_t b) -> void {
            const auto delta = chosen[b] - chosen[a];
            const double square = glm::dot(delta, delta);
            double worst = .045;
            size_t split = 0;
            for (size_t i = a + 1; i < b; i++) {
                const double t =
                    std::clamp(glm::dot(chosen[i] - chosen[a], delta) / std::max(1e-12, square), 0., 1.);
                const double error = glm::length(chosen[i] - chosen[a] - delta * t);
                if (error > worst) {
                    worst = error;
                    split = i;
                }
            }
            if (split) {
                self(self, a, split);
                self(self, split, b);
            } else
                keep.push_back(b);
        };
        simplify(simplify, 0, chosen.size() - 1);
        std::vector<glm::dvec3> compact;
        for (auto i : keep)
            compact.push_back(chosen[i]);
        chosen = std::move(compact);
        const auto id = uint16_t(s.streets.size());
        const double width = kind == StreetClass::Avenue ? 16 : kind == StreetClass::Footway ? 2.5 : 8;
        CityStreet street{mix64(s.layoutSeed ^ mix64(a) ^ mix64(uint64_t(b) + 1000)),
                          a,
                          b,
                          kind,
                          width,
                          uint32_t(s.roads.size()),
                          uint32_t(chosen.size() - 1)};
        for (size_t i = 1; i < chosen.size(); i++)
            s.roads.push_back({chosen[i - 1], chosen[i], width, kind, id});
        s.streets.push_back(street);
        links.emplace(key, id);
        return id;
    }
    void organic() {
        struct Block {
            double x0, z0, x1, z1;
            uint64_t key;
        };
        std::vector<Block> blocks;
        const double boundary = std::floor(extent / 5) * 5, core = 105;
        const auto subdivide = [&](auto&& self, Block b, unsigned depth) -> void {
            const V centre{(b.x0 + b.x1) * .5, (b.z0 + b.z1) * .5};
            const double maturity = smoothstep(180., 700., s.foundingYears);
            const double oldCore =
                1 - smoothstep(0., extent * .8, glm::length(centre - s.districts[0].centre));
            const double target = 170 - oldCore * maturity * 65;
            const double w = b.x1 - b.x0, h = b.z1 - b.z0;
            if (depth >= 6 || (w < target * 1.4 && h < target * 1.4)) {
                blocks.push_back(b);
                return;
            }
            const Random r{mix64(s.layoutSeed ^ b.key)};
            bool vertical = w > h * (.8 + r.unit(0) * .4);
            if (w < 95)
                vertical = false;
            if (h < 95)
                vertical = true;
            const double low = vertical ? b.x0 : b.z0, high = vertical ? b.x1 : b.z1;
            const double cut = std::clamp(std::round((low + (high - low) * (.38 + r.unit(1) * .24)) / 5) * 5,
                                          low + 55, high - 55);
            if (!(cut > low + 20 && cut < high - 20)) {
                blocks.push_back(b);
                return;
            }
            auto a = b, z = b;
            a.key = mix64(b.key ^ 1);
            z.key = mix64(b.key ^ 2);
            if (vertical) {
                a.x1 = cut;
                z.x0 = cut;
            } else {
                a.z1 = cut;
                z.z0 = cut;
            }
            self(self, a, depth + 1);
            self(self, z, depth + 1);
        };
        subdivide(subdivide, {-boundary, -boundary, boundary, -core, 100}, 0);
        subdivide(subdivide, {-boundary, core, boundary, boundary, 200}, 0);
        subdivide(subdivide, {-boundary, -core, -core, core, 300}, 0);
        subdivide(subdivide, {core, -core, boundary, core, 400}, 0);
        using Key = std::pair<int, int>;
        const auto key = [](V p) { return Key{int(std::lround(p.x)), int(std::lround(p.y))}; };
        std::map<Key, uint16_t> vertices;
        const auto warp = [&](V p) {
            return p + V(std::sin(p.y / extent * 2.8 + random.unit(3) * 6) * 32,
                         std::sin(p.x / extent * 3.2 + random.unit(4) * 6) * 28);
        };
        for (const auto& b : blocks)
            for (V p : {V(b.x0, b.z0), V(b.x1, b.z0), V(b.x1, b.z1), V(b.x0, b.z1)})
                vertices.try_emplace(key(p), 0);
        for (auto& [p, id] : vertices)
            id = node(warp(V(p.first, p.second)),
                      mix64(uint64_t(int64_t(p.first)) ^ mix64(uint64_t(int64_t(p.second)))));
        for (const auto& b : blocks) {
            if (stopped())
                return;
            const std::array<V, 4> q{V(b.x0, b.z0), V(b.x1, b.z0), V(b.x1, b.z1), V(b.x0, b.z1)};
            Face face{b.key,
                      {vertices.at(key(q[0])), vertices.at(key(q[1])), vertices.at(key(q[2])),
                       vertices.at(key(q[3]))},
                      {}};
            for (unsigned side = 0; side < 4; side++) {
                std::vector<std::pair<double, uint16_t>> onEdge;
                for (const auto& [p, id] : vertices) {
                    const V at{p.first, p.second};
                    if (segmentDistance(at, q[side], q[(side + 1) % 4]) < .1)
                        onEdge.push_back({glm::length(at - q[side]), id});
                }
                std::sort(onEdge.begin(), onEdge.end());
                const bool major = (std::abs(q[side].x) == core && std::abs(q[(side + 1) % 4].x) == core) ||
                                   (std::abs(q[side].y) == core && std::abs(q[(side + 1) % 4].y) == core);
                for (size_t i = 1; i < onEdge.size(); i++) {
                    const int id = edge(onEdge[i - 1].second, onEdge[i].second,
                                        major ? StreetClass::Avenue : StreetClass::Local);
                    if (id >= 0)
                        face.streets[side].push_back(id);
                }
            }
            faces.push_back(std::move(face));
        }
    }
    void graph() {
        // Four pedestrian gates surround the retained landing pad. The street
        // graph grows outside this clear public space, rather than through it.
        for (V p : {V(0, -72), V(72, 0), V(0, 72), V(-72, 0)})
            node(p, 100 + s.junctions.size());
        for (unsigned i = 0; i < 4; i++)
            edge(i, (i + 1) % 4, StreetClass::Footway);
        if (s.pattern == CityPattern::Radial) {
            constexpr unsigned rings = 5, sectors = 12;
            std::array<std::array<uint16_t, sectors>, rings> grid;
            const double angle = random.unit(2) * .28;
            for (unsigned ring = 0; ring < rings; ring++)
                for (unsigned sector = 0; sector < sectors; sector++) {
                    const double a = angle + sector * 2 * pi / sectors;
                    const double r = 105 + (extent - 105) * ring / (rings - 1);
                    grid[ring][sector] =
                        node({std::cos(a) * r, std::sin(a) * r}, 1000 + ring * sectors + sector);
                }
            for (unsigned ring = 0; ring < rings; ring++)
                for (unsigned sector = 0; sector < sectors; sector++) {
                    const unsigned next = (sector + 1) % sectors;
                    edge(grid[ring][sector], grid[ring][next],
                         ring == 2 ? StreetClass::Avenue : StreetClass::Local, true);
                    if (ring + 1 < rings)
                        edge(grid[ring][sector], grid[ring + 1][sector],
                             sector % 3 == 0 ? StreetClass::Avenue : StreetClass::Local);
                    if (ring + 1 < rings)
                        faces.push_back({ring * sectors + sector,
                                         {grid[ring][sector], grid[ring][next], grid[ring + 1][next],
                                          grid[ring + 1][sector]},
                                         {}});
                }
        } else if (s.pattern == CityPattern::Organic) {
            organic();
        } else {
            const int half = s.kind == SettlementKind::City ? 5 : s.kind == SettlementKind::Colony ? 3 : 2;
            const int side = half * 2 + 1;
            const double pitch = extent / half;
            std::vector<int> grid(size_t(side * side), -1);
            const auto index = [&](int x, int z) { return size_t((z + half) * side + x + half); };
            const auto normal = glm::inverse(s.orientation) * u.terrainNormal(planet, s.direction);
            const double angle = s.pattern == CityPattern::Contour ? std::atan2(normal.x, -normal.z) : 0;
            for (int z = -half; z <= half; z++)
                for (int x = -half; x <= half; x++) {
                    if (stopped())
                        return;
                    if (x == 0 && z == 0)
                        continue;
                    V p{x * pitch, z * pitch};
                    if (s.pattern == CityPattern::Organic) {
                        p += V(std::sin(p.y / extent * 2.8 + random.unit(3) * 6) * pitch * .27,
                               std::sin(p.x / extent * 3.2 + random.unit(4) * 6) * pitch * .24);
                    } else if (s.pattern == CityPattern::Contour) {
                        p.y *= .72;
                        p.y += std::sin(p.x / extent * 3.1) * pitch * .30;
                        p = V(p.x * std::cos(angle) - p.y * std::sin(angle),
                              p.x * std::sin(angle) + p.y * std::cos(angle));
                    }
                    if (glm::length(p) < 92)
                        continue;
                    grid[index(x, z)] = node(p, 2000 + index(x, z));
                }
            for (int z = -half; z <= half; z++)
                for (int x = -half; x <= half; x++) {
                    const auto a = grid[index(x, z)];
                    if (a < 0)
                        continue;
                    if (x < half && grid[index(x + 1, z)] >= 0)
                        edge(a, grid[index(x + 1, z)], z == 0 ? StreetClass::Avenue : StreetClass::Local);
                    if (z < half && grid[index(x, z + 1)] >= 0)
                        edge(a, grid[index(x, z + 1)], x == 0 ? StreetClass::Avenue : StreetClass::Local);
                    if (x < half && z < half) {
                        const auto b = grid[index(x + 1, z)], c = grid[index(x + 1, z + 1)],
                                   d = grid[index(x, z + 1)];
                        if (b >= 0 && c >= 0 && d >= 0)
                            faces.push_back(
                                {index(x, z), {uint16_t(a), uint16_t(b), uint16_t(c), uint16_t(d)}, {}});
                    }
                }
        }
        // Port access links choose the nearest generated node in each sector.
        for (uint16_t gate = 0; gate < 4; gate++) {
            std::vector<std::pair<double, uint16_t>> choices;
            for (uint16_t i = 4; i < s.junctions.size(); i++)
                choices.push_back({glm::length(s.junctions[i].local - s.junctions[gate].local), i});
            std::sort(choices.begin(), choices.end());
            for (const auto& [distance, id] : choices)
                if (edge(gate, id, StreetClass::Footway) >= 0)
                    break;
        }
        adjacency.resize(s.junctions.size());
        for (const auto& e : s.streets) {
            const double length = glm::length(s.junctions[e.a].position - s.junctions[e.b].position);
            adjacency[e.a].push_back({e.b, length});
            adjacency[e.b].push_back({e.a, length});
        }
        distance.assign(adjacency.size(), 1e30);
        parent.assign(adjacency.size(), -1);
        using Item = std::pair<double, uint16_t>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
        distance[0] = 0;
        queue.push({0, 0});
        while (!queue.empty()) {
            const auto [cost, a] = queue.top();
            queue.pop();
            if (cost != distance[a])
                continue;
            for (auto [b, length] : adjacency[a])
                if (cost + length < distance[b]) {
                    distance[b] = cost + length;
                    parent[b] = a;
                    queue.push({distance[b], b});
                }
        }
        connected.resize(distance.size());
        for (size_t i = 0; i < distance.size(); i++)
            connected[i] = distance[i] < 1e29;
        // Reject inaccessible components as a whole. Roads, parcels and resident
        // routes therefore all refer to the same connected graph.
        std::vector<uint16_t> remap(s.junctions.size(), UINT16_MAX);
        std::vector<CityJunction> nodes;
        std::vector<double> costs;
        std::vector<int> parents;
        for (size_t i = 0; i < s.junctions.size(); i++)
            if (connected[i]) {
                remap[i] = uint16_t(nodes.size());
                nodes.push_back(s.junctions[i]);
                costs.push_back(distance[i]);
                parents.push_back(parent[i]);
            }
        for (auto& id : parents)
            if (id >= 0)
                id = remap[size_t(id)];
        std::vector<CityStreet> streets;
        std::vector<SurfaceRoad> roads;
        links.clear();
        std::vector<int> edgeRemap(s.streets.size(), -1);
        for (size_t source = 0; source < s.streets.size(); source++) {
            auto e = s.streets[source];
            if (connected[e.a] && connected[e.b]) {
                const auto first = e.roadBegin, count = e.roadCount;
                e.a = remap[e.a];
                e.b = remap[e.b];
                e.roadBegin = uint32_t(roads.size());
                const auto id = uint16_t(streets.size());
                edgeRemap[source] = id;
                for (size_t i = first; i < first + count; i++) {
                    auto road = s.roads[i];
                    road.street = id;
                    roads.push_back(road);
                }
                streets.push_back(e);
                links.emplace(std::minmax(e.a, e.b), id);
            }
        }
        std::erase_if(faces, [&](const auto& f) {
            for (auto n : f.nodes)
                if (!connected[n])
                    return true;
            return false;
        });
        for (auto& f : faces)
            for (auto& n : f.nodes)
                n = remap[n];
        s.junctions = std::move(nodes);
        s.streets = std::move(streets);
        s.roads = std::move(roads);
        distance = std::move(costs);
        parent = std::move(parents);
        connected.assign(distance.size(), true);
        for (auto& face : faces)
            for (unsigned i = 0; i < 4; i++) {
                if (!face.streets[i].empty()) {
                    std::vector<int> mapped;
                    for (auto id : face.streets[i])
                        if (edgeRemap[size_t(id)] >= 0)
                            mapped.push_back(edgeRemap[size_t(id)]);
                    face.streets[i] = std::move(mapped);
                    continue;
                }
                const auto key = std::minmax(face.nodes[i], face.nodes[(i + 1) % 4]);
                const auto found = links.find(key);
                if (found != links.end())
                    face.streets[i].push_back(found->second);
            }
    }
    void districts() {
        const auto climate = planetClimate(planet);
        const auto wind = glm::inverse(s.orientation) * samplePlanningClimate(climate, s.direction, 0).wind;
        V downwind{wind.x, wind.z};
        if (glm::length(downwind) < .1)
            downwind = {1, .3};
        downwind = glm::normalize(downwind);
        s.foundingYears = (s.sealed ? 45. : 180.) + random.unit(10) * (s.sealed ? 160 : 700);
        const auto add = [&](BuildingUse use, V p, double r, double age) {
            s.districts.push_back({mix64(s.layoutSeed ^ mix64(s.districts.size() + 800)), use, p, r, age});
        };
        add(BuildingUse::Civic, {-extent * .25, -extent * .18}, extent * .31, s.foundingYears);
        add(BuildingUse::Commerce, {extent * .23, -extent * .10}, extent * .31, s.foundingYears * .65);
        add(BuildingUse::Housing, {-extent * .30, extent * .35}, extent * .65, s.foundingYears * .45);
        add(s.kind == SettlementKind::Research ? BuildingUse::Civic : BuildingUse::Industry,
            downwind * extent * .68, extent * .32, s.foundingYears * .30);
        add(BuildingUse::Agriculture, -downwind * extent * .67, extent * .28, s.foundingYears * .22);
    }
    uint16_t district(V p) const {
        double best = 1e30;
        uint16_t chosen = 2;
        for (uint16_t i = 0; i < s.districts.size(); i++) {
            const auto& d = s.districts[i];
            const double score = glm::length(p - d.centre) / d.radius;
            if (score < best) {
                best = score;
                chosen = i;
            }
        }
        return chosen;
    }
    bool roadClear(const std::array<V, 4>& corners) const {
        for (const auto& road : s.roads) {
            const auto a = local(road.a), b = local(road.b);
            const double margin = road.width * .5 + 2.2;
            for (unsigned i = 0; i < 4; i++) {
                if (segmentDistance(corners[i], a, b) < margin ||
                    segmentDistance(a, corners[i], corners[(i + 1) % 4]) < margin ||
                    segmentDistance(b, corners[i], corners[(i + 1) % 4]) < margin)
                    return false;
            }
            if (inside((a + b) * .5, corners))
                return false;
        }
        return true;
    }
    void prop(SurfaceMesh kind, V at, glm::dvec3 size, glm::vec3 tint, double yaw = 0) {
        const auto p = point(at, .26);
        s.props.push_back({kind, p, size,
                           tangentFrame(glm::normalize(p)) * glm::angleAxis(yaw, glm::dvec3(0, 1, 0)), tint});
    }
    void lots(const std::vector<SurfaceBuilding>& legacy) {
        std::vector<Lot> lots;
        for (const auto& face : faces) {
            if (stopped())
                return;
            std::array<V, 4> q;
            for (unsigned i = 0; i < 4; i++)
                q[i] = s.junctions[face.nodes[i]].local;
            const auto centre = (q[0] + q[1] + q[2] + q[3]) * .25;
            if (glm::length(centre) < 125 || glm::length(centre) > s.radius * .88)
                continue;
            const auto zone = district(centre);
            const Random cell{mix64(s.layoutSeed ^ face.id)};
            if (!s.sealed && cell.unit(0) < .13 && glm::length(centre) > 180) {
                // Parks occupy real street blocks; vegetation is restricted to
                // the same temperate biosphere proxy as the open city.
                for (unsigned i = 0; i < 5; i++) {
                    const V at = glm::mix(glm::mix(q[0], q[1], .25 + .5 * cell.unit(20 + i)),
                                          glm::mix(q[3], q[2], .25 + .5 * cell.unit(30 + i)),
                                          .25 + .5 * cell.unit(40 + i));
                    prop(SurfaceMesh::Tree, at, {4, 6 + cell.unit(50 + i) * 4, 4}, {.38, .72, .48});
                }
                continue;
            }
            for (unsigned side : {0u, 2u})
                for (const int street : face.streets[side]) {
                    if (street < 0 || !connected[s.streets[street].a] || !connected[s.streets[street].b])
                        continue;
                    const auto a = s.junctions[s.streets[street].a].local,
                               b = s.junctions[s.streets[street].b].local;
                    const double length = glm::length(b - a);
                    const auto along = (b - a) / length;
                    auto inward = V(-along.y, along.x);
                    if (glm::dot(inward, centre - (a + b) * .5) < 0)
                        inward = -inward;
                    const double faceDepth = std::abs(glm::dot(centre - (a + b) * .5, inward)) * 2;
                    const double margin = 14, available = length - 2 * margin;
                    if (available < 17)
                        continue;
                    const unsigned plots = std::clamp(unsigned(available / (s.sealed ? 33 : 37)), 1u, 5u);
                    const double width = std::min(52., available / plots - 4.5);
                    const double depth = std::min(38., faceDepth * .44 - s.streets[street].width * .5 - 6);
                    if (width < 14 || depth < 14)
                        continue;
                    for (unsigned plot = 0; plot < plots; plot++) {
                        const double t = (margin + (plot + .5) * available / plots) / length;
                        const auto at =
                            glm::mix(a, b, t) + inward * (s.streets[street].width * .5 + 5.5 + depth * .5);
                        const std::array<V, 4> corners{at - along * width * .5 - inward * depth * .5,
                                                       at + along * width * .5 - inward * depth * .5,
                                                       at + along * width * .5 + inward * depth * .5,
                                                       at - along * width * .5 + inward * depth * .5};
                        bool fits = true;
                        for (auto p : corners)
                            fits &= inside(p, q) && glm::length(p) > 80;
                        if (!fits)
                            continue;
                        const uint64_t id = mix64(
                            s.layoutSeed ^ mix64(face.id) ^ mix64(s.streets[street].id) ^
                            mix64(side * 16 + plot + 10000) ^ mix64(uint64_t(s.pattern) + 0x70617263656cULL));
                        lots.push_back({id, at, along, inward, width, depth, Random{id}.unit(0),
                                        uint16_t(street), zone});
                    }
                }
        }
        std::sort(lots.begin(), lots.end(),
                  [](const auto& a, const auto& b) { return a.priority < b.priority; });
        std::set<uint64_t> assigned;
        for (const auto& lot : lots) {
            if (stopped() || s.buildings.size() >= 240)
                break;
            CityParcel parcel{lot.id,
                              {lot.centre - lot.along * lot.width * .5 - lot.inward * lot.depth * .5,
                               lot.centre + lot.along * lot.width * .5 - lot.inward * lot.depth * .5,
                               lot.centre + lot.along * lot.width * .5 + lot.inward * lot.depth * .5,
                               lot.centre - lot.along * lot.width * .5 + lot.inward * lot.depth * .5},
                              lot.frontage,
                              lot.district};
            if (!roadClear(parcel.corners))
                continue;
            bool blocked = false;
            for (const auto& other : s.parcels)
                if (overlaps(parcel.corners, other.corners)) {
                    blocked = true;
                    break;
                }
            if (blocked)
                continue;
            SurfaceBuilding b;
            b.parcel = lot.id;
            b.district = lot.district;
            b.frontage = lot.frontage;
            b.use = s.districts[lot.district].use;
            const Random r{lot.id};
            const auto p = point(lot.centre, 0), d = glm::normalize(p);
            if (!dry(p) || glm::dot(d, u.terrainNormal(planet, d)) < .98)
                continue;
            auto front = s.orientation * glm::dvec3(-lot.inward.x, 0, -lot.inward.y);
            front -= d * glm::dot(front, d);
            front = glm::normalize(front);
            b.orientation = glm::quatLookAtRH(front, d);
            b.dimensions = {lot.width, 1, lot.depth};
            double low = glm::length(p) - radius, high = low;
            bool valid = true;
            for (int x : {-1, 1})
                for (int z : {-1, 1}) {
                    const auto corner = glm::normalize(
                        d + b.orientation * glm::dvec3(x * lot.width * .5, 0, z * lot.depth * .5) / radius);
                    const double h = u.terrainHeight(planet, corner);
                    low = std::min(low, h);
                    high = std::max(high, h);
                    valid &= planet.geology->liquidWater < .08 || h > 2.2;
                }
            if (!valid || high - low > 12)
                continue;
            b.position = d * (radius + high + .5);
            b.foundationMetres = high - low + 1.5;
            const double accessibility =
                1 /
                (1 + std::min(distance[s.streets[b.frontage].a], distance[s.streets[b.frontage].b]) / 450);
            const auto& zone = s.districts[lot.district];
            const double centreDensity =
                1 - smoothstep(0., extent * .75, glm::length(lot.centre - s.districts[1].centre));
            unsigned floors = 0;
            if (s.sealed) {
                b.kind = b.use == BuildingUse::Industry      ? SurfaceMesh::Warehouse
                         : b.use == BuildingUse::Agriculture ? SurfaceMesh::Greenhouse
                         : b.use == BuildingUse::Civic       ? SurfaceMesh::Dome
                                                             : SurfaceMesh::HabitatCluster;
                floors = 2 + unsigned(r.unit(1) * 3);
                b.dimensions.y = 4 + floors * 3.;
            } else if (b.use == BuildingUse::Industry) {
                b.kind = SurfaceMesh::Warehouse;
                b.dimensions.y = 9 + r.unit(1) * 9;
            } else if (b.use == BuildingUse::Agriculture) {
                b.kind = SurfaceMesh::Greenhouse;
                b.dimensions.y = 7 + r.unit(1) * 5;
            } else if (b.use == BuildingUse::Civic) {
                b.kind = SurfaceMesh::Hall;
                b.dimensions.y = 12 + r.unit(1) * 12;
            } else {
                floors = 3 + unsigned((3 + centreDensity * 25 + accessibility * 4) * (.55 + r.unit(1) * .45));
                b.kind = floors > 14  ? SurfaceMesh::SteppedTower
                         : floors > 5 ? SurfaceMesh::Courtyard
                                      : SurfaceMesh::RowHousing;
                b.dimensions.y = floors * 3.2 + 2;
            }
            const double area = lot.width * lot.depth *
                                (b.kind == SurfaceMesh::Courtyard        ? .65
                                 : b.kind == SurfaceMesh::HabitatCluster ? .55
                                                                         : .85);
            if (b.use == BuildingUse::Housing || b.use == BuildingUse::Commerce) {
                b.inhabitants = unsigned(area * std::max(1u, floors) *
                                         (b.use == BuildingUse::Commerce ? .45 : 1) / (s.sealed ? 38 : 30));
                b.jobs =
                    unsigned(area * std::max(1u, floors) * (b.use == BuildingUse::Commerce ? .55 : .04) / 18);
            } else
                b.jobs = unsigned(area / 15);
            const auto base =
                glm::mix(glm::vec3(.66, .74, .78), glm::vec3(.83, .66, .51), float(random.unit(16)));
            const auto tint = b.use == BuildingUse::Industry      ? glm::vec3(.55, .62, .69)
                              : b.use == BuildingUse::Civic       ? glm::vec3(.90, .81, .65)
                              : b.use == BuildingUse::Agriculture ? glm::vec3(.52, .80, .68)
                                                                  : base;
            b.tint = tint * float(.88 + .12 * (1 - zone.ageYears / (s.foundingYears * 2)));
            // Retain nearby legacy building identities where possible. Newly
            // added parcels use the separate urban-layout stream.
            double nearest = 1e30;
            uint64_t identity = 0;
            for (const auto& old : legacy)
                if (!assigned.contains(old.id)) {
                    const double range = glm::length(local(old.position) - lot.centre);
                    if (range < nearest) {
                        nearest = range;
                        identity = old.id;
                    }
                }
            b.id = identity ? identity : mix64(s.layoutSeed ^ lot.id);
            while (assigned.contains(b.id))
                b.id = mix64(b.id ^ lot.id ^ 0x6e65776275696c64ULL);
            assigned.insert(b.id);
            s.population += b.inhabitants;
            s.parcels.push_back(parcel);
            s.buildings.push_back(b);
        }
    }
    void paths() {
        // The route follows the connected street graph, outside the frontage.
        // Each resident's home supplies its route; there is no camera-centred crowd.
        for (auto& b : s.buildings) {
            if (stopped())
                return;
            const auto& street = s.streets[b.frontage];
            const V home = local(b.position);
            const uint16_t target = distance[street.a] < distance[street.b] ? street.a : street.b;
            std::vector<V> route;
            const auto appendStreet = [&](const CityStreet& e, uint16_t target, bool initial) {
                std::vector<V> line{local(s.roads[e.roadBegin].a)};
                for (unsigned i = e.roadBegin; i < e.roadBegin + e.roadCount; i++)
                    line.push_back(local(s.roads[i].b));
                size_t near = 1;
                double nearest = 1e30, ratio = 0;
                if (initial)
                    for (size_t i = 1; i < line.size(); i++) {
                        const auto delta = line[i] - line[i - 1];
                        const double t =
                            std::clamp(glm::dot(home - line[i - 1], delta) / glm::dot(delta, delta), 0., 1.);
                        const double range = glm::length(home - glm::mix(line[i - 1], line[i], t));
                        if (range < nearest) {
                            nearest = range;
                            near = i;
                            ratio = t;
                        }
                    }
                const auto tangent = glm::normalize(line[near] - line[near - 1]);
                const auto normal = V(-tangent.y, tangent.x);
                const double sign =
                    glm::dot(normal, home - glm::mix(line[near - 1], line[near], ratio)) >= 0 ? 1. : -1.;
                const auto offset = [&](size_t i) {
                    const auto delta =
                        glm::normalize(line[std::min(i + 1, line.size() - 1)] - line[i ? i - 1 : 0]);
                    return line[i] + V(-delta.y, delta.x) * (sign * (e.width * .5 + 1));
                };
                if (initial)
                    route.push_back(glm::mix(line[near - 1], line[near], ratio) +
                                    normal * (sign * (e.width * .5 + 1)));
                if (target == e.a) {
                    for (int i = int(initial ? near - 1 : line.size() - 1); i >= 0; i--)
                        route.push_back(offset(size_t(i)));
                } else {
                    for (size_t i = initial ? near : 0; i < line.size(); i++)
                        route.push_back(offset(i));
                }
                // Crosswalk through the junction, avoiding diagonal shortcuts
                // across the corner parcels when changing sidewalk direction.
                route.push_back(s.junctions[target].local);
            };
            appendStreet(street, target, true);
            int current = target;
            unsigned hops = 0;
            while (current >= 0 && hops++ < 3) {
                if (current < 4)
                    break;
                const int next = parent[size_t(current)];
                if (next < 0)
                    break;
                const auto found = links.find(std::minmax(uint16_t(current), uint16_t(next)));
                if (found == links.end())
                    break;
                appendStreet(s.streets[found->second], uint16_t(next), false);
                current = next;
            }
            if (route.size() < 2)
                continue;
            std::vector<glm::dvec3> dense;
            for (size_t edge = 1; edge < route.size(); edge++) {
                const auto a = route[edge - 1], z = route[edge];
                const unsigned count = std::max(1u, unsigned(std::ceil(glm::length(z - a) / 9.)));
                for (unsigned i = 0; i < count; i++) {
                    const auto p = point(glm::mix(a, z, double(i) / count), .15);
                    if (dense.empty() || glm::length(p - dense.back()) > .01)
                        dense.push_back(p);
                }
            }
            dense.push_back(point(route.back(), .15));
            std::vector<size_t> keep{0};
            const auto simplify = [&](auto&& self, size_t a, size_t z) -> void {
                const auto d = dense[z] - dense[a];
                const double square = glm::dot(d, d);
                double worst = .025;
                size_t split = 0;
                for (size_t i = a + 1; i < z; i++) {
                    const double t =
                        std::clamp(glm::dot(dense[i] - dense[a], d) / std::max(1e-12, square), 0., 1.);
                    const double error = glm::length(dense[i] - dense[a] - d * t);
                    if (error > worst) {
                        worst = error;
                        split = i;
                    }
                }
                if (split) {
                    self(self, a, split);
                    self(self, split, z);
                } else
                    keep.push_back(z);
            };
            simplify(simplify, 0, dense.size() - 1);
            b.pathBegin = uint32_t(s.pedestrianPoints.size());
            for (size_t i = 0; i < std::min(size_t(40), keep.size()); i++)
                s.pedestrianPoints.push_back(
                    glm::vec3(glm::inverse(s.orientation) * (dense[keep[i]] - s.landingPoint)));
            b.pathCount = uint16_t(std::min(size_t(40), keep.size()));
        }
    }
    void groundRoads() {
        for (auto& road : s.roads) {
            if (stopped())
                return;
            const auto centre = (road.a + road.b) * .5;
            const auto side =
                glm::normalize(glm::cross(glm::normalize(centre), glm::normalize(road.b - road.a)));
            const std::array<double, 4> offsets{-road.width * .5 - 2, -road.width * .5, road.width * .5,
                                                road.width * .5 + 2};
            for (unsigned end = 0; end < 2; end++)
                for (unsigned i = 0; i < 4; i++) {
                    const auto d = glm::normalize((end ? road.b : road.a) + side * offsets[i]);
                    const auto p = d * (radius + u.terrainHeight(planet, d) + .12);
                    road.surface[end * 4 + i] = glm::vec3(p - centre);
                }
        }
    }
    void furniture() {
        // Paved civic square surrounds the untouched landing footprint.
        for (int x = -4; x <= 4; x++)
            for (int z = -4; z <= 4; z++) {
                const V at{x * 16., z * 16.};
                if (std::abs(x) <= 2 && std::abs(z) <= 2)
                    continue;
                prop(SurfaceMesh::Plaza, at, {16, 1, 16}, {.58, .66, .72});
            }
        for (size_t i = 0; i < s.streets.size(); i++) {
            const auto& e = s.streets[i];
            if (!connected[e.a] || e.kind == StreetClass::Footway)
                continue;
            const auto a = s.junctions[e.a].local, b = s.junctions[e.b].local;
            const auto direction = glm::normalize(b - a), side = V(-direction.y, direction.x);
            if (i % 2 == 0)
                prop(SurfaceMesh::Light, glm::mix(a, b, .5) + side * (e.width * .5 + 3.5), {1, 6, 1},
                     {.78, .88, 1});
            if (!s.sealed && i % 5 == 0)
                prop(SurfaceMesh::Tree, glm::mix(a, b, .32) - side * (e.width * .5 + 4), {3, 6, 3},
                     {.44, .8, .58});
            if (i % 9 == 0)
                prop(s.sealed ? SurfaceMesh::Solar : SurfaceMesh::Bench,
                     glm::mix(a, b, .60) + side * (e.width * .5 + 4),
                     s.sealed ? glm::dvec3(7, 2, 4) : glm::dvec3(2, .7, .7), {.62, .79, .85});
        }
    }
};
} // namespace
std::string_view cityPatternName(CityPattern pattern) {
    switch (pattern) {
    case CityPattern::Radial:
        return "RADIAL";
    case CityPattern::Organic:
        return "ORGANIC";
    case CityPattern::Contour:
        return "CONTOUR";
    default:
        return "PLANNED";
    }
}
bool refineCity(const Universe& u, const Node& p, SurfaceSettlement& original,
                const std::atomic<bool>* cancel, std::optional<CityPattern> override) {
    if (!hasSolidSurface(p) || original.buildings.empty() || (cancel && cancel->load()))
        return false;
    SurfaceSettlement next = original;
    next.layoutSeed =
        featureSeed(original.seed, Feature::SurfaceSociety, 0x636974790000ULL + cityRecipeVersion);
    const Random r{next.layoutSeed};
    next.pattern = original.kind == SettlementKind::City     ? CityPattern(r.bits(0) % 3)
                   : original.kind == SettlementKind::Mining ? CityPattern::Contour
                                                             : CityPattern::Planned;
    if (override)
        next.pattern = *override;
    next.buildings.clear();
    next.roads.clear();
    next.junctions.clear();
    next.streets.clear();
    next.districts.clear();
    next.parcels.clear();
    next.props.clear();
    next.pedestrianPoints.clear();
    next.population = 0;
    Planner planner(u, p, next, cancel);
    planner.districts();
    planner.graph();
    planner.lots(original.buildings);
    if ((cancel && cancel->load()) || next.buildings.size() < 4)
        return false;
    planner.paths();
    planner.groundRoads();
    planner.furniture();
    if (cancel && cancel->load())
        return false;
    next.layoutRecipe = cityRecipeVersion;
    next.buildings.shrink_to_fit();
    next.roads.shrink_to_fit();
    next.pedestrianPoints.shrink_to_fit();
    next.junctions.shrink_to_fit();
    next.streets.shrink_to_fit();
    next.parcels.shrink_to_fit();
    next.districts.shrink_to_fit();
    next.props.shrink_to_fit();
    original = std::move(next);
    return true;
}
uint64_t cityFingerprint(const SurfaceSettlement& s) {
    uint64_t hash = mix64(s.seed ^ s.layoutSeed ^ s.layoutRecipe ^ uint64_t(s.pattern) ^ s.population);
    const auto integer = [&](uint64_t v) { hash = mix64(hash ^ v); };
    const auto number = [&](double v) { hash = mix64(hash ^ std::bit_cast<uint64_t>(v)); };
    const auto vector = [&](const auto& v) {
        number(v.x);
        number(v.y);
        if constexpr (requires { v.z; })
            number(v.z);
    };
    integer(s.slot);
    integer(uint64_t(s.kind));
    integer(s.sealed);
    integer(s.name.size());
    for (unsigned char c : s.name)
        integer(c);
    vector(s.direction);
    vector(s.landingPoint);
    for (double v :
         {s.orientation.w, s.orientation.x, s.orientation.y, s.orientation.z, s.radius, s.foundingYears})
        number(v);
    for (const auto& p : s.walkway)
        vector(p);
    integer(s.junctions.size());
    for (const auto& n : s.junctions) {
        integer(n.id);
        vector(n.local);
        vector(n.position);
    }
    integer(s.streets.size());
    for (const auto& e : s.streets) {
        hash = mix64(hash ^ e.id ^ e.a ^ (uint64_t(e.b) << 16) ^ uint64_t(e.kind));
        number(e.width);
        integer(e.roadBegin);
        integer(e.roadCount);
    }
    integer(s.districts.size());
    for (const auto& d : s.districts) {
        integer(d.id);
        integer(uint64_t(d.use));
        vector(d.centre);
        number(d.radius);
        number(d.ageYears);
    }
    integer(s.parcels.size());
    for (const auto& p : s.parcels) {
        integer(p.id);
        integer(p.frontage);
        integer(p.district);
        for (const auto& corner : p.corners)
            vector(corner);
    }
    integer(s.roads.size());
    for (const auto& road : s.roads) {
        for (double v : {road.a.x, road.a.y, road.a.z, road.b.x, road.b.y, road.b.z, road.width})
            number(v);
        hash = mix64(hash ^ uint64_t(road.kind) ^ road.street);
        for (const auto& p : road.surface) {
            number(p.x);
            number(p.y);
            number(p.z);
        }
    }
    integer(s.buildings.size());
    for (const auto& b : s.buildings) {
        hash = mix64(hash ^ b.id ^ b.parcel ^ uint64_t(b.kind) ^ b.inhabitants ^ b.jobs);
        integer(uint64_t(b.use));
        integer(b.frontage);
        integer(b.district);
        integer(b.pathBegin);
        integer(b.pathCount);
        number(b.foundationMetres);
        for (double v : {b.position.x, b.position.y, b.position.z, b.dimensions.x, b.dimensions.y,
                         b.dimensions.z, b.orientation.w, b.orientation.x, b.orientation.y, b.orientation.z})
            number(v);
        number(b.tint.x);
        number(b.tint.y);
        number(b.tint.z);
    }
    integer(s.props.size());
    for (const auto& prop : s.props) {
        hash = mix64(hash ^ uint64_t(prop.kind));
        for (double v :
             {prop.position.x, prop.position.y, prop.position.z, prop.dimensions.x, prop.dimensions.y,
              prop.dimensions.z, prop.orientation.w, prop.orientation.x, prop.orientation.y,
              prop.orientation.z, double(prop.tint.x), double(prop.tint.y), double(prop.tint.z)})
            number(v);
    }
    integer(s.pedestrianPoints.size());
    for (const auto& p : s.pedestrianPoints) {
        number(p.x);
        number(p.y);
        number(p.z);
    }
    return hash;
}
} // namespace infinite
