#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>

namespace infinity {
class DockingValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ActivityBody body_;
    uint64_t descriptor_ = 0;
    unsigned shot_ = 0, stable_ = 0, hold_ = 0, frames_ = 0;
    bool waiting_ = true, complete_ = false;
    double maxStep_ = 0;
    static constexpr const char* names_[] = {"01-arrival",        "02-alignment",    "03-glide",
                                             "04-final-approach", "05-soft-capture", "06-docked",
                                             "07-departure",      "08-clear"};

  public:
    DockingValidation(const Universe& u, Navigation& nav, Pilot& pilot, ActivityState& activity)
        : u_(u), nav_(nav), pilot_(pilot) {
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto planet = u.describe(home);
        descriptor_ = descriptorFingerprint(planet);
        body_ = sampleActivity(planet, 0, 12);
        activity.seconds = 12;
        activity.selected = body_.id;
        const auto berth = *dockingBerth(body_);
        const auto q = body_.orientation * glm::angleAxis(-.22, glm::dvec3(0, 0, 1));
        const auto eye = berth.metres + body_.orientation * glm::dvec3(140, 65, berth.laneLength() + 650);
        const double log = 3;
        const auto offset = eye / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << home.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
          << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav.restore(s.str()))
            throw std::runtime_error("Invalid docking capture camera");
        pilot.resetMotion();
        pilot.setVelocity(body_.orientation * glm::dvec3(4, -1, -15));
        pilot.navigateWaypoint(
            {home, body_.position, body_.velocity, body_.radius * 4, body_.name, false, berth});
    }
    void advance(Renderer& renderer) {
        renderer.setMode(RenderMode::Hyperion);
        renderer.freezeVisualTime(12);
        if (waiting_ || hold_ || complete_)
            return;
        for (unsigned i = 0; i < 6; ++i) {
            pilot_.trackWaypoint({body_.id.home, body_.position, body_.velocity, body_.radius * 4, body_.name,
                                  false, dockingBerth(body_)});
            const auto before = nav_.cameraPosition() * std::pow(10., nav_.logDistance);
            const auto frame = nav_.anchor;
            pilot_.update({}, 1. / 60);
            const auto after = u_.pointRelative(
                nav_.anchor,
                nav_.cameraPosition() *
                    std::pow(10., nav_.logDistance - u_.describe(nav_.anchor).frame.logRadius),
                frame, 0);
            maxStep_ = std::max(maxStep_, glm::length(after - before));
            const auto& d = pilot_.instruments().docking;
            waiting_ = (shot_ == 1 && d.phase == DockingPhase::Align) ||
                       (shot_ == 2 && d.phase == DockingPhase::Glide) ||
                       (shot_ == 3 && d.phase == DockingPhase::Glide && d.offset.z < 200) ||
                       (shot_ == 4 && d.phase == DockingPhase::Capture) || (shot_ == 5 && d.docked) ||
                       (shot_ == 6 && d.phase == DockingPhase::Departure && d.offset.z > 120) ||
                       (shot_ == 7 && !d.active);
            if (waiting_) {
                stable_ = 0;
                break;
            }
        }
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        ++frames_;
        if (complete_)
            return;
        if (hold_) {
            if (--hold_ == 0) {
                if (shot_ == 6)
                    pilot_.launch();
                if (shot_ == 8) {
                    complete_ = true;
                    if (descriptorFingerprint(u_.describe(body_.id.home)) != descriptor_ || maxStep_ > 3)
                        throw std::runtime_error("Docking changed the world or jumped the physical ship");
                    std::cout << "PASS: native arrival, alignment, glide, soft capture, physical port access "
                                 "and departure / unchanged planet and harbour / max_step_m="
                              << maxStep_ << '\n';
                }
            }
            return;
        }
        if (!waiting_)
            return;
        const auto gen = cache.stats();
        const bool settled = stats.visibleObjects && !gen.pending && !gen.queued && !stats.lodTransitions &&
                             !stats.uploadDeferred && !stats.surfacePending && !stats.weatherPending;
        stable_ = settled ? stable_ + 1 : 0;
        if (stable_ < 12)
            return;
        if (shot_ >= 3 && shot_ <= 5 && .00005 * std::pow(10., nav_.logDistance) >= .1)
            throw std::runtime_error("Near-berth presentation clipped the real docking deck");
        if (shot_ == 6 && !stats.dockingGuideVertices)
            throw std::runtime_error("Departure lane was culled with the harbour behind the cockpit");
        if (shot_ == 5) {
            EconomicSite port;
            port.id = body_.id;
            port.metres = body_.position;
            port.radius = body_.radius;
            port.orbital = true;
            if (!economicTradeAccess(u_, nav_, pilot_, port) ||
                pilot_.instruments().docking.relativeSpeed > .001)
                throw std::runtime_error("Captured ship lost real port access or target-relative velocity");
        }
        renderer.screenshot(
            (std::filesystem::path(directory) / (std::string(names_[shot_]) + ".ppm")).string());
        std::cout << "DOCKING_CAPTURE " << names_[shot_]
                  << " phase=" << int(pilot_.instruments().docking.phase)
                  << " berth_m=" << pilot_.instruments().docking.range << " frame=" << frames_ << '\n';
        ++shot_;
        waiting_ = false;
        hold_ = 6;
    }
    bool finished() const {
        return complete_;
    }
};
} // namespace infinity
