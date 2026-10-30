#include "infinite/WeatherField.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <vector>
using namespace infinite;
int main(int argc, char** argv) {
    try {
        const std::filesystem::path out = argc > 1 ? argv[1] : "weather-benchmark";
        std::filesystem::create_directories(out);
        Universe u;
        const auto planet =
            u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 47).child(Kind::Planet, 9));
        const auto climate = planetClimate(planet);
        std::vector<glm::dvec3> directions;
        for (unsigned y = 0; y < WeatherField::height; y++)
            for (unsigned x = 0; x < WeatherField::width; x++) {
                const double latitude = std::numbers::pi * (.5 - double(y) / (WeatherField::height - 1));
                const double longitude = 2 * std::numbers::pi * ((x + .5) / WeatherField::width - .5);
                directions.push_back(y == 0 ? glm::dvec3(0, 1, 0)
                                     : y + 1 == WeatherField::height
                                         ? glm::dvec3(0, -1, 0)
                                         : glm::dvec3(std::cos(latitude) * std::cos(longitude),
                                                      std::sin(latitude),
                                                      std::cos(latitude) * std::sin(longitude)));
            }
        double checksum = 0;
        std::vector<double> oldTimes, newTimes, bakeTimes;
        for (unsigned repeat = 0; repeat < 7; repeat++) {
            auto start = std::chrono::steady_clock::now();
            for (auto d : directions) {
                const auto s = samplePlanningClimate(climate, d, 0, 12);
                checksum += s.cloud + s.storm + s.precipitation;
            }
            oldTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            start = std::chrono::steady_clock::now();
            const WeatherSampler sampler(climate, 12);
            for (auto d : directions) {
                const auto s = sampler.sample(d, 0);
                checksum += s.cloud + s.storm + s.precipitation;
            }
            newTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            start = std::chrono::steady_clock::now();
            const auto field = bakeWeatherField(planet, 12);
            checksum += field.pixels.at(0);
            bakeTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        }
        const auto median = [](auto values) {
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        };
        std::ofstream metrics(out / "generation.json");
        metrics << "{\n  \"planet\": \"" << planet.address.str()
                << "\",\n  \"queries_per_repeat\": " << directions.size()
                << ",\n  \"repeats\": 7,\n  \"legacy_query_median_ms\": " << median(oldTimes)
                << ",\n  \"live_query_median_ms\": " << median(newTimes)
                << ",\n  \"live_atlas_bake_median_ms\": " << median(bakeTimes)
                << ",\n  \"atlas_bytes\": " << WeatherField::byteBudget << ",\n  \"checksum\": " << checksum
                << "\n}\n";
        std::ofstream fields(out / "fields.csv");
        fields << "seconds,x,y,cloud,storm,precipitation,dust,wind_m_s\n";
        for (double seconds : {12., 43212.}) {
            const WeatherSampler sampler(climate, seconds);
            for (unsigned i = 0; i < directions.size(); ++i) {
                const auto s = sampler.sample(directions[i], 0);
                fields << seconds << ',' << i % WeatherField::width << ',' << i / WeatherField::width << ','
                       << s.cloud << ',' << s.storm << ',' << s.precipitation << ',' << s.dust << ','
                       << glm::length(s.wind) << '\n';
            }
        }
        metrics.flush();
        fields.flush();
        if (!metrics || !fields)
            throw std::runtime_error("Could not write weather measurements");
        std::cout << "Weather generation: legacy " << median(oldTimes) << " ms, live " << median(newTimes)
                  << " ms, atlas " << median(bakeTimes) << " ms / " << WeatherField::byteBudget << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
