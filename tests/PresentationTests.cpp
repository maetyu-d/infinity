#include "infinite/Navigation.hpp"
#include "infinite/Presentation.hpp"
#include "infinite/World.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool v, const char* message) {
    ++checks;
    if (!v)
        throw std::runtime_error(message);
}
int main() {
    try {
        check(nextRenderMode(nextRenderMode(nextRenderMode(RenderMode::Original))) == RenderMode::Original,
              "All three modes cycle without changing state");
        check(parseRenderMode("hybrid") == RenderMode::Hybrid, "Mode parsing");
        bool rejected = false;
        try {
            parseRenderMode("unknown");
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "Unknown mode is rejected");
        for (int i = 0; i < 1000; ++i) {
            auto c = hyperionPigment(float(i) / 73);
            check(glm::all(glm::greaterThanEqual(c, glm::vec3(0))) &&
                      glm::all(glm::lessThanEqual(c, glm::vec3(1))),
                  "Pigment bounded");
            auto display = hyperionDisplay(c * float(i));
            check(glm::all(glm::greaterThanEqual(display, glm::vec3(0))) &&
                      glm::all(glm::lessThanEqual(display, glm::vec3(1))),
                  "HDR display bounded");
        }
        check(glm::length(hyperionDisplay({0, 0, 0})) == 0, "Black remains black");
        for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
            Universe u(seed);
            GeometryGenerator generator(u);
            Address address;
            std::vector<std::pair<Address, uint64_t>> visited;
            std::map<Address, float> materialPhases;
            for (unsigned scale = 0; scale < 9; ++scale) {
                auto node = u.describe(address);
                auto geometry = generator.generate(node, 0);
                auto hash = geometryFingerprint(*geometry);
                auto descriptor = descriptorFingerprint(node);
                Navigation nav(u);
                const auto camera = nav.serialize();
                for (auto mode :
                     {RenderMode::Original, RenderMode::Hyperion, RenderMode::Hybrid, RenderMode::Original}) {
                    const auto scene = extractPresentation(node, *geometry, mode);
                    check(&scene.geometry == geometry.get(), "Adapter retains identical geometry");
                    check(geometryFingerprint(scene.geometry) == hash, "Modes never mutate geometry");
                    check(descriptorFingerprint(node) == descriptor, "Materials never mutate simulation");
                    check(nav.serialize() == camera, "Modes never mutate camera");
                    check(scene.strength >= 0 && scene.strength <= 1, "Presentation weights bounded");
                    check(scene.phase >= 0 && scene.phase < 1, "Inherited phase bounded");
                    auto [phase, inserted] = materialPhases.emplace(scene.materialFrame, scene.phase);
                    check(inserted || phase->second == scene.phase,
                          "Nested material uses the same inherited pigment phase");
                    if (scale >= 3 && scale <= 5)
                        check(scene.materialFrame.kind() == Kind::Planet,
                              "Terrain patches share the planet material frame");
                    if (scale >= 6)
                        check(scene.materialFrame.kind() == Kind::Rock,
                              "Nested matter shares the host material frame");
                    if (scale == 8)
                        check(scene.material.w == 0, "Lattice remains free of decorative fibres");
                }
                visited.emplace_back(address, hash);
                auto children = u.children(node, 1);
                if (!children.empty())
                    address = children.front();
            }
            check(visited.back().first.kind() == Kind::Lattice, "Every scale reaches atoms");
            Universe returned(seed);
            GeometryGenerator fresh(returned);
            for (auto it = visited.rbegin(); it != visited.rend(); ++it) {
                auto node = returned.describe(it->first);
                auto geometry = fresh.generate(node, 0);
                check(geometryFingerprint(*geometry) == it->second,
                      "Full cosmic-to-atomic round trip unchanged");
            }
        }
        std::cout << checks << " presentation preservation checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
