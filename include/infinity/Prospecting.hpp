#pragma once
#include "Universe.hpp"
namespace infinity {
using ResourceStock = std::array<int64_t, 4>;
enum class ExposureType : uint8_t { Vein, Ejecta, Layered, CarbonSeam, IceFracture };
struct ResourceExposure {
    ExposureType type = ExposureType::Layered;
    std::array<double, 4> fractions{};
    double concentration = 0, exposure = 0;
    glm::vec3 color{};
};
struct LocalCatchment {
    std::array<double, 4> grade{};
    double water = 0;
    unsigned dominant = 0;
};
LocalCatchment localCatchment(const Node&, glm::dvec3 direction);
// An additive resource field. Existing object descriptors, seeds and material lattices stay authoritative.
ResourceExposure resourceExposure(const Node&, glm::dvec3, const GeologySample&);
std::string_view exposureName(ExposureType);
glm::vec3 exposureColor(const Node&, glm::dvec3, const GeologySample&, glm::vec3 base);
ResourceStock legacyProspectResources(const Universe&, const Address&);
struct ProspectSite {
    Address surface, pool;
    std::vector<Node> rocks; // Actual canonical rocks, not decorative replacement meshes.
    std::vector<LocalFrame> parentFrames;
    glm::dvec3 metres{}, outcropMetres{}, direction{};
    ExposureType type = ExposureType::Layered;
    glm::vec3 color{};
    double slopeDegrees = 0, clearanceMetres = 0, concentration = 0;
    unsigned resource = 0;
    bool easy = false;
    std::string name;
};
inline constexpr size_t maxRegionalProspects = 12;
std::vector<ProspectSite> generateProspects(const Universe&, const Node& planet, Tile region,
                                            const std::atomic<bool>* cancel = nullptr);
Address samplingSourceNear(const Universe&, const Node& planet, glm::dvec3 portMetres,
                           double exclusionRadius);
} // namespace infinity
