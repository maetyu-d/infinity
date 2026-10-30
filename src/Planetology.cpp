#include "infinite/Planetology.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace infinite {
GiantFormation giantFormation(uint64_t seed, unsigned slot) {
    if (slot >= giantPopulationCount)
        throw std::invalid_argument("Invalid giant formation slot");
    const Random r{featureSeed(seed, Feature::PlanetEnvelope, planetDiversityRecipeVersion)};
    const double logRadius = slot == 1   ? 7.34 + .18 * r.unit(0)
                             : slot == 3 ? 7.05 + .24 * r.unit(0)
                                         : 7.74 + .18 * r.unit(0);
    const double density = slot == 1   ? 1200 + 750 * r.unit(1)
                           : slot == 3 ? 1700 + 800 * r.unit(1)
                                       : 550 + 700 * r.unit(1);
    return {std::pow(10., logRadius), density};
}
double planetDensity(const Node& p) {
    return p.bulkDensity > 0 ? p.bulkDensity : material(p.environment.mineral).density;
}
PlanetTraits planetTraits(const Node& p) {
    PlanetTraits t;
    if (!p.geology)
        return t;
    t.seed = featureSeed(p.planetSeed ? p.planetSeed : p.seed, Feature::PlanetAppearance,
                         planetDiversityRecipeVersion);
    const Random r{t.seed};
    const double temperature = p.environment.temperature;
    const double radius =
        std::pow(10., p.address.kind() == Kind::Planet ? p.frame.logRadius : p.planetLogRadius);
    t.phase = r.unit(0);
    const auto axis = p.geology->plates[0].angularVelocity;
    t.axis = glm::length(axis) > 1e-12 ? glm::normalize(axis) : glm::dvec3(0, 1, 0);
    t.thermal = temperature < 100    ? ThermalType::Frozen
                : temperature < 240  ? ThermalType::Cold
                : temperature < 310  ? ThermalType::Temperate
                : temperature < 500  ? ThermalType::Warm
                : temperature < 1100 ? ThermalType::Hot
                                     : ThermalType::Molten;
    if (p.geology->type == GeologyType::GasEnvelope) {
        t.thermal = temperature < 240   ? ThermalType::Cold
                    : temperature < 500 ? ThermalType::Warm
                                        : ThermalType::Hot;
        t.type = radius < 2.05e7 ? PlanetType::SubNeptune
                 : radius < 4e7  ? PlanetType::IceGiant
                                 : PlanetType::GasGiant;
        t.haze = t.type == PlanetType::IceGiant     ? .48 + r.unit(1) * .42
                 : t.type == PlanetType::SubNeptune ? .35 + r.unit(1) * .5
                                                    : .08 + r.unit(1) * .22;
        t.jetBands =
            t.type == PlanetType::GasGiant ? 8 + unsigned(r.bits(2) % 9) : 4 + unsigned(r.bits(2) % 6);
        t.bandContrast = (1 - t.haze) * (.48 + .35 * r.unit(3));
        if (t.type == PlanetType::GasGiant) {
            t.low = glm::mix(glm::vec3(.30, .11, .055), glm::vec3(.18, .21, .27),
                             float(smoothstep(500., 1400., temperature)));
            t.high = glm::mix(glm::vec3(.90, .73, .44), glm::vec3(.56, .35, .24),
                              float(smoothstep(500., 1400., temperature)));
        } else {
            t.low = glm::mix(glm::vec3(.035, .22, .32), glm::vec3(.16, .16, .29), float(r.unit(4) * .65));
            t.high = glm::mix(glm::vec3(.40, .76, .78), glm::vec3(.53, .59, .72), float(r.unit(5) * .6));
        }
        const Random rr{featureSeed(p.seed, Feature::PlanetRings, planetDiversityRecipeVersion)};
        const double rho = planetDensity(p), ice = 1 - smoothstep(140., 210., temperature);
        const double particleDensity = glm::mix(2900., 917., ice);
        const double roche = 2.456 * std::cbrt(rho / particleDensity);
        const double mass = 4.188790204786391 * rho * radius * radius * radius;
        const double hill = p.orbit.semiMajorMetres * (1 - p.orbit.eccentricity) *
                            std::cbrt(mass / (3 * std::max(1e-12, p.orbit.centralMassSolar) * 1.98847e30));
        t.rings = {rr.key, 1.18 + .16 * rr.unit(0), std::min(roche * .94, hill / radius * .24), 0, ice,
                   t.axis};
        if (t.rings.outerRadius > t.rings.innerRadius * 1.12 && (ice > .1 || rr.unit(1) < .25))
            t.rings.opticalDepth = (t.type == PlanetType::GasGiant ? .32 : .07) * (.7 + 1.2 * rr.unit(2));
        return t;
    }
    const auto climate = planetClimate(p);
    t.type = temperature > 1100                                         ? PlanetType::LavaWorld
             : p.geology->type == GeologyType::IceShell                 ? PlanetType::IceWorld
             : p.geology->liquidWater > .20 && climate.pressureBar > .1 ? PlanetType::Oceanic
             : p.environment.chemistry.metal > .48                      ? PlanetType::IronRich
             : temperature > 330 || p.environment.waterActivity < .18   ? PlanetType::Desert
                                                                        : PlanetType::Rocky;
    switch (t.type) {
    case PlanetType::LavaWorld:
        t.low = {.045, .025, .032};
        t.high = {.65, .17, .035};
        break;
    case PlanetType::IceWorld:
        t.low = {.08, .22, .31};
        t.high = {.72, .84, .85};
        break;
    case PlanetType::Oceanic:
        t.low = {.025, .12, .20};
        t.high = {.38, .46, .22};
        break;
    case PlanetType::IronRich:
        t.low = {.18, .065, .045};
        t.high = {.49, .27, .13};
        break;
    case PlanetType::Desert:
        t.low = {.25, .13, .065};
        t.high = {.68, .46, .25};
        break;
    default:
        t.low = {.12, .14, .15};
        t.high = glm::mix(glm::vec3(.38, .40, .38), glm::vec3(.44, .29, .19), float(r.unit(6)));
        break;
    }
    t.iceCoverage =
        std::clamp(p.environment.chemistry.ice * (1 - smoothstep(200., 275., temperature)) * 1.4, 0., 1.);
    return t;
}
glm::vec3 giantAlbedo(const PlanetTraits& t, glm::dvec3 d) {
    d = glm::normalize(d);
    const double lat = glm::dot(d, t.axis);
    const double warp = std::sin(d.x * 7 + t.phase * 6.283) * .55 + std::sin(d.z * 11 + lat * 8) * .28;
    const auto east = glm::normalize(glm::cross(t.axis, glm::dvec3(.13, .71, .43))),
               north = glm::cross(t.axis, east);
    double belt = lat * t.jetBands * std::numbers::pi + warp, storm = 0, spiral = 0;
    for (unsigned i = 0; i < 2; i++) {
        const double latitude = (i == 0 ? 1. : -1.) * (.22 + t.phase * .30),
                     longitude = t.phase * 6.283 + i * 2.7;
        const auto centre = t.axis * std::sin(latitude) +
                            (east * std::cos(longitude) + north * std::sin(longitude)) * std::cos(latitude);
        const auto tangent = glm::normalize(glm::cross(t.axis, centre)),
                   meridian = glm::cross(centre, tangent);
        const glm::dvec2 q(glm::dot(d - centre, tangent) / .22, glm::dot(d - centre, meridian) / .11);
        const double core = std::exp(-glm::dot(q, q) * 1.6) * smoothstep(.1, .8, glm::dot(d, centre));
        const double arms = .5 + .5 * std::sin(std::atan2(q.y, q.x) * 2 - std::log(glm::dot(q, q) + .04) * 4);
        belt += core * arms * 4;
        storm = std::max(storm, core);
        spiral = std::max(spiral, core * arms);
    }
    const double belts = .5 + .5 * std::sin(belt);
    const auto base = glm::mix(t.low, t.high, float(glm::mix(.5, belts, t.bandContrast)));
    return glm::mix(base, t.low * .55f + t.high * float(spiral * .75), float(storm * .65));
}
double ringOpticalDepth(const RingProcess& r, double radius) {
    if (!r.present() || radius <= r.innerRadius || radius >= r.outerRadius)
        return 0;
    const double u = (radius - r.innerRadius) / (r.outerRadius - r.innerRadius),
                 phase = double(r.seed % 10000) / 10000.;
    const double edge = smoothstep(0., .035, u) * (1 - smoothstep(.965, 1., u));
    const double gap = 1 - smoothstep(.014, .022, std::abs(u - (.54 + phase * .12)));
    const double narrow = 1 - smoothstep(.004, .010, std::abs(u - (.27 + phase * .07)));
    const double bands = .48 + .30 * std::sin(u * 91 + phase * 6.283) + .16 * std::sin(u * 263 + phase * 13.);
    return r.opticalDepth * edge * (1 - gap * .97) * (1 - narrow * .82) * std::max(.04, bands);
}
std::string_view planetTypeName(PlanetType t) {
    constexpr std::array names = {"Rocky",      "Iron-rich", "Desert",    "Oceanic",    "Ice world",
                                  "Lava world", "Gas giant", "Ice giant", "Sub-Neptune"};
    return names.at(size_t(t));
}
std::string_view thermalTypeName(ThermalType t) {
    constexpr std::array names = {"Frozen", "Cold", "Temperate", "Warm", "Hot", "Molten"};
    return names.at(size_t(t));
}
} // namespace infinite
