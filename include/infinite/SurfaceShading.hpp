#pragma once
#include "Geometry.hpp"

namespace infinite {
// Render-only, four bytes per existing vertex. Neither topology nor positions
// are changed; separated crystal faces should continue using flat shading.
struct ShadingNormal {
    int16_t x = 0, y = 0;
};
static_assert(sizeof(ShadingNormal) == 4);
ShadingNormal packNormal(glm::vec3);
glm::vec3 unpackNormal(ShadingNormal);
std::vector<ShadingNormal> surfaceNormals(const Mesh&, const std::atomic<bool>* cancel = nullptr);
unsigned atomPresentationLevel(double diameterPixels, bool original);
} // namespace infinite
