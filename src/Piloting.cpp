#include "infinite/Piloting.hpp"
#include <algorithm>
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
    ship_.frame = nav_.anchor;
    ship_.velocity = ship_.angularVelocity = ship_.thrusters = ship_.positionResidual = {};
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
    autopilot_ = landing_ = false;
    launchTime_ = 0;
    ship_.throttle = 0;
    ship_.assist = true;
    phase_ = "BRAKING";
}
void Pilot::toggleAssist() {
    ship_.assist = !ship_.assist;
    autopilot_ = landing_ = false;
    if (ship_.assist)
        ship_.throttle = std::clamp(
            glm::dot(ship_.velocity, nav_.orientation * glm::dvec3(0, 0, -1)) / speedLimit(), 0., 1.);
    phase_ = ship_.assist ? "ASSIST / MATCHING VELOCITY" : "INERTIAL / MOMENTUM RETAINED";
}
void Pilot::toggleSurvey() {
    if (ship_.survey) {
        if (auto g = ground(); g && g->altitude < 2.) {
            phase_ = "PROBE / CLEAR SOLID BEFORE SHIP MODE";
            instrument();
            return;
        }
    }
    ship_.survey = !ship_.survey;
    ship_.landed = false;
    autopilot_ = landing_ = false;
    phase_ = ship_.survey ? "SURVEY / VIRTUAL PROBE" : "SHIP / PHYSICAL CONTACT";
}
double Pilot::speedLimit() const {
    const double scale = std::max(1e-12, spatial_.clearanceMetres() * .32);
    const double base = ship_.survey ? scale : (ship_.cruise ? std::max(350., scale) : 350.);
    double limit = std::min(1e28, base * nav_.speedMultiplier() * (1 + 1.4 * ship_.boost));
    if (!ship_.survey)
        if (auto g = ground(); g && g->altitude < 10000)
            limit = std::min(limit, 350. + std::max(0., g->altitude - 100.) * .12);
    return limit;
}
glm::dvec3 Pilot::gravityAt(const Ground& g) const {
    const double radius = std::pow(10., g.planet.frame.logRadius),
                 r = std::max(radius * .5, radius + g.altitude);
    const double density = material(g.planet.environment.mineral).density;
    const auto pose = universe_.relative(g.planet.address, nav_.anchor, nav_.logDistance);
    return -unit(pose.orientation * g.direction) *
           (4.188790204786391 * 6.67430e-11 * density * radius * radius * radius / (r * r));
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
    a.validate();
    if (ship_.landed)
        launch();
    destination_ = a;
    autopilot_ = true;
    landing_ = false;
    ship_.assist = true;
    ship_.landed = false;
    if (auto planet = planetOf(a); planet && a.kind() <= Kind::Mountain && !ship_.survey) {
        landingPlanet_ = planet;
        const auto p = universe_.describe(*planet);
        const auto pose = universe_.relative(*planet, nav_.anchor, nav_.logDistance);
        landingDirection_ =
            a.kind() >= Kind::Surface
                ? universe_.describe(a).geographicDirection
                : unit(glm::inverse(pose.orientation) * (nav_.cameraPosition() - pose.position));
    } else
        landingPlanet_.reset();
    phase_ = "DEPARTURE / ALIGNING";
    instrument();
}
bool Pilot::land() {
    auto g = ground();
    if (!g || ship_.survey || g->slope > .61)
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
void Pilot::launch() {
    if (!ship_.landed)
        return;
    ship_.landed = false;
    ship_.assist = true;
    ship_.throttle = 0;
    autopilot_ = landing_ = false;
    launchTime_ = 4;
    phase_ = "LAUNCH / VERTICAL DEPARTURE";
}
glm::dvec3 Pilot::destinationPoint() const {
    if (landingPlanet_) {
        const auto p = universe_.describe(*landingPlanet_);
        const double radius = std::pow(10., p.frame.logRadius);
        const double h = universe_.terrainHeight(p, landingDirection_);
        return universe_.pointRelative(p.address,
                                       landingDirection_ * (1 + (h + (landing_ ? 2. : 5000.)) / radius),
                                       nav_.anchor, nav_.logDistance);
    }
    if (!destination_)
        return nav_.cameraPosition();
    const auto node = universe_.describe(*destination_);
    const auto pose = universe_.relative(*destination_, nav_.anchor, nav_.logDistance);
    const auto from = nav_.cameraPosition() - pose.position;
    return pose.position + unit(from) * std::pow(10., node.frame.logRadius - nav_.logDistance) * 2.5;
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
    ship_.boost += ((input.boost && !input.precision && !autopilot_ ? 1. : 0.) - ship_.boost) *
                   (1 - std::exp(-dt / .18));
    ship_.throttle = std::clamp(ship_.throttle + input.throttle * dt * .36, 0., 1.);
    const auto beforeGround = ship_.survey ? std::optional<Ground>{} : ground();
    const auto up = beforeGround ? beforeGround->normal : nav_.orientation * glm::dvec3(0, 1, 0);
    double limit = speedLimit() * (input.precision ? .18 : 1.);
    if (beforeGround && beforeGround->altitude < 10000)
        limit = std::min(limit, 350. + std::max(0., beforeGround->altitude - 100.) * .12);
    // Hyperion's asymmetric jet envelope, rate governor and 120 ms spool.
    const double gain =
        std::max(ship_.survey ? 1e-14 : 1., std::max(limit, glm::length(ship_.velocity) * .7) / 85.);
    const glm::dvec3 caps{22 * gain, 26 * gain, 52 * gain};
    const double retro = 32 * gain;
    glm::dvec3 wanted = nav_.orientation * glm::dvec3(input.strafe * limit * .28, input.lift * limit * .30,
                                                      -ship_.throttle * limit);
    glm::dvec3 spin{input.pitch * 1.05, -input.yaw * .95, -input.roll * 1.2};
    if (input.precision)
        spin *= .35;
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
        if (landing_ && beforeGround && beforeGround->altitude < 500) {
            auto forward = nav_.orientation * glm::dvec3(0, 0, -1);
            heading = unit(forward - up * glm::dot(forward, up),
                           unit(glm::cross(up, glm::dvec3(1, 0, 0)), {0, 0, -1}));
        }
        spin = angularTarget(nav_.orientation, heading, up);
        double speed = std::min(limit, std::sqrt(std::max(0., 2 * retro * distance)) * .55);
        if (landing_)
            speed = std::min(speed, std::max(.04, distance * .65));
        if (landing_ && distance < 100)
            speed = std::min(speed, 1.5);
        const double alignment = std::max(0., glm::dot(nav_.orientation * glm::dvec3(0, 0, -1), direction));
        if (!landing_ || distance > 500)
            speed *= alignment * alignment;
        wanted = direction * speed;
        ship_.throttle = std::clamp(speed / limit, 0., 1.);
        phase_ = landing_                                 ? "LANDING / CONTROLLED DESCENT"
                 : distance < std::max(10000., limit * 4) ? "APPROACH / BRAKING"
                                                          : "TRANSIT / NAVIGATING";
        if (!landing_ &&
            distance < std::clamp(std::pow(10., universe_.describe(*destination_).frame.logRadius) * .00001,
                                  2., 500.) &&
            glm::length(ship_.velocity) < std::max(2., limit * .005)) {
            autopilot_ = false;
            ship_.throttle = 0;
            wanted = {};
            phase_ = "ARRIVED / L SELECTS LANDING SITE";
        }
    }
    if (launchTime_ > 0) {
        wanted = up * 8.;
        spin = {};
        launchTime_ = std::max(0., launchTime_ - dt);
        if (launchTime_ == 0) {
            ship_.gear = false;
            phase_ = "DEPARTURE COMPLETE";
        }
    }
    auto angularAcceleration =
        limited((ship_.assist ? (spin - ship_.angularVelocity) * 9. : spin * 3.5), 4.2);
    auto nextSpin = ship_.angularVelocity + angularAcceleration * dt;
    if (!ship_.assist)
        nextSpin = limited(nextSpin, std::max(2.2, glm::length(ship_.angularVelocity)));
    const auto midSpin = (ship_.angularVelocity + nextSpin) * .5;
    ship_.angularVelocity = nextSpin;
    const auto angle = glm::length(midSpin) * dt;
    if (angle > 1e-15)
        nav_.orientation = glm::normalize(nav_.orientation * glm::angleAxis(angle, glm::normalize(midSpin)));
    // Ship-only point gravity; no world descriptor or orbit is advanced.
    const auto gravity = beforeGround ? gravityAt(*beforeGround) : glm::dvec3(0);
    auto local =
        glm::inverse(nav_.orientation) * (ship_.assist ? (wanted - ship_.velocity) * 1.6 : wanted * .6);
    auto support = ship_.assist ? glm::inverse(nav_.orientation) * -gravity : glm::dvec3(0);
    local = allocateForce(support, local, caps, retro);
    // Body -Z is forward. Fit all commands into one convex force envelope.
    glm::dvec3 demand{local.x / caps.x, local.y / caps.y, local.z / (local.z < 0 ? caps.z : retro)};
    demand = limited(demand, 1.);
    ship_.thrusters = glm::mix(ship_.thrusters, demand, 1 - std::exp(-dt / .12));
    const auto thrust =
        nav_.orientation * glm::dvec3(ship_.thrusters.x * caps.x, ship_.thrusters.y * caps.y,
                                      ship_.thrusters.z * (ship_.thrusters.z < 0 ? caps.z : retro));
    const auto oldVelocity = ship_.velocity;
    ship_.velocity += (thrust + gravity) * dt;
    // The fictional cruise/probe field has its own proximity braking. It acts
    // on SI momentum through a clearance-dependent acceleration, never through
    // render units. This lets it shed interstellar speed before local thrusters
    // take over; assistance-off vacuum flight retains its momentum.
    fieldBraking_ = false;
    const double speed = glm::length(ship_.velocity);
    if (ship_.assist && (ship_.cruise || ship_.survey) && speed > limit * 1.05) {
        const double tau = std::max(1e-100, .35 * spatial_.clearanceMetres() / speed);
        const double next = limit + (speed - limit) * std::exp(-dt / tau);
        ship_.velocity *= next / speed;
        fieldBraking_ = true;
    }
    const double units = std::pow(10., nav_.logDistance);
    const auto displacement = (oldVelocity + ship_.velocity) * (.5 * dt) + ship_.positionResidual;
    auto proposed = oldEye + displacement / units;
    nav_.offset = proposed - nav_.orientation * glm::dvec3(0, 0, 1);
    // Retain sub-ULP travel instead of discarding small metre displacements
    // while the camera happens to use galactic-sized units.
    ship_.positionResidual = displacement - (nav_.cameraPosition() - oldEye) * units;
    if (!ship_.survey) {
        if (auto g = ground(); g && g->altitude < 2.06) {
            const double sink = -glm::dot(ship_.velocity, g->normal);
            const double drift = glm::length(ship_.velocity + g->normal * sink);
            const double tilt =
                std::acos(std::clamp(glm::dot(nav_.orientation * glm::dvec3(0, 1, 0), g->normal), -1., 1.));
            const bool safe = ship_.gear && std::abs(sink) < 2. && drift < 2. && tilt < .26 && g->slope < .61;
            if (launchTime_ <= 0 && (g->altitude < 2. || (landing_ && safe))) {
                nav_.offset = g->point + g->normal * (2. / std::pow(10., nav_.logDistance)) -
                              nav_.orientation * glm::dvec3(0, 0, 1);
                ship_.positionResidual = {};
                if (safe) {
                    ship_.velocity = ship_.angularVelocity = ship_.thrusters = {};
                    ship_.throttle = 0;
                    ship_.landed = true;
                    autopilot_ = landing_ = false;
                    landingPlanet_ = g->planet.address;
                    landingDirection_ = g->direction;
                    phase_ = "LANDED / ENTER TO LAUNCH";
                } else {
                    ship_.velocity += g->normal * std::max(0., sink);
                    ship_.velocity *= .5;
                    ship_.hull = std::max(0., ship_.hull - std::max(0., sink - 2.) * .3);
                    phase_ = "SURFACE CONTACT / GEAR / LEVEL / SLOW";
                }
            }
        }
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
    synchronizeFrame();
    nav_.stopMotion();
    spatial_.resume();
    precisionScale_ = input.precision ? .18 : 1.;
    const double initialLog = nav_.logDistance;
    double remaining = std::min(dt, .1);
    for (int i = 0; remaining > 1e-9 && i < 32; ++i) {
        spatial_.update({}, std::min(.01, remaining), false, false, visible);
        synchronizeFrame();
        const double speed = glm::length(ship_.velocity);
        const double safeStep = std::max(1e-100, spatial_.clearanceMetres() * .08 / std::max(1e-30, speed));
        const double stepTime = std::min({.01, remaining, safeStep});
        step(bounded, stepTime);
        remaining -= stepTime;
    }
    scaleRate_ = (nav_.logDistance - initialLog) / dt;
    lastLog_ = nav_.logDistance;
    instrument();
}
void Pilot::instrument() {
    synchronizeFrame();
    instruments_ = {};
    auto& t = instruments_;
    t.localVelocity = glm::inverse(nav_.orientation) * ship_.velocity;
    t.speed = glm::length(ship_.velocity);
    t.driveSpeed = speedLimit() * precisionScale_;
    const double gain = std::max(ship_.survey ? 1e-14 : 1., std::max(t.driveSpeed, t.speed * .7) / 85.);
    const glm::dvec3 caps{22 * gain, 26 * gain, 52 * gain};
    const auto g = ground();
    const auto gravity = g && !ship_.survey ? gravityAt(*g) : glm::dvec3(0);
    if (t.speed > 1e-30) {
        const auto inverse = glm::inverse(nav_.orientation);
        const auto direction = ship_.velocity / t.speed;
        const auto force =
            allocateForce(inverse * -gravity, inverse * (-direction * caps.z * 200.), caps, 32 * gain);
        const double a = glm::dot(nav_.orientation * force + gravity, -direction);
        const auto thrust =
            nav_.orientation * glm::dvec3(ship_.thrusters.x * caps.x, ship_.thrusters.y * caps.y,
                                          ship_.thrusters.z * (ship_.thrusters.z < 0 ? caps.z : 32 * gain));
        const double delay = .18 + std::max(0., glm::dot(thrust, direction)) / std::max(1e-30, a) * .12;
        t.stoppingDistance = a > 1e-30 ? t.speed * delay + t.speed * t.speed / (2 * a) : 1e30;
    }
    t.throttle = ship_.throttle;
    t.engine = glm::length(ship_.thrusters);
    t.hull = ship_.hull;
    t.gear = ship_.gear;
    t.landed = ship_.landed;
    t.assist = ship_.assist;
    t.survey = ship_.survey;
    t.navigating = autopilot_;
    t.phase = phase_;
    t.logMetres = nav_.logDistance;
    t.scaleRate = scaleRate_;
    t.drive = ship_.survey          ? "SURVEY / VIRTUAL PROBE"
              : t.driveSpeed > 1400 ? "CRUISE / SCALE DRIVE"
                                    : "LOCAL / SHIP";
    if (fieldBraking_)
        t.drive = ship_.survey ? "SURVEY / FIELD BRAKING" : "CRUISE / FIELD BRAKING";
    if (g) {
        t.altitude = g->altitude - 2.;
        t.localUp = glm::inverse(nav_.orientation) * g->normal;
        t.descent = -glm::dot(ship_.velocity, g->normal);
        t.drift = glm::length(ship_.velocity + g->normal * t.descent);
        t.pitch = std::asin(std::clamp(-t.localUp.z, -1., 1.));
        t.roll = std::atan2(t.localUp.x, t.localUp.y);
        t.safe = ship_.gear && std::abs(t.descent) < 2 && t.drift < 2 && t.localUp.y > .966 && g->slope < .61;
    }
    if (destination_) {
        const auto delta = destinationPoint() - nav_.cameraPosition();
        t.range = glm::length(delta) * std::pow(10., nav_.logDistance);
        t.localTarget = glm::inverse(nav_.orientation) * delta;
        t.targetVisible = glm::length(delta) > 1e-30;
        t.destination = universe_.describe(*destination_).label();
    } else if (nav_.selected) {
        const auto delta = universe_.relative(*nav_.selected, nav_.anchor, nav_.logDistance).position -
                           nav_.cameraPosition();
        t.range = glm::length(delta) * std::pow(10., nav_.logDistance);
        t.localTarget = glm::inverse(nav_.orientation) * delta;
        t.targetVisible = glm::length(delta) > 1e-30;
        t.destination = universe_.describe(*nav_.selected).label() + " / J NAVIGATE";
    }
}
std::string Pilot::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    out << "INFINITE_SHIP 2 " << ship_.frame.str();
    for (auto v : {ship_.velocity, ship_.angularVelocity, ship_.thrusters, ship_.positionResidual})
        out << ' ' << v.x << ' ' << v.y << ' ' << v.z;
    out << ' ' << ship_.throttle << ' ' << ship_.boost << ' ' << ship_.hull << ' ' << ship_.assist << ' '
        << ship_.cruise << ' ' << ship_.survey << ' ' << ship_.gear << ' ' << ship_.landed;
    out << ' ' << (destination_ ? destination_->str() : "-") << ' '
        << (landingPlanet_ ? landingPlanet_->str() : "-");
    out << ' ' << landingDirection_.x << ' ' << landingDirection_.y << ' ' << landingDirection_.z;
    return out.str();
}
bool Pilot::restore(std::string_view text) {
    try {
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string header, frame, destination, planet;
        int version;
        ShipState candidate;
        glm::dvec3 direction;
        if (!(in >> header >> version >> frame) || header != "INFINITE_SHIP" ||
            (version != 1 && version != 2))
            return false;
        candidate.frame = Address::parse(frame);
        for (auto* v : {&candidate.velocity, &candidate.angularVelocity, &candidate.thrusters})
            if (!(in >> v->x >> v->y >> v->z) || !std::isfinite(glm::length(*v)) || glm::length(*v) > 1e30)
                return false;
        if (version == 2)
            if (!(in >> candidate.positionResidual.x >> candidate.positionResidual.y >>
                  candidate.positionResidual.z) ||
                !std::isfinite(glm::length(candidate.positionResidual)) ||
                glm::length(candidate.positionResidual) > 1e30)
                return false;
        if (!(in >> candidate.throttle >> candidate.boost >> candidate.hull >> candidate.assist >>
              candidate.cruise >> candidate.survey >> candidate.gear >> candidate.landed >> destination >>
              planet >> direction.x >> direction.y >> direction.z))
            return false;
        if (!(candidate.throttle >= 0 && candidate.throttle <= 1 && candidate.boost >= 0 &&
              candidate.boost <= 1 && candidate.hull >= 0 && candidate.hull <= 100) ||
            !std::isfinite(glm::length(direction)) || glm::length(direction) < .5)
            return false;
        auto dest = destination == "-" ? std::optional<Address>{} : Address::parse(destination);
        auto body = planet == "-" ? std::optional<Address>{} : Address::parse(planet);
        universe_.describe(candidate.frame);
        if (dest)
            universe_.describe(*dest);
        if (body)
            universe_.describe(*body);
        if (glm::length(candidate.thrusters) > 1.000001 || glm::length(candidate.angularVelocity) > 100)
            return false;
        in >> std::ws;
        if (!in.eof() || (body && body->kind() != Kind::Planet) ||
            (candidate.landed && (!body || !candidate.gear || candidate.survey)))
            return false;
        ship_ = candidate;
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
