#pragma once
#include "Piloting.hpp"
namespace infinite {
struct CockpitOverlay {
    Mesh panels, strokes, lettering;
};
// Port of Hyperion's CabinMesh / rebuildCockpit beveled screen-framed cabin.
Mesh cockpitMesh(double aspect);
CockpitOverlay cockpitOverlay(const FlightInstruments&, double aspect, std::string_view mode,
                              std::string_view notice = {});
std::string flightQuantity(double value, bool speed = false);
} // namespace infinite
