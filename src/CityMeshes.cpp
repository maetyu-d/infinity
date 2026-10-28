#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinite {
namespace {
const glm::vec3 wall{.83, .87, .9}, glass{.19, .57, .70}, stone{.60, .66, .72}, accent{.46, 1.05, .88};
void tri(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color) {
    const auto n = glm::cross(b - a, c - a);
    if (glm::length(n) < 1e-12)
        return;
    const float shade =
        float(.55 + .45 * std::max(0., glm::dot(glm::normalize(n), glm::normalize(glm::dvec3(.4, 1, .3)))));
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color * shade)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void box(Mesh& m, glm::dvec3 c, glm::dvec3 size, glm::vec3 color) {
    const auto h = size * .5;
    const std::array<glm::dvec3, 8> p{c + glm::dvec3(-h.x, -h.y, -h.z), c + glm::dvec3(h.x, -h.y, -h.z),
                                      c + glm::dvec3(h.x, h.y, -h.z),   c + glm::dvec3(-h.x, h.y, -h.z),
                                      c + glm::dvec3(-h.x, -h.y, h.z),  c + glm::dvec3(h.x, -h.y, h.z),
                                      c + glm::dvec3(h.x, h.y, h.z),    c + glm::dvec3(-h.x, h.y, h.z)};
    for (auto f : {std::array{0, 3, 2, 1}, std::array{4, 5, 6, 7}, std::array{0, 4, 7, 3},
                   std::array{1, 2, 6, 5}, std::array{3, 7, 6, 2}, std::array{0, 1, 5, 4}}) {
        tri(m, p[f[0]], p[f[1]], p[f[2]], color);
        tri(m, p[f[0]], p[f[2]], p[f[3]], color);
    }
}
void facade(Mesh& m, glm::dvec3 c, glm::dvec3 size, unsigned floors, unsigned bays, unsigned detail) {
    if (!detail)
        return;
    for (unsigned floor = 0; floor < floors; floor++)
        for (unsigned bay = 0; bay < bays; bay++) {
            const double y = c.y - size.y * .5 + size.y * (floor + .5) / floors;
            const double x = c.x - size.x * .5 + size.x * (bay + .5) / bays;
            box(m, {x, y, c.z}, {size.x / bays * .63, size.y / floors * .50, .006}, glass);
        }
    if (detail == 2)
        box(m, {c.x, c.y - size.y * .5 - .015, c.z}, {size.x * 1.02, .018, .025}, stone);
}
void dome(Mesh& m, glm::dvec3 centre, double radius, double height, unsigned detail) {
    const unsigned rings = detail ? 5 : 3, sectors = detail ? 16 : 8;
    const auto p = [&](double a, double b) {
        return centre + glm::dvec3(std::cos(a) * std::cos(b) * radius, std::sin(a) * height,
                                   std::cos(a) * std::sin(b) * radius);
    };
    for (unsigned r = 0; r < rings; r++)
        for (unsigned j = 0; j < sectors; j++) {
            const double a = r * std::numbers::pi / (2 * rings), b = (r + 1) * std::numbers::pi / (2 * rings);
            const double x = j * 2 * std::numbers::pi / sectors, z = (j + 1) * 2 * std::numbers::pi / sectors;
            tri(m, p(a, x), p(a, z), p(b, x), wall);
            if (r + 1 < rings)
                tri(m, p(a, z), p(b, z), p(b, x), wall);
        }
}
} // namespace
Mesh cityMesh(SurfaceMesh kind, unsigned detail) {
    detail = std::min(detail, 2u);
    Mesh m;
    if (kind == SurfaceMesh::Courtyard) {
        for (int side : {-1, 1}) {
            box(m, {side * .36, .45, 0}, {.26, .90, .98}, wall);
            box(m, {0, .45, side * .36}, {.48, .90, .26}, wall);
            facade(m, {0, .47, side * .495}, {.90, .74, .01}, detail == 2 ? 7 : 4, detail == 2 ? 7 : 4,
                   detail);
            if (detail)
                box(m, {0, .925, side * .36}, {.98, .05, .27}, stone);
        }
        box(m, {0, .08, -.505}, {.22, .16, .04}, stone);
        if (detail) {
            box(m, {0, .15, -.49}, {.15, .30, .012}, glass);
            for (int side : {-1, 1})
                facade(m, {0, .48, side * .225}, {.45, .7, .01}, 5, 4, detail);
        }
    } else if (kind == SurfaceMesh::SteppedTower) {
        box(m, {0, .08, 0}, {.98, .16, .98}, stone);
        box(m, {0, .44, 0}, {.70, .56, .72}, wall);
        box(m, {0, .84, 0}, {.45, .24, .48}, wall);
        box(m, {0, .98, 0}, {.48, .04, .51}, stone);
        for (int side : {-1, 1}) {
            facade(m, {0, .44, side * .365}, {.65, .53, .01}, detail == 2 ? 14 : 6, 4, detail);
            facade(m, {0, .84, side * .245}, {.41, .20, .01}, detail == 2 ? 6 : 3, 3, detail);
        }
        if (detail) {
            for (int side : {-1, 1})
                box(m, {side * .33, .44, -.37}, {.025, .56, .025}, accent);
            box(m, {0, .07, -.5}, {.20, .13, .02}, glass);
        }
    } else if (kind == SurfaceMesh::RowHousing) {
        for (int unit = -1; unit <= 1; unit++) {
            const double x = unit * .325;
            box(m, {x, .36, 0}, {.316, .72, .94}, wall);
            const glm::dvec3 a{x - .158, .72, -.49}, b{x + .158, .72, -.49}, c{x, 1, -.49};
            tri(m, a, b, c, stone);
            tri(m, {a.x, a.y, .49}, {c.x, c.y, .49}, {b.x, b.y, .49}, stone);
            tri(m, a, c, {c.x, c.y, .49}, stone);
            tri(m, a, {c.x, c.y, .49}, {a.x, a.y, .49}, stone);
            tri(m, c, b, {b.x, b.y, .49}, stone);
            tri(m, c, {b.x, b.y, .49}, {c.x, c.y, .49}, stone);
            facade(m, {x, .43, -.475}, {.28, .50, .01}, detail == 2 ? 4 : 2, 2, detail);
            if (detail)
                box(m, {x, .13, -.478}, {.08, .25, .025}, glass);
        }
    } else if (kind == SurfaceMesh::Hall) {
        box(m, {0, .17, 0}, {.98, .34, .96}, stone);
        const unsigned steps = detail ? 20 : 10;
        const auto p = [](double a, double z) {
            return glm::dvec3(std::cos(a) * .48, .34 + std::sin(a) * .60, z);
        };
        for (unsigned i = 0; i < steps; i++) {
            const double a = i * std::numbers::pi / steps, b = (i + 1) * std::numbers::pi / steps;
            tri(m, p(a, -.48), p(a, .48), p(b, .48), wall);
            tri(m, p(a, -.48), p(b, .48), p(b, -.48), wall);
            tri(m, {0, .34, -.48}, p(b, -.48), p(a, -.48), glass);
            tri(m, {0, .34, .48}, p(a, .48), p(b, .48), wall);
        }
        if (detail) {
            box(m, {0, .18, -.49}, {.45, .33, .018}, glass);
            for (int side : {-1, 1})
                box(m, {side * .45, .30, -.49}, {.035, .60, .025}, accent);
        }
    } else if (kind == SurfaceMesh::Warehouse) {
        box(m, {0, .35, 0}, {.98, .70, .96}, wall);
        for (int roof = 0; roof < 3; roof++) {
            const double x = -.49 + roof * .3266667;
            const glm::dvec3 a{x, .70, -.48}, b{x + .3266667, .70, -.48}, c{x + .23, .98, -.48};
            tri(m, a, b, c, stone);
            tri(m, {a.x, a.y, .48}, {c.x, c.y, .48}, {b.x, b.y, .48}, stone);
            tri(m, a, c, {c.x, c.y, .48}, wall);
            tri(m, a, {c.x, c.y, .48}, {a.x, a.y, .48}, wall);
            tri(m, c, b, {b.x, b.y, .48}, glass);
            tri(m, c, {b.x, b.y, .48}, {c.x, c.y, .48}, glass);
        }
        if (detail)
            for (int door = -1; door <= 1; door++)
                box(m, {door * .29, .23, -.487}, {.22, .44, .022}, stone * .75f);
    } else if (kind == SurfaceMesh::HabitatCluster) {
        box(m, {0, .09, 0}, {.94, .18, .94}, stone);
        dome(m, {-.24, .16, -.20}, .23, .74, detail);
        dome(m, {.23, .16, .19}, .23, .66, detail);
        dome(m, {-.23, .16, .29}, .18, .55, detail);
        box(m, {0, .24, 0}, {.66, .25, .19}, stone);
        box(m, {-.24, .24, 0}, {.20, .25, .65}, stone);
        if (detail) {
            box(m, {-.24, .25, -.445}, {.13, .23, .06}, glass);
            box(m, {.23, .25, -.05}, {.12, .20, .04}, glass);
        }
    } else if (kind == SurfaceMesh::Tree) {
        box(m, {0, .28, 0}, {.10, .56, .10}, {.46, .35, .24});
        const unsigned sectors = detail ? 9 : 6;
        for (unsigned tier = 0; tier < 3; tier++)
            for (unsigned sector = 0; sector < sectors; sector++) {
                const double a = sector * 2 * std::numbers::pi / sectors,
                             b = (sector + 1) * 2 * std::numbers::pi / sectors;
                const double r = .48 - tier * .09, y = .38 + tier * .17;
                tri(m, {std::cos(a) * r, y, std::sin(a) * r}, {std::cos(b) * r, y, std::sin(b) * r},
                    {0, y + .30, 0}, glm::vec3(.50f, .86f, .58f) * (1.f + float(tier) * .08f));
            }
    } else if (kind == SurfaceMesh::Light) {
        box(m, {0, .46, 0}, {.09, .92, .09}, stone);
        box(m, {.13, .93, 0}, {.34, .05, .12}, stone);
        box(m, {.24, .905, 0}, {.18, .035, .15}, accent);
    } else if (kind == SurfaceMesh::Bench) {
        box(m, {0, .45, 0}, {1, .16, .8}, stone);
        box(m, {0, .73, .32}, {1, .40, .12}, wall);
        for (int side : {-1, 1})
            box(m, {side * .35, .22, 0}, {.10, .44, .65}, stone);
    } else if (kind == SurfaceMesh::Solar) {
        for (int side : {-1, 1}) {
            box(m, {side * .25, .42, 0}, {.06, .84, .14}, stone);
            box(m, {side * .25, .83, 0}, {.45, .06, .96}, glass);
            if (detail)
                for (unsigned cell = 0; cell < 4; cell++)
                    box(m, {side * .25, .865, -.36 + cell * .24}, {.43, .008, .008}, accent * .7f);
        }
    } else {
        box(m, {0, 0, 0}, {1, .08, 1},
            kind == SurfaceMesh::Footway ? glm::vec3(.42, .49, .55) : glm::vec3(.52, .59, .65));
        if (detail)
            for (int side : {-1, 1})
                box(m, {side * .48, .045, 0}, {.025, .012, 1}, stone);
    }
    m.compact();
    return m;
}
} // namespace infinite
