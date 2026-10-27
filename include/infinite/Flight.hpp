#pragma once
#include "Navigation.hpp"
#include "World.hpp"

namespace infinite {
// Read-only spatial queries and camera control; generation remains authoritative.
class FlightController {
  public:
    FlightController(const Universe& universe, Navigation& nav, RegionCache& cache)
        : universe_(universe), nav_(nav), cache_(cache) {}
    void update(glm::dvec3 axes, double dt, bool fast = false, bool precise = false,
                const std::shared_ptr<const Geometry>& visibleGeometry = {});
    static Address surfaceAt(const Address& planet, glm::dvec3 planetLocalPoint);
    uint64_t frameChanges() const {
        return frameChanges_;
    }
    const std::optional<Address>& target() const {
        return target_;
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    RegionCache& cache_;
    uint64_t frameChanges_ = 0;
    std::optional<Address> target_;
    bool holdForFlight_ = false;
    std::shared_ptr<const Geometry> galaxySamples_;
    void changeFrame(const Address&);
};
} // namespace infinite
