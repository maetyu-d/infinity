#pragma once
#include <array>
#include <string_view>
namespace infinite {
inline constexpr unsigned maxWeaponMounts = 3, noWeaponMount = maxWeaponMounts;
inline std::string_view weaponMountName(unsigned mount) {
    constexpr std::array<std::string_view, maxWeaponMounts> names{"NOSE GUN", "PORT GUN", "STBD GUN"};
    return mount < names.size() ? names[mount] : "NO GUN";
}
inline std::string_view weaponGrade(unsigned level) {
    constexpr std::array<std::string_view, 4> grades{"--", "I", "II", "III"};
    return level < grades.size() ? grades[level] : "--";
}
} // namespace infinite
