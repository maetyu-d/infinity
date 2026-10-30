#include "infinite/WeatherField.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace infinite;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        Universe u;
        std::optional<Node> air, vacuum;
        for (unsigned star = 0; star < 256 && (!air || !vacuum); ++star)
            for (auto a :
                 u.children(u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star)))) {
                auto p = u.describe(a);
                const auto c = planetClimate(p);
                if (c.pressureBar > .5 && !air)
                    air = p;
                if (c.solid && c.pressureBar == 0 && !vacuum)
                    vacuum = p;
            }
        check(air && vacuum, "Missing real optical weather fixtures");
        const auto identity = descriptorFingerprint(*air);
        std::atomic<bool> cancel{true};
        check(bakeWeatherField(*air, 12, &cancel).pixels.empty(), "Cancelled weather field was published");
        check(bakeWeatherField(*vacuum, 12).pixels.empty(), "Airless body acquired optical weather");
        const auto a = bakeWeatherField(*air, 12), b = bakeWeatherField(*air, 12);
        check(a.pixels == b.pixels && a.pixels.size() == WeatherField::byteBudget,
              "Weather atlas changed seed/order or exceeded its mip budget");
        const auto climate = planetClimate(*air);
        for (unsigned y = 0; y < WeatherField::height; y += 7)
            for (unsigned x = 0; x < WeatherField::width; x += 11) {
                const double latitude = std::numbers::pi * (.5 - double(y) / (WeatherField::height - 1));
                const double longitude = 2 * std::numbers::pi * ((x + .5) / WeatherField::width - .5);
                const auto d = y == 0
                                   ? glm::dvec3(0, 1, 0)
                                   : glm::dvec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                                std::cos(latitude) * std::sin(longitude));
                const auto weather = sampleWeather(climate, d, 0, 12);
                const size_t i = (y * WeatherField::width + x) * 4;
                unsigned channel = 0;
                for (double value : {weather.cloud, weather.storm, weather.precipitation, weather.dust})
                    check(std::abs(a.pixels[i + channel++] / 255. - std::clamp(value, 0., 1.)) <=
                              .5 / 255. + 1e-12,
                          "Optical field replaced the existing weather");
            }
        for (unsigned x = 1; x < WeatherField::width; ++x)
            for (unsigned c = 0; c < 4; ++c) {
                check(a.pixels[x * 4 + c] == a.pixels[c], "North pole has a longitude seam");
                check(a.pixels[((WeatherField::height - 1) * WeatherField::width + x) * 4 + c] ==
                          a.pixels[(WeatherField::height - 1) * WeatherField::width * 4 + c],
                      "South pole has a longitude seam");
            }
        WeatherFieldCache cache;
        for (unsigned pair = 0; pair < 4; ++pair) {
            cache.beginFrame();
            cache.request(*air, pair * 16);
            cache.request(*air, pair * 16 + 8);
            check(cache.stats().pending <= 2, "Weather jobs became unbounded");
            cache.wait();
            check(cache.stats().entries <= 4 && cache.stats().bytes <= 4 * WeatherField::byteBudget,
                  "Weather cache exceeded its cap");
            check(bool(cache.request(*air, pair * 16)), "Completed optical weather was not retained");
        }
        check(cache.stats().evicted >= 4, "Unused weather snapshots were not evicted");
        for (unsigned i = 0; i < 301; ++i)
            cache.beginFrame();
        check(cache.stats().bytes == 0 && cache.stats().entries == 0,
              "Expired weather snapshots retained memory");
        bool rejected = false;
        try {
            cache.request(*air, std::nan(""));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "Invalid weather time was accepted");
        check(descriptorFingerprint(u.describe(air->address)) == identity,
              "Optical weather changed the source planet");
        for (auto s : {glm::ivec2(800, 500), glm::ivec2(601, 1001), glm::ivec2(1920, 1200),
                       glm::ivec2(3840, 2160), glm::ivec2(7680, 4320)}) {
            const auto q = cloudBufferSize(s.x, s.y);
            check(q.x > 0 && q.y > 0 && q.x <= 960 && q.y <= 600 && q.x <= s.x && q.y <= s.y,
                  "Cloud framebuffer is unbounded");
            check(std::abs(double(q.x) / q.y - double(s.x) / s.y) < .01,
                  "Cloud framebuffer distorted the projection");
        }
        std::cout << "PASS: inherited weather atlas, deterministic mips, pole continuity, cancellation, "
                     "bounded async cache and adaptive cloud resolution\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
