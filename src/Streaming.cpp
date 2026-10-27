#include "infinite/Streaming.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace infinite {
std::pair<Address, glm::dvec3> canonicalRoot(const Address& address, glm::dvec3 point) {
    if (!std::isfinite(glm::length(point)))
        throw std::invalid_argument("Non-finite root location");
    Address root{address.cell, {}};
    glm::i64vec3 shift{};
    for (unsigned axis = 0; axis < 3; ++axis)
        shift[axis] = int64_t(std::clamp(std::floor((point[axis] + 1) / rootCellWidth), double(INT64_MIN),
                                         std::nextafter(double(INT64_MAX), 0.)));
    if (auto cell = adjacentCell(root.cell, shift)) {
        point -= cellDisplacement(*cell, root.cell);
        root.cell = *cell;
    }
    return {root, point};
}
std::vector<Address> rootNeighborhood(const Address& address, glm::dvec3 eye, bool ahead) {
    std::vector<Address> out;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
                if (auto cell = adjacentCell(address.cell, {x, y, z}))
                    out.push_back({*cell, {}});
    if (ahead) {
        glm::ivec3 delta{};
        for (unsigned i = 0; i < 3; ++i)
            delta[i] = eye[i] > .35 ? 1 : eye[i] < -.35 ? -1 : 0;
        if (auto cell = adjacentCell(address.cell, delta); cell && *cell != address.cell) {
            auto extra = rootNeighborhood({*cell, {}});
            for (const auto& root : extra)
                if (std::find(out.begin(), out.end(), root) == out.end())
                    out.push_back(root);
        }
    }
    return out;
}
double streamedCosmicDensity(const Universe& u, const Address& root, glm::dvec3 point) {
    glm::ivec3 side{};
    glm::dvec3 weight{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        side[axis] = point[axis] >= 0 ? 1 : -1;
        weight[axis] = smoothstep(.96, 1.04, std::abs(point[axis]));
    }
    double density = 0, total = 0;
    for (unsigned mask = 0; mask < 8; ++mask) {
        glm::ivec3 delta{};
        double w = 1;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const bool other = mask & (1u << axis);
            delta[axis] = other ? side[axis] : 0;
            w *= other ? weight[axis] : 1 - weight[axis];
        }
        if (w == 0)
            continue;
        if (auto cell = adjacentCell(root.cell, delta)) {
            const Address a{*cell, {}};
            density +=
                w * u.cosmicDensity((point - glm::dvec3(delta) * rootCellWidth) * 4., seedOf(a, u.seed()));
            total += w;
        }
    }
    return total ? density / total : 0;
}
std::shared_ptr<Geometry> cosmicBoundaryGeometry(const Universe& u, const Node& root,
                                                 const std::atomic<bool>* cancel) {
    const auto begin = std::chrono::steady_clock::now();
    auto g = std::make_shared<Geometry>();
    g->address = g->referenceFrame = root.address;
    g->seed = root.seed;
    g->descriptor = descriptorFingerprint(root);
    g->logUnits = rootCellLogMetres;
    const Random random{featureSeed(root.seed, Feature::CosmicWeb, 0x626f756e64617279ULL)};
    for (uint64_t i = 0; i < 2441; ++i) {
        if (cancel && cancel->load())
            return {};
        // Uniformly sample only the collar that the legacy cube did not cover.
        const unsigned axis = unsigned(i % 3);
        glm::dvec3 p(random.signedUnit(i * 5), random.signedUnit(i * 5 + 1), random.signedUnit(i * 5 + 2));
        p[axis] = (random.unit(i * 5 + 3) < .5 ? -1 : 1) * (.96 + .04 * std::abs(p[axis]));
        const auto absolute = glm::abs(p);
        const unsigned largest = absolute.x >= absolute.y && absolute.x >= absolute.z ? 0
                                 : absolute.y >= absolute.z                           ? 1
                                                                                      : 2;
        if (largest != axis)
            continue;
        const double density = streamedCosmicDensity(u, root.address, p);
        if (random.unit(i * 5 + 4) < density * .55)
            g->particles.push_back({p, glm::vec3(.2, .45, .7) * float(density), .003f});
    }
    g->generationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return g;
}
namespace {
uint64_t id(Tile t) {
    return encodeTile(t);
}
Tile parent(Tile t) {
    return {t.face, t.level - 1, t.x / 2, t.y / 2};
}
bool contains(Tile p, Tile c) {
    return p.face == c.face && p.level <= c.level && (c.x >> (c.level - p.level)) == p.x &&
           (c.y >> (c.level - p.level)) == p.y;
}
std::array<Tile, 4> children(Tile t) {
    return {{{t.face, t.level + 1, t.x * 2, t.y * 2},
             {t.face, t.level + 1, t.x * 2 + 1, t.y * 2},
             {t.face, t.level + 1, t.x * 2, t.y * 2 + 1},
             {t.face, t.level + 1, t.x * 2 + 1, t.y * 2 + 1}}};
}
std::optional<Tile> covering(const std::map<uint64_t, Tile>& leaves, Tile t) {
    for (;;) {
        if (leaves.contains(id(t)))
            return t;
        if (!t.level)
            return {};
        t = parent(t);
    }
}
void balance(std::map<uint64_t, Tile>& leaves) {
    for (unsigned pass = 0; pass < 1000; ++pass) {
        std::optional<Tile> merge;
        for (const auto& [key, tile] : leaves) {
            for (unsigned e = 0; e < 4; ++e)
                if (auto coarse = covering(leaves, terrainNeighbor(tile, e));
                    coarse && coarse->level + 1 < tile.level) {
                    auto p = tile;
                    while (p.level > coarse->level + 1)
                        p = parent(p);
                    merge = p;
                    break;
                }
            if (merge)
                break;
        }
        if (!merge)
            return;
        std::erase_if(leaves, [&](const auto& entry) { return contains(*merge, entry.second); });
        leaves.emplace(id(*merge), *merge);
    }
    throw std::runtime_error("Terrain frontier could not be balanced");
}
unsigned remap(unsigned index, unsigned stitch) {
    unsigned x = index % 9, y = index / 9;
    if ((stitch & 1) && x == 0 && (y & 1))
        --y;
    if ((stitch & 2) && x == 8 && (y & 1))
        --y;
    if ((stitch & 4) && y == 0 && (x & 1))
        --x;
    if ((stitch & 8) && y == 8 && (x & 1))
        --x;
    return y * 9 + x;
}
} // namespace
Tile terrainNeighbor(Tile t, unsigned edge) {
    const double epsilon = 1e-5;
    double u = .5, v = .5;
    if (edge < 2)
        u = edge ? 1 + epsilon : -epsilon;
    else
        v = edge == 3 ? 1 + epsilon : -epsilon;
    return tileAt(tileDirection(t, u, v), t.level);
}
std::vector<Tile> terrainFrontier(glm::dvec3 eye, double altitude, double pixels, size_t budget) {
    budget = std::clamp(budget, size_t(6), size_t(384));
    const auto direction = glm::length(eye) > 0 ? glm::normalize(eye) : glm::dvec3(0, 1, 0);
    auto error = [&](Tile tile) {
        double width = std::ldexp(2.5, -int(tile.level));
        double lateral = std::max(0., glm::length(tileDirection(tile) - direction) - width);
        double distance = std::max(1e-15, std::hypot(std::abs(altitude), lateral));
        return width * pixels / (terrainCells * distance * 2 * std::tan(.5));
    };
    std::map<uint64_t, Tile> leaves;
    for (int face = 0; face < 6; ++face)
        leaves.emplace(id({face, 0, 0, 0}), Tile{face, 0, 0, 0});
    while (leaves.size() + 3 <= budget) {
        auto best = leaves.end();
        double maximum = 12;
        for (auto it = leaves.begin(); it != leaves.end(); ++it)
            if (it->second.level < 24)
                if (double e = error(it->second); e > maximum) {
                    maximum = e;
                    best = it;
                }
        if (best == leaves.end())
            break;
        const auto tile = best->second;
        leaves.erase(best);
        for (auto child : children(tile))
            leaves.emplace(id(child), child);
    }
    balance(leaves);
    std::vector<Tile> out;
    for (const auto& [key, tile] : leaves)
        out.push_back(tile);
    return out;
}
unsigned terrainStitches(Tile tile, const std::vector<Tile>& cut) {
    std::map<uint64_t, Tile> leaves;
    for (auto t : cut)
        leaves.emplace(id(t), t);
    unsigned mask = 0;
    for (unsigned edge = 0; edge < 4; ++edge)
        if (auto n = covering(leaves, terrainNeighbor(tile, edge)); n && n->level < tile.level)
            mask |= 1u << edge;
    return mask;
}
uint64_t terrainVertexKey(Tile tile, unsigned x, unsigned y) {
    unsigned level = tile.level;
    uint32_t u = tile.x * terrainCells + x, v = tile.y * terrainCells + y;
    uint32_t count = terrainCells << level;
    // Canonical face at seams/corners; all arithmetic is exact dyadic integer math.
    glm::i64vec3 p;
    const int64_t a = int64_t(u) * 2 - count, b = int64_t(v) * 2 - count, n = count;
    switch (tile.face) {
    case 0:
        p = {n, b, -a};
        break;
    case 1:
        p = {-n, b, a};
        break;
    case 2:
        p = {a, n, -b};
        break;
    case 3:
        p = {a, -n, b};
        break;
    case 4:
        p = {a, b, n};
        break;
    default:
        p = {-a, b, -n};
        break;
    }
    int face;
    int64_t s, t;
    if (std::abs(p.x) == n) {
        face = p.x > 0 ? 0 : 1;
        s = p.x > 0 ? -p.z : p.z;
        t = p.y;
    } else if (std::abs(p.y) == n) {
        face = p.y > 0 ? 2 : 3;
        s = p.x;
        t = p.y > 0 ? -p.z : p.z;
    } else {
        face = p.z > 0 ? 4 : 5;
        s = p.z > 0 ? p.x : -p.x;
        t = p.y;
    }
    u = uint32_t((s + n) / 2);
    v = uint32_t((t + n) / 2);
    while (level && !(u & 1) && !(v & 1)) {
        --level;
        u /= 2;
        v /= 2;
    }
    return (uint64_t(level) << 59) | (uint64_t(face) << 56) | (uint64_t(u) << 28) | v;
}
glm::dvec3 terrainVertexDirection(uint64_t key) {
    unsigned level = unsigned(key >> 59), face = unsigned((key >> 56) & 7);
    const auto u = uint32_t((key >> 28) & 0xfffffff), v = uint32_t(key & 0xfffffff);
    const double n = double(terrainCells << level);
    return cubeDirection(int(face), 2 * u / n - 1, 2 * v / n - 1);
}
std::shared_ptr<Geometry> terrainGeometry(const Universe& u, const Node& planet, Tile tile, unsigned stitch,
                                          const std::atomic<bool>* cancel) {
    auto begin = std::chrono::steady_clock::now();
    auto g = std::make_shared<Geometry>();
    g->address = g->referenceFrame = planet.address;
    g->seed = planet.seed;
    g->descriptor = descriptorFingerprint(planet);
    g->logUnits = planet.frame.logRadius;
    g->terrainTile = tile;
    g->terrainStitch = stitch;
    const double radius = std::pow(10., planet.frame.logRadius);
    for (unsigned y = 0; y <= 8; ++y) {
        if (cancel && cancel->load())
            return {};
        for (unsigned x = 0; x <= 8; ++x) {
            const auto d = terrainVertexDirection(terrainVertexKey(tile, x, y));
            const auto p = d * (1 + u.terrainHeight(planet, d) / radius);
            const auto normal = u.terrainNormal(planet, d);
            const auto color =
                u.terrainAlbedo(planet, d) *
                float(.19 + .81 * std::max(0., glm::dot(normal, glm::normalize(glm::dvec3(-.5, .8, 1)))));
            g->precisePositions.push_back(p);
            g->solid.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color)});
        }
    }
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x) {
            const unsigned a = y * 9 + x, b = a + 9;
            for (auto index : {a, a + 1, b, a + 1, b + 1, b})
                g->solid.indices.push_back(remap(index, stitch));
        }
    g->solid.compact();
    g->generationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return g;
}
void TerrainStreamer::clear() {
    planet_.reset();
    patches_.clear();
    starts_.clear();
    morph_ = 1;
}
glm::dvec3 TerrainStreamer::oldPosition(uint64_t key) const {
    const unsigned level = unsigned(key >> 59), face = unsigned((key >> 56) & 7);
    const double n = double(terrainCells << level);
    const double u = double((key >> 28) & 0xfffffff) / n, v = double(key & 0xfffffff) / n;
    for (const auto& patch : patches_) {
        const auto tile = patch.tile;
        if (tile.face != int(face))
            continue;
        const double size = double(1u << tile.level);
        const double gx = (u * size - tile.x) * 8, gy = (v * size - tile.y) * 8;
        if (gx < -1e-8 || gy < -1e-8 || gx > 8 + 1e-8 || gy > 8 + 1e-8)
            continue;
        const auto& geometry = *patch.geometry;
        const int cx = std::clamp(int(std::floor(gx)), 0, 7), cy = std::clamp(int(std::floor(gy)), 0, 7);
        for (int y = std::max(0, cy - 1); y <= std::min(7, cy + 1); ++y)
            for (int x = std::max(0, cx - 1); x <= std::min(7, cx + 1); ++x)
                for (unsigned tri = 0; tri < 2; ++tri) {
                    std::array<unsigned, 3> indices;
                    std::array<glm::dvec2, 3> q;
                    for (unsigned k = 0; k < 3; ++k) {
                        indices[k] = geometry.solid.index((y * 8 + x) * 6 + tri * 3 + k);
                        q[k] = {indices[k] % 9, indices[k] / 9};
                    }
                    const auto a = q[1] - q[0], b = q[2] - q[0], p = glm::dvec2(gx, gy) - q[0];
                    const double det = a.x * b.y - a.y * b.x;
                    if (std::abs(det) < 1e-12)
                        continue;
                    const double w1 = (p.x * b.y - p.y * b.x) / det;
                    const double w2 = (a.x * p.y - a.y * p.x) / det;
                    if (w1 < -1e-8 || w2 < -1e-8 || w1 + w2 > 1 + 1e-8)
                        continue;
                    glm::dvec3 weights(1 - w1 - w2, w1, w2), out{};
                    double sum = 0;
                    for (unsigned k = 0; k < 3; ++k) {
                        const auto point = geometry.precisePositions[indices[k]];
                        const double w = weights[k] / std::abs(point[face / 2]);
                        out += point * w;
                        sum += w;
                    }
                    return out / sum;
                }
    }
    throw std::runtime_error("Terrain morph source has a coverage hole");
}
glm::dvec3 TerrainStreamer::position(const TerrainPatch& patch, unsigned vertex) const {
    const auto point = patch.geometry->precisePositions[vertex];
    if (morph_ >= 1)
        return point;
    const auto key = terrainVertexKey(patch.tile, vertex % 9, vertex / 9);
    return glm::mix(starts_.at(key), point, smoothstep(0, 1, morph_));
}
void TerrainStreamer::update(const Node& planet, glm::dvec3 eye, double height, double dt,
                             RegionCache& cache) {
    if (!planet_ || *planet_ != planet.address) {
        clear();
        planet_ = planet.address;
    }
    morph_ = std::min(1., morph_ + std::max(0., dt) / .2);
    const double distance = glm::length(eye);
    const auto direction = distance > 0 ? eye / distance : glm::dvec3(0, 1, 0);
    const double altitude =
        distance - 1 - universe_.terrainHeight(planet, direction) / std::pow(10., planet.frame.logRadius);
    const auto wanted = terrainFrontier(eye, altitude, height);
    std::map<uint64_t, Tile> tree;
    for (auto tile : wanted)
        for (;;) {
            tree.emplace(id(tile), tile);
            if (!tile.level)
                break;
            tile = parent(tile);
        }
    std::map<uint64_t, std::shared_ptr<const Geometry>> ready;
    // Coarse coverage always has priority over deeper refinement.
    std::vector<Tile> ordered;
    for (auto [key, tile] : tree)
        ordered.push_back(tile);
    std::sort(ordered.begin(), ordered.end(), [](Tile a, Tile b) { return a.level < b.level; });
    for (auto tile : ordered)
        if (auto geometry = cache.requestTerrain(planet, tile, 0))
            ready.emplace(id(tile), geometry);
    std::map<uint64_t, Tile> cut;
    auto visit = [&](auto&& self, Tile tile) -> void {
        bool split = false;
        std::array<Tile, 4> kids{};
        if (tile.level < 24) {
            kids = children(tile);
            split = true;
            for (auto child : kids)
                split &= tree.contains(id(child)) && ready.contains(id(child));
        }
        if (split)
            for (auto child : kids)
                self(self, child);
        else if (ready.contains(id(tile)))
            cut.emplace(id(tile), tile);
    };
    for (int face = 0; face < 6; ++face) {
        const Tile root{face, 0, 0, 0};
        if (!ready.contains(id(root)))
            return;
        visit(visit, root);
    }
    balance(cut);
    std::vector<Tile> tiles;
    for (auto [key, tile] : cut)
        tiles.push_back(tile);
    std::vector<TerrainPatch> next;
    bool complete = true;
    for (auto tile : tiles) {
        unsigned stitch = 0;
        for (unsigned edge = 0; edge < 4; ++edge)
            if (auto n = covering(cut, terrainNeighbor(tile, edge)); n && n->level < tile.level)
                stitch |= 1u << edge;
        auto geometry = stitch ? cache.requestTerrain(planet, tile, stitch) : ready.at(id(tile));
        if (!geometry)
            complete = false;
        else
            next.push_back({tile, stitch, geometry});
    }
    if (!complete)
        return;
    if (morph_ < 1)
        return;
    bool same = next.size() == patches_.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = id(next[i].tile) == id(patches_[i].tile) && next[i].stitch == patches_[i].stitch;
    if (same)
        return;
    std::unordered_map<uint64_t, glm::dvec3> starts;
    if (!patches_.empty()) {
        for (const auto& patch : next)
            for (unsigned vertex = 0; vertex < 81; ++vertex) {
                const auto key = terrainVertexKey(patch.tile, vertex % 9, vertex / 9);
                if (!starts.contains(key))
                    starts.emplace(key, oldPosition(key));
            }
    }
    patches_ = std::move(next);
    starts_ = std::move(starts);
    morph_ = starts_.empty() ? 1 : 0;
}
} // namespace infinite
