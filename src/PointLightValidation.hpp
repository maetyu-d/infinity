#pragma once
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace infinite {
class PointLightValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    Address galaxy_ = Address{}.child(Kind::Galaxy, 0);
    std::string camera_;
    unsigned stage_ = 0, sample_ = 0, age_ = 0, hold_ = 0, stable_ = 0;
    uint64_t geometry_ = 0;
    bool captured_ = false, done_ = false, probes_ = false;
    void writeProbes(const Geometry& geometry, const std::string& directory) {
        const auto galaxy = u_.describe(galaxy_);
        const auto home = u_.describe(nav_.anchor);
        const auto eye = u_.pointRelative(
            nav_.anchor, nav_.cameraPosition() * std::pow(10., nav_.logDistance - home.frame.logRadius),
            galaxy_, galaxy.frame.logRadius);
        const auto rotation = u_.relative(galaxy_, nav_.anchor, 0).orientation;
        const double factor = std::pow(10., geometry.logUnits - galaxy.frame.logRadius),
                     focal = 450 / std::tan(70. * 3.141592653589793 / 360.);
        struct Probe {
            glm::dvec2 p;
            float light;
            uint64_t id;
        };
        std::vector<Probe> candidates, selected;
        for (const auto& star : geometry.particles) {
            if (star.objectId == UINT64_MAX)
                continue;
            auto delta = glm::inverse(nav_.orientation) * (rotation * (star.position * factor - eye));
            if (delta.z >= 0)
                continue;
            glm::dvec2 p{720 + delta.x / -delta.z * focal, 450 - delta.y / -delta.z * focal};
            if (p.x < 380 || p.x > 1060 || p.y < 190 || p.y > 550 ||
                glm::length(p - glm::dvec2(720, 450)) < 40)
                continue;
            candidates.push_back({p, std::max({star.color.r, star.color.g, star.color.b}), star.objectId});
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.light > b.light; });
        for (const auto& p : candidates) {
            if (std::none_of(selected.begin(), selected.end(),
                             [&](const auto& q) { return glm::length(p.p - q.p) < 28; }))
                selected.push_back(p);
            if (selected.size() == 64)
                break;
        }
        if (selected.size() < 20)
            throw std::runtime_error("Not enough addressed star probes");
        std::ofstream out(std::filesystem::path(directory) / "probes.json");
        out.precision(17);
        out << "{\"geometry\":" << geometry_ << ",\"probes\":[";
        for (size_t i = 0; i < selected.size(); ++i) {
            if (i)
                out << ',';
            const auto& p = selected[i];
            out << '[' << p.p.x << ',' << p.p.y << ',' << p.id << ']';
        }
        out << "]}\n";
        if (!out)
            throw std::runtime_error("Cannot write star probe metadata");
        probes_ = true;
    }

  public:
    PointLightValidation(const Universe& u, Navigation& nav, Pilot& pilot, ActivityState& activity)
        : u_(u), nav_(nav), pilot_(pilot) {
        pilot_.startInSystem();
        const auto home = u_.describe(nav_.anchor);
        const auto direction =
            glm::normalize(u_.relative(galaxy_, nav_.anchor, home.frame.logRadius).position);
        const auto q = glm::quatLookAtRH(direction, std::abs(direction.y) < .95 ? glm::dvec3(0, 1, 0)
                                                                                : glm::dvec3(1, 0, 0));
        const auto offset = direction * 2.5 - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << nav_.anchor.str() << ' ' << home.frame.logRadius << ' ' << q.w << ' ' << q.x << ' ' << q.y
             << ' ' << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(text.str()))
            throw std::runtime_error("Invalid star stability fixture");
        pilot_.resetMotion();
        nav_.selected.reset();
        activity.enabled = false;
        camera_ = nav_.serialize();
    }
    void apply(Renderer& renderer) {
        renderer.setPointLightFiltering(stage_ >= 2);
        renderer.setCockpitMotionEnabled(stage_ == 1 || stage_ == 3);
        renderer.freezeVisualTime(12);
        auto instruments = pilot_.instruments();
        instruments.localAcceleration = {0, 3 * 9.80665, 0};
        instruments.engine = .64;
        renderer.setCockpit(&instruments);
        renderer.setWeatherEffects(false);
        renderer.setCloudVolumes(false);
        renderer.setPolarLights(false);
        renderer.setStellarOptics(false);
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        ++age_;
        if (nav_.serialize() != camera_)
            throw std::runtime_error("Idle-star test moved physical ship");
        const auto geometry = renderer.galaxySamples();
        const auto c = cache.stats();
        stable_ = geometry && stats.skyStars > 100 && !c.pending && !c.queued && !stats.lodTransitions &&
                          !stats.uploadDeferred
                      ? stable_ + 1
                      : 0;
        if (!probes_) {
            if (stable_ < 24)
                return;
            geometry_ = geometryFingerprint(*geometry);
            writeProbes(*geometry, directory);
            age_ = 0;
        }
        if (!geometry || geometryFingerprint(*geometry) != geometry_)
            throw std::runtime_error("Point filter changed stellar geometry");
        if (captured_) {
            if (hold_) {
                --hold_;
                return;
            }
            captured_ = false;
            const unsigned count = stage_ % 2 ? 16 : 2;
            if (++sample_ == count) {
                sample_ = age_ = 0;
                ++stage_;
                if (stage_ == 4) {
                    done_ = true;
                    std::cout << "PASS: native fixed-ship star stability, static and real engine vibration, "
                                 "same addressed emitters and geometry\n";
                }
            }
            return;
        }
        if (age_ < (stage_ % 2 ? 60u : 12u))
            return;
        const std::string name = std::string(stage_ >= 2 ? "filtered" : "legacy") +
                                 (stage_ % 2 ? "-idle-" : "-still-") + std::to_string(sample_);
        renderer.screenshot((std::filesystem::path(directory) / (name + ".ppm")).string());
        std::cout << "STAR_STABILITY " << name << " stars=" << stats.skyStars << " geometry=" << geometry_
                  << " gpu_ms=" << stats.gpuMs << " cpu_ms=" << stats.cpuMs << '\n';
        captured_ = true;
        hold_ = 3;
        if (age_ > 1600)
            throw std::runtime_error("Star stability scenario stalled");
    }
    bool finished() const {
        return done_;
    }
};
} // namespace infinite
