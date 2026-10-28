#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinite {
Mesh cityMesh(SurfaceMesh, unsigned);
namespace {
void triangle(Mesh& mesh, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color) {
    const auto n = glm::normalize(glm::cross(b - a, c - a));
    const float shade = float(.55 + .45 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.4, 1, .3)))));
    const auto first = uint32_t(mesh.vertices.size());
    for (auto p : {a, b, c})
        mesh.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color * shade)});
    mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2});
}
void box(Mesh& m, glm::dvec3 c, glm::dvec3 size, glm::vec3 color) {
    const auto h = size * .5;
    const std::array<glm::dvec3, 8> p{c + glm::dvec3(-h.x, -h.y, -h.z), c + glm::dvec3(h.x, -h.y, -h.z),
                                      c + glm::dvec3(h.x, h.y, -h.z),   c + glm::dvec3(-h.x, h.y, -h.z),
                                      c + glm::dvec3(-h.x, -h.y, h.z),  c + glm::dvec3(h.x, -h.y, h.z),
                                      c + glm::dvec3(h.x, h.y, h.z),    c + glm::dvec3(-h.x, h.y, h.z)};
    for (auto face : {std::array{0, 3, 2, 1}, std::array{4, 5, 6, 7}, std::array{0, 4, 7, 3},
                      std::array{1, 2, 6, 5}, std::array{3, 7, 6, 2}, std::array{0, 1, 5, 4}}) {
        triangle(m, p[face[0]], p[face[1]], p[face[2]], color);
        triangle(m, p[face[0]], p[face[2]], p[face[3]], color);
    }
}
} // namespace
Mesh surfaceMesh(SurfaceMesh kind, unsigned detail) {
    if (kind > SurfaceMesh::Suit)
        return cityMesh(kind, detail);
    detail = std::min(detail, 2u);
    Mesh m;
    const glm::vec3 hull{.85, .88, .9}, glass{.2, .72, .82}, trim{.42, .5, .6}, lamp{.65, 1.1, .82};
    if (kind == SurfaceMesh::Resident || kind == SurfaceMesh::Suit) {
        box(m, {0, 1.05, 0}, {.38, .62, .25}, kind == SurfaceMesh::Suit ? hull : glm::vec3(.35, .65, .8));
        appendSphere(m, {0, 1.58, 0}, .16, kind == SurfaceMesh::Suit ? glass : glm::vec3(.75, .52, .38),
                     detail ? 6 : 3, detail ? 10 : 5);
        for (int side : {-1, 1}) {
            box(m, {side * .11, .38, side * .06}, {.13, .72, .16}, trim);
            box(m, {side * .27, 1.03, -side * .07}, {.12, .6, .14}, hull);
            if (detail)
                box(m, {side * .11, .06, -.06}, {.18, .12, .3}, trim);
        }
        if (kind == SurfaceMesh::Suit && detail)
            box(m, {0, 1.13, .23}, {.3, .43, .2}, trim);
    } else if (kind == SurfaceMesh::Foundation) {
        box(m, {0, -.5, 0}, {1, 1, 1}, trim);
    } else if (kind == SurfaceMesh::Road) {
        box(m, {0, 0, 0}, {1, .08, 1}, {.16, .22, .25});
        if (detail)
            box(m, {0, .045, 0}, {.025, .015, .8}, {.7, .62, .38});
    } else if (kind == SurfaceMesh::Pad) {
        box(m, {0, 0, 0}, {1, .12, 1}, {.20, .27, .30});
        for (int side : {-1, 1}) {
            box(m, {side * .42, .07, 0}, {.018, .012, .84}, lamp);
            box(m, {0, .07, side * .42}, {.84, .012, .018}, lamp);
        }
        box(m, {0, .075, 0}, {.04, .012, .48}, lamp);
        for (int side : {-1, 1})
            box(m, {side * .18, .075, 0}, {.04, .012, .48}, lamp);
    } else if (kind == SurfaceMesh::Dome || kind == SurfaceMesh::Greenhouse) {
        const unsigned rings = detail ? 8 : 4, sectors = detail ? 20 : 10;
        const auto point = [](double latitude, double longitude) {
            return glm::dvec3(.5 * std::cos(latitude) * std::cos(longitude), .9 * std::sin(latitude),
                              .5 * std::cos(latitude) * std::sin(longitude));
        };
        for (unsigned ring = 0; ring < rings; ring++)
            for (unsigned sector = 0; sector < sectors; sector++) {
                const double a = ring * std::numbers::pi / (rings * 2),
                             b = (ring + 1) * std::numbers::pi / (rings * 2);
                const double x = sector * 2 * std::numbers::pi / sectors,
                             z = (sector + 1) * 2 * std::numbers::pi / sectors;
                const auto color = kind == SurfaceMesh::Greenhouse ? glass : hull;
                triangle(m, point(a, x), point(a, z), point(b, x), color);
                if (ring + 1 < rings)
                    triangle(m, point(a, z), point(b, z), point(b, x), color);
            }
        box(m, {0, .06, 0}, {1, .12, 1}, trim);
        if (detail) {
            box(m, {0, .30, -.47}, {.20, .48, .16}, glass);
            box(m, {0, .15, -.57}, {.30, .3, .18}, trim);
        }
    } else {
        box(m, {0, .48, 0}, {.9, .96, .9}, hull);
        if (kind == SurfaceMesh::Tower) {
            box(m, {0, .97, 0}, {.72, .06, .72}, trim);
            if (detail)
                box(m, {0, 1.04, 0}, {.07, .1, .07}, lamp);
        } else if (kind == SurfaceMesh::Industry) {
            box(m, {.22, 1.05, .2}, {.18, .35, .18}, trim);
            box(m, {-.22, 1.0, .2}, {.22, .25, .22}, hull);
        } else if (kind == SurfaceMesh::Utility) {
            box(m, {0, 1.13, 0}, {.035, .36, .035}, trim);
            if (detail)
                box(m, {0, 1.30, 0}, {.6, .035, .3}, glass);
        }
        if (detail) {
            const unsigned floors = kind == SurfaceMesh::Tower ? (detail == 2 ? 12 : 5) : 3;
            for (unsigned floor = 0; floor < floors; floor++)
                for (int side : {-1, 1}) {
                    const double y = .18 + floor * (.7 / floors);
                    box(m, {side * .457, y, 0}, {.014, .027, .65}, glass);
                    box(m, {0, y, side * .457}, {.65, .027, .014}, glass);
                }
            box(m, {0, .16, -.47}, {.17, .32, .035}, glass);
        }
    }
    m.compact();
    return m;
}
} // namespace infinite
