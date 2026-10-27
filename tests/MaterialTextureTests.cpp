#include "infinite/MaterialTexture.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace infinite;
static void check(bool v, const char* message) {
    if (!v)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::atomic<bool> cancel{true};
        check(bakeMaterialTexture(&cancel).pixels.empty(), "Cancelled material bake produced data");
        const auto texture = bakeMaterialTexture(), repeat = bakeMaterialTexture();
        check(texture.pixels.size() == MaterialTexture::byteBudget,
              "Material mip chain exceeds its fixed memory budget");
        check(texture.pixels == repeat.pixels, "Material depends on task order or mutable random state");
        for (int i = 0; i < 200; i++) {
            const double v = (i + .5) / 256.;
            check(glm::length(materialTexel(0, v) - materialTexel(1, v)) < 1e-6f &&
                      glm::length(materialTexel(v, 0) - materialTexel(v, 1)) < 1e-6f,
                  "Material image is not periodic at the tile edge");
            const auto a = materialTexel(v, .37), b = materialTexel(v + 32, .37 - 64);
            check(glm::length(a - b) < 1e-5f, "Origin modulo period shifts material detail");
            check(glm::length(materialTexel(-1e-8, v) - materialTexel(1e-8, v)) < 1e-4f,
                  "Material fracture/height field jumps at a wrapped edge");
        }
        double cracked = 0, body = 0;
        size_t crackCount = 0, bodyCount = 0;
        for (size_t i = 0; i < MaterialTexture::size * MaterialTexture::size * 4; i += 4) {
            if (texture.pixels[i + 1] > 200) {
                cracked += texture.pixels[i];
                ++crackCount;
            }
            if (texture.pixels[i + 1] < 20) {
                body += texture.pixels[i];
                ++bodyCount;
            }
        }
        check(crackCount > 500 && bodyCount > 10000,
              "Material lost its distinct fracture and mineral regions");
        check(cracked / crackCount < body / bodyCount - 15, "Fractures do not cut into the mineral relief");
        size_t offset = 0;
        double mean[4]{};
        for (size_t i = 0; i < MaterialTexture::size * MaterialTexture::size; i++)
            for (unsigned c = 0; c < 4; c++)
                mean[c] += texture.pixels[i * 4 + c] / double(MaterialTexture::size * MaterialTexture::size);
        for (unsigned width = MaterialTexture::size; width; width /= 2) {
            double sum[4]{};
            for (size_t i = 0; i < width * width; i++)
                for (unsigned c = 0; c < 4; c++)
                    sum[c] += texture.pixels[offset + i * 4 + c] / double(width * width);
            for (unsigned c = 0; c < 4; c++)
                check(std::abs(mean[c] - sum[c]) < 4, "Mipmap filtering changes average material energy");
            offset += width * width * 4;
        }
        check(offset == texture.pixels.size(), "Mip levels leave missing or excess texels");
        std::cout << "Packed material: fixed GPU budget, deterministic bake, periodic origins, fracture "
                     "relief and mip energy passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
