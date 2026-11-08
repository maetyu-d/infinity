#pragma once
#include "Activity.hpp"
namespace infinity {
inline constexpr uint32_t raiderActivitySlot = 600;
enum class RaiderRole : uint8_t { Legacy, Cargo, MissileWing, Cutoff, Hunter, Skirmisher, Siege, Saboteur, Scavenger, Scout, Pursuit };
inline constexpr uint32_t cargoRaiderSlot = 601, missileWingSlot = 602, cutoffRaiderSlot = 603,
                          factionHunterSlot = 604, skirmisherSlot = 605, siegeRaiderSlot = 606,
                          saboteurSlot = 607, scavengerSlot = 608, raiderScoutSlot = 609, pursuitRaiderSlot = 610;
RaiderRole raiderRole(const ActivityId&);
std::string_view raiderFaction(uint64_t systemSeed);
bool isRaider(const ActivityId&);
ActivityBody sampleRaider(const Universe&, const Address& planet, double seconds);
ActivityBody sampleCombatActivity(const Universe&, const ActivityId&, double seconds);
} // namespace infinity
