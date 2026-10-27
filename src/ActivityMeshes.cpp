#include "infinite/Activity.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinite {
namespace {
const glm::vec3 hull{.31, .39, .45}, trim{.48, .62, .68}, glass{.13, .37, .53}, glow{.45, .91, .88};
void tri(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color) {
    const auto n = glm::normalize(glm::cross(b - a, c - a));
    const float light = float(.54 + .46 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.4, .8, -.5)))));
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color * light)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void box(Mesh& m, glm::dvec3 c, glm::dvec3 dimensions, glm::vec3 color) {
    const auto h = dimensions * .5;
    const std::array<glm::dvec3, 8> p{c + glm::dvec3(-h.x, -h.y, -h.z), c + glm::dvec3(h.x, -h.y, -h.z),
                                      c + glm::dvec3(h.x, h.y, -h.z),   c + glm::dvec3(-h.x, h.y, -h.z),
                                      c + glm::dvec3(-h.x, -h.y, h.z),  c + glm::dvec3(h.x, -h.y, h.z),
                                      c + glm::dvec3(h.x, h.y, h.z),    c + glm::dvec3(-h.x, h.y, h.z)};
    for (auto face : {std::array{0, 3, 2, 1}, std::array{4, 5, 6, 7}, std::array{0, 4, 7, 3},
                      std::array{1, 2, 6, 5}, std::array{3, 7, 6, 2}, std::array{0, 1, 5, 4}}) {
        tri(m, p[face[0]], p[face[1]], p[face[2]], color);
        tri(m, p[face[0]], p[face[2]], p[face[3]], color);
    }
}
void ring(Mesh& m, double radius, double thickness, double y, glm::vec3 color, unsigned sectors) {
    for (unsigned i = 0; i < sectors; i++) {
        const double a = i * 2 * std::numbers::pi / sectors, b = (i + 1) * 2 * std::numbers::pi / sectors;
        const auto p = [&](double r, double t, double h) {
            return glm::dvec3(std::cos(t) * r, h, std::sin(t) * r);
        };
        for (int side : {-1, 1}) {
            const double h = y + side * thickness * .5;
            tri(m, p(radius - thickness, a, h), p(radius + thickness, a, h), p(radius + thickness, b, h),
                color);
            tri(m, p(radius - thickness, a, h), p(radius + thickness, b, h), p(radius - thickness, b, h),
                color);
        }
        tri(m, p(radius + thickness, a, y - thickness * .5), p(radius + thickness, b, y - thickness * .5),
            p(radius + thickness, b, y + thickness * .5), color * .85f);
        tri(m, p(radius + thickness, a, y - thickness * .5), p(radius + thickness, b, y + thickness * .5),
            p(radius + thickness, a, y + thickness * .5), color * .85f);
    }
}
} // namespace
Mesh activityMesh(ActivityKind kind, unsigned detail) {
    detail = std::min(detail, 2u);
    Mesh m;
    // Source silhouettes: Hyperion flight/settlements.cpp::buildCraft and orbitalCore.
    if (kind == ActivityKind::Harbour) {
        box(m, {}, {1.55, .28, .36}, hull);
        box(m, {0, .12, 0}, {.36, .7, .4}, trim);
        ring(m, .82, .05, 0, hull, detail ? 48 : 20);
        if (detail) {
            ring(m, .82, .012, .06, glow, 48);
            for (int side : {-1, 1}) {
                box(m, {side * .59, .16, 0}, {.23, .37, .52}, hull);
                box(m, {side * .52, -.05, .58}, {.53, .025, .32}, glass);
                box(m, {side * .52, -.05, -.58}, {.53, .025, .32}, glass);
                box(m, {side * .82, .035, 0}, {.04, .08, .20}, glow);
            }
        }
        if (detail == 2) {
            for (int i = 0; i < 8; i++) {
                const double a = i * 2 * std::numbers::pi / 8;
                box(m, {std::cos(a) * .82, .12, std::sin(a) * .82}, {.13, .12, .13}, trim);
                box(m, {std::cos(a) * .82, .19, std::sin(a) * .82}, {.05, .02, .05}, glow);
            }
            for (int side : {-1, 1}) {
                box(m, {side * .45, .43, 0}, {.035, .42, .035}, trim);
                box(m, {side * .45, .64, .18}, {.035, .035, .38}, trim);
            }
        }
    } else if (kind == ActivityKind::Freighter) {
        box(m, {}, {1.4, .6, 2.2}, hull);
        for (int side : {-1, 1}) {
            box(m, {side * .9, 0, .1}, {.35, .4, 2}, hull);
            if (detail) {
                box(m, {side * .72, .33, .15}, {.12, .08, 1.8}, glow);
                for (int i = 0; i < (detail == 2 ? 4 : 2); i++)
                    box(m, {side * .91, -.45, .85 - i * .55}, {.5, .5, .48}, {.65f, .43f, .26f});
            }
        }
        if (detail)
            box(m, {0, .36, -.45}, {.55, .17, .5}, glass);
    } else if (kind == ActivityKind::Ferry) {
        for (int side : {-1, 1}) {
            tri(m, {0, .16, -1.7}, {side * 1.2, 0, .7}, {0, -.16, .9}, hull);
            tri(m, {0, .16, -1.7}, {0, .28, .8}, {side * 1.2, 0, .7}, hull);
            tri(m, {0, -.16, .9}, {side * 1.2, 0, .7}, {0, .28, .8}, hull * .65f);
        }
        if (detail)
            box(m, {0, .28, -.15}, {.3, .16, .8}, glass);
    } else if (kind == ActivityKind::Patrol) {
        for (int side : {-1, 1}) {
            box(m, {side * .65, 0, 0}, {.35, .42, 2.6}, hull);
            if (detail)
                box(m, {side * .65, .24, -.6}, {.15, .05, 1.2}, glow);
        }
        box(m, {}, {1.2, .25, .9}, hull);
        if (detail)
            box(m, {0, .22, -.2}, {.48, .22, .6}, glass);
    } else if (kind == ActivityKind::Survey) {
        appendSphere(m, {}, 1, hull, detail ? 8 : 4, detail ? 14 : 8);
        for (auto& v : m.vertices) {
            v.y *= .29f;
            v.z *= 1.4f;
        }
        if (detail) {
            ring(m, 1.05, .025, 0, glow, 32);
            box(m, {0, .36, -.3}, {.48, .16, .75}, glass);
        }
    } else {
        appendSphere(m, {}, 1, {1, 1, 1}, detail ? 6 : 3, detail ? 10 : 6);
        for (auto& v : m.vertices) {
            const float f = .85f + .14f * std::sin(v.x * 9 + v.z * 5);
            v.x *= f;
            v.y *= .66f;
        }
    }
    if (kind != ActivityKind::Harbour && kind != ActivityKind::Debris && detail)
        for (int side : {-1, 1})
            box(m, {side * .44, 0, 1.16}, {.25, .22, .14}, glow);
    m.compact();
    return m;
}
} // namespace infinite
