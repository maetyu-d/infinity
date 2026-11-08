#pragma once
#include "Activity.hpp"

namespace infinity {
inline constexpr uint32_t orbitalDebrisFirstSlot = 1200;
inline constexpr uint32_t orbitalDebrisCapacity = 32;
inline bool isOrbitalDebris(const ActivityId& id) {
    return id.slot >= orbitalDebrisFirstSlot && id.slot < orbitalDebrisFirstSlot + orbitalDebrisCapacity;
}
std::vector<ActivityBody> sampleOrbitalDebris(const Node& planet, double seconds);
inline constexpr double debrisFaceHeight = .44;
inline constexpr double debrisGearHeight = 2.;
inline bool landableDebris(const ActivityBody& body) {
    return isOrbitalDebris(body.id) && body.radius >= 85;
}
struct DebrisPad {
    glm::dvec3 surface{}, eye{}, velocity{}, up{}, forward{};
};
DebrisPad debrisPad(const ActivityBody&, int side);
Mesh landableDebrisMesh(unsigned detail);
struct DebrisDeposit {
    Mineral mineral = Mineral::Quartz;
    std::array<int64_t, 4> capacity{};
};
DebrisDeposit debrisDeposit(const Node& planet, const ActivityBody&);
} // namespace infinity
