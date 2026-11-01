#pragma once
#include "CombatActors.hpp"
#include "Weapons.hpp"
#include <map>
namespace infinite {
enum class NpcMode : uint8_t { Patrol, Attack, Evade, Retreat, Return, Disabled };
std::string_view npcModeName(NpcMode);
struct NpcAgent {
    ActivityId id;
    NpcMode mode = NpcMode::Patrol;
    glm::dvec3 offset{}, drift{};
    glm::dquat attitude{1, 0, 0, 0};
    double aggroUntil = 0, evadeUntil = 0, nextEvade = 0, cooldown = 0, heat = 0, lastHull = 100;
    uint64_t shots = 0;
};
struct NpcPlayer {
    Address reference;
    glm::dvec3 position{}, velocity{}, forward{0, 0, -1}; // SI in reference axes
    double size = 1, hull = 100, seconds = 0;
    bool landed = false, field = false;
};
struct NpcCombatReport {
    unsigned hostiles = 0, firing = 0;
    double nearest = -1, playerDamage = 0;
};
// Bounded mutable steering overlay on canonical activity routes; no generator writes.
class NpcCombat {
  public:
    NpcCombatReport update(const Universe&, const NpcPlayer&, std::span<const ActivityBody>,
                           const WeaponSystem&, double seconds, const Geometry* = nullptr);
    void apply(ActivityBody&) const;
    void annotate(FlightInstruments&) const;
    const std::map<ActivityId, NpcAgent>& agents() const {
        return agents_;
    }
    const std::vector<WeaponShot>& shots() const {
        return shots_;
    }
    std::string serialize(uint64_t universeSeed) const;
    bool restore(std::string_view, const Universe&);
    size_t residentBytes() const;

  private:
    std::map<ActivityId, NpcAgent> agents_;
    std::vector<WeaponShot> shots_;
    NpcCombatReport report_;
    double lastSeconds_ = -1, damageFlash_ = 0;
    uint64_t observedShots_ = 0;
};
} // namespace infinite
