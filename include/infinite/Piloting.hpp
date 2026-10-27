#pragma once
#include "Flight.hpp"

namespace infinite {
struct HelmInput {
    double throttle = 0, strafe = 0, lift = 0, pitch = 0, yaw = 0, roll = 0;
    bool boost = false, precision = false, brake = false;
};
// Velocity is SI in frame axes. Angular rate and thrusters are in ship axes.
// Neither is multiplied by the renderer's current magnification.
struct ShipState {
    Address frame;
    glm::dvec3 velocity{}, angularVelocity{}, thrusters{}, positionResidual{};
    double throttle = 0, boost = 0, hull = 100;
    bool assist = true, cruise = true, survey = false, gear = false, landed = false;
};
struct FlightInstruments {
    glm::dvec3 localVelocity{}, localTarget{}, localUp{0, 1, 0};
    double speed = 0, stoppingDistance = 0, altitude = -1, descent = 0, drift = 0;
    double range = 0, throttle = 0, engine = 0, hull = 100, logMetres = 0, scaleRate = 0;
    double roll = 0, pitch = 0, driveSpeed = 0;
    bool gear = false, landed = false, assist = true, survey = false, navigating = false;
    bool targetVisible = false, safe = false;
    std::string phase = "MANUAL", drive = "CRUISE", destination = "SELECT A DESTINATION / J NAVIGATE";
};
class Pilot {
  public:
    Pilot(const Universe&, Navigation&, FlightController&);
    const ShipState& ship() const {
        return ship_;
    }
    const FlightInstruments& instruments() const {
        return instruments_;
    }
    void update(const HelmInput&, double dt, const std::shared_ptr<const Geometry>& visible = {});
    void synchronizeFrame();
    void brake();
    void toggleAssist();
    void toggleCruise() {
        ship_.cruise = !ship_.cruise;
    }
    void toggleGear() {
        if (!ship_.landed)
            ship_.gear = !ship_.gear;
    }
    void toggleSurvey();
    void navigate(const Address&);
    bool land();
    void launch();
    void resetMotion();
    void setVelocity(glm::dvec3 velocity);
    std::string serialize() const;
    bool restore(std::string_view);

  private:
    struct Ground {
        Node planet;
        glm::dvec3 direction{}, normal{0, 1, 0}, point{};
        double altitude = 0, slope = 0;
    };
    const Universe& universe_;
    Navigation& nav_;
    FlightController& spatial_;
    ShipState ship_;
    FlightInstruments instruments_;
    std::optional<Address> destination_, landingPlanet_;
    glm::dvec3 landingDirection_{0, 1, 0};
    bool autopilot_ = false, landing_ = false;
    bool fieldBraking_ = false;
    double launchTime_ = 0, lastLog_ = 0, scaleRate_ = 0, precisionScale_ = 1;
    std::string phase_ = "MANUAL";
    std::optional<Ground> ground() const;
    glm::dvec3 gravityAt(const Ground&) const;
    glm::dvec3 destinationPoint() const;
    double speedLimit() const;
    void step(const HelmInput&, double dt);
    void instrument();
};
} // namespace infinite
