#pragma once
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>

namespace infinite {
class CelestialValidation {
  public:
    CelestialValidation(const Universe& u, Navigation& nav, Pilot& pilot, bool sunOnly = false)
        : sunOnly_(sunOnly), u_(u), nav_(nav), pilot_(pilot) {
        star_ = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 0));
        for (unsigned i = 0; i < 256 && (!air_ || !ice_); ++i) {
            const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, i));
            for (auto a : u.children(system)) {
                const auto p = u.describe(a);
                if (!air_ && !isMoon(a) && hasSolidSurface(p) && planetClimate(p).pressureBar > .5 &&
                    p.environment.temperature > 250 && p.environment.temperature < 330)
                    air_ = p;
                if (!ice_ && p.geology && p.geology->type == GeologyType::IceShell &&
                    planetClimate(p).pressureBar < .03)
                    ice_ = p;
            }
        }
        if (!air_ || !ice_)
            throw std::runtime_error("Celestial fixture needs existing solid and ice worlds");
        place();
    }
    void advance(Renderer& renderer) {
        renderer.setMode(stage_ == 4 && sample_ == 0 ? RenderMode::Original : RenderMode::Hyperion);
        renderer.setPolarLights(false);
        renderer.setCloudVolumes(false);
        renderer.setStellarOptics(sample_ != 0);
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
                std::cout << "PASS: celestial optics / "
                          << (sunOnly_ ? "sun / " : "sun, eclipse, away, day, ice and partial eclipse / ")
                          << ""
                             "opaque depth / "
                             "magnification / unchanged geometry\n";
            return;
        }
        const auto gen = cache.stats();
        stable_ = stats.visibleObjects && !gen.pending && !gen.queued && !stats.lodTransitions &&
                          !stats.uploadDeferred && !stats.weatherPending && !stats.surfacePending
                      ? stable_ + 1
                      : 0;
        if (stable_ < 24)
            return;
        const auto& p = stage_ == 0 ? star_ : stage_ == 4 ? *ice_ : *air_;
        const auto g = renderer.submittedGeometry(p.address);
        if ((!g && stage_ != 3) || descriptorFingerprint(u_.describe(p.address)) != descriptor_)
            throw std::runtime_error("Celestial optics lost its actual planet");
        const auto hash = g ? geometryFingerprint(*g) : 0;
        if (sample_ == 0) {
            geometry_ = hash;
            terrain_ = renderer.terrainGeometryDigest();
        } else if (geometry_ != hash || terrain_ != renderer.terrainGeometryDigest())
            throw std::runtime_error("Optical switching changed planet/terrain geometry");
        const auto physical = nav_.cameraPosition() * std::pow(10., nav_.logDistance - p.frame.logRadius);
        if (glm::length(physical - eye_) > 1e-10 || glm::length(nav_.orientation * glm::dvec3(0, 0, -1) -
                                                                orientation_ * glm::dvec3(0, 0, -1)) > 1e-12)
            throw std::runtime_error("Optical switching changed the physical camera");
        static const char* names[] = {"sun", "eclipse", "away", "day", "ice", "partial"};
        renderer.screenshot((std::filesystem::path(dir) /
                             (std::string(names[stage_]) + "-" + std::to_string(sample_) + ".ppm"))
                                .string());
        std::cout << "CELESTIAL_OPTICS scene=" << names[stage_] << " sample=" << sample_
                  << " geometry=" << hash << " terrain=" << terrain_ << " log=" << nav_.logDistance
                  << " frame=" << frames_ << " camera=unchanged\n";
        captured_ = true;
        hold_ = 6;
        stable_ = 0;
    }
    bool finished() const {
        return stage_ == (sunOnly_ ? 1u : 6u);
    }

  private:
    bool sunOnly_ = false;
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    Node star_;
    std::optional<Node> air_, ice_;
    unsigned stage_ = 0, sample_ = 0, stable_ = 0, hold_ = 0;
    unsigned frames_ = 0;
    bool captured_ = false;
    uint64_t descriptor_ = 0, geometry_ = 0, terrain_ = 0;
    glm::dvec3 eye_{};
    glm::dquat orientation_{};
    void place() {
        const auto& p = stage_ == 0 ? star_ : stage_ == 4 ? *ice_ : *air_;
        const double radius = std::pow(10., p.frame.logRadius);
        auto up = glm::dvec3(0, 1, 0);
        double log = p.frame.logRadius;
        glm::dvec3 aim(0);
        if (stage_ == 0) {
            eye_ = glm::normalize(glm::dvec3(.25, .1, 1)) * 4. * p.stellarRadiusMetres / radius;
            log = std::log10(p.stellarRadiusMetres) + 2;
        } else {
            const auto sun =
                glm::normalize(u_.relative(p.address.parent(), p.address, p.frame.logRadius).position);
            const auto side = glm::normalize(glm::cross(sun, glm::dvec3(0, 1, 0)));
            eye_ = (stage_ == 1 ? -sun : sun) * (stage_ == 3 ? 1.08 : 1.8);
            if (stage_ == 1 || stage_ == 3)
                aim = eye_ + sun;
            if (stage_ == 2)
                aim = eye_ - sun;
            if (stage_ == 4)
                eye_ = glm::normalize(sun + side * .55) * 1.8;
            if (stage_ == 5) {
                const auto sunPosition =
                    u_.relative(p.address.parent(), p.address, p.frame.logRadius).position;
                const double stellarRadius = u_.describe(p.address.parent()).stellarRadiusMetres / radius;
                double lo = 0, hi = 1.5;
                for (unsigned iteration = 0; iteration < 48; ++iteration) {
                    const double a = (lo + hi) * .5;
                    const auto e = (-sun * std::cos(a) + side * std::sin(a)) * 1.8;
                    const auto ray = glm::normalize(sunPosition - e), body = -glm::normalize(e);
                    const double separation =
                        std::atan2(glm::length(glm::cross(ray, body)), glm::dot(ray, body));
                    const double sunAngle = std::asin(stellarRadius / glm::length(sunPosition - e));
                    if (separation - std::asin(1 / 1.8) < sunAngle * .25)
                        lo = a;
                    else
                        hi = a;
                }
                eye_ = (-sun * std::cos(lo) + side * std::sin(lo)) * 1.8;
                aim = sunPosition;
            }
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
            throw std::runtime_error("Cloud fixture placement failed");
        pilot_.resetMotion();
        if (sample_ == 2)
            nav_.setRenderScale(log - 1);
        descriptor_ = descriptorFingerprint(p);
        stable_ = 0;
    }
};
} // namespace infinite
