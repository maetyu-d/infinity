#include "infinity/TerrainMaterial.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
std::array<glm::vec4, 4> TerrainMaterial::uniforms() const {
    return {glm::vec4(bedrock, rockRoughness), glm::vec4(sediment, sedimentRoughness),
            glm::vec4(layerMetres, sedimentCover, metal, radiusMetres), glm::vec4(fabric, frozen)};
}
TerrainMaterial terrainMaterial(const Node& n) {
    TerrainMaterial m;
    if (!hasSolidSurface(n)) return m;
    const auto& p = *n.geology;
    const auto& e = n.environment;
    const std::array<glm::vec3, 5> rock{{{.27f,.29f,.32f}, {.29f,.14f,.075f}, {.16f,.24f,.15f},
                                       {.085f,.27f,.36f}, {.065f,.075f,.10f}}};
    m.bedrock = rock[size_t(e.mineral)];
    m.frozen = float(std::clamp(std::max({e.mineral == Mineral::Ice ? 1. : 0.,
        p.type == GeologyType::IceShell ? .9 : 0., e.chemistry.ice}) *
        (1 - smoothstep(265., 305., e.temperature)), 0., 1.));
    m.bedrock = glm::mix(m.bedrock, glm::vec3(.075f,.25f,.34f), m.frozen);
    // Weathered iron, silicate dust and carbon remain chemically distinct.
    const auto dust = glm::mix(glm::vec3(.35f,.29f,.21f), glm::vec3(.38f,.18f,.09f), float(e.chemistry.metal));
    m.sediment = glm::mix(m.bedrock * 1.12f, dust, float(.25 + .45 * e.waterActivity));
    m.sediment = glm::mix(m.sediment, glm::vec3(.63f,.78f,.83f), m.frozen);
    m.sedimentCover = float(std::clamp(.25 + p.terrain.weathering * .6 + e.ageGyr * .025 - p.activity * .15, .12, .85));
    m.metal = float(std::clamp(e.chemistry.metal * (e.mineral == Mineral::Iron ? .85 : .30), 0., .8));
    m.rockRoughness = glm::mix(.66f - m.metal * .20f, .24f, m.frozen);
    m.sedimentRoughness = glm::mix(.91f, .78f, m.frozen);
    m.layerMetres = float(std::clamp(p.terrain.wavelengthMetres / 320., 2., 28.));
    m.radiusMetres = float(std::pow(10., p.logRadius));
    m.fabric = glm::vec3(p.terrain.fabric * glm::dvec3(0, 1, 0));
    return m;
}
} // namespace infinity
