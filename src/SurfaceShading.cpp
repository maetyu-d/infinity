#include "infinite/SurfaceShading.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace infinite {
namespace {
float signNZ(float x) {
    return x < 0 ? -1.f : 1.f;
}
glm::vec3 unit(glm::vec3 n) {
    const float square = glm::dot(n, n);
    return square > 1e-30f ? n / std::sqrt(square) : glm::vec3(0, 0, 1);
}
struct Position {
    int32_t x, y, z;
    bool operator==(const Position&) const = default;
};
struct PositionHash {
    size_t operator()(Position p) const {
        return size_t(uint32_t(p.x)) * 73856093u ^ size_t(uint32_t(p.y)) * 19349663u ^
               size_t(uint32_t(p.z)) * 83492791u;
    }
};
} // namespace
ShadingNormal packNormal(glm::vec3 n) {
    n = unit(n);
    n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    glm::vec2 p(n);
    if (n.z < 0)
        p = {(1 - std::abs(n.y)) * signNZ(n.x), (1 - std::abs(n.x)) * signNZ(n.y)};
    return {int16_t(std::round(p.x * 32767)), int16_t(std::round(p.y * 32767))};
}
glm::vec3 unpackNormal(ShadingNormal packed) {
    glm::vec2 p(float(packed.x) / 32767, float(packed.y) / 32767);
    glm::vec3 n(p, 1 - std::abs(p.x) - std::abs(p.y));
    if (n.z < 0) {
        const auto xy = glm::vec2(n);
        n.x = (1 - std::abs(xy.y)) * signNZ(xy.x);
        n.y = (1 - std::abs(xy.x)) * signNZ(xy.y);
    }
    return unit(n);
}
std::vector<ShadingNormal> surfaceNormals(const Mesh& mesh, const std::atomic<bool>* cancel) {
    const auto stopped = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    if (mesh.vertices.empty() || stopped())
        return {};
    const auto position = [&](size_t i) {
        const auto& v = mesh.vertices[i];
        return glm::dvec3(v.x, v.y, v.z);
    };
    glm::dvec3 lo(position(0)), hi(lo);
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        lo = glm::min(lo, position(i));
        hi = glm::max(hi, position(i));
    }
    const auto extent = hi - lo;
    const double scale = 1e6 / std::max({extent.x, extent.y, extent.z, 1e-30});
    const auto key = [&](size_t i) {
        const auto p = glm::round((position(i) - lo) * scale);
        return Position{int32_t(p.x), int32_t(p.y), int32_t(p.z)};
    };
    // Welding applies only to lighting. Duplicate iso-surface vertices must
    // agree, while creases over 50 degrees retain their original face normal.
    struct Weld {
        glm::vec3 sum{0}, reference{0};
        bool crease = false;
    };
    std::unordered_map<Position, Weld, PositionHash> welded;
    welded.reserve(mesh.vertices.size());
    std::vector<glm::vec3> face(mesh.vertices.size(), glm::vec3(0));
    for (size_t i = 0; i + 2 < mesh.indexCount(); i += 3) {
        if ((i & 255) == 0 && stopped())
            return {};
        const uint32_t a = mesh.index(i), b = mesh.index(i + 1), c = mesh.index(i + 2);
        if (a >= face.size() || b >= face.size() || c >= face.size())
            continue;
        const auto n =
            glm::vec3(glm::cross((position(b) - position(a)) * scale, (position(c) - position(a)) * scale));
        if (glm::dot(n, n) < 1e-30f)
            continue;
        for (auto index : {a, b, c}) {
            face[index] += n;
            auto [it, inserted] = welded.try_emplace(key(index));
            auto& weld = it->second;
            const auto direction = unit(n);
            if (inserted)
                weld.reference = direction;
            else if (glm::dot(weld.reference, direction) < .642788f)
                weld.crease = true;
            weld.sum += n;
        }
    }
    std::vector<ShadingNormal> result;
    result.reserve(face.size());
    for (size_t i = 0; i < face.size(); ++i) {
        if ((i & 255) == 0 && stopped())
            return {};
        const auto original = unit(face[i]);
        const auto found = welded.find(key(i));
        const auto smooth =
            found == welded.end() || found->second.crease ? original : unit(found->second.sum);
        result.push_back(packNormal(glm::dot(original, smooth) > .642788f ? smooth : original));
    }
    return result;
}
unsigned atomPresentationLevel(double pixels, bool original) {
    if (!original && pixels > 48)
        return 3;
    return pixels > 18 ? 2 : pixels > 6 ? 1 : 0;
}
bool streamedTerrainCovers(const Address& object, const Address& focus, bool original) {
    if (object.kind() < Kind::Planet || object.kind() > Kind::Mountain)
        return false;
    if (original)
        return object.ancestorOf(focus);
    auto planet = object;
    while (planet.kind() > Kind::Planet)
        planet = planet.parent();
    return planet.ancestorOf(focus);
}
glm::mat4 periodicMaterialFrame(const glm::dmat4& mapping, double radius, double tile, double period) {
    const double frequency = radius / tile;
    glm::mat4 result(1);
    for (unsigned i = 0; i < 3; i++)
        result[i] = glm::vec4(glm::vec3(mapping[i] * frequency), 0);
    result[3] = glm::vec4(glm::vec3(glm::mod(glm::dvec3(mapping[3]) * frequency, glm::dvec3(period))), 1);
    return result;
}
float daylightSkyTransmission(double pressure, double altitude, double height, double sun) {
    const double density = pressure * std::exp(-std::max(0., altitude) / std::max(1., height));
    const double daylight = smoothstep(-.16, .08, sun);
    return float(std::exp(-density * daylight * 14.));
}
SunShadowFrame sunShadowFrame(glm::dvec3 anchor, glm::dvec3 light, glm::dquat rotation, glm::dvec3 eye,
                              double units, double span, bool homogeneous, bool bottom) {
    SunShadowFrame result;
    light = glm::normalize(light);
    const auto right = glm::normalize(
        glm::cross(-light, std::abs(light.y) < .9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0)));
    const auto up = glm::normalize(glm::cross(right, -light));
    const double texel = span / sunShadowResolution;
    anchor += right * (std::round(glm::dot(anchor, right) / texel) * texel - glm::dot(anchor, right));
    anchor += up * (std::round(glm::dot(anchor, up) / texel) * texel - glm::dot(anchor, up));
    result.anchorPlanet = anchor;
    result.lightPlanet = light;
    result.spanMetres = span;
    const auto centre = rotation * (anchor - eye) / units;
    const double size = span / units;
    result.view = glm::lookAtRH(centre + rotation * light * (size * 1.5), centre, rotation * up);
    result.projection =
        homogeneous ? glm::orthoRH_NO(-size * .5, size * .5, -size * .5, size * .5, size * .001, size * 3.)
                    : glm::orthoRH_ZO(-size * .5, size * .5, -size * .5, size * .5, size * .001, size * 3.);
    glm::dmat4 bias(1);
    bias[0][0] = .5;
    bias[1][1] = bottom ? .5 : -.5;
    bias[2][2] = homogeneous ? .5 : 1.;
    bias[3] = {.5, .5, homogeneous ? .5 : 0., 1};
    result.textureFromWorld = bias * result.projection * result.view;
    return result;
}
} // namespace infinite
