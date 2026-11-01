#pragma once
#include "Core.hpp"
#include <array>
namespace infinite {
enum class ShipSystem : uint8_t {
    Hull,
    Engines,
    Thrusters,
    NoseGun,
    PortGun,
    StarboardGun,
    LifeSupport,
    Count
};
inline constexpr size_t shipSystemCount = size_t(ShipSystem::Count);
using SystemHealth = std::array<double, shipSystemCount>;
inline SystemHealth intactSystems() {
    SystemHealth h;
    h.fill(100);
    return h;
}
inline std::string_view shipSystemName(ShipSystem s) {
    constexpr std::array<std::string_view, shipSystemCount> names{
        "HULL", "ENGINES", "THRUSTERS", "NOSE GUN", "PORT GUN", "STBD GUN", "LIFE SUPPORT"};
    return size_t(s) < names.size() ? names[size_t(s)] : "HULL";
}
inline glm::dvec3 systemOffset(ShipSystem s) {
    switch (s) {
    case ShipSystem::Engines:
        return {0, 0, .65};
    case ShipSystem::Thrusters:
        return {.55, 0, .25};
    case ShipSystem::NoseGun:
        return {0, .08, -.65};
    case ShipSystem::PortGun:
        return {-.6, .08, -.1};
    case ShipSystem::StarboardGun:
        return {.6, .08, -.1};
    case ShipSystem::LifeSupport:
        return {0, .35, .05};
    default:
        return {};
    }
}
} // namespace infinite
