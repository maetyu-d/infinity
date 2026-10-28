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
WeatherSample sampleWeather(const PlanetClimate& c, glm::dvec3 direction, double altitude, double seconds) {
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
    default:
        return "VACUUM";
    }
}
} // namespace infinite
