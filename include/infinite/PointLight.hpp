#pragma once
#include <cmath>
namespace infinite {
struct PointLightFootprint {
    double radiusScale = 1, radianceScale = 1;
};
inline constexpr double pointLightSupport = .5;
// Integral of the old tapered core/halo divided by that of the compact
// four-triangle footprint with its linear vertex fade (Simpson reference).
inline constexpr double pointLightEnergy = 1.6213249598303459;
// The existing emitter shader has a Gaussian core exp(-48*r*r): its sigma
// is only radius/sqrt(96). Give the core a 0.61-pixel optical footprint
// before rasterization. Multiplying radiance by the inverse area preserves
// core/halo energy; neither the world radius nor procedural identity changes.
inline PointLightFootprint pointLightFootprint(double radiusPixels) {
    if (!std::isfinite(radiusPixels) || radiusPixels <= 0)
        return {};
    const double ratio = std::hypot(radiusPixels, 6.) / radiusPixels;
    return {ratio, pointLightEnergy / (ratio * ratio)};
}
} // namespace infinite
