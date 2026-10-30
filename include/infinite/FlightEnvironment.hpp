#pragma once
#include "Core.hpp"
#include <memory>
namespace infinite {
class Universe;
class Navigation;
class FlightController;
struct ShipEnvironmentSample {
    glm::dvec3 gravity{}, wind{};
    double airDensity = 0, pressureBar = 0;
};
// Read-only force sources. Surface/collision eligibility never gates gravity.
class FlightEnvironment {
  public:
    FlightEnvironment();
    void invalidate();
    bool matches(const Address&) const;
    void refresh(const Universe&, const Navigation&, FlightController&);
    ShipEnvironmentSample sample(glm::dvec3 eyeMetres, double seconds) const;
    size_t sourceCount() const;

  private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace infinite
