#pragma once
#include "Activity.hpp"
namespace infinite {
struct FlightInstruments;
inline constexpr unsigned maxWeaponLevel = 3;
struct WeaponSpec {
    std::string_view name;
    double damage, rangeMetres, interval, heatPerShot, cooling;
    glm::vec3 colour;
};
const WeaponSpec& weaponSpec(unsigned level);
struct WeaponRay {
    Address reference;
    glm::dvec3 origin{}, direction{0, 0, -1}, right{1, 0, 0}; // SI in reference axes
    double scale = 1, seconds = 0;
};
struct WeaponShot {
    Address reference;
    glm::dvec3 from{}, to{};
    double age = 0, scale = 1;
    bool hit = false, disabled = false;
    unsigned level = 1;
    bool enemy = false;
};
struct WeaponTrace {
    double distance = 0;
    bool blocked = false;
    std::optional<ActivityBody> body;
};
WeaponTrace traceWeapon(const Universe&, const WeaponRay&, double rangeMetres, std::span<const ActivityBody>,
                        const Geometry* = nullptr);
struct WeaponDamage {
    ActivityId id;
    double hull = 100, hitSeconds = 0;
    glm::dvec3 position{}; // retained home-frame SI wreck position
    glm::dquat orientation{1, 0, 0, 0};
};
// Mutable gameplay overlay. World addresses, generators and craft recipes remain read-only.
class WeaponSystem {
  public:
    void setLevel(unsigned level) {
        level_ = std::min(level, maxWeaponLevel);
    }
    unsigned level() const {
        return level_;
    }
    double heat() const {
        return heat_;
    }
    double cooldown() const {
        return cooldown_;
    }
    uint64_t shotsFired() const {
        return shotsFired_;
    }
    bool overheated() const {
        return overheated_;
    }
    void advance(double seconds);
    bool fire(const Universe&, const WeaponRay&, std::span<const ActivityBody>, const Geometry* = nullptr);
    void apply(ActivityBody&) const;
    void annotate(FlightInstruments&, bool inhibited = false) const;
    const std::vector<WeaponShot>& shots() const {
        return shots_;
    }
    const std::vector<WeaponDamage>& damage() const {
        return damage_;
    }
    std::string serialize() const;
    bool restore(std::string_view, const Universe&);

  private:
    unsigned level_ = 1;
    double heat_ = 0, cooldown_ = 0;
    bool overheated_ = false;
    uint64_t shotsFired_ = 0;
    std::vector<WeaponShot> shots_;
    std::vector<WeaponDamage> damage_;
};
} // namespace infinite
