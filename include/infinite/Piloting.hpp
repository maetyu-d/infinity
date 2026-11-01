#pragma once
#include "Armament.hpp"
#include "Flight.hpp"
#include "FlightEnvironment.hpp"
#include "ShipDynamics.hpp"
#include <cmath>

namespace infinite {
struct SurfaceWorld;
struct HelmInput {
    double throttle = 0, strafe = 0, lift = 0, pitch = 0, yaw = 0, roll = 0;
    bool boost = false, precision = false, brake = false;
};
// Keyboard commands and frame-distance mouse motion share one bounded helm.
// The bank modifier routes A/D to roll while Q/E remains vertical thrust.
HelmInput resolveHelmInput(HelmInput keyboard, glm::dvec2 mouseDelta, double seconds,
                           bool bankModifier = false);
struct FlightWaypoint {
    Address reference;
    glm::dvec3 metres{}, velocity{};
    double standOff = 100;
    std::string label;
    bool surfacePort = false;
};
// Velocity is SI in frame axes. Angular rate and thrusters are in ship axes.
// Neither is multiplied by the renderer's current magnification.
struct ShipState {
    Address frame;
    glm::dvec3 velocity{}, angularVelocity{}, thrusters{}, positionResidual{}, angularThrusters{};
    bool driveBraking = false;
    double throttle = 0, boost = 0, hull = 100;
    bool assist = true, cruise = false, shrinking = false, gear = false, landed = false;
    bool autoShrink = true;
    double logSize = 0, targetLogSize = 0;
    double cruiseSpeed = 9.4607304725808e17; // 100 light years per second, optional drive.
    double flightSeconds = 0, travelMetres = 0;
    uint64_t appliedRepairPoints = 0;
};
struct FlightInstruments {
    glm::dvec3 localVelocity{}, localTarget{}, localUp{0, 1, 0};
    glm::dvec3 localAcceleration{}, localAngularVelocity{}, contactImpulse{};
    uint64_t contactSequence = 0; // transient read-only presentation events, not a saved simulation rule
    double speed = 0, stoppingDistance = 0, altitude = -1, descent = 0, drift = 0;
    double range = 0, approachError = 0, throttle = 0, engine = 0, hull = 100, logMetres = 0, scaleRate = 0;
    double roll = 0, pitch = 0, driveSpeed = 0, rollRate = 0;
    bool gear = false, landed = false, assist = true, shrinking = false, navigating = false;
    bool autoShrink = false, growthBlocked = false;
    double hullMetres = 12, targetHullMetres = 12, flightSeconds = 0, travelMetres = 0, etaSeconds = -1;
    bool targetVisible = false, safe = false;
    std::string phase = "MANUAL", drive = "CRUISE", destination = "SELECT A DESTINATION / J NAVIGATE";
    std::string weapon, weaponStatus;
    bool weaponBattery = false;
    unsigned playerGun = noWeaponMount;
    glm::dquat gunAim{1, 0, 0, 0}; // read-only camera/weapon aim in physical ship axes
    std::string stationStatus, reliefHelm, helmOrder;
    double helmQuality = 0, gunYaw = 0, gunPitch = 0;
    double gunMinYaw = -60, gunMaxYaw = 60;
    double weaponHeat = 0;
    std::string combatStatus;
    std::string combatMission;
    unsigned hostileContacts = 0;
    double incomingFire = 0;
    std::string weather;
    std::string weatherDetail;
    double weatherSeverity = 0;
    glm::dvec3 localRelativeVelocity{};
    bool relativeMotion = false;
    double massKg = 12000, properG = 0, airDensity = 0, brakingSeconds = 0, relativeSpeed = 0,
           corridorError = 0;
    bool fieldActive = false, thrustLimited = false;
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
    void updateCrew(const Address& courseFrame, glm::dquat course, double quality, double dt,
                    const std::shared_ptr<const Geometry>& visible = {});
    void updateCrewMotion(const Address& frame, glm::dquat course, glm::dvec3 velocity, double quality,
                          double dt, const std::shared_ptr<const Geometry>& visible = {});
    void synchronizeFrame();
    void brake();
    void toggleAssist();
    void toggleCruise();
    void adjustDrive(double steps);
    void toggleGear() {
        if (!ship_.landed)
            ship_.gear = !ship_.gear;
    }
    void setCargoMass(double kg);
    void applyCombatDamage(double points);
    void applyPortRepairs(uint64_t purchasedPoints);
    double cargoMass() const {
        return cargoMassKg_;
    }
    void toggleShrink();
    void adjustSize(double decades);
    void restoreFullSize();
    void startInSystem();
    void selectNextPlanet();
    void selectNextStar();
    void selectPlanet(const Address&);
    void selectNextMoon();
    void fitToView(); // Explicit deployment from the legacy inspector, not a flight rebase.
    double sizeRatio() const {
        return std::pow(10., ship_.logSize);
    }
    double collisionRadius() const {
        return 2 * sizeRatio();
    }
    void navigate(const Address&);
    void trackWaypoint(const FlightWaypoint&);
    void navigateWaypoint(const FlightWaypoint&);
    void clearWaypoint();
    bool land();
    bool landAt(const Address& planet, glm::dvec3 direction);
    void setSurfaceWorld(std::shared_ptr<const SurfaceWorld> world) {
        surfaceWorld_ = std::move(world);
    }
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
    FlightEnvironment environment_;
    ShipEnvironmentSample environmentSample_;
    glm::dvec3 properAcceleration_{};
    glm::dvec3 contactImpulse_{};
    uint64_t contactSequence_ = 0;
    glm::dvec3 approachAxis_{0, 0, 1};
    FlightInstruments instruments_;
    std::optional<Address> destination_, landingPlanet_;
    std::optional<FlightWaypoint> waypoint_;
    glm::dvec3 landingDirection_{0, 1, 0};
    bool autopilot_ = false, landing_ = false;
    bool fieldBraking_ = false;
    bool growthBlocked_ = false;
    double cargoMassKg_ = 0;
    double crewQuality_ = 0;
    Address crewCourseFrame_;
    glm::dquat crewCourse_{1, 0, 0, 0};
    std::optional<glm::dvec3> crewVelocity_;
    double launchTime_ = 0, lastLog_ = 0, scaleRate_ = 0, precisionScale_ = 1;
    glm::dvec3 assistCapture_{};
    double assistCaptureTime_ = 0;
    void clearAssistCapture() {
        assistCapture_ = {};
        assistCaptureTime_ = 0;
    }
    std::string phase_ = "MANUAL";
    std::optional<Ground> ground() const;
    glm::dvec3 targetVelocity() const;
    glm::dvec3 destinationPoint() const;
    double speedLimit() const;
    double operatingClearance() const;
    void step(const HelmInput&, double dt);
    void instrument();
    void resize(double dt);
    bool fits(double logSize) const;
    bool continuumContact() const;
    void matterContact(const std::shared_ptr<const Geometry>&, glm::dvec3 previousEye);
    void contactFeedback(glm::dvec3 previousVelocity);
    std::shared_ptr<const Geometry> collisionGeometry_;
    std::shared_ptr<const SurfaceWorld> surfaceWorld_;
};
} // namespace infinite
