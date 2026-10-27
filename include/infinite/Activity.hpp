#pragma once
#include "Geometry.hpp"
#include <span>

namespace infinite {
// Separate namespace: an activity ID never becomes a segment in a world Address.
struct ActivityId {
    Address home;
    uint32_t slot = 0;
    auto operator<=>(const ActivityId&) const = default;
    std::string str() const;
    static ActivityId parse(std::string_view);
};
enum class ActivityKind : uint8_t { Harbour, Freighter, Ferry, Patrol, Survey, Debris };
struct ActivityBody {
    ActivityId id;
    ActivityKind kind = ActivityKind::Harbour;
    // SI coordinates in the existing home planet's axes, independent of the eye.
    glm::dvec3 position{}, velocity{};
    glm::dquat orientation{1, 0, 0, 0};
    glm::vec3 tint{1};
    double radius = 1, phase = 0;
    uint64_t seed = 0;
    std::string name, status;
};
struct ActivityContact {
    ActivityBody body;
    glm::dvec3 localDirection{};
    double distance = 0, relativeSpeed = 0;
    bool selected = false, occluded = false;
};
std::vector<uint32_t> activitySlots(const Node& planet);
ActivityBody sampleActivity(const Node& planet, uint32_t slot, double seconds);
std::vector<ActivityBody> sampleActivity(const Node& planet, double seconds);
std::string hailActivity(const ActivityBody&);
Mesh activityMesh(ActivityKind, unsigned detail);

struct ActivityState {
    double seconds = 0;
    bool enabled = true;
    std::optional<ActivityId> selected;
    void advance(double dt);
    std::string serialize() const;
    bool restore(std::string_view, const Universe&);
};
} // namespace infinite
