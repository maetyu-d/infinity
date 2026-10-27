#include "infinite/MaterialTexture.hpp"
#include <algorithm>
#include <cmath>

namespace infinite {
namespace {
// Exact integer lookup from Hyperion src/flight/renderer.cpp::lookupNoise.
uint8_t lookup(int x, int y) {
    uint32_t h = uint32_t(x & 255) * 1973u + uint32_t(y & 255) * 9277u + 89173u;
    h = (h ^ (h >> 16)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    return uint8_t((h ^ (h >> 16)) >> 24);
}
glm::dvec3 noise(int x, int y) {
    return glm::dvec3(lookup(x, y), lookup(x + 37, y + 17), lookup(x + 91, y + 53)) / 255.;
}
glm::dvec3 filteredNoise(glm::dvec2 uv) {
    const auto p = glm::fract(uv) * 256. - .5;
    const auto cell = glm::ivec2(glm::floor(p));
    const auto f = glm::fract(p);
    return glm::mix(glm::mix(noise(cell.x, cell.y), noise(cell.x + 1, cell.y), f.x),
                    glm::mix(noise(cell.x, cell.y + 1), noise(cell.x + 1, cell.y + 1), f.x), f.y);
}
int wrap16(int x) {
    return (x % 16 + 16) % 16;
}
} // namespace
glm::vec4 materialTexel(double u, double v) {
    const glm::dvec2 uv = glm::fract(glm::dvec2(u, v)), p = uv * 16.;
    const auto cell = glm::ivec2(glm::floor(p));
    const auto f = glm::fract(p);
    double nearest = 10, second = 10, tint = .5;
    // Port of Hyperion surface_detail.hpp::detailBakeFragment. Cell centres,
    // skipped cells, fracture width, domes and chips follow the source recipe.
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            const auto r = noise(wrap16(cell.x + x), wrap16(cell.y + y));
            if (r.b < .21)
                continue;
            const auto delta = glm::dvec2(x, y) + .01 + glm::dvec2(r) * .98 - f;
            const double d = glm::dot(delta, delta);
            if (d < nearest) {
                second = nearest;
                nearest = d;
                tint = r.b;
            } else
                second = std::min(second, d);
        }
    const double edge = std::sqrt(second) - std::sqrt(nearest);
    const double crack = 1 - glm::smoothstep(.010, .047, edge);
    const double dome = std::pow(std::clamp(1 - std::sqrt(nearest) * 1.05, 0., 1.), .8);
    const double grain =
        filteredNoise(uv).r * .54 + filteredNoise(uv * 2.).g * .29 + filteredNoise(uv * 4.).b * .17;
    const double chipped = glm::smoothstep(.60, .83, grain) * crack;
    const double height =
        .22 + dome * .53 - crack * (.11 + grain * .05) + (grain - .5) * .065 - chipped * .018;
    return glm::vec4(height, crack, tint, grain);
}
MaterialTexture bakeMaterialTexture(const std::atomic<bool>* cancel) {
    const auto stopped = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    MaterialTexture result;
    result.pixels.reserve(MaterialTexture::byteBudget);
    for (unsigned y = 0; y < MaterialTexture::size; ++y) {
        if (stopped())
            return {};
        for (unsigned x = 0; x < MaterialTexture::size; ++x) {
            const auto value =
                materialTexel((x + .5) / MaterialTexture::size, (y + .5) / MaterialTexture::size);
            for (unsigned c = 0; c < 4; ++c)
                result.pixels.push_back(uint8_t(std::round(std::clamp(value[c], 0.f, 1.f) * 255)));
        }
    }
    size_t offset = 0;
    for (unsigned width = MaterialTexture::size; width > 1; width /= 2) {
        if (stopped())
            return {};
        const auto next = width / 2;
        for (unsigned y = 0; y < next; ++y)
            for (unsigned x = 0; x < next; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (unsigned dy = 0; dy < 2; ++dy)
                        for (unsigned dx = 0; dx < 2; ++dx)
                            sum += result.pixels[offset + ((y * 2 + dy) * width + x * 2 + dx) * 4 + c];
                    result.pixels.push_back(uint8_t((sum + 2) / 4));
                }
        offset += width * width * 4;
    }
    return result;
}
} // namespace infinite
