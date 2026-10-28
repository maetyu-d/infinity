#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace infinite {
// Hyperion's shared close-material image, independent of all universe seeds.
// R=height, G=fracture, B=mineral tint, A=grain; complete RGBA8 mip chain.
struct MaterialTexture {
    static constexpr unsigned size = 512;
    static constexpr size_t byteBudget = 1398100;
    std::vector<uint8_t> pixels;
};
std::vector<uint8_t> materialNoiseTexture();
glm::vec4 materialTexel(double u, double v);
MaterialTexture bakeMaterialTexture(const std::atomic<bool>* cancel = nullptr);
} // namespace infinite
