#include "infinite/Presentation.hpp"
#include "infinite/Navigation.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace infinite {
std::string_view renderModeName(RenderMode mode) {
    switch (mode) {
    case RenderMode::Original:
        return "ORIGINAL";
    case RenderMode::Hyperion:
        return "HYPERION";
    case RenderMode::Hybrid:
        return "HYBRID";
    }
    throw std::invalid_argument("Invalid rendering mode");
}
RenderMode parseRenderMode(std::string_view name) {
    if (name == "original")
        return RenderMode::Original;
    if (name == "hyperion")
        return RenderMode::Hyperion;
    if (name == "hybrid")
        return RenderMode::Hybrid;
    throw std::invalid_argument("Use --render-mode original|hyperion|hybrid");
}
RenderMode nextRenderMode(RenderMode mode) {
    return static_cast<RenderMode>((static_cast<unsigned>(mode) + 1) % 3);
}
ScenePresentation extractPresentation(const Node& node, const Geometry& geometry, RenderMode mode) {
    float roughness = .78f, metal = 0;
    switch (node.environment.mineral) {
    case Mineral::Iron:
        roughness = .42f;
        metal = .75f;
        break;
    case Mineral::Ice:
        roughness = .25f;
        break;
    case Mineral::Quartz:
        roughness = .36f;
        break;
    case Mineral::Olivine:
        roughness = .66f;
        break;
    case Mineral::Graphite:
        roughness = .80f;
        break;
    }
    if (node.geology && node.geology->type == GeologyType::GasEnvelope) {
        roughness = .94f;
        metal = 0;
    }
    auto kind = node.address.kind();
    float pigment = kind >= Kind::Rock ? .68f : .90f;
    float fibre = kind >= Kind::Rock ? .13f : .28f;
    if (kind == Kind::Lattice) {
        pigment = .035f;
        fibre = 0;
    }
    // Use an existing inherited key, never a new procedural random stream.
    uint64_t key = kind >= Kind::Rock ? (node.hostRockSeed ? node.hostRockSeed : node.seed)
                                      : (node.planetSeed ? node.planetSeed : node.seed);
    const float phase = hyperionPhase(key);
    float strength = mode == RenderMode::Original ? 0.f : mode == RenderMode::Hybrid ? .24f : 1.f;
    auto materialFrame = node.address;
    if (kind >= Kind::Planet && kind <= Kind::Mountain)
        while (materialFrame.kind() != Kind::Planet)
            materialFrame = materialFrame.parent();
    else if (kind >= Kind::Rock)
        while (materialFrame.kind() != Kind::Rock)
            materialFrame = materialFrame.parent();
    const float domain = kind == Kind::System                             ? 6.f
                         : kind == Kind::Lattice                          ? 2.f
                         : kind >= Kind::Planet && kind <= Kind::Mountain ? 1.f
                                                                          : 0.f;
    ScenePresentation result{
        geometry,
        materialFrame,
        {roughness, metal, pigment, fibre},
        phase,
        strength,
        {domain,
         kind == Kind::System ? float(node.stellarRadiusMetres / std::pow(10., node.frame.logRadius))
                              : float(node.environment.strain),
         float(node.geology ? node.geology->activity : .5), float(node.environment.chemistry.ice)},
        {float(node.environment.mineral), float(node.environment.waterActivity),
         float(node.environment.temperature), geometry.section ? 1.f : 0.f},
        {kind == Kind::Planet ? float(planetClimate(node).pressureBar) : 0.f,
         float(node.geology ? node.geology->terrain.type : TerrainType::None),
         node.geology && node.geology->type == GeologyType::GasEnvelope ? 1.f : 0.f,
         float(node.geology ? node.geology->type : GeologyType::MobileLid)}};
    if (domain == 1.f)
        describePlanetPresentation(result, node);
    return result;
}
void describePlanetPresentation(ScenePresentation& result, const Node& node) {
    const auto t = planetTraits(node);
    result.planet = {glm::vec4(float(t.type) + 1, float(t.phase), float(t.jetBands), float(t.haze)),
                     glm::vec4(t.low, float(t.thermal)), glm::vec4(t.high, float(t.iceCoverage)),
                     glm::vec4(glm::vec3(t.axis), float(t.bandContrast))};
    result.planetRings = {glm::vec4(float(t.rings.innerRadius), float(t.rings.outerRadius),
                                    float(t.rings.opticalDepth), float(t.rings.seed % 10000) / 10000.f),
                          glm::vec4(glm::vec3(t.rings.axis), float(t.rings.iceFraction))};
}
float hyperionPhase(uint64_t key) {
    const float phase = float(key % 10000) * .0371f;
    return phase - std::floor(phase);
}
glm::vec3 hyperionPigment(float phase) {
    return glm::pow(.5f + .5f * glm::cos(6.283185f * (phase + glm::vec3(0, .68f, .34f))), glm::vec3(1.85f));
}
glm::vec3 hyperionDisplay(glm::vec3 c) {
    const float luminance = glm::dot(c, glm::vec3(.299f, .587f, .114f));
    c = glm::max(glm::mix(glm::vec3(luminance), c, 1.28f), glm::vec3(0));
    c = glm::max(c * 1.36f, glm::vec3(0));
    float peak = std::max({c.r, c.g, c.b});
    float mapped = (peak * (2.51f * peak + .03f)) / (peak * (2.43f * peak + .59f) + .14f);
    auto film = (c * (2.51f * c + .03f)) / (c * (2.43f * c + .59f) + .14f);
    return glm::pow(
        glm::clamp(glm::mix(film, c / std::max(peak, .0001f) * mapped, .86f), glm::vec3(0), glm::vec3(1)),
        glm::vec3(1.f / 2.2f));
}
SpectralSky extractSpectralSky(const Universe& universe, const Navigation& nav) {
    const auto lineage = universe.lineage(nav.anchor);
    const auto found = std::find_if(lineage.begin(), lineage.end(),
                                    [](const auto& n) { return n.address.kind() == Kind::Galaxy; });
    const auto& context = found == lineage.end() ? lineage.front() : *found;
    const auto pose = universe.relative(context.address, nav.anchor, nav.logDistance);
    SpectralSky sky;
    sky.cameraToField = glm::inverse(pose.orientation) * nav.orientation;
    // Use the universe's key outside a galaxy, so crossing a root-cell boundary
    // cannot change the distant optical material abruptly.
    const auto key = found == lineage.end() ? universe.seed() : context.seed;
    sky.phase = hyperionPhase(key);
    sky.gas = found == lineage.end() ? .12f : float(context.galaxy.gasFraction);
    const double radius = std::pow(10., context.frame.logRadius - nav.logDistance);
    sky.strength = float((found == lineage.end() ? .06 : smoothstep(3., 12., radius)) *
                         smoothstep(-2., 3., nav.logDistance));
    return sky;
}
PolarLight extractPolarLight(const Node& planet) {
    const auto climate = planetClimate(planet);
    PolarLight result;
    result.axis = climate.axis;
    if (!climate.solid || climate.pressureBar < .04)
        return result;
    const double radius = std::pow(10., planet.frame.logRadius);
    const double base = std::clamp(climate.scaleHeightMetres * 6., radius * .001, radius * .04);
    const double thickness = std::clamp(climate.scaleHeightMetres * 8., radius * .002, radius * .06);
    result.innerRadius = 1 + base / radius;
    result.outerRadius = result.innerRadius + thickness / radius;
    result.strength = smoothstep(.04, .30, climate.pressureBar) * std::min(1., climate.pressureBar);
    return result;
}
glm::dvec2 polarLightSpan(glm::dvec3 eye, glm::dvec3 ray, const PolarLight& light, double opaque) {
    if (light.strength <= 0)
        return {};
    ray = glm::normalize(ray);
    const auto sphere = [&](double radius) {
        const auto perpendicular = glm::cross(eye, ray);
        const double h = radius * radius - glm::dot(perpendicular, perpendicular);
        if (h < 0)
            return glm::dvec2(-1);
        const double b = glm::dot(eye, ray), root = std::sqrt(h);
        return glm::dvec2(-b - root, -b + root);
    };
    const auto outer = sphere(light.outerRadius), inner = sphere(light.innerRadius);
    double start = std::max(0., outer.x), end = std::min(outer.y, opaque);
    if (inner.x > 0)
        end = std::min(end, inner.x);
    else if (glm::length(eye) < light.innerRadius)
        start = std::max(start, inner.y);
    const auto ground = sphere(1.);
    if (ground.x > 0)
        end = std::min(end, ground.x);
    return end > start ? glm::dvec2(start, end) : glm::dvec2(0);
}
bool celestialOccluded(glm::dvec3 direction, double distance, std::span<const CelestialContact> contacts,
                       const Address* except, double targetAngularRadius) {
    for (const auto& body : contacts) {
        if ((except && body.address == *except) || body.distanceMetres >= distance || body.angularRadius <= 0)
            continue;
        const double mask = body.angularRadius - targetAngularRadius;
        if (mask > 0 && glm::dot(direction, body.localDirection) > std::cos(mask))
            return true;
    }
    return false;
}
double celestialCellWeight(glm::dvec3 centre, glm::dvec3 eye) {
    double weight = 1;
    for (unsigned axis = 0; axis < 3; ++axis)
        weight *= 1 - smoothstep(0., 2., std::abs(centre[axis] - eye[axis]));
    return weight;
}
std::vector<CelestialContact> extractCelestialContacts(const Universe& universe, const Navigation& nav,
                                                       const Node& system, std::span<const Node> planets) {
    if (system.address.kind() != Kind::System)
        throw std::invalid_argument("Celestial contacts require an existing system");
    std::vector<CelestialContact> contacts;
    contacts.reserve(planets.size() + 1);
    const auto inverse = glm::inverse(nav.orientation);
    const auto eye = nav.cameraPosition();
    const double units = std::pow(10., nav.logDistance);
    auto add = [&](const Node& node) {
        const auto delta = universe.relative(node.address, nav.anchor, nav.logDistance).position - eye;
        const double distance = glm::length(delta) * units;
        if (distance <= 0 || !std::isfinite(distance))
            return;
        const bool star = node.address.kind() == Kind::System;
        const double radius = star ? node.stellarRadiusMetres : std::pow(10., node.frame.logRadius);
        // Inside material the inspection section, rather than a whole celestial
        // sphere, supplies local occlusion. Do not mask the entire instrument.
        const double angular = distance > radius ? std::asin(std::min(1., radius / distance)) : 0;
        contacts.push_back({node.address, inverse * glm::normalize(delta), distance, angular,
                            star ? stellarColor(node.temperatureK) : material(node.environment.mineral).color,
                            star ? "STAR " + std::to_string(node.seed % 100000) : node.label(), false,
                            nav.selected && *nav.selected == node.address, isMoon(node.address)});
    };
    add(system);
    for (const auto& planet : planets) {
        if (planet.address.kind() != Kind::Planet || planet.address.parent() != system.address)
            throw std::invalid_argument("Foreign planet in celestial contacts");
        add(planet);
    }
    for (auto& contact : contacts)
        contact.occluded = celestialOccluded(contact.localDirection, contact.distanceMetres, contacts,
                                             &contact.address, contact.angularRadius);
    return contacts;
}
} // namespace infinite
