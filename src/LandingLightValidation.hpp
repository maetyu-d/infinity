#pragma once
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>

namespace infinite {
// A real, landable airless world isolates the source's gear contact treatment
// from moving weather, atmosphere and cockpit labels. No generated data changes.
class LandingLightValidation {
  public:
    LandingLightValidation(const Universe& u, Navigation& nav, Pilot& pilot)
        : u_(u), nav_(nav), pilot_(pilot) {
        for (unsigned star = 0; star < 64 && !planet_; ++star) {
            const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
            for (auto a : u.children(system)) {
                const auto p = u.describe(a);
                if (!isMoon(a) && hasSolidSurface(p) && planetClimate(p).pressureBar == 0 &&
                    p.geology->terrain.type == TerrainType::Highlands) {
                    planet_ = p;
                    break;
                }
            }
        }
        if (!planet_)
            throw std::runtime_error("Landing-light fixture needs an existing airless highland world");
        place();
    }
    void advance(Renderer& renderer) {
        renderer.setMode(RenderMode::Hyperion);
        renderer.setLandingShade(sample_ != 0);
        renderer.setStellarOptics(false);
        renderer.setPolarLights(false);
        renderer.setCloudVolumes(false);
        renderer.setWeatherEffects(false);
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
                std::cout << "PASS: landing contact / near, distant, Original and Hybrid / unchanged "
                             "terrain, geometry and physical camera\n";
            return;
        }
        const auto gen = cache.stats();
        stable_ = stats.terrainTiles > 50 && !gen.pending && !gen.queued && !stats.lodTransitions &&
                          !stats.uploadDeferred && !stats.weatherPending && !stats.surfacePending
                      ? stable_ + 1
                      : 0;
        if (stable_ < 24)
            return;
        const auto g = renderer.submittedGeometry(planet_->address);
        if (!g || descriptorFingerprint(u_.describe(planet_->address)) != descriptor_)
            throw std::runtime_error("Landing-light fixture lost its existing world");
        const auto hash = geometryFingerprint(*g), terrain = renderer.terrainGeometryDigest();
        if (sample_ == 0) {
            geometry_ = hash;
            terrain_ = terrain;
        } else if (geometry_ != hash || terrain_ != terrain)
            throw std::runtime_error("Landing contact changed terrain or geometry");
        const auto physical =
            nav_.cameraPosition() * std::pow(10., nav_.logDistance - planet_->frame.logRadius);
        if (glm::length(physical - eye_) > 1e-10 || glm::length(nav_.orientation * glm::dvec3(0, 0, -1) -
                                                                orientation_ * glm::dvec3(0, 0, -1)) > 1e-12)
            throw std::runtime_error("Landing contact changed the physical camera");
        static const char* names[] = {"near", "distant"};
        renderer.screenshot((std::filesystem::path(dir) /
                             (std::string(names[stage_]) + "-" + std::to_string(sample_) + ".ppm"))
                                .string());
        std::cout << "LANDING_LIGHT scene=" << names[stage_] << " sample=" << sample_ << " geometry=" << hash
                  << " terrain=" << terrain << " frame=" << frames_ << " camera=unchanged\n";
        captured_ = true;
        hold_ = 6;
        stable_ = 0;
    }
    bool finished() const {
        return stage_ == 2;
    }

  private:
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    std::optional<Node> planet_;
    unsigned stage_ = 0, sample_ = 0, stable_ = 0, hold_ = 0, frames_ = 0;
    bool captured_ = false;
    uint64_t descriptor_ = 0, geometry_ = 0, terrain_ = 0;
    glm::dvec3 eye_{};
    glm::dquat orientation_{};
    void place() {
        const auto& p = *planet_;
        const double radius = std::pow(10., p.frame.logRadius);
        const auto sun =
            glm::normalize(u_.relative(p.address.parent(), p.address, p.frame.logRadius).position);
        glm::dvec3 direction = sun;
        double best = -1;
        for (unsigned i = 0; i < 64; ++i) {
            const auto d =
                glm::normalize(glm::dvec3(std::sin(i * 1.7), .35 + std::sin(i * .37), std::cos(i * 2.1)));
            const double score = glm::dot(d, sun) > .35 ? glm::dot(d, u_.terrainNormal(p, d)) : -1;
            if (score > best) {
                direction = d;
                best = score;
            }
        }
        const auto tangent = tangentFrame(direction) * glm::dvec3(1, 0, 0);
        const double height = stage_ == 1 ? 100. : 8.;
        eye_ = direction * (1 + (u_.terrainHeight(p, direction) + height) / radius);
        orientation_ = glm::quatLookAtRH(glm::normalize(tangent * .35 - direction), direction);
        constexpr double log = 3;
        const auto offset =
            eye_ * std::pow(10., p.frame.logRadius - log) - orientation_ * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << p.address.str() << ' ' << log << ' ' << orientation_.w << ' ' << orientation_.x << ' '
          << orientation_.y << ' ' << orientation_.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(s.str()))
            throw std::runtime_error("Invalid landing-light camera");
        pilot_.resetMotion();
        if (sample_ == 2)
            nav_.setRenderScale(log + 1); // Keep the enclosing planet inside the retained far plane.
        descriptor_ = descriptorFingerprint(p);
        stable_ = 0;
    }
};
} // namespace infinite
