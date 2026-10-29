#include "infinite/WorldDepth.hpp"
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
        for (bool homogeneous : {false, true}) {
            const auto projection = worldProjection(1., 1.6, homogeneous);
            const auto original = homogeneous ? glm::perspectiveRH_NO(1., 1.6, worldNearPlane, worldFarPlane)
                                              : glm::perspectiveRH_ZO(1., 1.6, worldNearPlane, worldFarPlane);
            double previous = 2;
            for (double z = worldNearPlane; z < worldFarPlane; z *= 1.13) {
                const auto clip = projection * glm::dvec4(.1 * z, -.2 * z, -z, 1);
                const auto old = original * glm::dvec4(.1 * z, -.2 * z, -z, 1);
                const auto depth = homogeneous ? clip.z / clip.w * .5 + .5 : clip.z / clip.w;
                check(depth < previous && depth >= 0 && depth <= 1.0000001,
                      "World depth lost monotonic order or clip range");
                check(std::abs(worldViewDistance(depth) / z - 1) < 2e-7,
                      "Atmosphere depth reconstruction disagrees with the world projection");
                check(clip.x / clip.w == old.x / old.w && clip.y / clip.w == old.y / old.w,
                      "Depth correction moved the projected geometry");
                previous = depth;
            }
        }
        // Exercise the actual float matrix/clip arithmetic used by Metal.
        const glm::mat4 gpu(worldProjection(1., 1.6, false));
        const auto depth = [&](float z) {
            const auto p = gpu * glm::vec4(0, 0, -z, 1);
            return p.z / p.w;
        };
        for (float z : {.00006f, .001f, 1.f, 500.f, 4000.f, 9000.f}) {
            check(depth(z) > depth(z * 1.0001f) && depth(z) > 0,
                  "Float depth collapsed nearby planetary surfaces or rounded them into the sky");
            check(std::abs(worldViewDistance(depth(z)) / z - 1) < 3e-7,
                  "Float world depth cannot reconstruct the physical view distance");
        }
        const glm::mat4 legacy(glm::perspectiveRH_ZO(1., 1.6, worldNearPlane, worldFarPlane));
        const auto old = legacy * glm::vec4(0, 0, -4000, 1);
        check(old.z / old.w == 1.f, "Negative control no longer reproduces the old missing planet");
        std::cout << "World depth: unchanged projection, monotonic ordering, float far-planet separation "
                     "and atmosphere reconstruction passed; legacy failure reproduced\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
