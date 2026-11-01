#pragma once
#include "Activity.hpp"
#include "Armament.hpp"
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
    ShipSystem targetSystem = ShipSystem::Hull;
    bool direct = false; // player at a turret: sight originates at that mount, no nose convergence
};
struct WeaponShot {
    Address reference;
    glm::dvec3 from{}, to{};
    double age = 0, scale = 1;
    bool hit = false, disabled = false;
    unsigned level = 1;
    bool enemy = false;
    unsigned mount = 0;
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
    SystemHealth systems = intactSystems();
};
struct WeaponMountState {
    unsigned level = 0;
    double heat = 0, cooldown = 0, crewReadiness = 0;
    bool overheated = false;
    uint64_t shotsFired = 0;
    double integrity = 100;
};
// Mutable gameplay overlay. World addresses, generators and craft recipes remain read-only.
class WeaponSystem {
  public:
    WeaponSystem() {
        mounts_[0].level = 1;
        mounts_[0].crewReadiness = 1;
    }
    void setLevel(unsigned level) {
        mounts_[0].level = std::min(level, maxWeaponLevel);
    }
    void setLoadout(const std::array<unsigned, maxWeaponMounts>&);
    const WeaponMountState& mount(unsigned i) const {
        return mounts_.at(i);
    }
    unsigned level() const {
        return mounts_[0].level;
    }
    double heat() const {
        return mounts_[0].heat;
    }
    double cooldown() const {
        return mounts_[0].cooldown;
    }
    uint64_t shotsFired() const {
        uint64_t total = 0;
        for (const auto& m : mounts_)
            total += m.shotsFired;
        return total;
    }
    bool overheated() const {
        return mounts_[0].overheated;
    }
    void setCrewReadiness(double readiness) {
        if (std::isfinite(readiness))
            mounts_[0].crewReadiness = std::clamp(readiness, .65, 1.2);
    }
    void setMountReadiness(unsigned, double);
    void setMountIntegrity(unsigned mount, double integrity);
    unsigned firingMask() const;
    void advance(double seconds);
    bool fire(const Universe&, const WeaponRay&, std::span<const ActivityBody>, const Geometry* = nullptr);
    bool fireMount(unsigned, const Universe&, const WeaponRay&, std::span<const ActivityBody>,
                   const Geometry* = nullptr, const ActivityId* requiredTarget = nullptr);
    unsigned fireBattery(const Universe&, const WeaponRay&, std::span<const ActivityBody>,
                         const Geometry* = nullptr);
    void apply(ActivityBody&) const;
    void receiveNpcDamage(const ActivityBody&, double points, double seconds);
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
    std::array<WeaponMountState, maxWeaponMounts> mounts_{};
    std::vector<WeaponShot> shots_;
    std::vector<WeaponDamage> damage_;
};
} // namespace infinite
