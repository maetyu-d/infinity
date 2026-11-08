#pragma once
#include "Universe.hpp"

namespace infinity {
struct TerrainMaterial {
    glm::vec3 bedrock{}, sediment{};
    float rockRoughness = .72f, sedimentRoughness = .9f;
    float sedimentCover = 0, frozen = 0, metal = 0;
    float layerMetres = 8, radiusMetres = 1;
    glm::vec3 fabric{0, 1, 0};
    std::array<glm::vec4, 4> uniforms() const;
};
// A reflectance description of the existing host planet, never a terrain recipe.
TerrainMaterial terrainMaterial(const Node&);
} // namespace infinity
