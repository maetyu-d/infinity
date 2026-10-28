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
    glm::vec4 domain{3, 0, 0, 0}; // object class, inherited strain/activity/ice
    glm::vec4 surface{};          // mineral, water activity, temperature, inspection section
    glm::vec4 environment{};      // retained pressure, terrain regime, gas envelope, geology type
                                  // Architecture domain: frame-relative sky up.xyz, pressure.w.
};
ScenePresentation extractPresentation(const Node&, const Geometry&, RenderMode);
float hyperionPhase(uint64_t inheritedKey);
glm::vec3 hyperionPigment(float phase);      // CPU reference for shader validation
glm::vec3 hyperionDisplay(glm::vec3 linear); // flight HDR transform reference

// Optical pigment only: the actual universe still supplies every star, galaxy
// and gas particle. Orientation is anchored to the host galaxy across rebases.
struct SpectralSky {
    glm::dquat cameraToField{1, 0, 0, 0};
    float phase = 0, gas = 0, strength = 0;
};
SpectralSky extractSpectralSky(const Universe&, const Navigation&);

// Unresolved celestial lights and cockpit contacts retain the real addresses,
// bearings and distances. Their screen footprint never changes world geometry.
enum class LifeDetection : uint8_t { Unknown, Scanning, None, Detected };
struct CelestialContact {
    Address address;
    glm::dvec3 localDirection{};
    double distanceMetres = 0, angularRadius = 0;
    glm::vec3 color{1};
    std::string label;
    bool occluded = false, selected = false;
    bool moon = false;
    LifeDetection life = LifeDetection::Unknown;
    uint64_t population = 0;
    uint32_t surfacePorts = 0, openPorts = 0;
};
inline bool hasSurfaceLife(const CelestialContact& c) {
    return c.address.kind() == Kind::Planet && c.life == LifeDetection::Detected && c.population > 0;
}
std::vector<CelestialContact> extractCelestialContacts(const Universe&, const Navigation&, const Node& system,
                                                       std::span<const Node> planets);
bool celestialOccluded(glm::dvec3 localDirection, double distanceMetres, std::span<const CelestialContact>,
                       const Address* except = nullptr, double targetAngularRadius = 0);
double celestialCellWeight(glm::dvec3 cellCentre, glm::dvec3 eye);
} // namespace infinite
