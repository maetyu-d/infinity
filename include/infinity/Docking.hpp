#pragma once
#include "Activity.hpp"
#include <algorithm>

namespace infinity {
// A presentation/flight adapter for an existing harbour. No new world address or seed.
struct DockingBerth {
    ActivityId id;
    glm::dvec3 metres{}, velocity{};
    glm::dquat orientation{1, 0, 0, 0};
    double radius = 420;
    double laneLength() const {
        return std::max(600., radius * 3);
    }
    double halfWidth() const {
        return std::clamp(radius * .14, 32., 64.);
    }
};
std::optional<DockingBerth> dockingBerth(const ActivityBody&);
enum class DockingPhase : uint8_t { None, Arrival, Align, Glide, Capture, Docked, Departure };
std::string_view dockingPhaseName(DockingPhase);
struct DockingReadout {
    DockingPhase phase = DockingPhase::None;
    bool available = false, active = false, docked = false;
    glm::dvec3 offset{}, localForward{0, 0, -1}, localUp{0, 1, 0};
    double range = 0, closing = 0, relativeSpeed = 0, heading = 0, roll = 0, width = 1, capture = 0;
};
struct DockingGuidance {
    glm::dvec3 point{}, velocity{}, forward{0, 0, -1}, up{0, 1, 0};
    bool clamp = false, finished = false;
};
DockingReadout dockingReadout(const DockingBerth&, glm::dvec3 eye, glm::dvec3 velocity, glm::dquat attitude);
class DockingController {
  public:
    void begin();
    void beginFinal();
    void cancel();
    void depart();
    bool resumeClamp(const DockingReadout&);
    DockingPhase phase() const {
        return phase_;
    }
    double capture() const {
        return dwell_ / 1.6;
    }
    DockingGuidance update(const DockingBerth&, glm::dvec3 eye, glm::dvec3 velocity, glm::dquat attitude,
                           double dt);

  private:
    DockingPhase phase_ = DockingPhase::None;
    double dwell_ = 0;
    bool routeChecked_ = false, bypass_ = false;
    glm::dvec3 bypassPoint_{};
};
// Bounded lines in metres relative to the berth; actual eye subtraction happens in the renderer.
Mesh dockingLane(const DockingBerth&, double seconds, DockingPhase);
} // namespace infinity
