#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace infinite {
PlanetClimate planetClimate(const Node& planet) {
    PlanetClimate c;
    c.seed = featureSeed(planet.seed, Feature::PlanetClimate, climateRecipeVersion);
    c.solid = planet.address.kind() == Kind::Planet && hasSolidSurface(planet);
    c.temperatureK = planet.environment.temperature;
    if (!c.solid)
        return c;
    const Random r{c.seed};
    const double radius = std::pow(10., planet.frame.logRadius);
    c.radiusMetres = radius;
    c.gravity = 4.188790204786391 * 6.67430e-11 * material(planet.environment.mineral).density * radius;
    const double escape = std::sqrt(2 * c.gravity * radius);
    // Formation pressure is NOT atmospheric pressure. Retention is an independent
    // proxy based on escape energy, thermal energy and inherited volatile inventory.
    const double retention =
        smoothstep(1400., 6500., escape / std::sqrt(std::max(60., c.temperatureK) / 280.));
    c.moisture = std::clamp(planet.environment.waterActivity, 0., 1.);
    c.pressureBar = retention * (.08 + 1.5 * c.moisture + .25 * planet.environment.chemistry.carbon) *
                    (1 - smoothstep(650., 1050., c.temperatureK)) * smoothstep(45., 110., c.temperatureK);
    if (c.pressureBar < .015)
        c.pressureBar = 0;
    c.daySeconds = 36000 + r.unit(0) * 140000;
    c.axis = tangentFrame(glm::normalize(planet.geology->plates[0].angularVelocity)) * glm::dvec3(0, 1, 0);
    c.scaleHeightMetres = std::clamp(287. * c.temperatureK / std::max(.15, c.gravity), 500., 35000.);
    c.cloudHeightMetres = std::clamp(radius * .012 + c.scaleHeightMetres * .6, 2000., 65000.);
    c.windMetresPerSecond = c.pressureBar == 0 ? 0 : (4 + 28 * r.unit(1)) * std::sqrt(retention);
    // "Temperate" is a gameplay biosphere proxy, not a claim that temperature
    // alone establishes oxygen or life. Only these worlds permit open cities.
    c.temperate = c.temperatureK > 265 && c.temperatureK < 310 && c.pressureBar > .5 && c.pressureBar < 1.8 &&
                  c.moisture > .35 && planet.environment.ageGyr > 1.5 && c.gravity < 25 &&
                  planet.environment.chemistry.silicate > .25;
    return c;
}
WeatherSample samplePlanningClimate(const PlanetClimate& c, glm::dvec3 direction, double altitude,
                                    double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12 || !std::isfinite(altitude) ||
        !std::isfinite(glm::length(direction)) || glm::length(direction) < 1e-20)
        throw std::invalid_argument("Invalid planetary weather query");
    WeatherSample s;
    const auto d = glm::normalize(direction);
    const double latitude = glm::dot(d, c.axis);
    const double phase = seconds / c.daySeconds;
    const auto q = glm::angleAxis(-std::fmod(phase, 1.) * 2 * std::numbers::pi, c.axis) * d;
    const double front = fractalNoise(q * 5., c.seed, 3);
    const double eddy =
        valueNoise(q * 19. + glm::dvec3(std::sin(phase * .12), std::cos(phase * .12), std::sin(phase * .073)),
                   mix64(c.seed ^ 1));
    s.temperatureK = std::max(30., c.temperatureK + 30 * (.333333 - latitude * latitude) -
                                       std::clamp(altitude, 0., 12000.) * .004 + front * 4);
    s.pressureBar = c.pressureBar * std::exp(-std::max(0., altitude) / std::max(1., c.scaleHeightMetres));
    if (!c.solid || c.pressureBar == 0)
        return s;
    if (s.pressureBar < 1e-5)
        return s;
    const double bands = std::cos(latitude * 3 * std::numbers::pi);
    s.cloud = smoothstep(-.28, .45, front * .8 + eddy * .25 + c.moisture * .65 + bands * .1 - .30);
    // A pressure-dependent boiling proxy prevents water weather on hot,
    // low-pressure worlds even when their formation inventory was hydrated.
    const double boilingK = 373.15 + 27. * std::log(std::max(.006, s.pressureBar));
    s.cloud *= 1 - smoothstep(boilingK - 8, boilingK + 8, s.temperatureK);
    s.storm = smoothstep(.48, .91, s.cloud) * smoothstep(-.1, .4, eddy + front);
    const auto east = glm::cross(c.axis, d);
    const auto meridian = c.axis - d * latitude;
    s.wind =
        (east * (bands + .3) + meridian * (front * .6 + eddy * .2)) * c.windMetresPerSecond * (1 + s.storm);
    const double dustFront = (1 - smoothstep(.08, .20, c.moisture)) *
                             smoothstep(.02, .27, eddy + std::abs(front) * .5) *
                             smoothstep(5., 18., glm::length(s.wind));
    s.storm = std::max(s.storm, dustFront * .85);
    s.precipitation = c.moisture * smoothstep(.5, .95, s.cloud) * (1 + s.storm);
    const double belowCloud = 1 - smoothstep(c.cloudHeightMetres * .7, c.cloudHeightMetres, altitude);
    s.precipitation *= belowCloud;
    s.type = s.cloud > .35 ? WeatherType::Cloud : WeatherType::Clear;
    if (s.precipitation > .09)
        s.type = s.temperatureK < 273.15 ? WeatherType::Snow : WeatherType::Rain;
    else if (c.moisture < .18 && s.storm > .08 && glm::length(s.wind) > 8) {
        s.type = WeatherType::Dust;
        s.precipitation = std::clamp(s.storm * 2, 0., 1.) * belowCloud;
    }
    s.visibilityMetres = 120000. / (1 + 55 * s.precipitation + 8 * s.storm);
    s.wind *= std::sqrt(s.pressureBar / c.pressureBar);
    return s;
}
WeatherSample sampleWeather(const PlanetClimate& c, glm::dvec3 direction, double altitude, double seconds) {
    return WeatherSampler(c, seconds).sample(direction, altitude);
}
WeatherLayers weatherLayers(const PlanetClimate& c) {
    if (!c.solid || c.pressureBar == 0)
        return {};
    // A lifting-condensation-height proxy: moist air forms lower banks;
    // dry air needs more lift. Thickness follows the retained scale height.
    const double base =
        std::min(std::clamp((1 - c.moisture) * 3200. + 500., 500., 6500.), c.scaleHeightMetres * 1.4);
    const double depth = std::clamp(c.scaleHeightMetres * .70, 600., 11000.);
    return {base, std::min(base + depth, c.scaleHeightMetres * 4.), std::clamp(base * .8, 400., 3500.)};
}
double weatherDischarge(const PlanetClimate& c, double seconds) {
    if (!c.solid || c.pressureBar == 0 || c.moisture < .25 || c.temperatureK < 250)
        return 0;
    const Random r{c.seed};
    const double cycle = seconds / (9 + r.unit(71) * 13) + r.unit(72);
    const double primary = smoothstep(.982, 1., std::cos(cycle * 2 * std::numbers::pi));
    const double echo = smoothstep(.994, 1., std::cos((cycle - .026) * 2 * std::numbers::pi));
    return std::max(primary * primary, echo * echo * .65);
}
WeatherSampler::WeatherSampler(const PlanetClimate& c, double seconds) : climate_(c), seconds_(seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::invalid_argument("Invalid planetary weather time");
    const double radius = std::max(10000., c.radiusMetres);
    evolution_ = seconds / std::max(14400., radius / std::max(40., c.windMetresPerSecond * 20.));
    const Random r{mix64(c.seed ^ 0x77656174686572ULL ^ (uint64_t(weatherRecipeVersion) << 32))};
    const auto axis = glm::normalize(c.axis);
    const auto frame = tangentFrame(axis);
    const double omega = 2 * std::numbers::pi / std::max(1000., c.daySeconds);
    const double deformation = std::sqrt(std::max(1., c.gravity * c.scaleHeightMetres)) / (omega * radius);
    for (unsigned i = 0; i < cyclones_.size(); ++i) {
        auto& cyclone = cyclones_[i];
        cyclone.hemisphere = i < 2 ? 1. : -1.;
        const double latitude = cyclone.hemisphere * (.30 + r.unit(i * 5) * .65);
        const double drift = seconds * c.windMetresPerSecond / radius * (.45 + r.unit(i * 5 + 1));
        const double longitude =
            std::fmod(r.unit(i * 5 + 2) * 2 * std::numbers::pi + drift, 2 * std::numbers::pi);
        cyclone.centre = frame * glm::dvec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                            std::cos(latitude) * std::sin(longitude));
        cyclone.east = glm::normalize(glm::cross(axis, cyclone.centre));
        cyclone.north = glm::cross(cyclone.centre, cyclone.east);
        cyclone.radius = std::clamp(deformation * (.15 + r.unit(i * 5 + 3) * .12), .065, .25);
        const double lifetime = 43200. + r.unit(i * 5 + 4) * 86400.;
        const double cycle = .5 + .5 * std::sin(seconds * 2 * std::numbers::pi / lifetime + i * 2.4);
        cyclone.strength = (.25 + .75 * cycle * cycle) * smoothstep(.05, .55, c.moisture);
    }
}
WeatherSample WeatherSampler::sample(glm::dvec3 direction, double altitude) const {
    if (!std::isfinite(altitude) || !std::isfinite(glm::length(direction)) || glm::length(direction) < 1e-20)
        throw std::invalid_argument("Invalid planetary weather query");
    const auto& c = climate_;
    WeatherSample s;
    const auto d = glm::normalize(direction), axis = glm::normalize(c.axis);
    const double latitude = std::asin(std::clamp(glm::dot(d, axis), -1., 1.));
    s.temperatureK = std::max(30., c.temperatureK + 30 * (.333333 - std::pow(glm::dot(d, axis), 2)) -
                                       std::clamp(altitude, 0., 16000.) * .004);
    s.pressureBar = c.pressureBar * std::exp(-std::max(0., altitude) / std::max(1., c.scaleHeightMetres));
    if (!c.solid || c.pressureBar == 0 || s.pressureBar < 1e-5)
        return s;
    // Advect in the body's axes at wind speed, rather than moving every front
    // around an entire planet once per day. Latitude shear forms jet fronts.
    const double zonal = .45 - 1.15 * std::cos(latitude * 3.);
    const double drift = seconds_ * c.windMetresPerSecond / std::max(10000., c.radiusMetres);
    const auto q = glm::angleAxis(-std::fmod(drift * zonal, 2 * std::numbers::pi), axis) * d;
    const glm::dvec3 evolution(std::sin(evolution_ * .31), std::cos(evolution_ * .23) - 1,
                               std::sin(evolution_ * .17));
    const double pressure = fractalNoise(q * 5. + evolution * .5, c.seed, 3);
    const double eddy = valueNoise(q * 19. + evolution, mix64(c.seed ^ 1));
    const double tropicalLift = std::exp(-latitude * latitude * 13.);
    const double subsidence = std::exp(-std::pow((std::abs(latitude) - .50) / .19, 2));
    const double jet = std::exp(-std::pow((std::abs(latitude + pressure * .26) - .92) / .24, 2));
    s.front = jet * std::exp(-std::pow(pressure + eddy * .12, 2) * 30.);
    s.convection = c.moisture * smoothstep(245., 305., s.temperatureK) *
                   smoothstep(-.15, .42, eddy + pressure * .45) * (.30 + tropicalLift * .70);
    s.temperatureK += pressure * 4.;
    const double boilingK = 373.15 + 27. * std::log(std::max(.006, s.pressureBar));
    const double condensate =
        (1 - smoothstep(boilingK - 8, boilingK + 8, s.temperatureK)) * smoothstep(105., 165., s.temperatureK);
    s.cloud = smoothstep(-.25, .44,
                         pressure * .65 + eddy * .20 + c.moisture * .78 + tropicalLift * .15 + s.front * .24 -
                             subsidence * .16 - .30);
    const auto east = glm::cross(axis, d), meridian = axis - d * glm::dot(axis, d);
    s.wind =
        (east * zonal + meridian * (-std::sin(latitude * 6.) * .25 + pressure * .35)) * c.windMetresPerSecond;
    double spiralStorm = 0;
    for (const auto& cyclone : cyclones_) {
        const double separation =
            std::atan2(glm::length(glm::cross(d, cyclone.centre)), glm::dot(d, cyclone.centre));
        const double r = separation / cyclone.radius;
        if (r > 3.)
            continue;
        const double angle = std::atan2(glm::dot(d, cyclone.north), glm::dot(d, cyclone.east));
        const double envelope = std::exp(-r * r * .72) * (1 - smoothstep(2.4, 3., r));
        const double arms = .5 + .5 * std::sin(angle * 2 * cyclone.hemisphere - r * 8.);
        const double eye = smoothstep(.13, .40, r);
        const double band = envelope * (.38 + arms * .62) * eye * cyclone.strength;
        s.cloud = std::max(s.cloud, band * 1.25);
        // A calm, clearer eye is inside a moisture-fed rotating rain band.
        s.cloud *= 1 - (1 - eye) * envelope * cyclone.strength * .68;
        spiralStorm = std::max(spiralStorm, band);
        const auto swirl = glm::cross(cyclone.centre, d);
        const auto inward = cyclone.centre - d * glm::dot(cyclone.centre, d);
        const double norm = std::max(1e-8, glm::length(swirl));
        s.wind += (swirl * (cyclone.hemisphere / norm) + inward / norm * .18) * c.windMetresPerSecond *
                  envelope * eye * cyclone.strength * 1.7;
    }
    s.cloud = std::clamp(s.cloud * condensate * smoothstep(.005, .14, c.moisture), 0., 1.);
    s.storm = std::clamp(smoothstep(.50, .94, s.cloud) *
                             (.18 + s.convection * .70 + s.front * .50 + spiralStorm * .90),
                         0., 1.);
    const double wind = glm::length(s.wind);
    const double entrainment = (1 - smoothstep(.08, .20, c.moisture)) * smoothstep(5., 16., wind) *
                               smoothstep(-.10, .30, eddy + std::abs(pressure) * .6);
    s.dust = std::clamp(entrainment, 0., 1.);
    s.storm = std::max(s.storm, s.dust * .85);
    const auto layers = weatherLayers(c);
    const double belowCloud = 1 - smoothstep(layers.cloudBaseMetres * .8, layers.cloudBaseMetres, altitude);
    s.precipitation =
        std::clamp(c.moisture * smoothstep(.48, .94, s.cloud) * (.55 + s.storm * .8), 0., 1.) * belowCloud;
    s.type = s.cloud > .35 ? WeatherType::Cloud : WeatherType::Clear;
    if (s.precipitation > .09)
        s.type = s.temperatureK < 273.15 ? WeatherType::Snow
                 : s.storm > .60         ? WeatherType::Thunderstorm
                                         : WeatherType::Rain;
    else if (s.dust > .08 && wind > 8) {
        s.type = WeatherType::Dust;
        s.precipitation = s.dust * belowCloud;
    }
    const double inside = smoothstep(layers.cloudBaseMetres - 300., layers.cloudBaseMetres + 300., altitude) *
                          (1 - smoothstep(layers.cloudTopMetres * .8, layers.cloudTopMetres, altitude));
    s.visibilityMetres =
        std::clamp(120000. / (1 + 48 * s.precipitation + 80 * s.dust + 5 * s.storm + 450 * inside * s.cloud),
                   150., 120000.);
    s.wind *= std::sqrt(s.pressureBar / c.pressureBar);
    s.wind *= std::min(1., 120. / std::max(1., glm::length(s.wind)));
    s.gustMetresPerSecond = glm::length(s.wind) * (1.15 + s.storm * .55 + s.dust * .25);
    return s;
}
std::string_view weatherName(WeatherType type) {
    switch (type) {
    case WeatherType::Clear:
        return "CLEAR";
    case WeatherType::Cloud:
        return "CLOUD";
    case WeatherType::Rain:
        return "RAIN";
    case WeatherType::Snow:
        return "SNOW";
    case WeatherType::Dust:
        return "DUST";
    case WeatherType::Thunderstorm:
        return "THUNDER";
    default:
        return "VACUUM";
    }
}
} // namespace infinite
