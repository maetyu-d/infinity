#include "infinite/Navigation.hpp"
#include "infinite/Presentation.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
using namespace infinite;
static void check(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
int main() {
    try {
        Universe u;
        Navigation nav(u);
        const auto star = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 0));
        const auto descriptor = descriptorFingerprint(star);
        const double log = std::log10(star.stellarRadiusMetres) + 2;
        const glm::dvec3 eye(0, 0, .04);
        std::ostringstream s;
        s.precision(17);
        s << star.address.str() << ' ' << log << " 1 0 0 0 " << eye.x << ' ' << eye.y << ' ' << eye.z - 1;
        check(nav.restore(s.str()), "Stellar fixture camera restore failed");
        const auto image = extractStellarImage(u, nav, {});
        check(image.address == star.address && image.visibility == 1,
              "Parent star identity or visibility lost");
        check(std::abs(image.angularRadius - std::asin(.25)) < 1e-12, "Stellar apparent radius was invented");
        check(glm::length(image.direction - glm::dvec3(0, 0, -1)) < 1e-12, "Stellar camera bearing changed");
        check(glm::length(image.color - stellarColor(star.temperatureK)) < 1e-7,
              "Stellar temperature colour changed");
        nav.setRenderScale(log - 3);
        const auto rebased = extractStellarImage(u, nav, {});
        check(glm::length(rebased.direction - image.direction) < 1e-12 &&
                  std::abs(rebased.angularRadius - image.angularRadius) < 1e-12,
              "Stellar optics changed under a render-unit rebase");
        check(std::abs(rebased.distanceMetres / image.distanceMetres - 1) < 1e-12,
              "Stellar physical distance changed");
        check(std::abs(rebased.nearDepthUnits / image.nearDepthUnits - 1000) < 1e-8,
              "Stellar depth did not follow render units");
        CelestialContact body;
        body.localDirection = {0, 0, -1};
        body.distanceMetres = 1e8;
        body.angularRadius = .02;
        check(stellarDiscVisibility({0, 0, -1}, .01, 1e9, {&body, 1}) == 0,
              "Opaque planet failed to eclipse its star");
        body.angularRadius = .005;
        check(std::abs(stellarDiscVisibility({0, 0, -1}, .01, 1e9, {&body, 1}) - .75) < 1e-12,
              "Annular transit lost its physical disc area");
        body.angularRadius = .01;
        body.localDirection = {std::sin(.01), 0, -std::cos(.01)};
        const double overlap = (2 * std::numbers::pi / 3 - std::sqrt(3.) / 2) / std::numbers::pi;
        check(std::abs(stellarDiscVisibility({0, 0, -1}, .01, 1e9, {&body, 1}) - (1 - overlap)) < 1e-10,
              "Partial eclipse overlap is incorrect");
        body.distanceMetres = 2e9;
        check(stellarDiscVisibility({0, 0, -1}, .01, 1e9, {&body, 1}) == 1,
              "A body behind the star blocked it");
        // Contacts are camera-frame bearings. Exercise the real planet-side
        // API with a rolled camera so world and camera directions cannot alias.
        std::optional<Node> world;
        for (unsigned i = 0; i < 256 && !world; ++i)
            for (auto a : u.children(u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, i)))) {
                auto p = u.describe(a);
                if (!isMoon(a) && hasSolidSurface(p) && planetClimate(p).pressureBar > .5 &&
                    p.environment.temperature > 250 && p.environment.temperature < 330) {
                    world = p;
                    break;
                }
            }
        check(bool(world), "No actual planetary day fixture");
        const auto parent = u.describe(world->address.parent());
        const auto ray =
            glm::normalize(u.relative(parent.address, world->address, world->frame.logRadius).position);
        const auto orientation =
            glm::quatLookAtRH(ray, glm::dvec3(0, 1, 0)) * glm::angleAxis(.6, glm::dvec3(0, 0, 1));
        const auto offset = ray * 1.08 - orientation * glm::dvec3(0, 0, 1);
        std::ostringstream day;
        day.precision(17);
        day << world->address.str() << ' ' << world->frame.logRadius << ' ' << orientation.w << ' '
            << orientation.x << ' ' << orientation.y << ' ' << orientation.z << ' ' << offset.x << ' '
            << offset.y << ' ' << offset.z;
        check(nav.restore(day.str()), "Planetary stellar camera restore failed");
        std::vector<Node> planets;
        for (auto a : u.children(parent))
            planets.push_back(u.describe(a));
        const auto contacts = extractCelestialContacts(u, nav, parent, planets);
        const auto daylight = extractStellarImage(u, nav, contacts);
        const auto sun = std::find_if(contacts.begin(), contacts.end(),
                                      [&](const auto& c) { return c.address == parent.address; });
        check(sun != contacts.end() && glm::length(daylight.direction - sun->localDirection) < 1e-12,
              "Stellar and contact camera frames disagree");
        check(sun->occluded || daylight.visibility > .9,
              "A clear day star was hidden by a coordinate-frame mismatch");
        const auto geometry = GeometryGenerator(u).generate(star, 1);
        const auto hash = geometryFingerprint(*geometry);
        for (auto mode : {RenderMode::Hyperion}) {
            const auto p = extractPresentation(star, *geometry, mode);
            check(p.domain.x == 6 && std::abs(p.domain.y - star.stellarRadiusMetres /
                                                               std::pow(10., star.frame.logRadius)) < 1e-8,
                  "Stellar material lost its actual radius");
            check(geometryFingerprint(*geometry) == hash &&
                      descriptorFingerprint(u.describe(star.address)) == descriptor,
                  "Stellar presentation changed generation");
        }
        std::cout << "PASS: addressed stellar optics, temperature colours, physical radius, reversed-depth "
                     "scale, partial/annular/full eclipses and unchanged geometry\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
