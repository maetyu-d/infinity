#pragma once
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

namespace infinite {
inline constexpr double worldNearPlane = .00005;
inline constexpr double worldFarPlane = 10000.;
// Near maps to one and far to zero. Floating-point depth then retains useful
// precision throughout the multiscale view instead of rounding planets to sky.
// X/Y projection, clipping distances and authoritative coordinates are unchanged.
inline glm::dmat4 worldProjection(double fov, double aspect, bool homogeneous) {
    return homogeneous ? glm::perspectiveRH_NO(fov, aspect, worldFarPlane, worldNearPlane)
                       : glm::perspectiveRH_ZO(fov, aspect, worldFarPlane, worldNearPlane);
}
inline double worldViewDistance(double depth) {
    return worldNearPlane * worldFarPlane / (worldNearPlane + depth * (worldFarPlane - worldNearPlane));
}
} // namespace infinite
