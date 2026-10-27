#include "infinite/Flight.hpp"
#include <iostream>
#include <oneapi/tbb/global_control.h>
#include <sstream>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static void near(glm::dvec3 a, glm::dvec3 b, double tolerance, const char* message) {
    check(glm::length(a - b) < tolerance * std::max(1., glm::length(b)), message);
}
static void aim(Navigation& nav, glm::dvec3 point) {
    const auto eye = nav.cameraPosition();
    const auto delta = point - eye;
    if (glm::length(delta) < 1e-14)
        return;
    auto direction = glm::normalize(delta);
    const auto up = std::abs(direction.y) > .95 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
    nav.orientation = glm::quatLookAtRH(direction, up);
    nav.offset = eye - nav.orientation * glm::dvec3(0, 0, 1);
}
static void place(Navigation& nav, Address address, double log, glm::dvec3 eye) {
    std::ostringstream state;
    state.precision(17);
    state << address.str() << ' ' << log << " 1 0 0 0 " << eye.x << ' ' << eye.y << ' ' << eye.z - 1;
    check(nav.restore(state.str()), "invalid flight fixture");
}
struct Entry {
    Address address;
    glm::dvec3 localPoint;
    uint64_t descriptor, geometry;
};
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        Navigation nav(u);
        auto galaxy = nav.anchor;
        auto system = u.children(u.describe(galaxy), 1).front();
        auto planet = u.children(u.describe(system), 1).front();
        for (int face = 0; face < 6; ++face)
            for (uint32_t x : {0u, 1u, 127u, 128u, 254u, 255u})
                for (uint32_t y : {0u, 1u, 127u, 128u, 254u, 255u}) {
                    Tile tile{face, 8, x, y};
                    check(FlightController::surfaceAt(planet, tileDirection(tile)) ==
                              planet.child(Kind::Surface, encodeTile(tile)),
                          "surface lookup changed canonical tile or cube face");
                }
        // Pure rescaling must leave every projected ray and physical eye fixed.
        auto reference = nav.anchor;
        const auto eye = nav.cameraPosition();
        const auto orientation = nav.orientation;
        const auto log = nav.logDistance;
        for (double delta : {-1.5, 1.5, -3., 3., 0.}) {
            nav.setRenderScale(log + delta);
            near(nav.cameraPosition() * std::pow(10., delta), eye, 2e-12,
                 "unit adaptation translated the physical camera");
            check(nav.orientation == orientation, "unit adaptation rotated the eye");
            const auto pose = u.relative(system, reference, nav.logDistance);
            const auto oldPose = u.relative(system, reference, log);
            near(glm::normalize(glm::inverse(nav.orientation) * (pose.position - nav.cameraPosition())),
                 glm::normalize(glm::inverse(orientation) * (oldPose.position - eye)), 2e-12,
                 "unit adaptation moved an object on screen");
        }
        for (auto child : {system, planet}) {
            const auto before = nav.cameraPosition();
            const auto q = nav.orientation;
            const auto old = nav.anchor;
            const auto transform = u.relative(old, child, nav.logDistance);
            nav.changeFrame(child);
            near(nav.cameraPosition(), transform.position + transform.orientation * before, 2e-12,
                 "automatic frame change teleported the eye");
            check(std::abs(glm::dot(nav.orientation, transform.orientation * q)) > 1 - 1e-14,
                  "automatic frame change rotated the physical camera");
        }
        {
            Navigation terrain(u);
            RegionCache cache(u);
            FlightController flight(u, terrain, cache);
            const auto p = u.describe(planet);
            const auto direction = glm::normalize(glm::dvec3(.3, 1, .4));
            const auto point =
                direction * (1.005 + u.terrainHeight(p, direction) / std::pow(10., p.frame.logRadius));
            const auto tile = FlightController::surfaceAt(planet, point);
            place(terrain, planet, p.frame.logRadius, point);
            auto idle = [&] {
                cache.beginFrame();
                flight.update({}, 1. / 60);
                cache.finishFrame();
                cache.wait();
            };
            for (int i = 0; i < 120; ++i)
                idle();
            check(terrain.anchor == tile, "planetary approach did not resolve the actual tile underfoot");
            auto physicalEye = [&] {
                const auto node = u.describe(terrain.anchor);
                return u.pointRelative(terrain.anchor,
                                       terrain.cameraPosition() *
                                           std::pow(10., terrain.logDistance - node.frame.logRadius),
                                       planet, p.frame.logRadius);
            };
            near(physicalEye(), point, 2e-12, "idle automatic terrain entry moved the physical eye");
            // Cross a tile edge, then let only the frame/scale controller run.
            const auto radius = std::pow(10., u.describe(tile).frame.logRadius - terrain.logDistance);
            terrain.offset = glm::dvec3(1.1, .5, 0) * radius - terrain.orientation * glm::dvec3(0, 0, 1);
            const auto moved = physicalEye();
            const auto next = FlightController::surfaceAt(planet, moved);
            check(next != tile, "lateral fixture did not cross a tile boundary");
            for (int i = 0; i < 120; ++i)
                idle();
            check(terrain.anchor == next, "lateral flight did not stream the neighbouring canonical tile");
            near(physicalEye(), moved, 2e-12, "lateral terrain rebasing teleported the eye");
            terrain.select(u.children(u.describe(next)).back());
            const auto chosen = terrain.selected;
            idle();
            check(terrain.selected == chosen, "automatic targeting overwrote an explicit click selection");
            terrain.parent();
            idle();
            for (int i = 0; i < 2000 && terrain.transitioning; ++i)
                terrain.update(1. / 60);
            check(!terrain.transitioning, "manual parent focus failed to settle");
            const auto parentView = terrain.serialize();
            for (int i = 0; i < 32; ++i)
                idle();
            check(terrain.serialize() == parentView,
                  "automatic flight hijacked a manual parent view before movement resumed");
        }
        {
            Navigation stars(u);
            RegionCache cache(u);
            FlightController flight(u, stars, cache);
            auto samples = GeometryGenerator(u).generate(u.describe(galaxy), 0);
            const auto destination = galaxy.child(Kind::System, 2999);
            auto star = u.describe(destination);
            const auto galaxyRadius = u.describe(galaxy).frame.logRadius;
            place(stars, galaxy, galaxyRadius, star.frame.position + glm::dvec3(.000001, 0, 0));
            auto tick = [&] {
                cache.beginFrame();
                flight.update({}, 1. / 60, false, false, samples);
                cache.finishFrame();
                cache.wait();
            };
            for (int i = 0; i < 5; ++i)
                tick();
            stars.changeFrame(system); // Start in the earlier star's precision frame.
            auto galaxyEye = [&] {
                const auto node = u.describe(stars.anchor);
                return u.pointRelative(stars.anchor,
                                       stars.cameraPosition() *
                                           std::pow(10., stars.logDistance - node.frame.logRadius),
                                       galaxy, galaxyRadius);
            };
            const auto before = galaxyEye();
            for (int i = 0; i < 5; ++i)
                tick();
            check(stars.anchor == destination, "early star frame trapped flight away from a nearer sibling");
            near(galaxyEye(), before, 2e-12, "switching nearby star systems moved the physical camera");
        }
        // No F, focus(), parent(), zoom(), or teleport is used in these flights.
        for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
            Universe world(seed);
            Navigation camera(world);
            RegionCache cache(world);
            FlightController flight(world, camera, cache);
            GeometryGenerator generate(world);
            Address root{{INT64_MIN, 7, INT64_MAX}, {}};
            place(camera, root, 26.2, {.1, .2, 1.2});
            const auto startEye = camera.cameraPosition();
            std::vector<Entry> entries;
            std::shared_ptr<const Geometry> geometry;
            std::optional<Address> geometryAddress;
            auto tick = [&](glm::dvec3 axes, bool boost = true) {
                const auto old = camera.anchor;
                if (!geometryAddress || *geometryAddress != old) {
                    geometry = generate.generate(world.describe(old), 0);
                    geometryAddress = old;
                }
                cache.beginFrame();
                flight.update(axes, 1. / 30, boost, false, geometry);
                cache.finishFrame();
                cache.wait(); // The fixture waits; the interactive update never does.
                check(!camera.transitioning && std::isfinite(glm::length(camera.cameraPosition())),
                      "automatic flight invoked a focus transition or lost finite coordinates");
                check(std::abs(glm::length(camera.orientation) - 1) < 1e-12,
                      "automatic flight lost camera orientation precision");
                return old != camera.anchor;
            };
            for (int frame = 0; frame < 6000; ++frame) {
                const auto oldAddress = camera.anchor;
                tick({});
                const auto target = flight.target().value_or(camera.anchor);
                aim(camera, world.relative(target, camera.anchor, camera.logDistance).position);
                tick({0, 0, -1});
                if (camera.anchor != oldAddress) {
                    const auto node = world.describe(camera.anchor);
                    const auto own =
                        camera.cameraPosition() * std::pow(10., camera.logDistance - node.frame.logRadius);
                    entries.push_back({node.address, own, descriptorFingerprint(node),
                                       geometryFingerprint(*generate.generate(node, 0))});
                    std::cout << "ENTER " << kindName(camera.anchor.kind()) << " " << camera.anchor.str()
                              << " log=" << camera.logDistance << '\n';
                }
                if (camera.anchor.kind() == Kind::Lattice &&
                    camera.logDistance < world.describe(camera.anchor).frame.logRadius + .55)
                    break;
                check(frame < 5999, "automatic inward flight did not reach atomic viewing scale");
            }
            check(entries.size() == 8, "automatic flight skipped or revisited an inward level");
            Navigation saved(world);
            check(saved.restore(camera.serialize()) && saved.serialize() == camera.serialize(),
                  "automatic flight did not preserve restartable camera/breadcrumbs");
            size_t returning = entries.size();
            for (int frame = 0; frame < 6000 && returning; ++frame) {
                const auto& entry = entries[returning - 1];
                const auto child = world.describe(entry.address);
                const auto goal =
                    world.pointRelative(entry.address.parent(), glm::normalize(child.frame.position) * 2.5,
                                        camera.anchor, camera.logDistance);
                aim(camera, goal);
                tick({0, 0, -1});
                if (camera.anchor == entry.address.parent()) {
                    const auto node = world.describe(entry.address);
                    check(descriptorFingerprint(node) == entry.descriptor &&
                              geometryFingerprint(*generate.generate(node, 0)) == entry.geometry,
                          "automatic return changed procedural identity or geometry");
                    std::cout << "EXIT " << kindName(entry.address.kind()) << '\n';
                    --returning;
                }
                check(frame < 5999, "automatic outward flight did not return through every level");
            }
            check(camera.anchor == root && returning == 0, "flight did not return to its cosmic root");
            const auto switches = flight.frameChanges();
            for (int i = 0; i < 32; ++i)
                tick({});
            check(flight.frameChanges() == switches, "stationary automatic frames oscillated");
            check(cache.stats().peakPending <= 3 && cache.stats().bytes <= 32 * 1024 * 1024,
                  "flight exceeded generation concurrency or resident cache limits");
            (void)startEye;
        }
        std::cout
            << "PASS: " << checks
            << " seamless-flight checks; full cosmic-to-atomic and outward "
               "flights, no focus/zoom commands, three seeds, finite frames and deterministic geometry.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " seamless-flight checks: " << e.what() << '\n';
        return 1;
    }
}
