#include "infinite/Navigation.hpp"
#include "infinite/Presentation.hpp"
#include "infinite/World.hpp"
#include <iostream>
#include <map>
#include <sstream>
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
        {
            Universe u;
            Navigation nav(u);
            const auto system = u.describe(nav.anchor.child(Kind::System, 9));
            std::vector<Node> planets;
            for (auto a : u.children(system))
                planets.push_back(u.describe(a));
            std::ostringstream placement;
            placement.precision(17);
            placement << planets.front().address.str() << ' ' << planets.front().frame.logRadius
                      << " 1 0 0 0 0 0 1.2";
            check(nav.restore(placement.str()), "Invalid planetary contact fixture");
            nav.selected = planets.back().address;
            const auto view = nav.serialize();
            const auto fingerprint = descriptorFingerprint(system);
            const auto contacts = extractCelestialContacts(u, nav, system, planets);
            check(contacts.size() == planets.size() + 1, "Planetary view retains every system contact");
            bool beyondClip = false;
            for (size_t i = 0; i < contacts.size(); i++) {
                const auto& contact = contacts[i];
                check(contact.address == (i ? planets[i - 1].address : system.address),
                      "Celestial light retains its actual world address");
                const auto delta =
                    u.relative(contact.address, nav.anchor, nav.logDistance).position - nav.cameraPosition();
                const auto direction = glm::inverse(nav.orientation) * glm::normalize(delta);
                check(glm::length(contact.localDirection - direction) < 1e-12,
                      "Celestial light changed the physical bearing");
                check(
                    std::abs(contact.distanceMetres / (glm::length(delta) * std::pow(10., nav.logDistance)) -
                             1) < 1e-12,
                    "Celestial light changed the physical distance");
                check(contact.selected == (contact.address == *nav.selected), "Contact selection differs");
                beyondClip |= glm::length(delta) > 10000.;
            }
            check(beyondClip, "Fixture did not exercise unresolved planets beyond the local far plane");
            check(nav.serialize() == view && descriptorFingerprint(u.describe(system.address)) == fingerprint,
                  "Celestial presentation mutated the camera or system");
            // An exact physical ray into a planet is masked; an unrelated sky
            // direction and a foreground emitter remain visible.
            CelestialContact blocker;
            blocker.address = planets.front().address;
            blocker.localDirection = {0, 0, -1};
            blocker.distanceMetres = 100;
            blocker.angularRadius = .2;
            const std::array blockers{blocker};
            check(celestialOccluded({0, 0, -1}, 1000, blockers), "Planet did not occult a background star");
            check(!celestialOccluded({1, 0, 0}, 1000, blockers), "Planet masked an unrelated sky bearing");
            check(!celestialOccluded({0, 0, -1}, 10, blockers), "Planet masked a foreground emitter");
            check(!celestialOccluded({0, 0, -1}, 1000, blockers, &blocker.address),
                  "Contact occulted itself");
            for (auto eye : {glm::dvec3(0), glm::dvec3(.9, -.4, .2), glm::dvec3(1, 1, 1)}) {
                double weight = 0;
                size_t cells = 0;
                for (int z = -1; z <= 1; z++)
                    for (int y = -1; y <= 1; y++)
                        for (int x = -1; x <= 1; x++) {
                            const double w = celestialCellWeight(glm::dvec3(x, y, z) * 2., eye);
                            weight += w;
                            cells += w > 0;
                        }
                check(std::abs(weight - 1) < 1e-12 && cells <= 8,
                      "Celestial catalogues exceeded eight cells or lost continuous exposure");
            }
            check(std::abs(celestialCellWeight({0, 0, 0}, {1.00001, .3, -.6}) -
                           celestialCellWeight({-2, 0, 0}, {-.99999, .3, -.6})) < 1e-12,
                  "Changing root frame changed the same galaxy's sky exposure");
        }
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
