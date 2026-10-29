#pragma once
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>

namespace infinite {
class PolarValidation {
  public:
    PolarValidation(const Universe& u, Navigation& nav, Pilot& pilot) : u_(u), nav_(nav), pilot_(pilot) {
        for (unsigned star = 0; star < 256 && (!air_ || !vacuum_); ++star) {
            const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
            for (auto a : u.children(system)) {
                const auto p = u.describe(a);
                if (!hasSolidSurface(p) || isMoon(a))
                    continue;
                const auto climate = planetClimate(p);
                if (!air_ && climate.pressureBar > .5 && climate.pressureBar < 1.5)
                    air_ = p;
                if (!vacuum_ && climate.pressureBar == 0)
                    vacuum_ = p;
            }
        }
        if (!air_ || !vacuum_)
            throw std::runtime_error("Polar fixture requires actual atmospheric and airless worlds");
        place();
    }
    void advance(Renderer& renderer) {
        renderer.setMode(RenderMode::Hyperion);
        renderer.setPolarLights(sample_ != 0);
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
                std::cout << "PASS: polar optics / night, day, ground, blocked and airless / opaque depth / "
                             "magnification / unchanged geometry\n";
            return;
        }
        const auto gen = cache.stats();
        stable_ = stats.visibleObjects && !gen.pending && !gen.queued && !stats.lodTransitions &&
                          !stats.uploadDeferred && !stats.surfacePending
                      ? stable_ + 1
                      : 0;
        if (stable_ < 24)
            return;
        const auto& p = stage_ == 4 ? *vacuum_ : *air_;
        const auto g = renderer.submittedGeometry(p.address);
        if (!g || descriptorFingerprint(u_.describe(p.address)) != descriptor_)
            throw std::runtime_error("Polar optics lost its actual planet");
        const auto hash = geometryFingerprint(*g);
        if (sample_ == 0) {
            geometry_ = hash;
            terrain_ = renderer.terrainGeometryDigest();
        } else if (geometry_ != hash || terrain_ != renderer.terrainGeometryDigest())
            throw std::runtime_error("Optical switching changed planet/terrain geometry");
        const auto physical = nav_.cameraPosition() * std::pow(10., nav_.logDistance - p.frame.logRadius);
        if (glm::length(physical - eye_) > 1e-10 || glm::length(nav_.orientation * glm::dvec3(0, 0, -1) -
                                                                orientation_ * glm::dvec3(0, 0, -1)) > 1e-12)
            throw std::runtime_error("Optical switching changed the physical camera");
        static const char* names[] = {"night", "day", "ground", "blocked", "airless"};
        renderer.screenshot((std::filesystem::path(dir) /
                             (std::string(names[stage_]) + "-" + std::to_string(sample_) + ".ppm"))
                                .string());
        std::cout << "POLAR_OPTICS scene=" << names[stage_] << " sample=" << sample_ << " geometry=" << hash
                  << " terrain=" << terrain_ << " log=" << nav_.logDistance << " frame=" << frames_
                  << " camera=unchanged\n";
        captured_ = true;
        hold_ = 6;
        stable_ = 0;
    }
    bool finished() const {
        return stage_ == 5;
    }

  private:
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    std::optional<Node> air_, vacuum_;
    unsigned stage_ = 0, sample_ = 0, stable_ = 0, hold_ = 0;
    unsigned frames_ = 0;
    bool captured_ = false;
    uint64_t descriptor_ = 0, geometry_ = 0, terrain_ = 0;
    glm::dvec3 eye_{};
    glm::dquat orientation_{};
    void place() {
        const auto& p = stage_ == 4 ? *vacuum_ : *air_;
        const double radius = std::pow(10., p.frame.logRadius);
        const auto sun =
            glm::normalize(u_.relative(p.address.parent(), p.address, p.frame.logRadius).position);
        auto up = planetClimate(p).axis;
        auto direction = stage_ == 1 ? sun : -sun;
        double log = p.frame.logRadius;
        eye_ = direction * 1.8;
        glm::dvec3 aim(0);
        if (stage_ == 2 || stage_ == 3) {
            const auto axis = up * (glm::dot(up, sun) > 0 ? -1. : 1.);
            const auto side = glm::normalize(-sun + up * glm::dot(up, sun));
            const double phase = hyperionPhase(p.planetSeed ? p.planetSeed : p.seed);
            const double drift = std::sin(12 * .027 + phase * 31.);
            double lo = .55, hi = .86;
            for (unsigned i = 0; i < 40; ++i) {
                const double latitude = (lo + hi) * .5;
                const auto d = axis * latitude + side * std::sqrt(1 - latitude * latitude);
                const double wave = std::sin(d.x * 10 + d.z * 7 + drift * .7) +
                                    .42 * std::sin(d.z * 21 - d.x * 9 - drift * .4);
                if (latitude < .70 + wave * .045 + .006)
                    lo = latitude;
                else
                    hi = latitude;
            }
            direction = axis * lo + side * std::sqrt(1 - lo * lo);
            const double ground = u_.terrainHeight(p, direction);
            eye_ = direction * (1 + (ground + 50) / radius);
            if (stage_ == 2)
                aim = eye_ + side * .84 + direction * .55;
            up = direction;
            log = 3;
        }
        if (std::abs(glm::dot(glm::normalize(aim - eye_), up)) > .98)
            up = {0, 0, 1};
        orientation_ = glm::quatLookAtRH(glm::normalize(aim - eye_), up);
        const auto offset =
            eye_ * std::pow(10., p.frame.logRadius - log) - orientation_ * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << p.address.str() << ' ' << log << ' ' << orientation_.w << ' ' << orientation_.x << ' '
          << orientation_.y << ' ' << orientation_.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(s.str()))
            throw std::runtime_error("Polar fixture placement failed");
        pilot_.resetMotion();
        if (sample_ == 2)
            nav_.setRenderScale(log + (stage_ == 2 || stage_ == 3 ? 1 : -1));
        descriptor_ = descriptorFingerprint(p);
        stable_ = 0;
    }
};
} // namespace infinite
