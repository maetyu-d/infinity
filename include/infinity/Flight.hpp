#pragma once
#include "Navigation.hpp"
#include "World.hpp"

namespace infinity {
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
    double clearanceMetres() const {
        return clearanceMetres_;
    }
    std::shared_ptr<const std::vector<Node>> catalogue(const Node& node) {
        return cache_.catalog(node);
    }
    void resume() {
        holdForFlight_ = false;
    }
    void setExcavations(std::shared_ptr<const MiningProbeState> s, double seconds) {
        excavations_ = std::move(s); excavationSeconds_ = seconds;
    }
    void observeGalaxy(const std::shared_ptr<const Geometry>& geometry) {
        if (geometry && geometry->address.kind() == Kind::Galaxy &&
            geometry->address.ancestorOf(nav_.anchor) &&
            geometry->seed == seedOf(geometry->address, universe_.seed()))
            galaxySamples_ = geometry;
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    RegionCache& cache_;
    uint64_t frameChanges_ = 0;
    std::optional<Address> target_;
    bool holdForFlight_ = false;
    double clearanceMetres_ = 1e21;
    std::shared_ptr<const Geometry> galaxySamples_;
    std::shared_ptr<const MiningProbeState> excavations_;
    double excavationSeconds_ = 0;
    void changeFrame(const Address&);
};
} // namespace infinity
