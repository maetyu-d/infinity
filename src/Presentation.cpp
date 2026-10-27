#include "infinite/Presentation.hpp"
#include "infinite/Navigation.hpp"
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
    auto kind = node.address.kind();
    float pigment = kind >= Kind::Rock ? .24f : .56f;
    float fibre = kind >= Kind::Rock ? .035f : .072f;
    if (kind == Kind::Lattice) {
        pigment = .035f;
        fibre = 0;
    }
    // Use an existing inherited key, never a new procedural random stream.
    uint64_t key = kind >= Kind::Rock ? (node.hostRockSeed ? node.hostRockSeed : node.seed)
                                      : (node.planetSeed ? node.planetSeed : node.seed);
    float phase = static_cast<float>(key % 10000) * .0371f;
    phase -= std::floor(phase);
    float strength = mode == RenderMode::Original ? 0.f : mode == RenderMode::Hybrid ? .24f : 1.f;
    auto materialFrame = node.address;
    if (kind >= Kind::Planet && kind <= Kind::Mountain)
        while (materialFrame.kind() != Kind::Planet)
            materialFrame = materialFrame.parent();
    else if (kind >= Kind::Rock)
        while (materialFrame.kind() != Kind::Rock)
            materialFrame = materialFrame.parent();
    return {geometry, materialFrame, {roughness, metal, pigment, fibre}, phase, strength};
}
glm::vec3 hyperionPigment(float phase) {
    return glm::pow(.5f + .5f * glm::cos(6.283185f * (phase + glm::vec3(0, .68f, .34f))), glm::vec3(1.35f));
}
glm::vec3 hyperionDisplay(glm::vec3 c) {
    c = glm::max(c * 1.30f, glm::vec3(0));
    float peak = std::max({c.r, c.g, c.b});
    float mapped = (peak * (2.51f * peak + .03f)) / (peak * (2.43f * peak + .59f) + .14f);
    auto film = (c * (2.51f * c + .03f)) / (c * (2.43f * c + .59f) + .14f);
    return glm::pow(
        glm::clamp(glm::mix(film, c / std::max(peak, .0001f) * mapped, .48f), glm::vec3(0), glm::vec3(1)),
        glm::vec3(1.f / 2.2f));
}
bool celestialOccluded(glm::dvec3 direction, double distance, std::span<const CelestialContact> contacts,
                       const Address* except) {
    for (const auto& body : contacts) {
        if ((except && body.address == *except) || body.distanceMetres >= distance || body.angularRadius <= 0)
            continue;
        if (glm::dot(direction, body.localDirection) > std::cos(body.angularRadius))
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
                            nav.selected && *nav.selected == node.address});
    };
    add(system);
    for (const auto& planet : planets) {
        if (planet.address.kind() != Kind::Planet || planet.address.parent() != system.address)
            throw std::invalid_argument("Foreign planet in celestial contacts");
        add(planet);
    }
    for (auto& contact : contacts)
        contact.occluded =
            celestialOccluded(contact.localDirection, contact.distanceMetres, contacts, &contact.address);
    return contacts;
}
} // namespace infinite
