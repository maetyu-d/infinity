#pragma once
#include "Geometry.hpp"
#include "Planetology.hpp"
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
    std::array<glm::vec4, 5> planet{};
    std::array<glm::vec4, 2> planetRings{};
    std::array<glm::vec4, 2> starType{};
};
void describePlanetPresentation(ScenePresentation&, const Node& planet);
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

// Optical polar emission only. The retained climate supplies its axis and
// atmosphere; this does not add a magnetic simulation or alter weather.
struct PolarLight {
    glm::dvec3 axis{0, 1, 0};
    double strength = 0, innerRadius = 1, outerRadius = 1;
};
PolarLight extractPolarLight(const Node&);
// CPU reference for the shader's hollow, opaque-depth-clipped optical shell.
// Distances and radii are expressed in planet radii, independent of render units.
glm::dvec2 polarLightSpan(glm::dvec3 eye, glm::dvec3 ray, const PolarLight&, double opaqueDistance);

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
                                                       std::span<const Node> planets,
                                                       glm::dquat headInShip = {1, 0, 0, 0});
// Read-only image of the existing parent star. Camera-relative direction and
// physical angular size remain invariant when render units change.
struct StellarImage {
    Address address;
    glm::dvec3 direction{0, 0, -1};
    glm::vec3 color{};
    double angularRadius = 0, distanceMetres = 0, nearDepthUnits = 0;
    float visibility = 0;
    std::array<glm::vec4, 2> starType{};
    glm::dquat cameraToStar{1, 0, 0, 0};
};
StellarImage extractStellarImage(const Universe&, const Navigation&, std::span<const CelestialContact>,
                                 glm::dquat headInShip = {1, 0, 0, 0});
// Direction and contact bearings use the camera frame; distances are metres.
double stellarDiscVisibility(glm::dvec3 direction, double angularRadius, double distanceMetres,
                             std::span<const CelestialContact>);
bool celestialOccluded(glm::dvec3 localDirection, double distanceMetres, std::span<const CelestialContact>,
                       const Address* except = nullptr, double targetAngularRadius = 0);
double celestialCellWeight(glm::dvec3 cellCentre, glm::dvec3 eye);
} // namespace infinite
