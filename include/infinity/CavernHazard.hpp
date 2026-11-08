#pragma once
#include "ProbePresentation.hpp"

namespace infinity {
struct IceFault {
    CavernFind face;
    double weakness = 0;
};

// A fault belongs to the exposed geology at this location. Live strain is
// local to the occupied cavern, like the heat of an operating cutter.
std::optional<IceFault> iceFault(const Borehole&);

struct IceStress {
    uint64_t bore = 0;
    double load = 0, fallAge = -1;
    bool warning() const { return load >= .25 && fallAge < 0; }
    bool falling() const { return fallAge >= 0; }
    double update(uint64_t hole, const IceFault&, glm::dvec3 localEye, double speed,
                  double shipScale, bool cutting, double dt); // returns unscaled hull damage
};

struct IceCrack {
    glm::dvec3 from{}, to{};
    double threshold = 0;
};
void annotateIceStress(FlightInstruments&, const IceStress&, bool cutting, bool engineer);
std::array<IceCrack, 24> iceCracks(const Borehole&, const IceFault&);
ProbeMotes iceFallMotes(const Borehole&, const IceFault&, double age);
} // namespace infinity
