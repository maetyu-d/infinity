#pragma once
#include <array>
#include <string_view>
namespace infinity {
inline constexpr bool isPulseLaser(unsigned level) { return level >= 1 && level <= 3; }
inline constexpr unsigned ionDisruptor = 4, missileLauncher = 5, pointDefence = 6;
inline constexpr unsigned maxWeaponMounts = 3, noWeaponMount = maxWeaponMounts;
inline std::string_view weaponMountName(unsigned mount) {
    constexpr std::array<std::string_view, maxWeaponMounts> names{"NOSE GUN", "PORT GUN", "STBD GUN"};
    return mount < names.size() ? names[mount] : "NO GUN";
}
inline std::string_view weaponGrade(unsigned level) {
    constexpr std::array<std::string_view, 7> grades{"--", "I", "II", "III", "ION", "MSL", "PD"};
    return level < grades.size() ? grades[level] : "--";
}
} // namespace infinity
