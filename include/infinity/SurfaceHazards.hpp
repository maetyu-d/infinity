#pragma once
#include "SurfaceLife.hpp"

namespace infinity {
enum class SurfaceHazard { None, Gusts, Whiteout, LooseGround, FracturedIce, Heat, ElectricalStorm, Geothermal, Crevasses };
struct SurfaceConditions {
    SurfaceHazard kind = SurfaceHazard::None;
    double severity = 0;
    bool landingHold = false, outdoorHold = false;
    std::string label, action;
};
SurfaceConditions surfaceConditions(const Node&, glm::dvec3 direction, double slopeRadians,
                                    const WeatherSample&);
struct SurfaceExposure {
    double heat = 0, electrical = 0, scanQuality = 1;
};
// Physical altitude, independent of rendering magnification. Caverns and docked
// ships are sheltered by the caller; gas envelopes are excluded.
SurfaceExposure surfaceExposure(const Node&, const WeatherSample&, double altitudeMetres, std::optional<glm::dvec3> direction = {});
struct SurfaceExposureState {
    double heat = 0, charge = 0;
    // Returns unscaled hull damage; the ship's existing size vulnerability is
    // applied once by Pilot. Both loads survive saves and scale changes.
    double advance(const SurfaceExposure&, double size, double seconds);
};
} // namespace infinity
