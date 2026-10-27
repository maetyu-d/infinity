#include "infinite/Flight.hpp"
#include "infinite/Streaming.hpp"
#include <iostream>
#include <map>
#include <oneapi/tbb/global_control.h>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static bool contains(Tile p, Tile c) {
    return p.face == c.face && p.level <= c.level && (c.x >> (c.level - p.level)) == p.x &&
           (c.y >> (c.level - p.level)) == p.y;
}
static void cutProof(const std::vector<Tile>& tiles) {
    std::array<double, 6> area{};
    for (auto tile : tiles) {
        area[tile.face] += std::ldexp(4., -2 * int(tile.level));
        for (unsigned edge = 0; edge < 4; ++edge) {
            auto n = terrainNeighbor(tile, edge);
            for (auto other : tiles)
                if (contains(other, n))
                    check(other.level + 1 >= tile.level, "terrain frontier has an unbalanced edge");
        }
        for (auto other : tiles)
            if (encodeTile(tile) != encodeTile(other))
                check(!contains(tile, other), "terrain frontier contains overlapping parent/child tiles");
    }
    for (auto a : area)
        check(std::abs(a - 4) < 1e-12, "terrain frontier has a missing face or coverage hole");
}
static void place(Navigation& nav, Address address, glm::dvec3 eye) {
    std::ostringstream s;
    s.precision(17);
    s << address.str() << " 26 1 0 0 0 " << eye.x << ' ' << eye.y << ' ' << eye.z - 1;
    check(nav.restore(s.str()), "root crossing fixture restore");
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        Address root, neighbor{{1, 0, 0}, {}};
        const auto [wideCell, widePoint] =
            canonicalRoot({{100, 100, -100}, {}}, {10000.25, -9000.5, 200.125});
        check(wideCell.cell == std::array<int64_t, 3>{5100, -4400, 0} &&
                  widePoint == glm::dvec3(.25, -.5, .125),
              "root recentering imposed a travel-distance cap or lost local coordinates");
        check(cellDisplacement({INT64_MAX, INT64_MIN, -1}, {INT64_MAX - 1, INT64_MIN + 1, 0}) ==
                  glm::dvec3(2, -2, -2),
              "integer cell subtraction lost precision near signed extremes");
        check(!adjacentCell({INT64_MAX, 0, 0}, {1, 0, 0}) && !adjacentCell({INT64_MIN, 0, 0}, {-1, 0, 0}),
              "root streaming wrapped signed coordinates");
        check(rootNeighborhood(root).size() == 27 && rootNeighborhood(root, {.8, .8, .8}, true).size() == 46,
              "root prefetch neighbourhood is incomplete");
        for (unsigned axis = 0; axis < 3; ++axis)
            for (double sign : {-1., 1.}) {
                auto other = root;
                other.cell[axis] = int64_t(sign);
                for (double t : {-.9, -.3, 0., .4, .9}) {
                    glm::dvec3 p(.17, t, -.27);
                    p[axis] = sign;
                    auto q = p;
                    q[axis] -= 2 * sign;
                    check(std::abs(streamedCosmicDensity(u, root, p) - streamedCosmicDensity(u, other, q)) <
                              1e-13,
                          "cosmic collar density is discontinuous across a cell face");
                    p[axis] -= sign * 1e-7;
                    q[axis] += sign * 1e-7;
                    check(std::abs(streamedCosmicDensity(u, root, p) - streamedCosmicDensity(u, other, q)) <
                              1e-4,
                          "cosmic collar has a density jump near its boundary");
                }
            }
        Navigation nav(u);
        place(nav, root, {.99, .2, -.4});
        const auto star = neighbor.child(Kind::Galaxy, 7).child(Kind::System, 9);
        const auto ray = u.relative(star, nav.anchor, nav.logDistance).position - nav.cameraPosition();
        const auto q = nav.orientation;
        nav.select(star);
        nav.changeFrame(neighbor);
        check(glm::length((u.relative(star, nav.anchor, nav.logDistance).position - nav.cameraPosition()) -
                          ray) < 1e-13,
              "root handoff moved the same object on screen");
        check(nav.orientation == q && glm::length(nav.cameraPosition() - glm::dvec3(-1.01, .2, -.4)) < 1e-13,
              "root handoff teleported or rotated the camera");
        Navigation restored(u);
        check(restored.restore(nav.serialize()) && restored.serialize() == nav.serialize(),
              "restart lost cross-cell breadcrumbs or foreign selection");
        {
            RegionCache cache(u);
            FlightController flight(u, restored, cache);
            place(restored, root, {1.01, -.12, .23});
            const auto before = restored.cameraPosition();
            cache.beginFrame();
            flight.update({}, 1. / 60);
            cache.finishFrame();
            cache.wait();
            check(restored.anchor.cell[0] == 1, "flight did not recenter into the adjoining root cell");
            const auto point = u.pointRelative(
                restored.anchor,
                restored.cameraPosition() *
                    std::pow(10., restored.logDistance - u.describe(restored.anchor).frame.logRadius),
                root, 26);
            check(glm::length(point - before) < 1e-12, "automatic root crossing moved the physical eye");
        }
        auto galaxy = Address{}.child(Kind::Galaxy, 0);
        auto system = u.children(u.describe(galaxy), 1).front();
        const auto planet = u.describe(u.children(u.describe(system), 1).front());
        for (auto direction : {glm::dvec3(1, .1, .3), glm::dvec3(1, 1, .2), glm::dvec3(.1, 1, 1),
                               glm::dvec3(-1, -1, -1), glm::dvec3(.01, 1, .01)})
            for (double altitude : {2., .05, .001, 1e-6, 0.}) {
                auto cut = terrainFrontier(glm::normalize(direction) * (1 + altitude), altitude, 1080);
                check(cut.size() <= 192, "terrain planning exceeded its leaf budget");
                cutProof(cut);
            }
        // Canonical samples are bit-identical at all face seams and LOD edges.
        auto cut = terrainFrontier(glm::normalize(glm::dvec3(1, 1, .2)) * 1.0001, .0001, 1080);
        std::map<uint64_t, glm::dvec3> positions;
        std::map<uint64_t, uint32_t> colors;
        std::map<uint64_t, std::set<uint64_t>> used;
        std::vector<TerrainPatch> patches;
        for (auto tile : cut) {
            const auto stitch = terrainStitches(tile, cut);
            auto g = terrainGeometry(u, planet, tile, stitch);
            check(g && g->solid.vertices.size() == 81 && g->precisePositions.size() == 81,
                  "streamed terrain did not retain precise source vertices");
            for (unsigned i = 0; i < 81; ++i) {
                const auto key = terrainVertexKey(tile, i % 9, i / 9);
                auto [it, inserted] = positions.emplace(key, g->precisePositions[i]);
                check(inserted || it->second == g->precisePositions[i],
                      "shared terrain vertex positions differ");
                auto [c, fresh] = colors.emplace(key, g->solid.vertices[i].color);
                check(fresh || c->second == g->solid.vertices[i].color,
                      "shared terrain vertex shading differs");
            }
            for (unsigned i = 0; i < g->solid.indexCount(); ++i) {
                const auto vertex = g->solid.index(i);
                check(vertex < 81, "terrain stitch index exceeds its buffer");
                used[encodeTile(tile)].insert(terrainVertexKey(tile, vertex % 9, vertex / 9));
            }
            patches.push_back({tile, stitch, g});
        }
        for (const auto& p : patches)
            for (unsigned edge = 0; edge < 4; ++edge) {
                const auto n = terrainNeighbor(p.tile, edge);
                for (const auto& other : patches)
                    if (contains(other.tile, n))
                        for (unsigned i = 0; i <= 8; ++i) {
                            unsigned x = edge < 2 ? (edge ? 8 : 0) : i;
                            unsigned y = edge < 2 ? i : (edge == 3 ? 8 : 0);
                            const auto key = terrainVertexKey(p.tile, x, y);
                            if (used[encodeTile(p.tile)].contains(key))
                                check(used[encodeTile(other.tile)].contains(key),
                                      "stitched edge has a T junction or unmatched shared vertex");
                        }
            }
        RegionCache cache(u);
        TerrainStreamer terrain(u);
        for (int frame = 0; frame < 90; ++frame) {
            const double s = frame < 35 ? .98 : frame < 65 ? 1.02 : .98;
            const auto direction = glm::normalize(glm::dvec3(s, 1, .2));
            const auto eye = direction * (1.00001 + u.terrainHeight(planet, direction) /
                                                        std::pow(10., planet.frame.logRadius));
            cache.beginFrame();
            terrain.update(planet, eye, 1080, 1. / 30, cache);
            cache.finishFrame();
            cache.wait();
            if (!terrain.ready())
                continue;
            std::vector<Tile> active;
            std::map<uint64_t, glm::dvec3> shared;
            for (const auto& patch : terrain.patches()) {
                active.push_back(patch.tile);
                for (unsigned i = 0; i < 81; ++i) {
                    const auto key = terrainVertexKey(patch.tile, i % 9, i / 9);
                    const auto p = terrain.position(patch, i);
                    auto [it, inserted] = shared.emplace(key, p);
                    check(inserted || p == it->second, "terrain geomorph opened a seam");
                }
            }
            cutProof(active);
        }
        check(terrain.ready(), "asynchronous terrain never supplied complete coverage");
        check(cache.stats().peakPending <= 3 && cache.stats().bytes <= 32 * 1024 * 1024,
              "streaming exceeded worker/cache bounds");
        std::cout
            << "PASS: " << checks
            << " streaming checks; cross-cell coordinates/persistence, "
               "continuous cosmic collars, full balanced terrain coverage, stitched seams and geomorphs.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " streaming checks: " << e.what() << '\n';
        return 1;
    }
}
