#pragma once
#include "Activity.hpp"
#include "Piloting.hpp"
#include "Presentation.hpp"
namespace infinite {
struct CockpitRect {
    double x, y, w, h;
    bool contains(glm::dvec2) const;
    bool overlaps(const CockpitRect&) const;
};
struct CockpitLayout {
    double width;
    CockpitRect traffic, celestial, life, navigation, flight, scope, systems, windscreen;
    explicit CockpitLayout(double aspect);
    std::optional<size_t> trafficRow(glm::dvec2, size_t count) const;
    std::optional<size_t> celestialRow(glm::dvec2, size_t count) const;
};
// A short display list keeps a distant selected actor visible without reordering
// the authoritative contact list used by K cycling and stable identity lookup.
std::vector<size_t> cockpitActivityRows(std::span<const ActivityContact>);
std::vector<size_t> cockpitCelestialRows(std::span<const CelestialContact>);
std::optional<Address> nextLivingWorld(std::span<const CelestialContact>, const std::optional<Address>&);
struct CockpitTargetMarker {
    glm::dvec2 point{}, direction{};
    bool offscreen = false, aft = false;
};
CockpitTargetMarker cockpitTargetMarker(glm::dvec3 bearing, double aspect);
struct CockpitOverlay {
    Mesh panels, strokes, lettering;
    std::vector<CockpitRect> worldLabels, hitRegions;
};
// Port of Hyperion's CabinMesh / rebuildCockpit beveled screen-framed cabin.
Mesh cockpitMesh(double aspect);
CockpitOverlay cockpitOverlay(const FlightInstruments&, double aspect, std::string_view mode,
                              std::string_view notice = {}, std::span<const CelestialContact> contacts = {},
                              std::span<const ActivityContact> activity = {}, bool help = false);
std::string flightQuantity(double value, bool speed = false);
} // namespace infinite
