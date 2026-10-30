#include "infinite/Navigation.hpp"
#include "infinite/Presentation.hpp"
#include "infinite/StellarEvolution.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace infinite {
double stellarDiscVisibility(glm::dvec3 direction, double radius, double distance,
                             std::span<const CelestialContact> bodies) {
    if (!(radius > 0) || !(distance > 0) || glm::length(direction) < 1e-30)
        return 0;
    direction = glm::normalize(direction);
    double visible = 1;
    for (const auto& body : bodies) {
        if (body.distanceMetres <= 0 || body.distanceMetres >= distance || body.angularRadius <= 0)
            continue;
        const double r = body.angularRadius,
                     d = std::atan2(glm::length(glm::cross(direction, body.localDirection)),
                                    glm::dot(direction, body.localDirection));
        if (d >= radius + r)
            continue;
        double area;
        if (d + radius <= r)
            return 0;
        if (d + r <= radius)
            area = std::numbers::pi * r * r;
        else {
            const double a =
                std::acos(std::clamp((d * d + radius * radius - r * r) / (2 * d * radius), -1., 1.));
            const double b = std::acos(std::clamp((d * d + r * r - radius * radius) / (2 * d * r), -1., 1.));
            const double radical = (-d + radius + r) * (d + radius - r) * (d - radius + r) * (d + radius + r);
            area = radius * radius * a + r * r * b - .5 * std::sqrt(std::max(0., radical));
        }
        visible = std::min(visible, std::clamp(1 - area / (std::numbers::pi * radius * radius), 0., 1.));
    }
    return visible;
}
StellarImage extractStellarImage(const Universe& universe, const Navigation& nav,
                                 std::span<const CelestialContact> bodies) {
    StellarImage image;
    if (nav.anchor.kind() > Kind::Mountain)
        return image;
    const auto lineage = universe.lineage(nav.anchor);
    const auto star = std::find_if(lineage.begin(), lineage.end(),
                                   [](const Node& n) { return n.address.kind() == Kind::System; });
    if (star == lineage.end())
        return image;
    const auto delta =
        universe.relative(star->address, nav.anchor, nav.logDistance).position - nav.cameraPosition();
    const double units = std::pow(10., nav.logDistance), distance = glm::length(delta),
                 radius = star->stellarRadiusMetres / units;
    if (distance <= radius * 1.001)
        return image;
    const auto localDirection = delta / distance;
    image.address = star->address;
    image.direction = glm::inverse(nav.orientation) * localDirection;
    image.distanceMetres = distance * units;
    image.angularRadius = std::asin(std::clamp(radius / distance, 0., 1.));
    image.color = stellarRadiance(*star);
    image.starType = stellarPresentation(*star);
    image.cameraToStar =
        glm::inverse(universe.relative(star->address, nav.anchor, nav.logDistance).orientation) *
        nav.orientation;
    image.nearDepthUnits = std::max(0., -image.direction.z) * (distance - radius);
    image.visibility =
        image.direction.z < 0
            ? float(stellarDiscVisibility(image.direction, image.angularRadius, image.distanceMetres, bodies))
            : 0;
    return image;
}
} // namespace infinite
