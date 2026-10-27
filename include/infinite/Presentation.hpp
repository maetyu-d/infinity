#pragma once
#include "Geometry.hpp"
#include <string_view>

namespace infinite {
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
} // namespace infinite
