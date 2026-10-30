#pragma once
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace infinite {
// Real forecasts and real solid worlds: no synthetic rain or altered planet
// descriptors are injected to make the native weather captures look active.
class WeatherValidation {
  public:
    WeatherValidation(const Universe& u, Navigation& nav, Pilot& pilot, ActivityState& activity)
        : u_(u), nav_(nav), pilot_(pilot), activity_(activity) {
        for (unsigned star = 0; star < 256 && !complete(); ++star) {
            const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
            for (auto address : u.children(system)) {
                if (isMoon(address))
                    continue;
                const auto p = u.describe(address);
                const auto c = planetClimate(p);
                if (!c.solid)
                    continue;
                if (!scenes_[4].planet && c.pressureBar == 0 &&
                    p.geology->terrain.type == TerrainType::Highlands)
                    scenes_[4] = {p, glm::normalize(glm::dvec3(.3, .2, 1)), 12};
                if (c.pressureBar < .03)
                    continue;
                const auto sun =
                    glm::normalize(u.relative(system.address, p.address, p.frame.logRadius).position);
                const WeatherSampler sampler(c, 12);
                for (unsigned i = 0; i < 512; ++i) {
                    const double y = 1 - 2 * (i + .5) / 512., a = i * 2.399963229728653;
                    const glm::dvec3 d(std::sqrt(1 - y * y) * std::cos(a), y,
                                       std::sqrt(1 - y * y) * std::sin(a));
                    if (glm::dot(d, sun) < .25)
                        continue;
                    const auto groundForecast = sampler.sample(d, 0);
                    const bool storm = !scenes_[3].planet && groundForecast.storm > .55 && c.moisture > .25 &&
                                       c.temperatureK > 250;
                    int type = groundForecast.type == WeatherType::Snow   ? 1
                               : groundForecast.type == WeatherType::Dust ? 2
                               : groundForecast.type == WeatherType::Rain ||
                                       groundForecast.type == WeatherType::Thunderstorm
                                   ? 0
                                   : -1;
                    if (!storm && (type < 0 || scenes_[type].planet))
                        continue;
                    const double h = u.terrainHeight(p, d);
                    if (h < 0 || h + 80 > weatherLayers(c).cloudBaseMetres * .75)
                        continue;
                    const auto forecast = sampler.sample(d, h + 80);
                    if (storm) {
                        const Random r{c.seed};
                        const double peak = (1 - r.unit(72)) * (9 + r.unit(71) * 13);
                        scenes_[3] = {p, d, peak};
                    }
                    type = forecast.type == WeatherType::Snow   ? 1
                           : forecast.type == WeatherType::Dust ? 2
                           : forecast.type == WeatherType::Rain || forecast.type == WeatherType::Thunderstorm
                               ? 0
                               : -1;
                    if (type >= 0 && !scenes_[type].planet && forecast.precipitation > .12)
                        scenes_[type] = {p, d, 12};
                }
            }
        }
        if (!complete())
            throw std::runtime_error("Weather fixture needs real rain, snow, dust, storm and vacuum worlds");
        place();
    }
    void advance(Renderer& renderer) {
        renderer.setMode(RenderMode::Hyperion);
        renderer.setWeatherEffects(sample_ != 0);
        renderer.setPolarLights(false);
        renderer.setStellarOptics(false);
        renderer.setLandingShade(false);
        renderer.freezeVisualTime(scenes_[stage_].seconds);
        activity_.seconds = scenes_[stage_].seconds;
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer, const std::string& dir) {
        ++frames_;
        if (captured_) {
            if (hold_) {
                --hold_;
                return;
            }
            captured_ = false;
            if (++sample_ == 3) {
                sample_ = 0;
                ++stage_;
            }
            if (!finished())
                place();
            else
                std::cout << "PASS: real planetary rain, snow, dust, storm discharge and vacuum / same "
                             "terrain and physical camera\n";
            return;
        }
        const auto gen = cache.stats();
        bool ready = !gen.pending && !gen.queued && !stats.lodTransitions && !stats.uploadDeferred &&
                     !stats.surfacePending && !stats.weatherPending;
        ready &= stage_ == 3 ? stats.visibleObjects > 0 : stats.terrainTiles > 50;
        if (sample_ && stage_ < 3)
            ready &= stats.weatherParticles > 0;
        stable_ = ready ? stable_ + 1 : 0;
        if (stable_ < 24)
            return;
        const auto& p = *scenes_[stage_].planet;
        if (descriptorFingerprint(u_.describe(p.address)) != descriptor_)
            throw std::runtime_error("Weather changed planetary identity");
        const auto geometry = renderer.submittedGeometry(p.address);
        const auto hash = geometry ? geometryFingerprint(*geometry) : 0;
        const auto terrain = renderer.terrainGeometryDigest();
        if (!sample_) {
            terrain_ = terrain;
            geometry_ = hash;
        } else if (terrain != terrain_ || (hash && geometry_ && hash != geometry_))
            throw std::runtime_error("Weather effects changed terrain geometry");
        const auto physical = nav_.cameraPosition() * std::pow(10., nav_.logDistance - p.frame.logRadius);
        if (glm::length(physical - eye_) > 1e-10 || glm::length(nav_.orientation * glm::dvec3(0, 0, -1) -
                                                                orientation_ * glm::dvec3(0, 0, -1)) > 1e-12)
            throw std::runtime_error("Weather effects changed the physical camera");
        static const char* names[] = {"rain", "snow", "dust", "storm", "vacuum"};
        renderer.screenshot((std::filesystem::path(dir) /
                             (std::string(names[stage_]) + "-" + std::to_string(sample_) + ".ppm"))
                                .string());
        if (sample_ == 0) {
            std::ofstream saved(std::filesystem::path(dir) / (std::string(names[stage_]) + ".view"));
            saved << nav_.serialize() << '\n';
        }
        const auto forecast = sampleWeather(planetClimate(p), scenes_[stage_].direction,
                                            stage_ == 3 ? 0 : height_, scenes_[stage_].seconds);
        std::cout << "WEATHER_VISIBILITY scene=" << names[stage_] << " sample=" << sample_
                  << " planet=" << p.address.str() << " particles=" << stats.weatherParticles
                  << " cloud=" << forecast.cloud << " storm=" << forecast.storm
                  << " visibility_m=" << forecast.visibilityMetres << " terrain=" << terrain
                  << " frame=" << frames_ << " camera=unchanged\n";
        captured_ = true;
        hold_ = 6;
        stable_ = 0;
    }
    bool finished() const {
        return stage_ == scenes_.size();
    }

  private:
    struct Scene {
        std::optional<Node> planet;
        glm::dvec3 direction{};
        double seconds = 12;
    };
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ActivityState& activity_;
    std::array<Scene, 5> scenes_{};
    unsigned stage_ = 0, sample_ = 0, hold_ = 0, stable_ = 0, frames_ = 0;
    bool captured_ = false;
    uint64_t descriptor_ = 0, terrain_ = 0, geometry_ = 0;
    glm::dvec3 eye_{};
    glm::dquat orientation_{};
    double height_ = 0;
    bool complete() const {
        for (const auto& s : scenes_)
            if (!s.planet)
                return false;
        return true;
    }
    void place() {
        const auto& scene = scenes_[stage_];
        const auto& p = *scene.planet;
        const double radius = std::pow(10., p.frame.logRadius);
        const auto tangent = tangentFrame(scene.direction) * glm::dvec3(1, 0, 0);
        glm::dvec3 aim;
        double log = 3;
        if (stage_ == 3) {
            eye_ = scene.direction * 1.55;
            aim = glm::dvec3(0);
            log = p.frame.logRadius;
        } else {
            height_ = u_.terrainHeight(p, scene.direction) + 80;
            eye_ = scene.direction * (1 + height_ / radius);
            aim = eye_ + tangent - scene.direction * .06;
        }
        orientation_ = glm::quatLookAtRH(glm::normalize(aim - eye_), stage_ == 3 ? tangent : scene.direction);
        const auto offset =
            eye_ * std::pow(10., p.frame.logRadius - log) - orientation_ * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << p.address.str() << ' ' << log << ' ' << orientation_.w << ' ' << orientation_.x << ' '
          << orientation_.y << ' ' << orientation_.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(s.str()))
            throw std::runtime_error("Invalid weather fixture camera");
        pilot_.resetMotion();
        if (sample_ == 2)
            nav_.setRenderScale(log + 1);
        descriptor_ = descriptorFingerprint(p);
        stable_ = 0;
    }
};
} // namespace infinite
