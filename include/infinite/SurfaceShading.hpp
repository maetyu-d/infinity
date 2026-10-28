#pragma once
#include "Geometry.hpp"

namespace infinite {
// Render-only, four bytes per existing vertex. Neither topology nor positions
// are changed; separated crystal faces should continue using flat shading.
struct ShadingNormal {
    int16_t x = 0, y = 0;
};
static_assert(sizeof(ShadingNormal) == 4);
ShadingNormal packNormal(glm::vec3);
glm::vec3 unpackNormal(ShadingNormal);
std::vector<ShadingNormal> surfaceNormals(const Mesh&, const std::atomic<bool>* cancel = nullptr);
unsigned atomPresentationLevel(double diameterPixels, bool original);

// Render-only light frame. Snapping happens in the planet's double-precision
// axes before camera subtraction, so zoom/rebasing cannot move shadow texels.
struct SunShadowFrame {
    glm::dmat4 view{1}, projection{1}, textureFromWorld{1};
    glm::dvec3 anchorPlanet{}, lightPlanet{};
    double spanMetres = 0;
};
inline constexpr unsigned sunShadowResolution = 2048;
SunShadowFrame sunShadowFrame(glm::dvec3 anchorPlanet, glm::dvec3 lightPlanet, glm::dquat planetToFrame,
                              glm::dvec3 eyePlanet, double unitsMetres, double spanMetres,
                              bool homogeneousDepth, bool originBottomLeft);
} // namespace infinite
