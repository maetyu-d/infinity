#pragma once
#include "Universe.hpp"

namespace infinity {
inline constexpr uint32_t majorLandformRecipeVersion = 1;
enum class MajorLandformType : uint8_t { Supercaldera, ShieldVolcano, IceSheet, Rift, ImpactBasin };
// One bounded, immutable descriptor; no camera/LOD seed or per-sample allocation.
struct MajorLandform {
    MajorLandformType type = MajorLandformType::Supercaldera;
    uint64_t seed = 0;
    glm::dvec3 centre{0, 1, 0}, along{1, 0, 0}, across{0, 0, 1};
    double widthMetres = 1, lengthMetres = 1, reliefMetres = 1, radiusMetres = 1, activity = 0;
};
struct MajorLandformSample {
    double reliefMetres = 0, coverage = 0, heat = 0, ice = 0, fracture = 0, impact = 0;
};
std::shared_ptr<const MajorLandform> generateMajorLandform(const PlanetProcess&, const Environment&);
MajorLandformSample sampleMajorLandform(const MajorLandform&, glm::dvec3 direction);
std::string_view majorLandformName(MajorLandformType);
Address majorLandformAddress(const Node& planet);
} // namespace infinity
