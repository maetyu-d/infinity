#pragma once
#include "NpcCombat.hpp"
namespace infinite {
class RecoveryController {
  public:
    bool update(const Universe&, const NpcPlayer&, const std::optional<ActivityBody>&, double dt,
                bool holding, const Geometry* = nullptr, std::span<const ActivityBody> nearby = {});
    void reset() {
        progress_ = 0;
        target_.reset();
    }
    double range() const {
        return range_;
    }
    double relativeSpeed() const {
        return speed_;
    }
    double progress() const {
        return progress_;
    }
    const std::string& status() const {
        return status_;
    }

  private:
    std::optional<ActivityId> target_;
    double progress_ = 0, range_ = 0, speed_ = 0;
    std::string status_;
};
} // namespace infinite
