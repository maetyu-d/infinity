#pragma once
#include "Activity.hpp"
namespace infinite {
inline constexpr uint32_t raiderActivitySlot = 600;
bool isRaider(const ActivityId&);
ActivityBody sampleRaider(const Universe&, const Address& planet, double seconds);
ActivityBody sampleCombatActivity(const Universe&, const ActivityId&, double seconds);
} // namespace infinite
