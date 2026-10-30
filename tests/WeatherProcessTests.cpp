#include "infinite/WeatherField.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <set>
#include <stdexcept>
using namespace infinite;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static glm::dvec3 direction(unsigned i) {
    const double y = 1 - 2 * (i + .5) / 2048., a = i * 2.399963229728653;
    return {std::sqrt(1 - y * y) * std::cos(a), y, std::sqrt(1 - y * y) * std::sin(a)};
}
int main() {
    try {
        PlanetClimate c;
        c.seed = 3728941;
        c.solid = true;
        c.temperatureK = 292;
        c.pressureBar = 1;
        c.moisture = .85;
        c.gravity = 9.8;
        c.scaleHeightMetres = 8500;
        c.windMetresPerSecond = 25;
        c.axis = glm::normalize(glm::dvec3(.2, 1, .3));
        const WeatherSampler now(c, 12), later(c, 43212), repeat(c, 12);
        unsigned evolved = 0, rain = 0, storm = 0;
        double low = 1, high = 0, windMax = 0;
        for (unsigned i = 0; i < 2048; ++i) {
            const auto d = direction(i);
            const auto a = now.sample(d, 0), b = repeat.sample(d, 0), next = later.sample(d, 0);
            check(a.cloud == b.cloud && a.wind == b.wind && a.storm == b.storm,
                  "Weather depends on sample order");
            check(a.cloud >= 0 && a.cloud <= 1 && a.storm >= 0 && a.storm <= 1 && a.precipitation >= 0 &&
                      a.precipitation <= 1,
                  "Weather intensity escaped its physical bounds");
            check(std::abs(glm::dot(a.wind, d)) < 1e-10, "Circulation left the planetary tangent plane");
            check(a.gustMetresPerSecond >= glm::length(a.wind) && a.visibilityMetres >= 150,
                  "Invalid gust or visibility");
            check(std::abs(now.sample(glm::normalize(d + glm::dvec3(1e-9)), 0).cloud - a.cloud) < 1e-5,
                  "Spatial weather seam");
            check(std::abs(sampleWeather(c, d, 0, 12.001).cloud - a.cloud) < .001,
                  "Weather changes discontinuously with time");
            evolved += std::abs(a.cloud - next.cloud) > .05;
            rain += a.type == WeatherType::Rain || a.type == WeatherType::Thunderstorm;
            storm += a.storm > .5;
            low = std::min(low, a.cloud);
            high = std::max(high, a.cloud);
            windMax = std::max(windMax, glm::length(a.wind));
        }
        check(evolved > 200 && rain > 50 && storm > 5 && high - low > .5 && windMax > 25,
              "Live weather lacks evolving fronts, dry gaps, rain or rotating strong winds");
        auto dry = c;
        dry.moisture = 0;
        const WeatherSampler desert(dry, 12);
        unsigned dust = 0;
        for (unsigned i = 0; i < 2048; ++i) {
            const auto w = desert.sample(direction(i), 0);
            dust += w.type == WeatherType::Dust;
            check(w.cloud == 0, "Water clouds on a moisture-free planet");
        }
        check(dust > 10, "Dry circulation never entrains dust");
        auto cold = c;
        cold.temperatureK = 235;
        const WeatherSampler frozen(cold, 12);
        unsigned snow = 0;
        for (unsigned i = 0; i < 2048; ++i)
            snow += frozen.sample(direction(i), 0).type == WeatherType::Snow;
        check(snow > 20, "Cold, wet circulation never snows");
        const auto layers = weatherLayers(c), dryLayers = weatherLayers(dry);
        check(layers.cloudBaseMetres < dryLayers.cloudBaseMetres &&
                  layers.cloudTopMetres > layers.cloudBaseMetres &&
                  layers.cloudTopMetres <= c.scaleHeightMetres * 4,
              "Cloud layers ignore moisture or atmospheric height");
        const auto vacuum = now.sample({0, 1, 0}, 1e6);
        check(vacuum.type == WeatherType::Vacuum && vacuum.cloud == 0 && vacuum.wind == glm::dvec3(0),
              "Weather escaped into orbital vacuum");
        auto airless = c;
        airless.pressureBar = 0;
        check(sampleWeather(airless, {0, 1, 0}, 0, 12).precipitation == 0 &&
                  weatherDischarge(airless, 12) == 0,
              "Airless weather");
        double maxPulse = 0;
        unsigned flashing = 0;
        for (unsigned i = 0; i < 4000; ++i) {
            const double p = weatherDischarge(c, i * .025);
            check(p >= 0 && p <= 1, "Unbounded discharge");
            maxPulse = std::max(maxPulse, p);
            flashing += p > .1;
        }
        check(maxPulse > .9 && flashing < 600, "Lightning is missing or continuously flashes");
        std::cout << "PASS: evolving circulation, weather continuity, moist storms, dry dust, snow, gusts, "
                     "cloud heights and bounded lightning / evolved="
                  << evolved << " rain=" << rain << " storm=" << storm << " dust=" << dust << " snow=" << snow
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
