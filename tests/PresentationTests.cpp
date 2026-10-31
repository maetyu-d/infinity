#include "infinite/Navigation.hpp"
#include "infinite/Presentation.hpp"
#include "infinite/SurfaceLife.hpp"
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
        check(parseRenderMode("hyperion") == RenderMode::Hyperion &&
                  renderModeName(RenderMode::Hyperion) == "HYPERION",
              "Hyperion is the sole rendering mode");
        for (auto removed : {"original", "hybrid"}) {
            bool refused = false;
            try {
                parseRenderMode(removed);
            } catch (const std::invalid_argument&) {
                refused = true;
            }
            check(refused, "Removed graphics mode is still accepted");
        }
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
            const PolarLight optical{{0, 1, 0}, 1, 1.01, 1.03};
            const auto front = polarLightSpan({0, 0, 2}, {0, 0, -1}, optical, 100);
            check(glm::length(front - glm::dvec2(.97, .99)) < 1e-12,
                  "Polar light escaped its near-side hollow shell");
            check(polarLightSpan({0, 0, 2}, {0, 0, -1}, optical, .5) == glm::dvec2(0),
                  "Foreground geometry did not hide the polar light");
            const auto ground = polarLightSpan({0, 0, 1.005}, {0, 0, 1}, optical, 100);
            check(glm::length(ground - glm::dvec2(.005, .025)) < 1e-12,
                  "Ground observer integrated emission below the polar layer");
            check(polarLightSpan({0, 0, 1.005}, {0, 0, -1}, optical, 100) == glm::dvec2(0),
                  "Polar light was visible through the planet");
            check(polarLightSpan({0, 0, 2}, {0, 0, 1}, optical, 100) == glm::dvec2(0),
                  "A ray pointing away from the atmosphere emitted light");
            const auto grazing = polarLightSpan({1.02, 0, 2}, {0, 0, -1}, optical, 100);
            check(grazing.x > 0 && grazing.y > grazing.x, "Tangent rays lost the visible polar limb");
            check(polarLightSpan({0, 0, 2}, {0, 0, -1}, {}, 100) == glm::dvec2(0),
                  "Disabled polar light retained an old atmosphere");
            Universe u;
            bool air = false, vacuum = false;
            for (unsigned star = 0; star < 256; ++star) {
                const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
                for (auto a : u.children(system)) {
                    const auto node = u.describe(a);
                    const auto hash = descriptorFingerprint(node);
                    const auto climate = planetClimate(node);
                    const auto light = extractPolarLight(node);
                    check(light.strength >= 0 && light.strength <= 1 &&
                              glm::length(light.axis - climate.axis) < 1e-14,
                          "Polar optics lost the inherited climate axis or bounded strength");
                    if (light.strength > 0) {
                        air = true;
                        check(climate.solid && climate.pressureBar > .04 && light.innerRadius > 1 &&
                                  light.outerRadius > light.innerRadius && light.outerRadius <= 1.101,
                              "Polar optics escaped the retained solid atmosphere");
                    }
                    if (climate.pressureBar == 0) {
                        vacuum = true;
                        check(light.strength == 0, "Airless body acquired an aurora");
                    }
                    check(descriptorFingerprint(u.describe(a)) == hash,
                          "Polar-light presentation mutated the world descriptor");
                }
            }
            check(air, "Polar optics fixtures omitted an atmospheric world");
            check(vacuum, "Polar optics fixtures omitted an airless world");
            // The current generated catalogue is solid. Exercise the supported
            // envelope classification with an isolated descriptor fixture.
            auto gas =
                u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0));
            auto envelope = std::make_shared<PlanetProcess>(*gas.geology);
            envelope->type = GeologyType::GasEnvelope;
            gas.geology = envelope;
            check(extractPolarLight(gas).strength == 0,
                  "Optical solid-world shell was applied to a gas envelope");
        }
        const auto highlight = hyperionDisplay({8, .03, .4});
        check(highlight.r - highlight.g > .5f, "Bright spectral pigments washed out to white");
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
            const auto sky = extractSpectralSky(u, nav);
            check(sky.strength > .5 && sky.gas >= 0 && sky.gas <= 1,
                  "Planetary flight lost its inherited spectral medium");
            nav.changeFrame(system.address);
            const auto rebasedSky = extractSpectralSky(u, nav);
            check(sky.phase == rebasedSky.phase && sky.gas == rebasedSky.gas &&
                      std::abs(sky.strength - rebasedSky.strength) < 1e-7 &&
                      glm::length(sky.cameraToField * glm::dvec3(.2, .3, -.5) -
                                  rebasedSky.cameraToField * glm::dvec3(.2, .3, -.5)) < 1e-11,
                  "Changing precision frames moved the optical field");
            nav.setRenderScale(-12);
            check(extractSpectralSky(u, nav).strength == 0,
                  "Atomic inspection replaced the specimen with a cosmic backdrop");
            check(nav.restore(view), "Spectral medium test did not restore the exact view");
            check(extractSpectralSky(u, nav).phase == sky.phase,
                  "Returning to planetary flight changed the inherited visual phase");
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
                for (auto mode : {RenderMode::Hyperion}) {
                    const auto scene = extractPresentation(node, *geometry, mode);
                    check(&scene.geometry == geometry.get(), "Adapter retains identical geometry");
                    check(scene.environment.x == (node.address.kind() == Kind::Planet
                                                      ? float(planetClimate(node).pressureBar)
                                                      : 0.f),
                          "Optical pressure must follow the retained climate");
                    check(scene.environment.z ==
                              (node.geology && node.geology->type == GeologyType::GasEnvelope ? 1.f : 0.f),
                          "Gas-band presentation escaped the actual envelope classification");
                    check(scene.environment.y ==
                              float(node.geology ? node.geology->terrain.type : TerrainType::None),
                          "Surface presentation lost its inherited terrain regime");
                    check(geometryFingerprint(scene.geometry) == hash, "Presentation never mutates geometry");
                    check(descriptorFingerprint(node) == descriptor, "Materials never mutate simulation");
                    check(nav.serialize() == camera, "Presentation never mutates camera");
                    check(scene.surface.x == float(node.environment.mineral) &&
                              scene.surface.y == float(node.environment.waterActivity) &&
                              scene.surface.z == float(node.environment.temperature),
                          "Close material lost inherited environmental constraints");
                    check(scene.surface.w == (geometry->section ? 1.f : 0.f),
                          "Inspection material flag changed its geometry source");
                    check(scene.strength == 1.f, "Hyperion must retain its full colour strength");
                    check(scene.phase >= 0 && scene.phase < 1, "Inherited phase bounded");
                    auto [phase, inserted] = materialPhases.emplace(scene.materialFrame, scene.phase);
                    check(inserted || phase->second == scene.phase,
                          "Nested material uses the same inherited pigment phase");
                    if (scale >= 3 && scale <= 5)
                        check(scene.materialFrame.kind() == Kind::Planet && scene.domain.x == 1,
                              "Terrain patches share the planet material frame");
                    if (scale >= 6)
                        check(scene.materialFrame.kind() == Kind::Rock,
                              "Nested matter shares the host material frame");
                    if (scale == 8)
                        check(scene.material.w == 0 && scene.domain.x == 2,
                              "Lattice remains identifiable and free of decorative fibres");
                }
                visited.emplace_back(address, hash);
                auto children = u.children(node, 1);
                if (!children.empty())
                    address = children.front();
            }
            check(visited.back().first.kind() == Kind::Lattice, "Every scale reaches atoms");
            const auto enclosure = u.describe(address.parent());
            auto section =
                generator.section(enclosure, address, sectionBucket(enclosure.frame.logRadius - 6));
            const auto sectionHash = geometryFingerprint(*section);
            for (auto mode : {RenderMode::Hyperion}) {
                const auto scene = extractPresentation(enclosure, *section, mode);
                check(scene.surface.w == 1.f && &scene.geometry == section.get(),
                      "Inspected enclosure lost its retained section geometry");
                check(geometryFingerprint(scene.geometry) == sectionHash,
                      "Cutaway illumination changed the enclosing mineral");
            }
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
