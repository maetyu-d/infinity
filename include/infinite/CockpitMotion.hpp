#pragma once
#include "Core.hpp"

namespace infinite {
// Presentation-only feedback. Acceleration is SI in ship axes; camera units
// are metres in the full-size cockpit, independent of universe magnification.
struct CockpitMotionInput {
    glm::dvec3 acceleration{}, angularRate{}, contactImpulse{};
    double hullMetres = 12, hull = 100, engine = 0;
    uint64_t contactSequence = 0;
    bool landed = false;
};
enum class CockpitImpulse { Recoil, Impact, Damage };
struct CockpitMotionPose {
    glm::dvec3 translation{}, angles{};
    glm::dquat orientation{1, 0, 0, 0}; // head orientation relative to ship
};
class CockpitMotion {
  public:
    void update(const CockpitMotionInput&, double seconds);
    // Weapons call Recoil once per actual shot. This never creates a shot,
    // consumes ammunition, damages anything or applies force to the ship.
    void impulse(CockpitImpulse, double strength = 1, glm::dvec3 direction = {});
    void reset();
    const CockpitMotionPose& pose() const {
        return pose_;
    }

  private:
    struct Spring {
        glm::dvec3 position{}, velocity{};
        void advance(glm::dvec3 target, double omega, double damping, double seconds);
    };
    Spring lean_, slide_, shock_, jolt_;
    CockpitMotionPose pose_;
    glm::dvec3 previousRate_{};
    double time_ = 0, previousHull_ = 100;
    uint64_t previousContact_ = 0, impulses_ = 0;
    bool initialized_ = false, previouslyLanded_ = false;
};
} // namespace infinite
