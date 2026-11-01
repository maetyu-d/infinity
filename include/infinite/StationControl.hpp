#pragma once
#include "Crew.hpp"
#include "Piloting.hpp"
#include "Weapons.hpp"
namespace infinite {
enum class HelmOrder : uint8_t { Course, HoldRange, Circle, Evade, Retreat };
std::string_view helmOrderName(HelmOrder);
glm::dvec2 weaponTraverse(unsigned mount); // hull-relative degrees, port negative
// Captain/gunner authority and presentation only. Navigation and Pilot retain
// ownership of the physical hull, reference-frame changes and collision rules.
class StationControl {
  public:
    bool request(unsigned mount, ShipCrew&, const Pilot&, const Navigation&);
    void returnToHelm(ShipCrew&, std::string reason = "CAPTAIN AT HELM / GUNNER RETURNING");
    void synchronize(ShipCrew&, const Pilot&, const Navigation&);
    bool requested() const {
        return mount_ < maxWeaponMounts;
    }
    bool active() const {
        return requested() && active_;
    }
    unsigned mount() const {
        return mount_;
    }
    const std::string& status() const {
        return status_;
    }
    glm::dquat aim() const;
    void aim(double pitch, double yaw, glm::dvec2 mouse, double seconds);
    void fly(Pilot&, const ShipCrew&, double seconds, const std::shared_ptr<const Geometry>& = {});
    bool command(HelmOrder);
    HelmOrder order() const {
        return order_;
    }
    void fly(Pilot&, const ShipCrew&, const Universe&, const Navigation&,
             const std::optional<ActivityBody>& target, double seconds,
             const std::shared_ptr<const Geometry>& = {});
    void prepareWeapons(WeaponSystem&, const ShipCrew&) const;
    WeaponRay ray(const Navigation&, const Pilot&, double seconds) const;
    void annotate(FlightInstruments&, const ShipCrew&, const WeaponSystem&) const;

  private:
    unsigned mount_ = noWeaponMount;
    bool active_ = false;
    double yaw_ = 0, pitch_ = 0;
    Address courseFrame_;
    glm::dquat course_{1, 0, 0, 0};
    std::string status_;
    HelmOrder order_ = HelmOrder::Course;
    double response_ = 0, evasion_ = 0, lastHull_ = 100;
    std::optional<ActivityId> target_;
    std::string tactical_;
};
inline double weaponMountLateralMetres(unsigned mount) {
    return mount == 1 ? -4. : mount == 2 ? 4. : 0.;
}
} // namespace infinite
