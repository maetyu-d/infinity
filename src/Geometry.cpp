#include "infinite/Geometry.hpp"
#include <FastNoise/FastNoise.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include <stdexcept>
#include <unordered_map>

namespace infinite {
constexpr double pi = std::numbers::pi;
uint32_t rgba(glm::vec3 c, float a) {
    auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + .5f); };
    return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(a) << 24);
}
glm::vec3 elementColor(uint8_t e) {
    switch (e) {
    case 6:
        return {.63f, .65f, .7f};
    case 8:
        return {1, .29, .16};
    case 14:
        return {.38, .76, .95};
    case 26:
        return {.89, .66, .35};
    case 12:
        return {.54, .86, .26};
    default:
        return {.85, .9, 1};
    }
}
static Vertex vertex(glm::dvec3 p, glm::vec3 color) {
    return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), rgba(color)};
}
static glm::vec3 shade(glm::vec3 c, glm::dvec3 n) {
    if (glm::dot(n, n) < 1e-28)
        n = {0, 1, 0};
    double l = .19 + .81 * std::max(0.0, glm::dot(glm::normalize(n), glm::normalize(glm::dvec3(-.5, .8, 1))));
    return c * static_cast<float>(l);
}
void addLine(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::vec3 color) {
    auto i = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back(vertex(a, color));
    m.vertices.push_back(vertex(b, color));
    m.indices.push_back(i);
    m.indices.push_back(i + 1);
}
void appendSphere(Mesh& m, glm::dvec3 center, double radius, glm::vec3 color, unsigned rings,
                  unsigned sectors, bool emissive) {
    auto base = static_cast<uint32_t>(m.vertices.size());
    for (unsigned y = 0; y <= rings; ++y)
        for (unsigned x = 0; x <= sectors; ++x) {
            double a = 2 * pi * x / sectors, b = pi * y / rings;
            glm::dvec3 n(std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a));
            m.vertices.push_back(vertex(center + n * radius, emissive ? color : shade(color, n)));
        }
    for (unsigned y = 0; y < rings; ++y)
        for (unsigned x = 0; x < sectors; ++x) {
            auto a = base + y * (sectors + 1) + x, b = a + sectors + 1;
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}
std::vector<TerrainLeaf> terrainLeaves(glm::dvec3 view, unsigned maxLevel) {
    std::vector<TerrainLeaf> out;
    std::function<void(Tile)> visit = [&](Tile t) {
        double dot = glm::dot(tileDirection(t), view);
        bool split = t.level < maxLevel && (t.level < 1 || dot > -.2 + static_cast<double>(t.level) * .16);
        if (!split) {
            out.push_back({t});
            return;
        }
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 2; ++x)
                visit({t.face, t.level + 1, t.x * 2 + x, t.y * 2 + y});
    };
    for (int face = 0; face < 6; ++face)
        visit({face, 0, 0, 0});
    return out;
}
static void terrainGrid(Mesh& mesh, const Universe& universe, const Node& n, unsigned size,
                        const std::function<glm::dvec3(double, double)>& direction, bool planet,
                        const std::atomic<bool>* cancel) {
    struct Sample {
        glm::dvec3 point, world;
        glm::vec3 color;
    };
    unsigned side = size + 3;
    std::vector<Sample> samples(size_t(side) * side);
    double metres = std::pow(10.0, n.frame.logRadius), radius = std::pow(10.0, n.planetLogRadius);
    double ratio = metres / radius;
    double h0 = planet ? 0 : universe.terrainHeight(n, n.geographicDirection) / metres;
    auto basis = glm::inverse(n.terrainOrientation);
    for (unsigned y = 0; y < side; ++y) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return;
        for (unsigned x = 0; x < side; ++x) {
            double u = (static_cast<double>(x) - 1) / size, v = (static_cast<double>(y) - 1) / size;
            auto d = direction(u, v);
            auto field = geologySample(*n.geology, d);
            auto& sample = samples[y * side + x];
            sample.world = d * (1 + field.heightMetres / radius);
            sample.point = planet ? sample.world
                                  : basis * ((d - n.geographicDirection) / ratio +
                                             d * (field.heightMetres / metres) - n.geographicDirection * h0);
            sample.color = universe.terrainAlbedo(n, d, field);
        }
    }
    uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (unsigned y = 1; y <= size + 1; ++y)
        for (unsigned x = 1; x <= size + 1; ++x) {
            auto& sample = samples[y * side + x];
            auto normal = glm::cross(samples[(y + 1) * side + x].world - samples[(y - 1) * side + x].world,
                                     samples[y * side + x + 1].world - samples[y * side + x - 1].world);
            if (glm::dot(normal, sample.world) < 0)
                normal = -normal;
            mesh.vertices.push_back(vertex(sample.point, shade(sample.color, normal)));
        }
    for (unsigned y = 0; y < size; ++y)
        for (unsigned x = 0; x < size; ++x) {
            auto a = base + y * (size + 1) + x, b = a + size + 1;
            mesh.indices.insert(mesh.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
}
static void sdfMesh(Mesh& mesh, const std::function<double(glm::dvec3)>& sdf, unsigned cells,
                    const std::function<glm::vec3(glm::dvec3)>& color, const std::atomic<bool>* cancel,
                    double halfExtent = 1.3, glm::dquat lightFrame = {1, 0, 0, 0}) {
    constexpr int tetra[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6},
                                 {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
    constexpr int offsets[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                   {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    constexpr int edges[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    constexpr int triangles[16][7] = {{-1},
                                      {0, 1, 2, -1},
                                      {0, 3, 4, -1},
                                      {1, 2, 3, 2, 4, 3, -1},
                                      {1, 3, 5, -1},
                                      {0, 2, 3, 2, 5, 3, -1},
                                      {0, 1, 4, 1, 5, 4, -1},
                                      {2, 4, 5, -1},
                                      {2, 4, 5, -1},
                                      {0, 1, 4, 1, 5, 4, -1},
                                      {0, 2, 3, 2, 5, 3, -1},
                                      {1, 3, 5, -1},
                                      {1, 2, 3, 2, 4, 3, -1},
                                      {0, 3, 4, -1},
                                      {0, 1, 2, -1},
                                      {-1}};
    double step = 2 * halfExtent / cells;
    unsigned side = cells + 1;
    auto index = [&](unsigned x, unsigned y, unsigned z) { return (z * side + y) * side + x; };
    std::vector<float> field(size_t(side) * side * side);
    for (unsigned z = 0; z <= cells; ++z) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return;
        for (unsigned y = 0; y <= cells; ++y)
            for (unsigned x = 0; x <= cells; ++x)
                field[index(x, y, z)] = sdf(glm::dvec3(x, y, z) * step - glm::dvec3(halfExtent));
    }
    auto gradient = [&](unsigned x, unsigned y, unsigned z) {
        unsigned a = x ? x - 1 : x, b = std::min(x + 1, cells), c = y ? y - 1 : y, d = std::min(y + 1, cells),
                 e = z ? z - 1 : z, f = std::min(z + 1, cells);
        return glm::dvec3((field[index(b, y, z)] - field[index(a, y, z)]) / (b - a),
                          (field[index(x, d, z)] - field[index(x, c, z)]) / (d - c),
                          (field[index(x, y, f)] - field[index(x, y, e)]) / (f - e));
    };
    std::unordered_map<uint64_t, uint32_t> vertices;
    vertices.reserve(cells * cells * 8);
    std::vector<glm::vec3> normals;
    for (unsigned z = 0; z < cells; ++z) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return;
        for (unsigned y = 0; y < cells; ++y)
            for (unsigned x = 0; x < cells; ++x) {
                std::array<glm::dvec3, 8> p;
                std::array<double, 8> v;
                std::array<uint32_t, 8> ids;
                for (int i = 0; i < 8; ++i) {
                    auto& o = offsets[i];
                    p[i] = glm::dvec3(x + o[0], y + o[1], z + o[2]) * step - glm::dvec3(halfExtent);
                    ids[i] = index(x + o[0], y + o[1], z + o[2]);
                    v[i] = field[ids[i]];
                }
                auto edgeVertex = [&](int a, int b) {
                    if (ids[a] > ids[b])
                        std::swap(a, b);
                    uint64_t key = (uint64_t(ids[a]) << 32) | ids[b];
                    auto found = vertices.find(key);
                    if (found != vertices.end())
                        return found->second;
                    double t = v[a] / (v[a] - v[b]);
                    auto point = glm::mix(p[a], p[b], t);
                    auto grad = [&](int i) {
                        auto& o = offsets[i];
                        return gradient(x + o[0], y + o[1], z + o[2]);
                    };
                    auto normal = glm::mix(grad(a), grad(b), t);
                    uint32_t id = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(vertex(point, shade(color(point), lightFrame * normal)));
                    normals.push_back(glm::vec3(normal));
                    vertices.emplace(key, id);
                    return id;
                };
                for (auto& t : tetra) {
                    unsigned mask = 0;
                    for (unsigned i = 0; i < 4; ++i)
                        if (v[t[i]] < 0)
                            mask |= 1u << i;
                    auto& list = triangles[mask];
                    for (int k = 0; list[k] >= 0; k += 3) {
                        std::array<uint32_t, 3> tri;
                        for (int c = 0; c < 3; ++c) {
                            auto& e = edges[list[k + c]];
                            tri[c] = edgeVertex(t[e[0]], t[e[1]]);
                        }
                        auto pos = [&](uint32_t i) {
                            auto& v = mesh.vertices[i];
                            return glm::dvec3(v.x, v.y, v.z);
                        };
                        if (glm::dot(glm::cross(pos(tri[1]) - pos(tri[0]), pos(tri[2]) - pos(tri[0])),
                                     glm::dvec3(normals[tri[0]] + normals[tri[1]] + normals[tri[2]])) < 0)
                            std::swap(tri[1], tri[2]);
                        mesh.indices.insert(mesh.indices.end(), tri.begin(), tri.end());
                    }
                }
            }
    }
    mesh.vertices.shrink_to_fit();
    mesh.indices.shrink_to_fit();
}
std::function<double(glm::dvec3)> densityField(const Universe&, const Node& n, double outsideBand) {
    auto rock = std::make_shared<RockField>(n.address.kind() == Kind::Rock ? n.seed : n.hostRockSeed, n.rock);
    if (n.address.kind() == Kind::Rock)
        return [rock](glm::dvec3 p) { return (*rock)(p); };
    return [rock, n, outsideBand](glm::dvec3 p) {
        double sdf = crystalDensity(n.environment.mineral, n.crystal, p);
        if (outsideBand > 0 && sdf > outsideBand)
            return sdf;
        if (n.hostRockSeed) {
            auto host = n.frame.position + n.frame.orientation * (p * n.grainScaleToRock);
            sdf = std::max(sdf, (*rock)(host) / n.grainScaleToRock);
        }
        return sdf;
    };
}
// Subtract the focused patch footprint from the coarse inspection surface.
// Partition each crossing triangle into disjoint outside half-planes; no
// vertical cavity wall, depth bias, or moved physical surface is introduced.
static void excludePatch(Mesh& mesh, glm::dvec2 half, const std::atomic<bool>* cancel) {
    auto original = std::move(mesh.indices);
    struct Point {
        glm::dvec3 p;
        glm::vec4 color;
    };
    auto read = [&](uint32_t i) {
        auto& v = mesh.vertices[i];
        return Point{{v.x, v.y, v.z},
                     {float(v.color & 255) / 255, float((v.color >> 8) & 255) / 255,
                      float((v.color >> 16) & 255) / 255, float(v.color >> 24) / 255}};
    };
    auto clip = [](const std::vector<Point>& polygon, int axis, double boundary, double sign, bool inside) {
        std::vector<Point> out;
        if (polygon.empty())
            return out;
        auto distance = [&](const Point& p) { return sign * (p.p[axis] - boundary); };
        auto last = polygon.back();
        double a = distance(last);
        for (const auto& next : polygon) {
            double b = distance(next);
            bool keepA = inside ? a >= 0 : a <= 0, keepB = inside ? b >= 0 : b <= 0;
            if (keepA != keepB) {
                double t = a / (a - b);
                out.push_back({glm::mix(last.p, next.p, t), glm::mix(last.color, next.color, float(t))});
            }
            if (keepB)
                out.push_back(next);
            last = next;
            a = b;
        }
        return out;
    };
    auto append = [&](const std::vector<Point>& polygon) {
        if (polygon.size() < 3)
            return;
        uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (auto& p : polygon)
            mesh.vertices.push_back(
                {float(p.p.x), float(p.p.y), float(p.p.z), rgba(glm::vec3(p.color), p.color.a)});
        for (uint32_t i = 1; i + 1 < polygon.size(); ++i)
            mesh.indices.insert(mesh.indices.end(), {base, base + i, base + i + 1});
    };
    for (size_t i = 0; i < original.size(); i += 3) {
        if ((i & 255) == 0 && cancel && cancel->load())
            return;
        std::vector<Point> polygon{read(original[i]), read(original[i + 1]), read(original[i + 2])};
        bool far = false;
        for (int axis : {0, 2})
            for (double sign : {-1.0, 1.0}) {
                double edge = axis == 0 ? half.x : half.y;
                far = far || std::all_of(polygon.begin(), polygon.end(),
                                         [&](auto& p) { return sign * p.p[axis] > edge; });
            }
        if (far) {
            mesh.indices.insert(mesh.indices.end(), {original[i], original[i + 1], original[i + 2]});
            continue;
        }
        for (int side = 0; side < 4 && !polygon.empty(); ++side) {
            int axis = side < 2 ? 0 : 2;
            double edge = axis == 0 ? half.x : half.y;
            double sign = (side % 2) == 0 ? 1 : -1, boundary = -sign * edge;
            append(clip(polygon, axis, boundary, sign, false));
            polygon = clip(polygon, axis, boundary, sign, true);
        }
    }
}
static void microTint(Mesh& mesh, const Node& n, const std::function<glm::dvec3(glm::dvec3)>& localPoint) {
    if (mesh.vertices.empty())
        return;
    // SIMD noise is batched and strict FP; only the micro-inclusion tint
    // uses it. Identity and all authoritative density fields use scalar hashes.
    auto noise = FastNoise::New<FastNoise::Simplex>();
    size_t count = mesh.vertices.size();
    std::vector<float> x(count), y(count), z(count), out(count);
    for (size_t i = 0; i < count; ++i) {
        auto v = mesh.vertices[i];
        auto p = localPoint({v.x, v.y, v.z});
        x[i] = static_cast<float>(p.x * 13);
        y[i] = static_cast<float>(p.y * 13);
        z[i] = static_cast<float>(p.z * 13);
    }
    noise->GenPositionArray3D(out.data(), static_cast<int>(count), x.data(), y.data(), z.data(), 0, 0, 0,
                              static_cast<int>(n.seed & 0x7fffffff));
    for (size_t i = 0; i < count; ++i) {
        auto& v = mesh.vertices[i];
        // Growth zoning follows the habit's c axis. Foliation appears only in
        // strained hosts; graphite/ice retain their basal layering.
        float layer;
        if (n.environment.mineral == Mineral::Graphite || n.environment.mineral == Mineral::Ice)
            layer = .78f + .22f * std::cos(z[i] * 3);
        else if (n.rock.type == RockType::Metamorphic)
            layer = .75f + .25f * std::sin(y[i] * 3 + out[i] * .5f);
        else
            layer = .87f + .13f * std::sin(std::sqrt(x[i] * x[i] + y[i] * y[i]) * 2 + out[i] * .3f);
        uint32_t c = v.color;
        glm::vec3 col((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f);
        v.color = rgba(col * layer);
    }
}
std::shared_ptr<Geometry> GeometryGenerator::generate(const Node& n, unsigned lod,
                                                      const std::atomic<bool>* cancel,
                                                      unsigned viewKey) const {
    auto begin = std::chrono::steady_clock::now();
    auto g = std::make_shared<Geometry>();
    Random r{n.seed};
    if (cancel && cancel->load())
        return {};
    auto kind = n.address.kind();
    if (viewKey > 6 || (kind != Kind::Planet && viewKey))
        throw std::invalid_argument("Invalid terrain view key");
    g->viewKey = viewKey;
    lod = geometryDetail(kind, lod);
    g->address = n.address;
    g->referenceFrame = n.address;
    g->logUnits = n.frame.logRadius;
    g->seed = n.seed;
    g->descriptor = descriptorFingerprint(n);
    g->detail = lod;
    if (kind == Kind::Cosmic) {
        for (uint64_t i = 0; i < 18000 * (1 + lod); ++i) {
            if (cancel && cancel->load())
                return {};
            glm::dvec3 p(r.signedUnit(i * 4) * .96, r.signedUnit(i * 4 + 1) * .96,
                         r.signedUnit(i * 4 + 2) * .96);
            double density = universe_.cosmicDensity(p * 4.0, n.seed);
            if (r.unit(i * 4 + 3) < density * .55)
                g->particles.push_back({p, glm::vec3(.2, .45, .7) * static_cast<float>(density), .003f});
        }
    } else if (kind == Kind::Galaxy) {
        size_t count = 5000 + lod * 7000;
        g->particles.reserve(count + 1500);
        for (size_t i = 0; i < count; ++i) {
            if (cancel && cancel->load())
                return {};
            auto star = universe_.starSample(n, i);
            float intensity = static_cast<float>(.35 + .65 * std::min(star.mass / 3, 1.0));
            g->particles.push_back({star.position, star.color * intensity,
                                    static_cast<float>(.0006 + .0011 * std::min(star.mass, 3.0)), i});
        }
        for (size_t i = 0; i < static_cast<size_t>(3000 * n.galaxy.gasFraction); ++i) {
            g->particles.push_back({universe_.gasSample(n, i), {.11f, .16f, .22f}, .003f});
        }
    } else if (kind == Kind::System) {
        double radius = n.stellarRadiusMetres / std::pow(10.0, n.frame.logRadius);
        auto starColor = stellarColor(n.temperatureK);
        appendSphere(g->solid, {}, radius, starColor, 24, 32, true);
        g->particles.push_back({{}, starColor, static_cast<float>(radius * 2.5)});
        if (n.companions)
            appendSphere(g->solid, {radius * 9, 0, 0}, radius * .6, starColor * .8f, 10, 16, true);
        for (auto a : universe_.children(n)) {
            auto p = universe_.describe(a);
            for (int i = 0; i < 160; ++i) {
                auto orbit = p.orbit;
                orbit.phase = static_cast<double>(i) * 2 * pi / 160;
                auto b = orbit;
                b.phase = static_cast<double>(i + 1) * 2 * pi / 160;
                addLine(g->lines, keplerPosition(orbit, 0) / std::pow(10.0, n.frame.logRadius),
                        keplerPosition(b, 0) / std::pow(10.0, n.frame.logRadius), {.11f, .19f, .24f});
            }
        }
        for (int i = 0; i < 2800; ++i) {
            double a = r.unit(100 + i * 2) * 2 * pi, rad = .16 + .035 * r.signedUnit(101 + i * 2);
            g->particles.push_back(
                {glm::dvec3(std::cos(a) * rad, r.signedUnit(i + 5000) * .002, std::sin(a) * rad),
                 {.3f, .25f, .19f},
                 .00014f});
        }
    } else if (kind == Kind::Planet) {
        glm::dvec3 direction = viewKey ? tileDirection({static_cast<int>(viewKey - 1), 0, 0, 0})
                                       : glm::normalize(glm::dvec3(.3, .2, 1));
        auto leaves = terrainLeaves(direction, lod);
        for (auto leaf : leaves) {
            if (cancel && cancel->load())
                return {};
            terrainGrid(
                g->solid, universe_, n, 8, [&](double u, double v) { return tileDirection(leaf.tile, u, v); },
                true, cancel);
        }
    } else if (kind == Kind::Surface || kind == Kind::Mountain) {
        terrainGrid(
            g->solid, universe_, n, lod == 4 ? 192u : 8u << lod,
            [&](double u, double v) { return universe_.terrainDirection(n, (u - .5) * 2.4, (v - .5) * 2.4); },
            false, cancel);
    } else if (kind == Kind::Rock || kind == Kind::Grain) {
        std::vector<Node> grains;
        if (kind == Kind::Rock)
            for (auto address : universe_.children(n))
                grains.push_back(universe_.describe(address));
        auto albedo = [&](glm::dvec3 p) {
            auto color = kind == Kind::Rock ? rockAlbedo(n.rock, n.environment.mineral, p)
                                            : material(n.environment.mineral).color;
            // These are the very same grain descriptors exposed to picking and
            // fine meshing, not an independent noise texture or material roll.
            for (const auto& grain : grains) {
                auto local = glm::inverse(grain.frame.orientation) * (p - grain.frame.position) /
                             grain.grainScaleToRock;
                if (glm::length(local) < 1.3) {
                    double sdf = universe_.grainSdf(grain, local);
                    if (sdf < .1)
                        return material(grain.environment.mineral).color;
                }
            }
            return color;
        };
        sdfMesh(g->solid, densityField(universe_, n, 8 * 2.6 / (8u << lod)), 8u << lod, albedo, cancel);
        if (kind == Kind::Grain)
            microTint(g->solid, n, [](glm::dvec3 p) { return p; });
    } else if (kind == Kind::Lattice) {
        int bound = 4 + static_cast<int>(lod);
        g->lattice = generateLattice(n, {-bound, bound}, cancel);
        g->atomInstances.reserve(g->lattice.atoms.size());
        for (const auto& atom : g->lattice.atoms)
            g->atomInstances.push_back({glm::translate(glm::mat4(1), atom.position) *
                                            glm::scale(glm::mat4(1), glm::vec3(atom.radius)),
                                        glm::vec4(elementColor(atom.element), 1)});
        for (auto bond : g->lattice.bonds)
            addLine(g->lines, g->lattice.atoms[bond.a].position, g->lattice.atoms[bond.b].position,
                    {.17f, .32f, .4f});
    }
    if (cancel && cancel->load())
        return {};
    g->solid.compact();
    g->lines.compact();
    g->generationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return g;
}
std::shared_ptr<Geometry> GeometryGenerator::section(const Node& parent, const Address& anchor, int bucket,
                                                     const std::atomic<bool>* cancel) const {
    if (parent.address.kind() < Kind::Planet || parent.address.kind() > Kind::Grain ||
        !parent.address.ancestorOf(anchor) || parent.address == anchor || bucket < -22 || bucket > 54)
        throw std::invalid_argument("Invalid local material section");
    auto start = std::chrono::steady_clock::now();
    auto g = std::make_shared<Geometry>();
    g->address = parent.address;
    g->seed = parent.seed;
    g->descriptor = descriptorFingerprint(parent);
    g->referenceFrame = anchor;
    g->logUnits = bucket * .5;
    g->section = true;
    g->inspectionRadius = parent.address.kind() <= Kind::Mountain ? 0 : 1.15;
    auto sourceFrames = universe_.frames(anchor), parentFrames = universe_.frames(parent.address);
    double anchorFactor = std::pow(10.0, g->logUnits - sourceFrames.back().logRadius);
    double parentFactor = std::pow(10.0, parent.frame.logRadius - g->logUnits);
    auto parentTransform =
        pointTransform(sourceFrames, anchor, parentFrames, parent.address, parent.frame.logRadius);
    auto parentPoint = [&](glm::dvec3 p) { return parentTransform(p * anchorFactor); };
    std::optional<Node> planet;
    std::vector<LocalFrame> planetFrames;
    if (parent.address.kind() <= Kind::Mountain) {
        auto address = parent.address;
        address.path.resize(static_cast<size_t>(Kind::Planet));
        planet = universe_.describe(address);
        planetFrames = universe_.frames(address);
    }
    PointTransform planetTransform;
    if (planet)
        planetTransform =
            pointTransform(sourceFrames, anchor, planetFrames, planet->address, planet->frame.logRadius);
    auto planetPoint = [&](glm::dvec3 p) { return planetTransform(p * anchorFactor); };
    auto materialField = planet ? std::function<double(glm::dvec3)>{} : densityField(universe_, parent);
    auto density = [&](glm::dvec3 p) {
        double solid;
        if (planet) {
            auto q = planetPoint(p);
            auto d = glm::normalize(q);
            solid = (glm::length(q) - 1 -
                     universe_.terrainHeight(*planet, d) / std::pow(10.0, planet->frame.logRadius)) *
                    std::pow(10.0, planet->frame.logRadius - g->logUnits);
        } else {
            auto q = parentPoint(p);
            solid = materialField(q) * parentFactor;
        }
        // An explicit inspection cavity changes visibility only. The surrounding
        // section is sampled from the actual enclosing object's density/material.
        return g->inspectionRadius > 0 ? std::max(solid, g->inspectionRadius - glm::length(p)) : solid;
    };
    auto color = [&](glm::dvec3 p) {
        if (planet)
            return universe_.terrainAlbedo(*planet, glm::normalize(planetPoint(p)));
        return parent.address.kind() == Kind::Rock
                   ? rockAlbedo(parent.rock, parent.environment.mineral, parentPoint(p))
                   : material(parent.environment.mineral).color;
    };
    auto focus = universe_.describe(anchor);
    auto lightFrame =
        planet ? focus.terrainOrientation
               : relativePose(sourceFrames, anchor, parentFrames, parent.address, parent.frame.logRadius)
                     .orientation;
    sdfMesh(g->solid, density, planet ? 24 : 20, color, cancel, planet ? 4.0 : 1.3, lightFrame);
    if (planet && (anchor.kind() == Kind::Surface || anchor.kind() == Kind::Mountain)) {
        glm::dvec2 half(100);
        for (double x : {-1.2, 1.2})
            for (double z : {-1.2, 1.2}) {
                auto corner = universe_.terrainPoint(focus, x, z);
                half = glm::min(half, glm::dvec2(std::abs(corner.x), std::abs(corner.z)));
            }
        g->coveredHalfExtent = half * (.98 / anchorFactor);
        excludePatch(g->solid, g->coveredHalfExtent, cancel);
    }
    if (cancel && cancel->load())
        return {};
    if (parent.address.kind() == Kind::Grain)
        microTint(g->solid, parent, parentPoint);
    g->solid.compact();
    g->lines.compact();
    g->generationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return g;
}
uint64_t geometryFingerprint(const Geometry& g) {
    uint64_t hash = mix64(g.seed ^ g.descriptor ^ g.recipe);
    auto add = [&](uint64_t v) { hash = mix64(hash ^ v); };
    if (g.terrainTile) {
        add(encodeTile(*g.terrainTile));
        add(g.terrainStitch);
        for (auto p : g.precisePositions)
            for (unsigned axis = 0; axis < 3; ++axis)
                add(std::bit_cast<uint64_t>(p[axis]));
    }
    add(std::bit_cast<uint64_t>(g.logUnits));
    add(g.section);
    add(g.viewKey);
    add(std::bit_cast<uint64_t>(g.coveredHalfExtent.x));
    add(std::bit_cast<uint64_t>(g.coveredHalfExtent.y));
    add(std::bit_cast<uint64_t>(g.inspectionRadius));
    for (auto c : g.referenceFrame.cell)
        add(static_cast<uint64_t>(c));
    for (auto segment : g.referenceFrame.path) {
        add(static_cast<uint64_t>(segment.kind));
        add(segment.id);
    }
    for (auto m : {&g.solid, &g.lines}) {
        for (auto v : m->vertices) {
            add(std::bit_cast<uint32_t>(v.x));
            add(std::bit_cast<uint32_t>(v.y));
            add(std::bit_cast<uint32_t>(v.z));
            add(v.color);
        }
        add(m->wideIndices());
        for (size_t i = 0; i < m->indexCount(); ++i)
            add(m->index(i));
    }
    for (auto p : g.particles) {
        for (int i = 0; i < 3; ++i) {
            add(std::bit_cast<uint64_t>(p.position[i]));
            add(std::bit_cast<uint32_t>(p.color[i]));
        }
        add(std::bit_cast<uint32_t>(p.radius));
        add(p.objectId);
    }
    for (auto a : g.lattice.atoms) {
        for (int i = 0; i < 3; ++i)
            add(std::bit_cast<uint32_t>(a.position[i]));
        add(a.element);
        add(a.siteId);
        add(std::bit_cast<uint32_t>(a.radius));
    }
    for (const auto& instance : g.atomInstances)
        for (unsigned column = 0; column < 5; ++column)
            for (unsigned row = 0; row < 4; ++row)
                add(std::bit_cast<uint32_t>(column < 4 ? instance.transform[column][row]
                                                       : instance.color[row]));
    for (auto id : g.lattice.vacantSites)
        add(id);
    for (auto b : g.lattice.bonds) {
        add(b.a);
        add(b.b);
    }
    return hash;
}
unsigned geometryDetail(Kind kind, unsigned requested) {
    requested = std::min(requested, 4u);
    switch (kind) {
    case Kind::Cosmic:
    case Kind::Lattice:
        return std::min(requested, 2u);
    case Kind::System:
        return 0;
    case Kind::Planet:
        return requested;
    case Kind::Rock:
    case Kind::Grain:
        return std::min(requested, 3u);
    default:
        return requested;
    }
}
unsigned screenDetail(Kind kind, double pixels, unsigned previous) {
    // Maximum projected grid-cell size, with hysteresis around each subdivision.
    double cellPixels = (kind == Kind::Rock || kind == Kind::Grain) ? 10.0 : 4.0;
    previous = geometryDetail(kind, previous);
    auto maximum = geometryDetail(kind, 4);
    while (previous < maximum && pixels > 8 * cellPixels * std::pow(2.0, previous) * 1.15)
        ++previous;
    while (previous > 0 && pixels < 8 * cellPixels * std::pow(2.0, previous - 1) * .85)
        --previous;
    return previous;
}
unsigned terrainView(glm::dvec3 eye) {
    auto a = glm::abs(eye);
    int axis = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
    return 1 + static_cast<unsigned>(axis * 2 + (eye[axis] < 0));
}
int sectionBucket(double renderLog) {
    if (!std::isfinite(renderLog) || renderLog < -11 || renderLog > 27)
        throw std::invalid_argument("Invalid section view scale");
    // The entire camera must stay within the inspection cavity. Rounding down
    // would shrink the cavity below the eye when close to a cache boundary.
    return static_cast<int>(std::ceil(renderLog * 2));
}
void GeometryBlend::update(std::shared_ptr<const Geometry> candidate, double seconds) {
    if (candidate && current &&
        (candidate->address != current->address || candidate->seed != current->seed ||
         candidate->descriptor != current->descriptor || candidate->recipe != current->recipe))
        throw std::invalid_argument("Cannot blend different procedural identities");
    if (candidate && candidate != current) {
        if (candidate == previous) {
            std::swap(current, previous);
            blend_ = 1 - blend_;
        } else if (!previous) {
            previous = current;
            current = std::move(candidate);
            blend_ = previous ? 0.f : 1.f;
        }
    }
    blend_ = std::min(1.f, blend_ + static_cast<float>(std::clamp(seconds, 0.0, .1) / .3));
    if (blend_ >= 1)
        previous.reset();
}
} // namespace infinite
