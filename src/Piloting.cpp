#include "infinite/Piloting.hpp"
#include "infinite/StellarEvolution.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace infinite {
namespace {
glm::dvec3 limited(glm::dvec3 v, double maximum) {
    const auto n = glm::length(v);
    return n > maximum ? v * (maximum / n) : v;
}
glm::dvec3 unit(glm::dvec3 v, glm::dvec3 fallback = {0, 1, 0}) {
    return glm::length(v) > 1e-30 ? glm::normalize(v) : fallback;
}
// Restore the pre-0.37 helm: independent rate control and velocity-scaled
// manoeuvring jets. The inertial and navigation solvers remain separate.
const glm::dvec3 manualRates(1.20, 1.10, 1.80);
glm::dvec3 manualSpin(const HelmInput& input) {
    return glm::dvec3(input.pitch, -input.yaw, -input.roll) * manualRates * (input.precision ? .35 : 1.);
}
struct ClassicAuthority {
    glm::dvec3 caps;
    double retro;
};
ClassicAuthority classicAuthority(double size, double limit, double speed, double payloadKg) {
    const double gain = std::max(1e-14, std::max(size, std::max(limit, speed * .7) / 85.));
    const double load = 12000. / (12000. + payloadKg);
    return {glm::dvec3(44, 48, 52) * gain * load, 32 * gain * load};
}
void classicRotation(glm::dquat& orientation, glm::dvec3& rate, glm::dvec3 target, double dt,
                     double payloadKg) {
    glm::dvec3 correction;
    for (int axis = 0; axis < 3; ++axis) {
        const bool slowing = target[axis] * rate[axis] <= 0 || std::abs(target[axis]) < std::abs(rate[axis]);
        correction[axis] = (target[axis] - rate[axis]) * -std::expm1(-dt * (slowing ? 18. : 11.));
    }
    const bool slowing = glm::dot(rate, correction) < 0;
    const auto next =
        rate + limited(correction, (slowing ? 9.5 : 6.5) * (12000. / (12000. + payloadKg)) * dt);
    const auto mid = (rate + next) * .5;
    const auto angle = glm::length(mid) * dt;
    if (angle > 1e-15)
        orientation = glm::normalize(orientation * glm::angleAxis(angle, glm::normalize(mid)));
    rate = next;
}
glm::dvec3 allocateForce(glm::dvec3 support, glm::dvec3 command, glm::dvec3 caps, double retro) {
    auto normalized = [&](glm::dvec3 force) {
        return glm::dvec3(force.x / caps.x, force.y / caps.y, force.z / (force.z < 0 ? caps.z : retro));
    };
    const double load = glm::length(normalized(support));
    if (load > 1)
        support /= load;
    if (glm::length(normalized(support + command)) <= 1)
        return support + command;
    double lo = 0, hi = 1;
    for (int i = 0; i < 18; i++) {
        double t = (lo + hi) * .5;
        if (glm::length(normalized(support + command * t)) <= 1)
            lo = t;
        else
            hi = t;
    }
    return support + command * lo;
}
glm::dvec3 angularTarget(glm::dquat orientation, glm::dvec3 heading, glm::dvec3 up) {
    const auto forward = orientation * glm::dvec3(0, 0, -1);
    const auto currentUp = orientation * glm::dvec3(0, 1, 0);
    const double angle = std::acos(std::clamp(glm::dot(forward, unit(heading, forward)), -1., 1.));
    auto axis = unit(glm::cross(forward, unit(heading, forward)), currentUp);
    auto projectedUp = unit(up - forward * glm::dot(up, forward), currentUp);
    const double roll =
        std::atan2(glm::dot(glm::cross(currentUp, projectedUp), forward), glm::dot(currentUp, projectedUp));
    return glm::inverse(orientation) *
           limited(axis * std::min(1.2, angle * 2.2) + forward * std::clamp(roll * 2., -1., 1.), 1.2);
}
std::optional<Address> planetOf(Address a) {
    while (a.kind() > Kind::Planet)
        a = a.parent();
    return a.kind() == Kind::Planet ? std::optional<Address>(a) : std::nullopt;
}
} // namespace
HelmInput resolveHelmInput(HelmInput keyboard, glm::dvec2 mouseDelta, double seconds, bool bankModifier) {
    if (bankModifier) {
        keyboard.roll += keyboard.strafe;
        keyboard.strafe = 0;
    }
    if (seconds > 0 && std::isfinite(seconds) && std::isfinite(mouseDelta.x) && std::isfinite(mouseDelta.y)) {
        const auto rate = glm::clamp(mouseDelta * (.15 / (60 * seconds)), glm::dvec2(-1), glm::dvec2(1));
        keyboard.pitch -= rate.y;
        keyboard.yaw += rate.x;
    }
    for (auto* value : {&keyboard.throttle, &keyboard.strafe, &keyboard.lift, &keyboard.pitch, &keyboard.yaw,
                        &keyboard.roll})
        *value = std::isfinite(*value) ? std::clamp(*value, -1., 1.) : 0;
    return keyboard;
}
Pilot::Pilot(const Universe& u, Navigation& n, FlightController& f)
    : universe_(u), nav_(n), spatial_(f), lastLog_(n.logDistance) {
    ship_.frame = n.anchor;
    instrument();
}
void Pilot::synchronizeFrame() {
    if (ship_.frame != nav_.anchor) {
        const auto rotation = universe_.relative(ship_.frame, nav_.anchor, 0).orientation;
        ship_.velocity = rotation * ship_.velocity;
        ship_.positionResidual = rotation * ship_.positionResidual;
        assistCapture_ = rotation * assistCapture_;
        properAcceleration_ = rotation * properAcceleration_;
        environment_.invalidate();
        ship_.frame = nav_.anchor;
    }
}
void Pilot::setVelocity(glm::dvec3 v) {
    if (std::isfinite(glm::length(v)) && glm::length(v) < 1e30) {
        synchronizeFrame();
        ship_.velocity = v;
    }
}
void Pilot::resetMotion() {
    contactImpulse_ = {};
    properAcceleration_ = {};
    clearAssistCapture();
    waypoint_.reset();
    ship_.frame = nav_.anchor;
    ship_.velocity = ship_.angularVelocity = ship_.thrusters = ship_.positionResidual =
        ship_.angularThrusters = {};
    ship_.driveBraking = false;
    ship_.throttle = ship_.boost = 0;
    ship_.landed = false;
    autopilot_ = landing_ = false;
    launchTime_ = 0;
    fieldBraking_ = false;
    precisionScale_ = 1;
    lastLog_ = nav_.logDistance;
    phase_ = "MANUAL";
    instrument();
}
void Pilot::brake() {
    clearAssistCapture();
    autopilot_ = landing_ = false;
    launchTime_ = 0;
    ship_.throttle = 0;
    ship_.assist = true;
    phase_ = "BRAKING";
}
void Pilot::toggleAssist() {
    synchronizeFrame();
    clearAssistCapture();
    ship_.assist = !ship_.assist;
    autopilot_ = landing_ = false;
    if (ship_.assist) {
        ship_.throttle = std::clamp(
            glm::dot(ship_.velocity, nav_.orientation * glm::dvec3(0, 0, -1)) / speedLimit(), 0., 1.);
        // Hyperion handling.cpp captures the current SI velocity before blending
        // to forward-speed assistance. Re-engaging assist must not kick sideways.
        assistCapture_ = ship_.velocity;
        assistCaptureTime_ = .8;
    }
    phase_ = ship_.assist ? "ASSIST / MATCHING VELOCITY" : "INERTIAL / MOMENTUM RETAINED";
}
void Pilot::toggleCruise() {
    if (ship_.shrinking || ship_.logSize < -.0001) {
        phase_ = "REGROW BEFORE LONG-RANGE CRUISE";
        instrument();
        return;
    }
    ship_.cruise = !ship_.cruise;
    ship_.driveBraking = !ship_.cruise;
    phase_ = ship_.cruise ? "LONG-RANGE DRIVE ENGAGED" : "PLANETARY FLIGHT / DRIVE BRAKING";
    instrument();
}
void Pilot::adjustDrive(double steps) {
    if (!std::isfinite(steps))
        return;
    if (ship_.cruise)
        ship_.cruiseSpeed =
            std::clamp(ship_.cruiseSpeed * std::exp2(std::clamp(steps, -40., 40.)), 1e6, 1e28);
    else
        nav_.adjustSpeed(steps);
    instrument();
}
void Pilot::setCargoMass(double kg) {
    if (std::isfinite(kg) && kg >= 0 && kg <= maxShipPayloadKg)
        cargoMassKg_ = kg;
}
void Pilot::applyCombatDamage(double points) {
    if (!std::isfinite(points) || points <= 0)
        return;
    ship_.hull = std::max(0., ship_.hull - std::min(100., points));
    instrument();
}
void Pilot::applyPortRepairs(uint64_t purchasedPoints) {
    if (purchasedPoints <= ship_.appliedRepairPoints || purchasedPoints > 1000000000000ULL)
        return;
    ship_.hull = std::min(100., ship_.hull + double(purchasedPoints - ship_.appliedRepairPoints));
    ship_.appliedRepairPoints = purchasedPoints;
    instrument();
}
void Pilot::toggleShrink() {
    if (cargoMassKg_ > 0 && !ship_.shrinking) {
        phase_ = "UNLOAD BULK CARGO BEFORE SHRINKING";
        instrument();
        return;
    }
    if (ship_.shrinking) {
        restoreFullSize();
        return;
    }
    ship_.shrinking = true;
    ship_.autoShrink = true;
    ship_.targetLogSize = -3;
    ship_.cruise = false;
    ship_.landed = false;
    brake();
    phase_ = "SHRINK FIELD / AUTOMATIC FIT";
    instrument();
}
void Pilot::adjustSize(double decades) {
    if (cargoMassKg_ > 0 && decades < 0) {
        phase_ = "UNLOAD BULK CARGO BEFORE SHRINKING";
        instrument();
        return;
    }
    if (!std::isfinite(decades))
        return;
    if (!ship_.shrinking && decades >= 0) {
        phase_ = "FULL SIZE / PLANETARY FLIGHT";
        instrument();
        return;
    }
    if (!ship_.shrinking) {
        ship_.shrinking = true;
        ship_.targetLogSize = ship_.logSize;
        ship_.cruise = false;
        ship_.landed = false;
        brake();
    }
    ship_.autoShrink = false;
    ship_.targetLogSize = std::clamp(ship_.targetLogSize + std::clamp(decades, -12., 12.), -12., 0.);
    phase_ = "SHRINK FIELD / MANUAL SIZE";
    instrument();
}
void Pilot::restoreFullSize() {
    ship_.autoShrink = false;
    ship_.targetLogSize = 0;
    phase_ = "RESTORING FULL SIZE";
    instrument();
}
void Pilot::fitToView() {
    if (cargoMassKg_ > 0) {
        phase_ = "UNLOAD BULK CARGO BEFORE SHRINKING";
        instrument();
        return;
    }
    if (nav_.anchor.kind() < Kind::Rock)
        return;
    const auto n = universe_.describe(nav_.anchor);
    ship_.logSize = ship_.targetLogSize = std::clamp(
        std::log10(std::min(.012, material(n.environment.mineral).cellMetres * .08) / 12.), -12., -3.);
    ship_.shrinking = true;
    ship_.cruise = false;
    ship_.autoShrink = true;
    phase_ = "SCALED SHIP DEPLOYED / SHRINK FIELD";
    instrument();
}
void Pilot::startInSystem() {
    Address galaxy = nav_.anchor;
    while (galaxy.kind() > Kind::Galaxy)
        galaxy = galaxy.parent();
    if (galaxy.kind() == Kind::Cosmic)
        galaxy = galaxy.child(Kind::Galaxy, 0);
    auto system = galaxy.child(Kind::System, 0);
    auto planet = universe_.children(universe_.describe(system), 1).front();
    for (uint64_t i = 0; i < 64; i++) {
        bool found = false;
        system = galaxy.child(Kind::System, i);
        for (const auto& a : universe_.children(universe_.describe(system))) {
            if (isMoon(a) || a.id() >= universe_.legacyPlanetCount(universe_.describe(system)))
                continue;
            const auto p = universe_.describe(a);
            if (p.environment.temperature > 220 && p.environment.temperature < 500 &&
                p.environment.waterActivity > .15) {
                planet = a;
                found = true;
                break;
            }
        }
        if (found)
            break;
    }
    nav_.changeFrame(planet);
    const auto p = universe_.describe(planet);
    nav_.setRenderScale(p.frame.logRadius);
    const auto direction = unit(glm::dvec3(.3, 1, .4));
    nav_.orientation = glm::quatLookAtRH(-direction, unit(glm::cross(direction, glm::dvec3(1, 0, 0))));
    nav_.offset = direction * 2.2 - nav_.orientation * glm::dvec3(0, 0, 1);
    ship_ = ShipState{};
    destination_.reset();
    landingPlanet_.reset();
    resetMotion();
    selectNextPlanet();
    nav_.selected.reset();
    phase_ = "PLANETARY FLIGHT / Y NEXT PLANET / J NAVIGATE";
    instrument();
}
void Pilot::selectPlanet(const Address& planet) {
    if (planet.kind() != Kind::Planet)
        throw std::invalid_argument("Select an existing planet or moon");
    universe_.describe(planet);
    nav_.select(planet);
    waypoint_.reset();
    destination_.reset();
    landingPlanet_.reset();
    autopilot_ = landing_ = false;
    phase_ = "DESTINATION SELECTED / J NAVIGATE";
    instrument();
}
void Pilot::selectNextStar() {
    clearWaypoint();
    auto galaxy = nav_.anchor;
    while (galaxy.kind() > Kind::Galaxy)
        galaxy = galaxy.parent();
    if (galaxy.kind() != Kind::Galaxy)
        galaxy = galaxy.child(Kind::Galaxy, 0);
    unsigned next = 0;
    if (nav_.selected && galaxy.ancestorOf(*nav_.selected) &&
        nav_.selected->path.size() > galaxy.path.size()) {
        const auto id = nav_.selected->path[galaxy.path.size()].id;
        if (isDiverseStar(id))
            next = unsigned((id & ~stellarIdFlag) + 1) % stellarPopulationCount;
    }
    nav_.select(galaxy.child(Kind::System, stellarIdFlag | next));
    destination_.reset();
    landingPlanet_.reset();
    autopilot_ = landing_ = false;
    phase_ = "STELLAR DESTINATION / J NAVIGATE";
    instrument();
}
void Pilot::selectNextPlanet() {
    waypoint_.reset();
    auto system = nav_.anchor;
    while (system.kind() > Kind::System)
        system = system.parent();
    if (system.kind() != Kind::System) {
        phase_ = "ENTER A STAR SYSTEM TO CYCLE PLANETS";
        instrument();
        return;
    }
    auto planets = universe_.children(universe_.describe(system));
    std::erase_if(planets, [](const auto& a) { return isMoon(a); });
    if (planets.empty())
        return;
    uint64_t current = UINT64_MAX;
    if (nav_.selected && system.ancestorOf(*nav_.selected) && nav_.selected->path.size() > system.path.size())
        current = nav_.selected->path[system.path.size()].id;
    if (current != UINT64_MAX && (current & moonIdFlag))
        current = moonPrimary(current);
    auto next = current == UINT64_MAX ? 0 : (current + 1) % planets.size();
    if (planets[next] == nav_.anchor)
        next = (next + 1) % planets.size();
    nav_.select(planets[next]);
    destination_.reset();
    landingPlanet_.reset();
    autopilot_ = landing_ = false;
    phase_ = "DESTINATION SELECTED / J NAVIGATE";
    instrument();
}
void Pilot::selectNextMoon() {
    clearWaypoint();
    destination_.reset();
    landingPlanet_.reset();
    autopilot_ = landing_ = false;
    auto body = nav_.anchor;
    while (body.kind() > Kind::Planet)
        body = body.parent();
    if (nav_.selected && nav_.selected->kind() == Kind::Planet)
        body = *nav_.selected;
    if (body.kind() != Kind::Planet) {
        phase_ = "SELECT OR APPROACH A PLANET FIRST";
        instrument();
        return;
    }
    if (isMoon(body))
        body = body.parent().child(Kind::Planet, moonPrimary(body.id()));
    const auto moons = universe_.moons(universe_.describe(body));
    if (moons.empty()) {
        phase_ = "NO STABLE MOONS / SELECT ANOTHER PLANET";
        instrument();
        return;
    }
    auto it = nav_.selected ? std::find(moons.begin(), moons.end(), *nav_.selected) : moons.end();
    size_t next = it == moons.end() ? 0 : (size_t(it - moons.begin()) + 1) % moons.size();
    if (moons[next] == nav_.anchor) {
        if (moons.size() == 1) {
            phase_ = "NO OTHER MOONS / Y SELECTS PLANETS";
            instrument();
            return;
        }
        next = (next + 1) % moons.size();
    }
    nav_.select(moons[next]);
    waypoint_.reset();
    destination_.reset();
    landingPlanet_.reset();
    autopilot_ = landing_ = false;
    phase_ = "MOON SELECTED / J APPROACH / L LAND";
    instrument();
}
double Pilot::speedLimit() const {
    const double scale = std::max(1e-12, operatingClearance() * .32);
    const double ratio = sizeRatio();
    const double base = ship_.cruise ? std::min(ship_.cruiseSpeed, std::max(85000., scale))
                                     : std::clamp(scale, 350. * ratio, 85000.);
    double limit =
        std::min(1e28, base * (ship_.cruise ? 1. : nav_.speedMultiplier()) * (1 + 1.4 * ship_.boost));
    if (auto g = ground())
        limit = std::min(limit, (350. + std::max(0., g->altitude - 100.) * .12));
    if (!ship_.cruise && (!ship_.assist || autopilot_ || launchTime_ > 0)) {
        const auto p = shipProperties(ratio, 0, cargoMassKg_);
        const double brake = p.reverseForce / p.massKg;
        limit =
            std::min(limit, std::sqrt(2 * brake * std::max(collisionRadius(), operatingClearance()) * .35));
    }
    return std::max(.01 * ratio, limit);
}
double Pilot::operatingClearance() const {
    double clearance = spatial_.clearanceMetres();
    if (!ship_.shrinking || nav_.anchor.kind() < Kind::Rock)
        return clearance;
    auto a = nav_.anchor;
    if (a.kind() != Kind::Lattice) {
        const auto n = universe_.describe(a);
        const double radius = std::pow(10., n.frame.logRadius);
        const auto point = nav_.cameraPosition() * std::pow(10., nav_.logDistance - n.frame.logRadius);
        const double sdf =
            a.kind() == Kind::Rock ? universe_.rockSdf(n, point) : universe_.grainSdf(n, point);
        if (sdf > 0)
            clearance = std::min(clearance, sdf * radius);
    }
    if (collisionGeometry_ && collisionGeometry_->address == nav_.anchor &&
        !collisionGeometry_->lattice.atoms.empty()) {
        const auto& g = *collisionGeometry_;
        const double units = std::pow(10., g.logUnits);
        const auto pose = universe_.relative(g.referenceFrame, nav_.anchor, nav_.logDistance);
        const auto point = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) *
                           std::pow(10., nav_.logDistance) / units;
        for (const auto& atom : g.lattice.atoms)
            clearance =
                std::min(clearance,
                         std::max(0., glm::length(point - glm::dvec3(atom.position)) - atom.radius) * units);
    }
    return std::max(1e-12, clearance);
}
bool Pilot::continuumContact() const {
    if (!ship_.shrinking)
        return true;
    const auto n = universe_.describe(nav_.anchor);
    return 12 * sizeRatio() > material(n.environment.mineral).cellMetres * .1;
}
bool Pilot::fits(double log) const {
    const double radius = 2 * std::pow(10., log);
    if (auto g = ground(); g && nav_.anchor.kind() < Kind::Rock && g->altitude < radius)
        return false;
    auto a = nav_.anchor;
    if (a.kind() >= Kind::Rock) {
        if (a.kind() == Kind::Lattice)
            a = a.parent();
        const auto n = universe_.describe(a);
        if (12 * std::pow(10., log) > material(n.environment.mineral).cellMetres * .1) {
            const auto pose = universe_.relative(a, nav_.anchor, nav_.logDistance);
            const double r = std::pow(10., n.frame.logRadius);
            const auto p = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) *
                           std::pow(10., nav_.logDistance) / r;
            const double value = a.kind() == Kind::Rock ? universe_.rockSdf(n, p) : universe_.grainSdf(n, p);
            if (value < radius / r)
                return false;
        }
    }
    if (collisionGeometry_ && collisionGeometry_->address == nav_.anchor &&
        !collisionGeometry_->lattice.atoms.empty()) {
        const auto& g = *collisionGeometry_;
        const auto pose = universe_.relative(g.referenceFrame, nav_.anchor, nav_.logDistance);
        const double units = std::pow(10., g.logUnits);
        const auto p = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) *
                       std::pow(10., nav_.logDistance) / units;
        for (const auto& atom : g.lattice.atoms)
            if (glm::length(p - glm::dvec3(atom.position)) < atom.radius + radius / units)
                return false;
    }
    return true;
}
void Pilot::resize(double dt) {
    growthBlocked_ = false;
    if (!ship_.shrinking)
        return;
    if (ship_.autoShrink) {
        const auto n = universe_.describe(nav_.anchor);
        double desired = std::min(.012, operatingClearance() * .03);
        if (n.address.kind() >= Kind::Rock) {
            desired = std::min(desired, std::pow(10., n.frame.logRadius) * .03);
            if (n.address.kind() >= Kind::Grain)
                desired = std::min(desired, material(n.environment.mineral).cellMetres * .08);
            const auto pose = universe_.relative(n.address, nav_.anchor, nav_.logDistance);
            const auto p = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) *
                           std::pow(10., nav_.logDistance - n.frame.logRadius);
            if (n.address.kind() == Kind::Rock && universe_.rockSdf(n, p) <= 0)
                desired = std::min(desired, material(n.environment.mineral).cellMetres * .08);
        }
        ship_.targetLogSize = std::clamp(std::log10(std::max(1.2e-11, desired) / 12.), -12., -3.);
    }
    const double next = ship_.logSize + std::clamp(ship_.targetLogSize - ship_.logSize, -2 * dt, 2 * dt);
    if (next < ship_.logSize &&
        glm::length(ship_.velocity) > speedLimit() * std::pow(10., next - ship_.logSize) * 1.5) {
        phase_ = "SHRINK FIELD / BRAKING FIRST";
        return;
    }
    if (next > ship_.logSize && !fits(next)) {
        growthBlocked_ = true;
        phase_ = "GROWTH BLOCKED / MOVE TO OPEN SPACE";
        return;
    }
    ship_.logSize = next;
    if (ship_.logSize == 0 && ship_.targetLogSize == 0) {
        ship_.shrinking = false;
        phase_ = "FULL SIZE / PLANETARY FLIGHT";
    }
}
std::optional<Pilot::Ground> Pilot::ground() const {
    auto address = planetOf(nav_.anchor);
    if (!address && landingPlanet_)
        address = landingPlanet_;
    if (!address && destination_)
        address = planetOf(*destination_);
    if (!address)
        return {};
    const auto p = universe_.describe(*address);
    if (!hasSolidSurface(p))
        return {};
    const auto pose = universe_.relative(*address, nav_.anchor, nav_.logDistance);
    const double factor = std::pow(10., nav_.logDistance - p.frame.logRadius);
    const auto local = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) * factor;
    if (!std::isfinite(glm::length(local)) || glm::length(local) > 3 || glm::length(local) < 1e-30)
        return {};
    const auto direction = glm::normalize(local);
    const double radius = std::pow(10., p.frame.logRadius);
    const double height = universe_.terrainHeight(p, direction);
    const auto normal = universe_.terrainNormal(p, direction);
    return Ground{
        p,
        direction,
        unit(pose.orientation * normal),
        universe_.pointRelative(*address, direction * (1 + height / radius), nav_.anchor, nav_.logDistance),
        (glm::length(local) - 1) * radius - height,
        std::acos(std::clamp(glm::dot(normal, direction), -1., 1.))};
}
void Pilot::navigate(const Address& a) {
    clearAssistCapture();
    waypoint_.reset();
    a.validate();
    if (ship_.landed)
        launch();
    destination_ = a;
    autopilot_ = true;
    landing_ = false;
    ship_.assist = true;
    ship_.landed = false;
    if (auto planet = planetOf(a); planet && a.kind() <= Kind::Mountain && !ship_.shrinking) {
        landingPlanet_ = planet;
        const auto p = universe_.describe(*planet);
        const auto pose = universe_.relative(*planet, nav_.anchor, nav_.logDistance);
        landingDirection_ =
            a.kind() >= Kind::Surface
                ? universe_.describe(a).geographicDirection
                : unit(glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position));
    } else
        landingPlanet_.reset();
    if (landingPlanet_ && !hasSolidSurface(universe_.describe(*landingPlanet_)))
        landingPlanet_.reset();
    phase_ = "DEPARTURE / ALIGNING";
    instrument();
}
void Pilot::trackWaypoint(const FlightWaypoint& waypoint) {
    waypoint.reference.validate();
    if (!std::isfinite(glm::length(waypoint.metres)) || !std::isfinite(glm::length(waypoint.velocity)) ||
        !std::isfinite(waypoint.standOff) || waypoint.standOff < 0)
        throw std::invalid_argument("Invalid activity waypoint");
    universe_.describe(waypoint.reference);
    const bool changed =
        !waypoint_ || waypoint_->reference != waypoint.reference || waypoint_->label != waypoint.label;
    waypoint_ = waypoint;
    if (changed) {
        const auto pose = universe_.relative(waypoint.reference, nav_.anchor, nav_.logDistance);
        const auto point =
            pose.position + pose.orientation * (waypoint.metres / std::pow(10., nav_.logDistance));
        approachAxis_ = unit(glm::inverse(pose.orientation) * (nav_.cameraPosition() - point));
    }
    instrument();
}
void Pilot::navigateWaypoint(const FlightWaypoint& waypoint) {
    navigate(waypoint.reference);
    landingPlanet_.reset();
    if (waypoint.surfacePort) {
        const auto p = universe_.describe(waypoint.reference);
        if (p.address.kind() != Kind::Planet || !hasSolidSurface(p))
            throw std::invalid_argument("Surface waypoint requires a solid planet");
        landingPlanet_ = p.address;
        landingDirection_ = unit(waypoint.metres);
    }
    trackWaypoint(waypoint);
    phase_ = "RENDEZVOUS / MATCHING COURSE";
    instrument();
}
void Pilot::clearWaypoint() {
    if (waypoint_) {
        waypoint_.reset();
        destination_.reset();
        autopilot_ = false;
        instrument();
    }
}
bool Pilot::land() {
    waypoint_.reset();
    auto g = ground();
    if (!g || !continuumContact() || g->slope > .61)
        return false;
    landingPlanet_ = g->planet.address;
    landingDirection_ = g->direction;
    destination_ = FlightController::surfaceAt(*landingPlanet_, landingDirection_);
    ship_.gear = true;
    ship_.assist = true;
    autopilot_ = landing_ = true;
    phase_ = "LANDING / GEAR DOWN";
    return true;
}
bool Pilot::landAt(const Address& planet, glm::dvec3 direction) {
    if (planet.kind() != Kind::Planet || !std::isfinite(glm::length(direction)) ||
        glm::length(direction) < 1e-20)
        return false;
    const auto p = universe_.describe(planet);
    if (!hasSolidSurface(p) || !continuumContact())
        return false;
    direction = glm::normalize(direction);
    const double radius = std::pow(10., p.frame.logRadius);
    const auto normal = universe_.terrainNormal(p, direction);
    const auto point =
        universe_.pointRelative(planet, direction * (1 + universe_.terrainHeight(p, direction) / radius),
                                nav_.anchor, nav_.logDistance);
    const auto delta = (nav_.cameraPosition() - point) * std::pow(10., nav_.logDistance);
    const auto up = universe_.relative(planet, nav_.anchor, 0).orientation * direction;
    if (glm::dot(normal, direction) < .994 || glm::length(delta - up * glm::dot(delta, up)) > 500 ||
        glm::dot(delta, up) < 0 || glm::dot(delta, up) > 1500)
        return false;
    waypoint_.reset();
    landingPlanet_ = planet;
    landingDirection_ = direction;
    destination_ = FlightController::surfaceAt(planet, direction);
    ship_.gear = ship_.assist = true;
    autopilot_ = landing_ = true;
    phase_ = "SURFACE PORT / CONTROLLED LANDING";
    return true;
}
void Pilot::launch() {
    if (!ship_.landed)
        return;
    clearAssistCapture();
    ship_.landed = false;
    ship_.assist = true;
    ship_.throttle = 0;
    autopilot_ = landing_ = false;
    launchTime_ = 4;
    phase_ = "LAUNCH / VERTICAL DEPARTURE";
}
glm::dvec3 Pilot::destinationPoint() const {
    if (waypoint_) {
        const auto node = universe_.describe(waypoint_->reference);
        const auto point = universe_.pointRelative(waypoint_->reference,
                                                   waypoint_->metres / std::pow(10., node.frame.logRadius),
                                                   nav_.anchor, nav_.logDistance);
        const auto rotation =
            universe_.relative(waypoint_->reference, nav_.anchor, nav_.logDistance).orientation;
        return point + rotation * approachAxis_ * (waypoint_->standOff / std::pow(10., nav_.logDistance));
    }
    if (landingPlanet_) {
        const auto p = universe_.describe(*landingPlanet_);
        const double radius = std::pow(10., p.frame.logRadius);
        const double h = universe_.terrainHeight(p, landingDirection_);
        return universe_.pointRelative(
            p.address,
            landingDirection_ * (1 + (h + (landing_ ? collisionRadius() : 5000. * sizeRatio())) / radius),
            nav_.anchor, nav_.logDistance);
    }
    if (!destination_)
        return nav_.cameraPosition();
    const auto node = universe_.describe(*destination_);
    const auto pose = universe_.relative(*destination_, nav_.anchor, nav_.logDistance);
    const auto from = nav_.cameraPosition() - pose.position;
    return pose.position + unit(from) * std::pow(10., node.frame.logRadius - nav_.logDistance) * 2.5;
}
glm::dvec3 Pilot::targetVelocity() const {
    return waypoint_
               ? universe_.relative(waypoint_->reference, nav_.anchor, 0).orientation * waypoint_->velocity
               : glm::dvec3(0);
}
void Pilot::step(const HelmInput& input, double dt) {
    const auto oldEye = nav_.cameraPosition();
    if (ship_.landed) {
        if (input.throttle > .01 || input.lift > .01)
            launch();
        else
            return;
    }
    const bool manual = std::abs(input.throttle) + std::abs(input.strafe) + std::abs(input.lift) +
                            std::abs(input.pitch) + std::abs(input.yaw) + std::abs(input.roll) >
                        .02;
    if (manual && launchTime_ <= 0) {
        autopilot_ = landing_ = false;
        phase_ = "MANUAL";
    }
    if (input.brake)
        brake();
    if (autopilot_ || launchTime_ > 0 ||
        std::abs(input.throttle) + std::abs(input.strafe) + std::abs(input.lift) > .01)
        clearAssistCapture();
    ship_.boost += ((input.boost && !input.precision && !autopilot_ ? 1. : 0.) - ship_.boost) *
                   (1 - std::exp(-dt / .18));
    ship_.throttle = std::clamp(ship_.throttle + input.throttle * dt * .36, 0., 1.);
    const auto beforeGround = continuumContact() ? ground() : std::optional<Ground>{};
    const auto up = beforeGround ? beforeGround->normal : nav_.orientation * glm::dvec3(0, 1, 0);
    double limit = speedLimit() * (input.precision ? .18 : 1.);
    const auto properties = shipProperties(sizeRatio(), ship_.boost, cargoMassKg_);
    if (!environment_.matches(nav_.anchor))
        environment_.refresh(universe_, nav_, spatial_);
    environmentSample_ = environment_.sample(oldEye * std::pow(10., nav_.logDistance), ship_.flightSeconds);
    // The shrink field explicitly exchanges momentum with the ship. Ordinary
    // hulls always receive the full gravitational field, independently of LOD.
    const auto gravity = environmentSample_.gravity * sizeRatio();
    glm::dvec3 wanted = nav_.orientation * glm::dvec3(input.strafe * limit * .28, input.lift * limit * .30,
                                                      -ship_.throttle * limit);
    glm::dvec3 spin = manualSpin(input);
    if (autopilot_) {
        auto delta = (destinationPoint() - oldEye) * std::pow(10., nav_.logDistance);
        if (landingPlanet_ && beforeGround && beforeGround->planet.address == *landingPlanet_) {
            const double radius = std::pow(10., beforeGround->planet.frame.logRadius);
            const double cosine = std::clamp(glm::dot(beforeGround->direction, landingDirection_), -1., 1.);
            if (cosine < .99 && beforeGround->altitude < radius * .25) {
                const auto tangent =
                    unit(landingDirection_ - beforeGround->direction * cosine,
                         unit(glm::cross(beforeGround->direction, glm::dvec3(1, 0, 0)), {0, 0, 1}));
                const double angle = std::min(.12, std::acos(cosine));
                const auto direction = beforeGround->direction * std::cos(angle) + tangent * std::sin(angle);
                const double clearance = std::max(5000., radius * .12);
                auto waypoint = universe_.pointRelative(*landingPlanet_, direction * (1 + clearance / radius),
                                                        nav_.anchor, nav_.logDistance);
                delta = (waypoint - oldEye) * std::pow(10., nav_.logDistance);
            }
        } else if (beforeGround && glm::dot(delta, beforeGround->normal) < 0) {
            // A destination below the local horizon must not steer through the
            // departure planet. Climb along its actual sphere before transit.
            const double radius = std::pow(10., beforeGround->planet.frame.logRadius);
            if (beforeGround->altitude < radius * .25) {
                const auto pose =
                    universe_.relative(beforeGround->planet.address, nav_.anchor, nav_.logDistance);
                const auto radial = pose.orientation * beforeGround->direction;
                const auto tangent =
                    unit(delta - radial * glm::dot(delta, radial), nav_.orientation * glm::dvec3(1, 0, 0));
                const auto localTangent = glm::inverse(pose.orientation) * tangent;
                const auto next =
                    unit(beforeGround->direction * std::cos(.12) + localTangent * std::sin(.12));
                const auto waypoint = universe_.pointRelative(
                    beforeGround->planet.address, next * (1 + std::max(5000., radius * .12) / radius),
                    nav_.anchor, nav_.logDistance);
                delta = (waypoint - oldEye) * std::pow(10., nav_.logDistance);
            }
        }
        double distance = glm::length(delta);
        const auto direction = unit(delta, nav_.orientation * glm::dvec3(0, 0, -1));
        auto heading = direction;
        if (waypoint_) {
            const auto reference = universe_.describe(waypoint_->reference);
            const auto contact = universe_.pointRelative(
                waypoint_->reference, waypoint_->metres / std::pow(10., reference.frame.logRadius),
                nav_.anchor, nav_.logDistance);
            heading = unit(contact - oldEye, heading);
        }
        if (landing_ && beforeGround && beforeGround->altitude < 500) {
            auto forward = nav_.orientation * glm::dvec3(0, 0, -1);
            heading = unit(forward - up * glm::dot(forward, up),
                           unit(glm::cross(up, glm::dvec3(1, 0, 0)), {0, 0, -1}));
        }
        spin = angularTarget(nav_.orientation, heading, up);
        const auto relativeVelocity = ship_.velocity - targetVelocity();
        const auto inverse = glm::inverse(nav_.orientation);
        const auto brakeWrench =
            allocateShipWrench(properties, inverse * -gravity * properties.massKg,
                               inverse * (-direction * properties.forceCaps.z * 200.), {});
        const double availableBrake = std::max(
            .01 * sizeRatio(),
            glm::dot(nav_.orientation * shipForce(properties, brakeWrench.linear) / properties.massKg +
                         gravity,
                     -direction));
        double speed = std::min(limit, std::sqrt(std::max(0., 2 * availableBrake * distance)) * .55);
        if (waypoint_)
            speed = std::min(speed, distance * .35);
        if (waypoint_ && distance < std::max(1000. * sizeRatio(), waypoint_->standOff * 10)) {
            const auto axis =
                universe_.relative(waypoint_->reference, nav_.anchor, 0).orientation * approachAxis_;
            const double lateral = glm::length(delta - axis * glm::dot(delta, axis));
            const double corridor = std::max(2. * sizeRatio(), std::max(waypoint_->standOff, distance) * .12);
            if (lateral > corridor)
                speed = std::min(speed, std::max(.1 * sizeRatio(), distance * .10));
            speed = std::min(speed, std::max(.05 * sizeRatio(), distance * .20));
        }
        if (landing_)
            speed = std::min(speed, std::max(.04 * sizeRatio(), distance * .65));
        if (landing_ && distance < 100 * sizeRatio())
            speed = std::min(speed, 1.5 * sizeRatio());
        const double alignment = std::max(0., glm::dot(nav_.orientation * glm::dvec3(0, 0, -1), direction));
        if ((!waypoint_ || distance > 2000) && (!landing_ || distance > 500))
            speed *= alignment * alignment;
        const auto target = universe_.describe(*destination_);
        const double targetRadius = std::pow(10., target.frame.logRadius);
        double arrival = std::clamp(targetRadius * .00001, 2. * sizeRatio(), 500. * sizeRatio());
        const auto referenceVelocity = targetVelocity();
        if (waypoint_)
            arrival = 5 * sizeRatio();
        if (!waypoint_ && target.address.kind() <= Kind::System)
            arrival = std::max(arrival, targetRadius * 1e-6);
        if (!landing_ && distance < arrival)
            speed = 0;
        wanted = direction * speed + referenceVelocity;
        if (crewQuality_ > 0)
            wanted = direction * speed * std::clamp(.4 + .5 * crewQuality_, .5, 1.) + referenceVelocity;
        ship_.throttle = std::clamp(glm::length(wanted) / limit, 0., 1.);
        phase_ = landing_                                 ? "LANDING / CONTROLLED DESCENT"
                 : distance < std::max(10000., limit * 4) ? "APPROACH / BRAKING"
                                                          : "TRANSIT / NAVIGATING";
        if (waypoint_ && distance < arrival)
            phase_ = "HOLDING / MATCHING TARGET VELOCITY";
        if (!waypoint_ && !landing_ && distance < arrival &&
            glm::length(relativeVelocity) < .5 * sizeRatio()) {
            autopilot_ = false;
            ship_.throttle = 0;
            wanted = {};
            phase_ = "ARRIVED / L SELECTS LANDING SITE";
        }
    }
    if (crewQuality_ > 0) {
        if (crewVelocity_) {
            wanted = universe_.relative(crewCourseFrame_, nav_.anchor, 0).orientation * *crewVelocity_;
            // An order cannot command a descent inside the existing stopping envelope.
            if (beforeGround &&
                beforeGround->altitude <
                    std::max(200. * sizeRatio(), instruments_.stoppingDistance + 80 * sizeRatio())) {
                wanted -= up * std::min(0., glm::dot(wanted, up));
                wanted += up * (20 * sizeRatio());
            }
        }
        if (!autopilot_ && launchTime_ <= 0) {
            const auto course =
                universe_.relative(crewCourseFrame_, nav_.anchor, 0).orientation * crewCourse_;
            spin =
                angularTarget(nav_.orientation, course * glm::dvec3(0, 0, -1), course * glm::dvec3(0, 1, 0));
        }
        spin *= std::clamp(.25 + .55 * crewQuality_, .35, 1.);
    }
    if (ship_.assist && assistCaptureTime_ > 0 && !autopilot_) {
        const double t = std::clamp(1 - assistCaptureTime_ / .8, 0., 1.);
        wanted = glm::mix(assistCapture_, wanted, t * t * (3 - 2 * t));
        assistCaptureTime_ = std::max(0., assistCaptureTime_ - dt);
    }
    const bool manualAssist = ship_.assist && !autopilot_ && launchTime_ <= 0;
    if (launchTime_ > 0) {
        wanted = up * (8. * sizeRatio());
        spin = {};
        launchTime_ = std::max(0., launchTime_ - dt);
        if (launchTime_ == 0) {
            ship_.gear = false;
            phase_ = "DEPARTURE COMPLETE";
        }
    }
    glm::dvec3 appliedAcceleration;
    if (manualAssist) {
        // This is the original assisted controller, not another gain adjustment
        // to the coupled torque allocator. Turning cannot steal steering jets.
        classicRotation(nav_.orientation, ship_.angularVelocity, spin, dt, cargoMassKg_);
        ship_.angularThrusters = {};
        const auto inverse = glm::inverse(nav_.orientation);
        const auto authority =
            classicAuthority(sizeRatio(), limit, glm::length(ship_.velocity), cargoMassKg_);
        const double continuum = smoothstep(-7., -5., std::log10(12 * sizeRatio()));
        const auto drag = atmosphericDrag(properties, nav_.orientation,
                                          ship_.velocity - environmentSample_.wind * sizeRatio(),
                                          environmentSample_.airDensity * continuum, dt);
        const auto support = inverse * (-gravity - drag / properties.massKg);
        const auto command = inverse * (wanted - ship_.velocity) * glm::dvec3(2.4, 2.4, 1.6);
        const auto steering =
            allocateForce(support, {command.x, command.y, 0}, authority.caps, authority.retro);
        const auto local = allocateForce(steering, {0, 0, command.z}, authority.caps, authority.retro);
        const auto demand = limited({local.x / authority.caps.x, local.y / authority.caps.y,
                                     local.z / (local.z < 0 ? authority.caps.z : authority.retro)},
                                    1.);
        ship_.thrusters = glm::mix(ship_.thrusters, demand, -std::expm1(-dt / .12));
        appliedAcceleration =
            nav_.orientation *
                glm::dvec3(ship_.thrusters.x * authority.caps.x, ship_.thrusters.y * authority.caps.y,
                           ship_.thrusters.z * (ship_.thrusters.z < 0 ? authority.caps.z : authority.retro)) +
            drag / properties.massKg;
    } else {
        const auto inverse = glm::inverse(nav_.orientation);
        // The explicit shrink field rescales local wind and shields a hull below
        // the continuum regime; atomic collision geometry remains authoritative.
        const double continuum = smoothstep(-7., -5., std::log10(12 * sizeRatio()));
        const auto drag = atmosphericDrag(properties, nav_.orientation,
                                          ship_.velocity - environmentSample_.wind * sizeRatio(),
                                          environmentSample_.airDensity * continuum, dt);
        glm::dvec3 angularCommand;
        if (ship_.assist) {
            // Arrest released/reversed axes more firmly than a new turn spools up.
            // This remains real, allocated RCS torque rather than a spin overwrite.
            const auto error = spin - ship_.angularVelocity;
            glm::dvec3 gain(11., 11., 14.);
            for (int axis = 0; axis < 3; ++axis)
                if (spin[axis] * ship_.angularVelocity[axis] <= 0 ||
                    std::abs(spin[axis]) < std::abs(ship_.angularVelocity[axis]))
                    gain[axis] = axis == 2 ? 22. : 18.;
            const auto acceleration = error * gain;
            angularCommand = properties.inertia * acceleration +
                             glm::cross(ship_.angularVelocity, properties.inertia * ship_.angularVelocity);
        } else {
            angularCommand = properties.torqueCaps * (spin / manualRates) * .55;
        }
        const auto support = inverse * (-gravity - drag / properties.massKg) * properties.massKg;
        glm::dvec3 commanded;
        if (ship_.assist) {
            const auto error = inverse * (wanted - ship_.velocity);
            commanded = error * glm::dvec3(manualAssist ? 2.4 : 1.6, manualAssist ? 2.4 : 1.6, 1.6) *
                        properties.massKg;
        } else {
            const double fine = input.precision ? .18 : 1.;
            commanded = shipForce(properties, {input.strafe * fine, input.lift * fine, -ship_.throttle});
        }
        const auto wrench =
            allocateShipWrench(properties, ship_.assist ? support : glm::dvec3(0), commanded, angularCommand);
        ship_.thrusters = glm::mix(ship_.thrusters, wrench.linear, -std::expm1(-dt / .12));
        for (int axis = 0; axis < 3; ++axis) {
            const bool arrest = spin[axis] * ship_.angularVelocity[axis] <= 0 ||
                                std::abs(spin[axis]) < std::abs(ship_.angularVelocity[axis]);
            ship_.angularThrusters[axis] = ship_.assist
                                               ? glm::mix(ship_.angularThrusters[axis], wrench.angular[axis],
                                                          -std::expm1(-dt / (arrest ? .015 : .06)))
                                               : wrench.angular[axis];
        }
        // Independently timed valve responses can leave the shared envelope. The
        // final allocation enforces the same physical force/torque budget again.
        const auto actual = allocateShipWrench(properties, {}, shipForce(properties, ship_.thrusters),
                                               shipTorque(properties, ship_.angularThrusters));
        ship_.thrusters = actual.linear;
        ship_.angularThrusters = actual.angular;
        appliedAcceleration = nav_.orientation * shipForce(properties, actual.linear) / properties.massKg +
                              drag / properties.massKg;
        integrateShipRotation(nav_.orientation, ship_.angularVelocity, properties.inertia,
                              shipTorque(properties, actual.angular), dt);
    }
    const auto oldVelocity = ship_.velocity;
    const bool resizingField = ship_.shrinking && ship_.targetLogSize < ship_.logSize;
    glm::dvec3 fieldAcceleration{};
    fieldBraking_ = ship_.cruise || ship_.driveBraking || resizingField;
    if (fieldBraking_) {
        const double speed = glm::length(ship_.velocity);
        const auto goal = ship_.cruise ? wanted : unit(ship_.velocity) * std::min(speed, limit);
        const double tau = std::max(
            1e-100, std::min(resizingField ? .08 : 1.5, .20 * operatingClearance() / std::max(1e-30, speed)));
        fieldAcceleration = (goal - ship_.velocity) * (-std::expm1(-dt / tau) / dt);
        if (!ship_.cruise && speed <= limit * 1.001)
            ship_.driveBraking = false;
    }
    properAcceleration_ = appliedAcceleration;
    ship_.velocity += (properAcceleration_ + gravity + fieldAcceleration) * dt;
    const double units = std::pow(10., nav_.logDistance);
    const auto displacement = (oldVelocity + ship_.velocity) * (.5 * dt) + ship_.positionResidual;
    auto proposed = oldEye + displacement / units;
    nav_.offset = proposed - nav_.orientation * glm::dvec3(0, 0, 1);
    // Retain sub-ULP travel instead of discarding small metre displacements
    // while the camera happens to use galactic-sized units.
    ship_.positionResidual = displacement - (nav_.cameraPosition() - oldEye) * units;
    if (surfaceWorld_ && nav_.anchor.kind() < Kind::Rock) {
        const auto planet = universe_.describe(surfaceWorld_->planet);
        const double factor =
            std::pow(10., nav_.logDistance - universe_.describe(nav_.anchor).frame.logRadius);
        const auto toSI = [&](glm::dvec3 point) {
            return universe_.pointRelative(nav_.anchor, point * factor, planet.address, 0);
        };
        const auto from = toSI(oldEye), to = toSI(nav_.cameraPosition());
        if (const auto hit = surfaceCollision(*surfaceWorld_, from, to, collisionRadius())) {
            const auto contactVelocity = ship_.velocity;
            const double length = glm::length(to - from);
            const double safe = std::max(0., *hit - .02 / std::max(.02, length));
            nav_.offset =
                oldEye + (nav_.cameraPosition() - oldEye) * safe - nav_.orientation * glm::dvec3(0, 0, 1);
            ship_.velocity = ship_.thrusters = ship_.positionResidual = {};
            ship_.angularThrusters = {};
            ship_.throttle = 0;
            autopilot_ = landing_ = false;
            phase_ = "STRUCTURE CONTACT / BRAKED";
            contactFeedback(contactVelocity);
        }
    }
    ship_.flightSeconds += dt;
    ship_.travelMetres += glm::length((oldVelocity + ship_.velocity) * (.5 * dt));
    if (continuumContact()) {
        if (auto g = ground(); g && g->altitude < collisionRadius() * 1.03) {
            const double sink = -glm::dot(ship_.velocity, g->normal);
            const double drift = glm::length(ship_.velocity + g->normal * sink);
            const double tilt =
                std::acos(std::clamp(glm::dot(nav_.orientation * glm::dvec3(0, 1, 0), g->normal), -1., 1.));
            const bool safe = ship_.gear && std::abs(sink) < 2. * sizeRatio() && drift < 2. * sizeRatio() &&
                              tilt < .26 && g->slope < .61;
            if (launchTime_ <= 0 && (g->altitude < collisionRadius() || (landing_ && safe))) {
                const auto contactVelocity = ship_.velocity;
                nav_.offset = g->point + g->normal * (collisionRadius() / std::pow(10., nav_.logDistance)) -
                              nav_.orientation * glm::dvec3(0, 0, 1);
                ship_.positionResidual = {};
                if (safe) {
                    ship_.velocity = ship_.angularVelocity = ship_.thrusters = ship_.angularThrusters = {};
                    ship_.throttle = 0;
                    ship_.landed = true;
                    autopilot_ = landing_ = false;
                    landingPlanet_ = g->planet.address;
                    landingDirection_ = g->direction;
                    phase_ = "LANDED / ENTER TO LAUNCH";
                } else {
                    ship_.velocity += g->normal * std::max(0., sink);
                    ship_.velocity *= .5;
                    ship_.hull = std::max(0., ship_.hull - std::max(0., sink / sizeRatio() - 2.) * .3);
                    phase_ = "SURFACE CONTACT / GEAR / LEVEL / SLOW";
                }
                contactFeedback(contactVelocity);
            }
        }
    }
    matterContact(collisionGeometry_, oldEye);
}
void Pilot::matterContact(const std::shared_ptr<const Geometry>& geometry, glm::dvec3 previousEye) {
    if (nav_.anchor.kind() < Kind::Rock)
        return;
    auto apply = [&](glm::dvec3 normal, double penetration) {
        const auto contactVelocity = ship_.velocity;
        const double inward = std::max(0., -glm::dot(ship_.velocity, normal));
        const auto eye = nav_.cameraPosition();
        nav_.offset = eye + normal * (penetration / std::pow(10., nav_.logDistance)) -
                      nav_.orientation * glm::dvec3(0, 0, 1);
        ship_.velocity += normal * inward;
        ship_.positionResidual = {};
        ship_.hull = std::max(0., ship_.hull - std::max(0., inward / sizeRatio() - 2) * .1);
        phase_ = "MATERIAL CONTACT / SHRINK OR STEER CLEAR";
        contactFeedback(contactVelocity);
    };
    if (continuumContact()) {
        auto address = nav_.anchor;
        if (address.kind() == Kind::Lattice)
            address = address.parent();
        const auto n = universe_.describe(address);
        const auto pose = universe_.relative(address, nav_.anchor, nav_.logDistance);
        const double radius = std::pow(10., n.frame.logRadius);
        const auto p = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) *
                       std::pow(10., nav_.logDistance) / radius;
        auto sdf = [&](glm::dvec3 v) {
            return address.kind() == Kind::Rock ? universe_.rockSdf(n, v) : universe_.grainSdf(n, v);
        };
        const double value = sdf(p), r = collisionRadius() / radius;
        if (value < r) {
            const double e = std::max(1e-8, std::min(.001, r * .25));
            glm::dvec3 gradient;
            for (int i = 0; i < 3; i++) {
                glm::dvec3 delta(0);
                delta[i] = e;
                gradient[i] = (sdf(p + delta) - sdf(p - delta)) / (2 * e);
            }
            const double slope = std::max(.1, glm::length(gradient));
            apply(pose.orientation * unit(gradient), std::min((r - value) / slope, r * .5) * radius);
        }
    }
    if (geometry && geometry->address == nav_.anchor && nav_.anchor.kind() == Kind::Lattice) {
        const double units = std::pow(10., geometry->logUnits);
        const auto pose = universe_.relative(geometry->referenceFrame, nav_.anchor, nav_.logDistance);
        const auto local = glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position) *
                           std::pow(10., nav_.logDistance) / units;
        const auto previous = glm::inverse(pose.orientation) * (previousEye - pose.position) *
                              std::pow(10., nav_.logDistance) / units;
        const auto segment = local - previous;
        double first = 2;
        glm::dvec3 hitNormal;
        if (glm::dot(segment, segment) > 1e-30)
            for (const auto& atom : geometry->lattice.atoms) {
                const auto offset = previous - glm::dvec3(atom.position);
                const double radius = atom.radius + collisionRadius() / units;
                const double a = glm::dot(segment, segment), b = glm::dot(offset, segment),
                             c = glm::dot(offset, offset) - radius * radius;
                const double disc = b * b - a * c;
                if (c <= 0 || b >= 0 || disc < 0)
                    continue;
                const double t = (-b - std::sqrt(disc)) / a;
                if (t >= 0 && t <= 1 && t < first) {
                    first = t;
                    hitNormal = pose.orientation * unit(offset + segment * t);
                }
            }
        if (first <= 1) {
            nav_.offset = previousEye + (nav_.cameraPosition() - previousEye) * first -
                          nav_.orientation * glm::dvec3(0, 0, 1);
            apply(hitNormal, 0);
            return;
        }
        double deepest = 0;
        glm::dvec3 normal;
        for (const auto& atom : geometry->lattice.atoms) {
            const auto delta = local - glm::dvec3(atom.position);
            const double overlap = atom.radius + collisionRadius() / units - glm::length(delta);
            if (overlap > deepest) {
                deepest = overlap;
                normal = pose.orientation * unit(delta);
            }
        }
        if (deepest > 0)
            apply(normal, deepest * units);
    }
}
void Pilot::contactFeedback(glm::dvec3 previousVelocity) {
    const auto impulse = glm::inverse(nav_.orientation) * (ship_.velocity - previousVelocity) / sizeRatio();
    if (glm::length(impulse) > 1e-8) {
        contactImpulse_ = limited(contactImpulse_ + impulse, 200.);
        ++contactSequence_;
    }
}
void Pilot::update(const HelmInput& input, double dt, const std::shared_ptr<const Geometry>& visible) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    for (double v : {input.throttle, input.strafe, input.lift, input.pitch, input.yaw, input.roll})
        if (!std::isfinite(v))
            return;
    auto bounded = input;
    for (auto* value :
         {&bounded.throttle, &bounded.strafe, &bounded.lift, &bounded.pitch, &bounded.yaw, &bounded.roll})
        *value = std::clamp(*value, -1., 1.);
    if (bounded.brake) {
        // B is the recovery control even when a turn key or mouse is still
        // commanding rotation. It must also suppress a held throttle/boost.
        bounded = {};
        bounded.brake = true;
    }
    contactImpulse_ = {};
    synchronizeFrame();
    if (visible && visible->address == nav_.anchor)
        collisionGeometry_ = visible;
    if (collisionGeometry_ && collisionGeometry_->address != nav_.anchor)
        collisionGeometry_.reset();
    nav_.stopMotion();
    spatial_.resume();
    precisionScale_ = input.precision ? .18 : 1.;
    environment_.refresh(universe_, nav_, spatial_);
    const double initialLog = nav_.logDistance;
    double remaining = std::min(dt, .1);
    for (int i = 0; remaining > 1e-9 && i < 32; ++i) {
        spatial_.update({}, std::min(.01, remaining), false, false, visible);
        synchronizeFrame();
        const double speed = glm::length(ship_.velocity);
        double safeStep = std::max(1e-100, spatial_.clearanceMetres() * .08 / std::max(1e-30, speed));
        if (nav_.anchor.kind() >= Kind::Rock && nav_.anchor.kind() < Kind::Lattice && continuumContact())
            safeStep = std::min(safeStep, .5 * collisionRadius() / std::max(1e-30, speed));
        const double stepTime = std::min({.01, remaining, safeStep});
        resize(stepTime);
        step(bounded, stepTime);
        remaining -= stepTime;
    }
    scaleRate_ = (nav_.logDistance - initialLog) / dt;
    lastLog_ = nav_.logDistance;
    instrument();
}
void Pilot::updateCrew(const Address& frame, glm::dquat course, double quality, double dt,
                       const std::shared_ptr<const Geometry>& visible) {
    if (!std::isfinite(quality) || quality <= 0 || !std::isfinite(glm::length(course)) ||
        std::abs(glm::length(course) - 1) > .01)
        return;
    crewQuality_ = std::clamp(quality, .2, 1.4);
    crewCourseFrame_ = frame;
    crewCourse_ = course;
    const bool assist = ship_.assist;
    ship_.assist = true;
    update({}, dt, visible);
    ship_.assist = assist;
    crewQuality_ = 0;
}
void Pilot::updateCrewMotion(const Address& frame, glm::dquat course, glm::dvec3 velocity, double quality,
                             double dt, const std::shared_ptr<const Geometry>& visible) {
    if (!std::isfinite(glm::length(velocity)))
        return;
    const bool navigating = autopilot_;
    autopilot_ = false;
    clearAssistCapture();
    crewVelocity_ = velocity;
    updateCrew(frame, course, quality, dt, visible);
    crewVelocity_.reset();
    autopilot_ = navigating && !ship_.landed && ship_.hull > 0;
}
void Pilot::instrument() {
    synchronizeFrame();
    instruments_ = {};
    auto& t = instruments_;
    t.localVelocity = glm::inverse(nav_.orientation) * ship_.velocity;
    t.localAcceleration = glm::inverse(nav_.orientation) * properAcceleration_;
    t.localAngularVelocity = ship_.angularVelocity;
    t.contactImpulse = contactImpulse_;
    t.contactSequence = contactSequence_;
    t.rollRate = -ship_.angularVelocity.z;
    t.speed = glm::length(ship_.velocity);
    t.driveSpeed = speedLimit() * precisionScale_;
    const auto properties = shipProperties(sizeRatio(), ship_.boost, cargoMassKg_);
    const auto g = ground();
    if (!environment_.matches(nav_.anchor))
        environment_.refresh(universe_, nav_, spatial_);
    const auto environment =
        environment_.sample(nav_.cameraPosition() * std::pow(10., nav_.logDistance), ship_.flightSeconds);
    const auto gravity = environment.gravity * sizeRatio();
    const auto relativeVelocity = ship_.velocity - targetVelocity();
    t.relativeSpeed = glm::length(relativeVelocity);
    t.localRelativeVelocity = glm::inverse(nav_.orientation) * relativeVelocity;
    t.relativeMotion = bool(waypoint_);
    t.massKg = properties.massKg;
    t.properG = glm::length(properAcceleration_) / 9.80665;
    t.airDensity = environment.airDensity;
    t.fieldActive = ship_.cruise || ship_.driveBraking || fieldBraking_;
    if (t.relativeSpeed > 1e-30) {
        const auto inverse = glm::inverse(nav_.orientation);
        const auto direction = relativeVelocity / t.relativeSpeed;
        double a;
        if (ship_.assist && !autopilot_ && launchTime_ <= 0) {
            const auto authority = classicAuthority(sizeRatio(), t.driveSpeed, t.speed, cargoMassKg_);
            const auto force =
                allocateForce(inverse * -gravity, inverse * (-direction * authority.caps.z * 200.),
                              authority.caps, authority.retro);
            a = glm::dot(nav_.orientation * force + gravity, -direction);
        } else {
            const auto wrench =
                allocateShipWrench(properties, inverse * -gravity * properties.massKg,
                                   inverse * (-direction * properties.forceCaps.z * 200.), {});
            a = glm::dot(nav_.orientation * shipForce(properties, wrench.linear) / properties.massKg +
                             gravity,
                         -direction);
        }
        t.brakingSeconds = a > 1e-30 ? t.relativeSpeed / a + .24 : 1e30;
        t.stoppingDistance =
            a > 1e-30 ? t.relativeSpeed * .24 + t.relativeSpeed * t.relativeSpeed / (2 * a) : 1e30;
    }
    const auto authority = classicAuthority(sizeRatio(), t.driveSpeed, t.speed, cargoMassKg_);
    t.thrustLimited =
        glm::length(gravity) >
        (ship_.assist && !autopilot_ && launchTime_ <= 0
             ? std::max({authority.caps.x, authority.caps.y, authority.caps.z})
             : std::max({properties.forceCaps.x, properties.forceCaps.y, properties.forceCaps.z}) /
                   properties.massKg);
    t.throttle = ship_.throttle;
    t.engine = glm::length(ship_.thrusters);
    t.hull = ship_.hull;
    t.gear = ship_.gear;
    t.landed = ship_.landed;
    t.assist = ship_.assist;
    t.shrinking = ship_.shrinking;
    t.hullMetres = 12 * sizeRatio();
    t.targetHullMetres = 12 * std::pow(10., ship_.targetLogSize);
    t.autoShrink = ship_.autoShrink;
    t.growthBlocked = growthBlocked_;
    t.flightSeconds = ship_.flightSeconds;
    t.travelMetres = ship_.travelMetres;
    t.navigating = autopilot_;
    t.phase = phase_;
    t.logMetres = nav_.logDistance;
    t.scaleRate = scaleRate_;
    t.drive = ship_.shrinking ? (t.driveSpeed > 350 * sizeRatio() * 4 ? "SHRINK FIELD / SCALE TRANSIT"
                                                                      : "SHRUNK SHIP / MANOEUVRE")
              : ship_.cruise  ? "LONG-RANGE CRUISE"
                              : "PLANETARY FLIGHT";
    if (fieldBraking_)
        t.drive = ship_.shrinking ? "SHRINK FIELD / BRAKING"
                  : ship_.cruise  ? "CRUISE / FIELD BRAKING"
                                  : "PLANETARY / BRAKING";
    if (g) {
        t.altitude = g->altitude - collisionRadius();
        t.localUp = glm::inverse(nav_.orientation) * g->normal;
        t.descent = -glm::dot(ship_.velocity, g->normal);
        t.drift = glm::length(ship_.velocity + g->normal * t.descent);
        t.pitch = std::asin(std::clamp(-t.localUp.z, -1., 1.));
        t.roll = std::atan2(t.localUp.x, t.localUp.y);
        t.safe = ship_.gear && std::abs(t.descent) < 2 * sizeRatio() && t.drift < 2 * sizeRatio() &&
                 t.localUp.y > .966 && g->slope < .61;
    }
    if (destination_ || waypoint_) {
        auto delta = destinationPoint() - nav_.cameraPosition();
        t.approachError = glm::length(delta) * std::pow(10., nav_.logDistance);
        if (waypoint_) {
            const auto axis =
                universe_.relative(waypoint_->reference, nav_.anchor, 0).orientation * approachAxis_;
            t.corridorError =
                glm::length(delta - axis * glm::dot(delta, axis)) * std::pow(10., nav_.logDistance);
        }
        if (waypoint_) {
            const auto reference = universe_.describe(waypoint_->reference);
            delta = universe_.pointRelative(waypoint_->reference,
                                            waypoint_->metres / std::pow(10., reference.frame.logRadius),
                                            nav_.anchor, nav_.logDistance) -
                    nav_.cameraPosition();
        }
        t.range = glm::length(delta) * std::pow(10., nav_.logDistance);
        t.localTarget = glm::inverse(nav_.orientation) * delta;
        t.targetVisible = glm::length(delta) > 1e-30;
        t.destination = waypoint_ ? waypoint_->label : universe_.describe(*destination_).label();
    } else if (nav_.selected) {
        const auto delta = universe_.relative(*nav_.selected, nav_.anchor, nav_.logDistance).position -
                           nav_.cameraPosition();
        t.range = glm::length(delta) * std::pow(10., nav_.logDistance);
        t.localTarget = glm::inverse(nav_.orientation) * delta;
        t.targetVisible = glm::length(delta) > 1e-30;
        t.destination = universe_.describe(*nav_.selected).label() + " / J NAVIGATE";
    }
    if (t.targetVisible) {
        const auto targetVelocity =
            waypoint_ ? glm::inverse(nav_.orientation) *
                            (universe_.relative(waypoint_->reference, nav_.anchor, 0).orientation *
                             waypoint_->velocity)
                      : glm::dvec3(0);
        const double closing = -glm::dot(t.localVelocity - targetVelocity, unit(-t.localTarget));
        if (waypoint_ && t.approachError < 5 * sizeRatio())
            t.etaSeconds = 0;
        else if (closing > 1e-30)
            t.etaSeconds = (waypoint_ ? t.approachError : t.range) / closing;
    }
}
std::string Pilot::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    out << "INFINITE_SHIP 5 " << ship_.frame.str();
    for (auto v : {ship_.velocity, ship_.angularVelocity, ship_.thrusters, ship_.positionResidual})
        out << ' ' << v.x << ' ' << v.y << ' ' << v.z;
    out << ' ' << ship_.throttle << ' ' << ship_.boost << ' ' << ship_.hull << ' ' << ship_.assist << ' '
        << ship_.cruise << ' ' << ship_.shrinking << ' ' << ship_.gear << ' ' << ship_.landed;
    out << ' ' << (destination_ ? destination_->str() : "-") << ' '
        << (landingPlanet_ ? landingPlanet_->str() : "-");
    out << ' ' << landingDirection_.x << ' ' << landingDirection_.y << ' ' << landingDirection_.z;
    out << ' ' << ship_.autoShrink << ' ' << ship_.logSize << ' ' << ship_.targetLogSize << ' '
        << ship_.cruiseSpeed << ' ' << ship_.flightSeconds << ' ' << ship_.travelMetres;
    out << ' ' << ship_.angularThrusters.x << ' ' << ship_.angularThrusters.y << ' '
        << ship_.angularThrusters.z << ' ' << ship_.driveBraking << ' ' << ship_.appliedRepairPoints;
    return out.str();
}
bool Pilot::restore(std::string_view text) {
    try {
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        // Formatted stream extraction can reject finite subnormal numbers on
        // libc++. Long-decayed actuator/rate states must still round-trip.
        auto real = [&](double& value) {
            std::string token;
            if (!(in >> token))
                return false;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
            return result.ec == std::errc{} && result.ptr == token.data() + token.size() &&
                   std::isfinite(value);
        };
        auto vector = [&](glm::dvec3& v) { return real(v.x) && real(v.y) && real(v.z); };
        std::string header, frame, destination, planet;
        int version;
        ShipState candidate;
        glm::dvec3 direction;
        if (!(in >> header >> version >> frame) || header != "INFINITE_SHIP" || (version < 1 || version > 5))
            return false;
        candidate.frame = Address::parse(frame);
        for (auto* v : {&candidate.velocity, &candidate.angularVelocity, &candidate.thrusters})
            if (!vector(*v) || !std::isfinite(glm::length(*v)) || glm::length(*v) > 1e30)
                return false;
        if (version >= 2)
            if (!vector(candidate.positionResidual) ||
                !std::isfinite(glm::length(candidate.positionResidual)) ||
                glm::length(candidate.positionResidual) > 1e30)
                return false;
        if (!real(candidate.throttle) || !real(candidate.boost) || !real(candidate.hull) ||
            !(in >> candidate.assist >> candidate.cruise >> candidate.shrinking >> candidate.gear >>
              candidate.landed >> destination >> planet) ||
            !vector(direction))
            return false;
        if (!(candidate.throttle >= 0 && candidate.throttle <= 1 && candidate.boost >= 0 &&
              candidate.boost <= 1 && candidate.hull >= 0 && candidate.hull <= 100) ||
            !std::isfinite(glm::length(direction)) || glm::length(direction) < .5)
            return false;
        auto dest = destination == "-" ? std::optional<Address>{} : Address::parse(destination);
        auto body = planet == "-" ? std::optional<Address>{} : Address::parse(planet);
        if (version >= 3) {
            if (!(in >> candidate.autoShrink) || !real(candidate.logSize) || !real(candidate.targetLogSize) ||
                !real(candidate.cruiseSpeed) || !real(candidate.flightSeconds) ||
                !real(candidate.travelMetres))
                return false;
            if (!(candidate.logSize >= -12 && candidate.logSize <= 0 && candidate.targetLogSize >= -12 &&
                  candidate.targetLogSize <= 0 && candidate.cruiseSpeed >= 1e6 &&
                  candidate.cruiseSpeed <= 1e28 && candidate.flightSeconds >= 0 &&
                  candidate.travelMetres >= 0) ||
                !std::isfinite(candidate.flightSeconds) || !std::isfinite(candidate.travelMetres) ||
                (!candidate.shrinking && candidate.logSize != 0) || (candidate.shrinking && candidate.cruise))
                return false;
        } else {
            candidate.cruise = false;
            if (candidate.shrinking) {
                const auto n = universe_.describe(nav_.anchor);
                candidate.logSize = candidate.targetLogSize = std::clamp(
                    std::log10(std::min(.012, material(n.environment.mineral).cellMetres * .08) / 12.), -12.,
                    -3.);
                candidate.landed = false;
            }
        }
        if (version >= 4) {
            if (!vector(candidate.angularThrusters) || !(in >> candidate.driveBraking) ||
                !std::isfinite(glm::length(candidate.angularThrusters)) ||
                glm::length(candidate.angularThrusters) > 1.000001)
                return false;
        } else if (!candidate.cruise && candidate.assist && glm::length(candidate.velocity) > 85000)
            candidate.driveBraking = true;
        if (version >= 5 &&
            (!(in >> candidate.appliedRepairPoints) || candidate.appliedRepairPoints > 1000000000000ULL))
            return false;
        universe_.describe(candidate.frame);
        if (dest)
            universe_.describe(*dest);
        if (body)
            universe_.describe(*body);
        if ((version < 4 ? glm::length(candidate.thrusters) > 1.000001
                         : std::abs(candidate.thrusters.z) > 1.000001 ||
                               candidate.thrusters.x * candidate.thrusters.x +
                                       candidate.thrusters.y * candidate.thrusters.y +
                                       std::pow(std::max(0., candidate.thrusters.z), 2) +
                                       glm::dot(candidate.angularThrusters, candidate.angularThrusters) >
                                   1.000001))
            return false;
        in >> std::ws;
        if (!in.eof() || (body && body->kind() != Kind::Planet) ||
            (candidate.landed && (!body || !candidate.gear)))
            return false;
        ship_ = candidate;
        clearAssistCapture();
        waypoint_.reset();
        destination_ = dest;
        landingPlanet_ = body;
        landingDirection_ = unit(direction);
        autopilot_ = landing_ = false;
        launchTime_ = 0;
        phase_ = ship_.landed ? "LANDED / ENTER TO LAUNCH" : "MANUAL / FLIGHT RESTORED";
        instrument();
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace infinite
