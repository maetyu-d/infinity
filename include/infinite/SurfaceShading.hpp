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
bool streamedTerrainCovers(const Address& object, const Address& focus, bool original);

// Render-only light frame. Snapping happens in the planet's double-precision
// axes before camera subtraction, so zoom/rebasing cannot move shadow texels.
struct SunShadowFrame {
    glm::dmat4 view{1}, projection{1}, textureFromWorld{1};
    glm::dvec3 anchorPlanet{}, lightPlanet{};
    double spanMetres = 0;
};
// Periodic local material frames subtract the camera in doubles before
// conversion to floats. Every sampled frequency must divide this period.
glm::mat4 periodicMaterialFrame(const glm::dmat4& planetFromCamera, double radiusMetres, double tileMetres,
                                double period);
// Split the periodic origin before casting to float. XYZ stays close to the
// camera; the last row carries integer noise cells, added after floor/fract.
glm::mat4 periodicNoiseFrame(const glm::dmat4& planetFromCamera, double radiusMetres, double tileMetres,
                             double period);
// Atmospheric daylight masks the optical galaxy field using retained climate.
float daylightSkyTransmission(double pressureBar, double altitudeMetres, double scaleHeightMetres,
                              double sunElevation);
inline constexpr unsigned sunShadowResolution = 2048;
SunShadowFrame sunShadowFrame(glm::dvec3 anchorPlanet, glm::dvec3 lightPlanet, glm::dquat planetToFrame,
                              glm::dvec3 eyePlanet, double unitsMetres, double spanMetres,
                              bool homogeneousDepth, bool originBottomLeft);
} // namespace infinite
