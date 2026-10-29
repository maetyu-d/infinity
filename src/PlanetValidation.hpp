#pragma once
#include "infinite/Piloting.hpp"
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>

namespace infinite {
// Render the same physical orbit using two legal magnifications. At the
// smaller unit size the planet is 4,000 units away, still inside the 10,000
// unit far plane. Conventional float depth rounds it into the cleared sky.
class PlanetValidation {
  public:
    PlanetValidation(const Universe& universe, Navigation& nav, Pilot& pilot)
        : universe_(universe), nav_(nav), pilot_(pilot) {
        for (unsigned star = 0; star < 64 && (!solid_ || !moon_); ++star) {
            const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, star);
            for (auto a : universe_.children(universe_.describe(system))) {
                const auto n = universe_.describe(a);
                if (hasSolidSurface(n) && !isMoon(n.address) && !solid_)
                    solid_ = n;
                if (isMoon(n.address) && !moon_)
                    moon_ = n;
            }
        }
        if (!solid_ || !moon_)
            throw std::runtime_error("Planet stability fixture requires a solid primary and moon");
        place();
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        if (hold_) {
            --hold_;
            return;
        }
        if (captured_) {
            captured_ = false;
            if (++sample_ == 3) {
                sample_ = 0;
                if (++mode_ == 3) {
                    mode_ = 0;
                    ++world_;
                }
            }
            if (finished()) {
                std::cout << "PASS: planet stability captured primary/moon, three modes, same physical "
                             "orbit at ordinary/far-depth/jittered magnifications\n";
                return;
            }
            renderer.setMode(static_cast<RenderMode>(mode_));
            place();
            return;
        }
        const auto generation = cache.stats();
        stable_ = stats.visibleObjects && !generation.pending && !generation.queued &&
                          !stats.lodTransitions && !stats.uploadDeferred
                      ? stable_ + 1
                      : 0;
        if (stable_ < 24)
            return;
        const auto& planet = world_ == 0 ? *solid_ : *moon_;
        const auto geometry = renderer.submittedGeometry(planet.address);
        if (!geometry || descriptorFingerprint(universe_.describe(planet.address)) != descriptor_)
            throw std::runtime_error("Planet stability fixture lost its authoritative world");
        const auto hash = geometryFingerprint(*geometry);
        if (sample_ == 0) {
            geometry_ = hash;
            terrain_ = renderer.terrainGeometryDigest();
        } else if (geometry_ != hash || terrain_ != renderer.terrainGeometryDigest())
            throw std::runtime_error("Magnification replaced planetary geometry");
        if (!directory.empty())
            renderer.screenshot(
                (std::filesystem::path(directory) /
                 (std::string(world_ ? "moon" : "solid") + "-" +
                  std::string(renderModeName(renderer.mode())) + "-" + std::to_string(sample_) + ".ppm"))
                    .string());
        std::cout << "PLANET_STABILITY " << planet.address.str()
                  << " mode=" << renderModeName(renderer.mode()) << " sample=" << sample_
                  << " geometry=" << hash << " terrain=" << terrain_ << " log=" << nav_.logDistance << '\n';
        captured_ = true;
        hold_ = 6; // Complete asynchronous framebuffer readback before changing the view.
    }
    bool finished() const {
        return world_ == 2;
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    Pilot& pilot_;
    std::optional<Node> solid_, moon_;
    unsigned world_ = 0, mode_ = 0, sample_ = 0, stable_ = 0, hold_ = 0;
    bool captured_ = false;
    uint64_t descriptor_ = 0, geometry_ = 0, terrain_ = 0;
    void place() {
        const auto& planet = world_ == 0 ? *solid_ : *moon_;
        const auto direction = glm::normalize(glm::dvec3(.3, .2, 1));
        const auto q = glm::quatLookAtRH(-direction, glm::dvec3(0, 1, 0));
        const auto offset = direction * 4. - q * glm::dvec3(0, 0, 1);
        std::ostringstream view;
        view.precision(17);
        view << planet.address.str() << ' ' << planet.frame.logRadius << ' ' << q.w << ' ' << q.x << ' '
             << q.y << ' ' << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(view.str()))
            throw std::runtime_error("Invalid planet stability placement");
        pilot_.resetMotion();
        if (sample_)
            nav_.setRenderScale(planet.frame.logRadius - 3 + (sample_ == 2 ? 1e-6 : 0));
        descriptor_ = descriptorFingerprint(planet);
        stable_ = 0;
    }
};
} // namespace infinite
