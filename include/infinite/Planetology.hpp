#pragma once
#include "Universe.hpp"

namespace infinite {
inline constexpr uint32_t planetDiversityRecipeVersion = 1;
inline constexpr unsigned giantPopulationCount = 4;
enum class PlanetType : uint8_t {
    Rocky,
    IronRich,
    Desert,
    Oceanic,
    IceWorld,
    LavaWorld,
    GasGiant,
    IceGiant,
    SubNeptune
};
enum class ThermalType : uint8_t { Frozen, Cold, Temperate, Warm, Hot, Molten };
struct GiantFormation {
    double radiusMetres = 0, density = 0;
};
GiantFormation giantFormation(uint64_t seed, unsigned slot);
double planetDensity(const Node&);
struct RingProcess {
    uint64_t seed = 0;
    double innerRadius = 0, outerRadius = 0, opticalDepth = 0, iceFraction = 0;
    glm::dvec3 axis{0, 1, 0};
    bool present() const {
        return opticalDepth > 0 && outerRadius > innerRadius;
    }
};
struct PlanetTraits {
    uint64_t seed = 0;
    PlanetType type = PlanetType::Rocky;
    ThermalType thermal = ThermalType::Temperate;
    glm::dvec3 axis{0, 1, 0};
    glm::vec3 low{.2f}, high{.6f};
    double phase = 0, haze = 0, bandContrast = 0, iceCoverage = 0;
    unsigned jetBands = 0;
    RingProcess rings;
};
PlanetTraits planetTraits(const Node&);
std::string_view planetTypeName(PlanetType);
std::string_view thermalTypeName(ThermalType);
// Shared physical broad features; GPU presentation refines these same belts.
glm::vec3 giantAlbedo(const PlanetTraits&, glm::dvec3 direction);
double ringOpticalDepth(const RingProcess&, double radiusInPlanetRadii);
} // namespace infinite
