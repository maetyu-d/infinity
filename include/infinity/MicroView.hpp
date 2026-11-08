#pragma once
#include "Geometry.hpp"
#include "Navigation.hpp"

namespace infinity {
struct MicroCellSite {
    glm::vec3 position{};
    uint8_t element = 0;
};
// Read-only cockpit data, derived from the current addressed material. It
// neither generates geometry nor changes navigation, collisions or seeds.
struct MicroView {
    bool active = false, atomic = false, proximityKnown = false, insideMaterial = false;
    bool atomsKnown = false;
    Address address;
    std::string name, mineral, formula, phase;
    double cellMetres = 0, fieldMetres = 0, proximityMetres = 0;
    size_t atoms = 0, bonds = 0;
    std::array<glm::vec3, 8> cellCorners{};
    std::vector<MicroCellSite> cellSites;
};
MicroView extractMicroView(const Universe&, const Navigation&, double hullMetres,
                           const Geometry* visible = nullptr);
} // namespace infinity
