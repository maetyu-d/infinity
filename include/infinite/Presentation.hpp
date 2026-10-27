#pragma once
#include "Geometry.hpp"
#include <string_view>

namespace infinite {
class Navigation;
enum class RenderMode : uint8_t { Original, Hyperion, Hybrid };
std::string_view renderModeName(RenderMode);
RenderMode parseRenderMode(std::string_view);
RenderMode nextRenderMode(RenderMode);

// Read-only simulation -> presentation boundary. No seeds, geometry or LOD
// requests are changed by a presentation choice. Coordinates remain local.
struct ScenePresentation {
    const Geometry& geometry;
    Address materialFrame;
    glm::vec4 material; // roughness, metallic, pigment amount, fibre amount
    float phase;
    float strength;
};
ScenePresentation extractPresentation(const Node&, const Geometry&, RenderMode);
glm::vec3 hyperionPigment(float phase);      // CPU reference for shader validation
glm::vec3 hyperionDisplay(glm::vec3 linear); // flight HDR transform reference

// Unresolved celestial lights and cockpit contacts retain the real addresses,
// bearings and distances. Their screen footprint never changes world geometry.
struct CelestialContact {
    Address address;
    glm::dvec3 localDirection{};
    double distanceMetres = 0, angularRadius = 0;
    glm::vec3 color{1};
    std::string label;
    bool occluded = false, selected = false;
    bool moon = false;
};
std::vector<CelestialContact> extractCelestialContacts(const Universe&, const Navigation&, const Node& system,
                                                       std::span<const Node> planets);
bool celestialOccluded(glm::dvec3 localDirection, double distanceMetres, std::span<const CelestialContact>,
                       const Address* except = nullptr, double targetAngularRadius = 0);
double celestialCellWeight(glm::dvec3 cellCentre, glm::dvec3 eye);
} // namespace infinite
