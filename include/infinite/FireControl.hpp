#pragma once
#include "StationControl.hpp"
namespace infinite {
enum class FireOrder : uint8_t { Hold, Defend, Focus };
std::string_view fireOrderName(FireOrder);
// Bounded ship-only fire director. It never spawns contacts or changes their identity.
class FireControl {
  public:
    void order(FireOrder);
    FireOrder order() const {
        return order_;
    }
    unsigned update(const Universe&, const WeaponRay& hullRay, glm::dquat hullAttitude,
                    const std::optional<ActivityId>& selected, unsigned captainMount, const ShipCrew&,
                    WeaponSystem&, std::span<const ActivityBody>, double dt, bool inhibited,
                    const Geometry* = nullptr);
    const std::array<std::string, maxWeaponMounts>& status() const {
        return status_;
    }
    std::string serialize() const;
    bool restore(std::string_view);

  private:
    FireOrder order_ = FireOrder::Hold;
    double clock_ = 0;
    std::array<double, maxWeaponMounts> tracking_{};
    std::array<std::optional<ActivityId>, maxWeaponMounts> targets_;
    std::array<std::string, maxWeaponMounts> status_;
};
} // namespace infinite
