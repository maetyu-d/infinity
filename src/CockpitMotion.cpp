#include "infinite/CockpitMotion.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinite {
namespace {
bool finite(glm::dvec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
glm::dvec3 bounded(glm::dvec3 v, double limit) {
    const double length = glm::length(v);
    return length > limit ? v * (limit / length) : v;
}
} // namespace
void CockpitMotion::Spring::advance(glm::dvec3 target, double omega, double damping, double dt) {
    // Exact solution of an underdamped spring for a held target; no Euler
    // instability, extra simulation tasks or frame-dependent random samples.
    const double decay = std::exp(-damping * omega * dt);
    const double frequency = omega * std::sqrt(1 - damping * damping);
    const double c = std::cos(frequency * dt), s = std::sin(frequency * dt);
    const auto error = position - target;
    const auto b = (velocity + damping * omega * error) / frequency;
    position = target + (error * c + b * s) * decay;
    velocity = ((-error * frequency * s + b * frequency * c) - damping * omega * (error * c + b * s)) * decay;
}
void CockpitMotion::reset() {
    *this = {};
}
void CockpitMotion::impulse(CockpitImpulse type, double strength, glm::dvec3 direction) {
    if (!std::isfinite(strength) || strength <= 0 || !finite(direction))
        return;
    strength = std::clamp(strength, 0., 1.);
    const double side = std::sin(double(++impulses_ % 1024) * 2.39996323);
    if (type == CockpitImpulse::Recoil) {
        shock_.velocity += glm::dvec3(.070, side * .012, -side * .009) * strength;
        jolt_.velocity += glm::dvec3(side * .005, .006, .065) * strength;
    } else {
        const double length = glm::length(direction);
        const auto d = length > 1e-12 ? direction / length : glm::dvec3(side * .5, .6, .5);
        const double gain = type == CockpitImpulse::Damage ? .8 : 1.;
        shock_.velocity += glm::dvec3(-d.y * .16, d.x * .15, d.x * .12 + side * .025) * strength * gain;
        jolt_.velocity -= d * (.10 * strength * gain);
    }
    // Automatic fire or repeated collision substeps cannot accumulate an
    // unbounded camera kick. The physical ship is never clamped here.
    shock_.velocity = bounded(shock_.velocity, .24);
    jolt_.velocity = bounded(jolt_.velocity, .14);
}
void CockpitMotion::update(const CockpitMotionInput& in, double dt) {
    if (!std::isfinite(dt) || dt <= 0 || !finite(in.acceleration) || !finite(in.angularRate) ||
        !finite(in.contactImpulse) || !std::isfinite(in.hullMetres) || in.hullMetres <= 0 ||
        !std::isfinite(in.hull) || !std::isfinite(in.engine))
        return;
    dt = std::min(dt, .1);
    const double scale = std::clamp(in.hullMetres / 12, 1e-12, 1.);
    const double gravityUnit = 9.80665 * scale;
    const auto g = bounded(
        glm::clamp(in.acceleration, glm::dvec3(-gravityUnit * 8), glm::dvec3(gravityUnit * 8)) / gravityUnit,
        8.);
    const auto rate = bounded(in.angularRate, 3.);
    const auto angularAcceleration = initialized_ ? bounded((rate - previousRate_) / dt, 10.) : glm::dvec3(0);
    const bool contact = initialized_ && in.contactSequence != previousContact_;
    if (contact) {
        const double strength = std::clamp(glm::length(in.contactImpulse) / 20., 0., 1.);
        impulse(CockpitImpulse::Impact, strength, in.contactImpulse);
    }
    if (initialized_ && in.hull < previousHull_ && !contact)
        impulse(CockpitImpulse::Damage, (previousHull_ - in.hull) / 12.);
    if (initialized_ && in.landed && !previouslyLanded_ && !contact)
        impulse(CockpitImpulse::Impact, .10, {0, 1, 0});
    previousHull_ = in.hull;
    previousContact_ = in.contactSequence;
    previouslyLanded_ = in.landed;
    previousRate_ = rate;
    initialized_ = true;
    time_ = std::fmod(time_ + dt, 600.);
    const auto rotation = bounded(glm::dvec3(-g.y * .0007, g.x * .00065, -g.x * .0009) - rate * .0018 -
                                      angularAcceleration * .00030,
                                  .0065);
    const auto translation =
        bounded(-g * .00135 - glm::cross(angularAcceleration, glm::dvec3(0, .1, .35)) * .0006, .007);
    lean_.advance(in.landed ? glm::dvec3(0) : rotation, 13., .82, dt);
    slide_.advance(in.landed ? glm::dvec3(0) : translation, 15., .85, dt);
    shock_.advance({}, 31., .60, dt);
    jolt_.advance({}, 34., .64, dt);
    const double load = in.landed ? 0 : std::clamp((glm::length(g) - 1.3) / 4.5, 0., 1.);
    const double motor = in.landed ? 0 : std::clamp(in.engine, 0., 1.) * .16;
    // Smooth bounded vibration, attenuated below Nyquist on slow frames.
    auto wave = [&](double hz, double phase) {
        const double fade = 1 - smoothstep(.30, .48, hz * dt);
        return std::sin(time_ * (2 * std::numbers::pi * hz) + phase) * fade;
    };
    const glm::dvec3 rattle(wave(6.1, .4), wave(7.7, 1.7), wave(9.3, 2.9));
    const double vibration = load * .00055 + motor * .00030;
    pose_.angles = bounded(lean_.position + shock_.position + rattle * vibration, .010);
    pose_.translation = bounded(slide_.position + jolt_.position + rattle * (vibration * .35), .009);
    pose_.orientation = glm::normalize(glm::dquat(pose_.angles));
}
} // namespace infinite
