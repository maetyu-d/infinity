#pragma once
#include "Activity.hpp"
#include "Piloting.hpp"
#include "Presentation.hpp"
namespace infinite {
struct CockpitOverlay {
    Mesh panels, strokes, lettering;
};
// Port of Hyperion's CabinMesh / rebuildCockpit beveled screen-framed cabin.
Mesh cockpitMesh(double aspect);
CockpitOverlay cockpitOverlay(const FlightInstruments&, double aspect, std::string_view mode,
                              std::string_view notice = {}, std::span<const CelestialContact> contacts = {},
                              std::span<const ActivityContact> activity = {});
std::string flightQuantity(double value, bool speed = false);
} // namespace infinite
