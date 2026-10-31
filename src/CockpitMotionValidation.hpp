#pragma once
#include "infinite/Cockpit.hpp"
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace infinite {
class CockpitMotionValidation {
  public:
    CockpitMotionValidation(const Universe& u, Navigation& nav, Pilot& pilot)
        : universe_(u), nav_(nav), pilot_(pilot) {
        pilot_.startInSystem();
        camera_ = nav_.serialize();
        ship_ = pilot_.serialize();
        descriptor_ = descriptorFingerprint(universe_.describe(nav_.anchor));
    }
    void apply(Renderer& renderer) {
        if (finished())
            return;
        if (age_ == 0) {
            renderer.setMode(RenderMode::Hyperion);
            renderer.setCockpitMotionEnabled(false);
            renderer.setCockpitMotionEnabled(true);
        }
        auto feedback = pilot_.instruments();
        feedback.localAcceleration = feedback.localAngularVelocity = {};
        feedback.engine = 0;
        feedback.hull = 100;
        feedback.contactSequence = 0;
        feedback.contactImpulse = {};
        feedback.landed = false;
        const auto effect = stage_ % 6;
        if (age_ >= 4) {
            if (effect == 1) {
                feedback.localAcceleration = {28, 20, -30};
                feedback.localAngularVelocity = {.6, -.5, .85};
                feedback.engine = .8;
            }
            if (effect == 3) {
                feedback.contactSequence = 1;
                feedback.contactImpulse = {8, 12, 0};
                feedback.hull = 92;
            }
            if (effect == 4)
                feedback.hull = 88;
        }
        renderer.setCockpit(&feedback);
        if (age_ == 4 && effect == 2)
            renderer.cockpitImpulse(CockpitImpulse::Recoil);
        if (age_ == 4 && effect == 5)
            renderer.cockpitImpulse(CockpitImpulse::Impact, 1, {1, .3, 0});
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer, const std::string& dir) {
        if (nav_.serialize() != camera_ || pilot_.serialize() != ship_ ||
            descriptorFingerprint(universe_.describe(nav_.anchor)) != descriptor_)
            throw std::runtime_error("Cockpit feedback changed the saved ship, camera or world");
        if (stats.cockpitMotionDegrees > .574 || stats.cockpitMotionMetres > .009000001)
            throw std::runtime_error("Native cockpit motion exceeded its restrained view limits");
        const auto effect = stage_ % 6;
        if (stage_ == 0 && age_ < 4) {
            const auto g = cache.stats();
            if (!stats.visibleObjects || g.pending || g.queued || stats.lodTransitions ||
                stats.uploadDeferred || stats.weatherPending || stats.surfacePending || stats.lifeScanning)
                return;
        }
        const unsigned captureAge = effect == 1 ? 60 : effect == 5 ? 100 : effect == 0 ? 30 : 6;
        if (age_++ < captureAge)
            return;
        if (hold_++ == 0) {
            const auto& worlds = renderer.celestialContacts();
            const auto viewport = renderer.cockpitViewport();
            const CockpitLayout layout(viewport.aspect());
            if (worlds.empty())
                throw std::runtime_error("Cockpit motion fixture has no physical contacts");
            const auto point = viewport.toPixels({layout.celestial.x + 24, 217});
            if (renderer.pick(point.x, point.y) != worlds[cockpitCelestialRows(worlds).front()].address)
                throw std::runtime_error("Cockpit motion displaced an instrument click target");
            if ((effect == 0 || effect == 5) && stats.cockpitMotionDegrees > .00001)
                throw std::runtime_error("Resting/recovered cockpit failed to return to neutral");
            if (effect > 0 && effect < 5 && stats.cockpitMotionDegrees < .0001)
                throw std::runtime_error("Native feedback event did not move the cockpit view");
            const std::string name = std::string("hyperion") + "-" + effects_[effect];
            renderer.screenshot((std::filesystem::path(dir) / (name + ".ppm")).string());
            std::cout << "COCKPIT_MOTION case=" << name << " degrees=" << stats.cockpitMotionDegrees
                      << " translation_mm=" << stats.cockpitMotionMetres * 1000
                      << " physical_camera=unchanged ship=unchanged instrument_pick=PASS\n";
        }
        if (hold_ > 6) {
            ++stage_;
            age_ = hold_ = 0;
            if (finished())
                std::cout << "PASS: native cockpit inertia / recoil hook / impact / damage / recovery "
                             "in Hyperion rendering; unchanged physical flight and picking\n";
        }
    }
    bool finished() const {
        return stage_ == 6;
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    Pilot& pilot_;
    std::string camera_, ship_;
    uint64_t descriptor_ = 0;
    unsigned stage_ = 0, age_ = 0, hold_ = 0;
    static constexpr const char* effects_[] = {"neutral", "turn", "recoil", "impact", "damage", "recovery"};
};
} // namespace infinite
