#pragma once
#include "Universe.hpp"

namespace infinity {
enum class SurfaceProvince : uint8_t { Bedrock, Upland, Volcanic, Impact, Basin, Frost };
struct ProvinceSample {
    std::array<double, 6> weights{};
    SurfaceProvince dominant() const;
};
ProvinceSample surfaceProvince(const PlanetProcess&, glm::dvec3 direction, const GeologySample&);
std::string_view provinceName(SurfaceProvince);
glm::vec3 provinceAlbedo(const ProvinceSample&, glm::vec3 substrate);
std::vector<Address> geologicalLandmarks(const Universe&, const Node& planet, size_t limit);
} // namespace infinity
