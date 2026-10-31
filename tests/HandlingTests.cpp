#include "infinite/Piloting.hpp"
#include <iomanip>
#include <iostream>
#include <limits>
#include <oneapi/tbb/global_control.h>
#include <sstream>
#include <stdexcept>

using namespace infinite;
namespace {
size_t checks = 0;
void check(bool b, const char* message) {
    ++checks;
    if (!b)
        throw std::runtime_error(message);
}
struct Rig {
    Navigation nav;
    RegionCache cache;
    FlightController spatial;
    Pilot pilot;
    explicit Rig(const Universe& u) : nav(u), cache(u), spatial(u, nav, cache), pilot(u, nav, spatial) {
        check(nav.restore("0,0,0 24 1 0 0 0 0 0 3"), "Invalid vacuum handling fixture");
        pilot.resetMotion();
    }
    void fly(const HelmInput& h, double seconds, double dt = 1. / 60) {
        for (int i = 0; i < int(std::round(seconds / dt)); ++i)
            pilot.update(h, dt);
    }
};
struct Response {
    double rate = 0, settle = 0, overshoot = 0, reverse = 0;
};
Response response(const Universe& u, double dt, double direction = 1, bool precision = false, int axis = 2,
                  double strength = 1, double cargoKg = 0) {
    Rig r(u);
    r.pilot.setCargoMass(cargoKg);
    HelmInput h;
    auto& command = axis == 0 ? h.pitch : axis == 1 ? h.yaw : h.roll;
    command = direction * strength;
    h.precision = precision;
    const auto forward = r.nav.orientation * glm::dvec3(0, 0, -1);
    r.fly(h, 1, dt);
    Response result;
    result.rate = std::abs(r.pilot.ship().angularVelocity[axis]);
    check(axis != 2 || glm::dot(forward, r.nav.orientation * glm::dvec3(0, 0, -1)) > 1 - 1e-10,
          "Pure roll changed the physical forward heading");
    command = 0;
    double previous = result.rate;
    while (result.settle < 2 && std::abs(r.pilot.ship().angularVelocity[axis]) > .02) {
        r.pilot.update(h, dt);
        const auto rate = std::abs(r.pilot.ship().angularVelocity[axis]);
        result.overshoot += (previous + rate) * .5 * dt;
        previous = rate;
        result.settle += dt;
    }
    command = direction * strength;
    r.fly(h, 1, dt);
    command = -direction * strength;
    while (result.reverse < 2 &&
           r.pilot.ship().angularVelocity[axis] * direction * (axis == 0 ? 1 : -1) > 0) {
        r.pilot.update(h, dt);
        result.reverse += dt;
    }
    return result;
}
} // namespace
int main(int argc, char**) {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        const auto r = response(u, 1. / 60);
        const auto pitch = response(u, 1. / 60, 1, false, 0);
        const auto yaw = response(u, 1. / 60, 1, false, 1);
        const auto nudge = response(u, 1. / 60, 1, false, 1, .15);
        Rig combined(u);
        HelmInput combinedInput;
        combinedInput.pitch = combinedInput.yaw = combinedInput.roll = 1;
        double peakCombined = 0;
        for (int i = 0; i < 180; ++i) {
            combined.pilot.update(combinedInput, 1. / 60);
            peakCombined = std::max(peakCombined, glm::length(combined.pilot.ship().angularVelocity));
        }
        const auto initialCombined = peakCombined;
        combined.fly({}, .5);
        const double combinedResidual = glm::length(combined.pilot.ship().angularVelocity);
        Rig capture(u);
        capture.pilot.toggleAssist();
        const glm::dvec3 initial{1200, -400, -200};
        capture.pilot.setVelocity(initial);
        capture.pilot.toggleAssist();
        capture.pilot.update({}, 1. / 60);
        const double assistKick = glm::length(capture.pilot.ship().velocity - initial);
        capture.fly({}, 2);
        const auto local = glm::inverse(capture.nav.orientation) * capture.pilot.ship().velocity;
        const double residual = glm::length(glm::dvec2(local));
        Rig corner(u);
        HelmInput h;
        h.throttle = 1;
        corner.fly(h, 6);
        h.throttle = 0;
        h.yaw = 1;
        corner.fly(h, 1);
        const auto v = glm::inverse(corner.nav.orientation) * corner.pilot.ship().velocity;
        const auto slip = std::atan2(glm::length(glm::dvec2(v)), -v.z);
        std::cout << std::setprecision(12) << "{\"roll_rate_rad_s\":" << r.rate
                  << ",\"roll_settle_s\":" << r.settle << ",\"release_overshoot_rad\":" << r.overshoot
                  << ",\"roll_reverse_s\":" << r.reverse << ",\"assist_capture_kick_m_s\":" << assistKick
                  << ",\"assist_residual_drift_m_s\":" << residual << ",\"turn_slip_rad\":" << slip
                  << ",\"pitch_rate_rad_s\":" << pitch.rate << ",\"yaw_rate_rad_s\":" << yaw.rate
                  << ",\"small_yaw_rate_rad_s\":" << nudge.rate
                  << ",\"combined_peak_rate_rad_s\":" << initialCombined
                  << ",\"combined_release_residual_rad_s\":" << combinedResidual << "}\n";
        if (argc > 1)
            return 0; // --measure records the baseline without the new handling thresholds.
        check(r.rate > .94 && r.rate < .96, "Roll is outside the controllable assisted turn envelope");
        check(r.settle <= .2 && r.overshoot < .07, "Released roll feels loose or overshoots excessively");
        check(r.reverse < .2, "Roll reversal is too slow");
        check(pitch.rate > .69 && pitch.rate < .71 && yaw.rate > .64 && yaw.rate < .66,
              "Pitch/yaw is outside the controllable assisted turn envelope");
        check(nudge.rate > .02 && nudge.rate < yaw.rate * .08,
              "Small mouse corrections lack a fine centre response");
        check(peakCombined < 1 && combinedResidual < .001,
              "Simultaneous axes multiply turn speed or leave the ship tumbling on release");
        const double initialDrift = glm::length(glm::dvec2(initial));
        check(assistKick < .2 && residual < initialDrift && residual > initialDrift - 100,
              "Assist capture kicked, failed to counter drift, or exceeded finite RCS authority");
        for (double dt : {1. / 15, 1. / 30, 1. / 144}) {
            const auto other = response(u, dt);
            std::cout << "response_dt=" << dt << " rate=" << other.rate << " settle=" << other.settle
                      << " overshoot=" << other.overshoot << '\n';
            check(std::abs(other.rate - r.rate) < .01 &&
                      std::abs(other.settle - r.settle) <= std::max(.04, dt) &&
                      std::abs(other.overshoot - r.overshoot) < .02,
                  "Rotational handling depends on display frame rate");
        }
        // A fully loaded bay and low display frame rate must still arrest each
        // axis promptly; the force/torque model retains the cargo's real inertia.
        for (int axis = 0; axis < 3; ++axis)
            for (double dt : {1. / 15, 1. / 60, 1. / 144}) {
                const auto laden = response(u, dt, 1, false, axis, 1, 4000);
                check(laden.settle <= .3 && laden.overshoot < .1 && laden.reverse < .3,
                      "Cargo or a low frame rate makes rotation difficult to arrest");
            }
        const auto left = response(u, 1. / 60, -1);
        check(std::abs(left.rate - r.rate) < 1e-10 && std::abs(left.settle - r.settle) < 1e-10,
              "Left and right roll are asymmetric");
        const auto fine = response(u, 1. / 60, 1, true);
        check(fine.rate < r.rate * .36 && fine.rate > r.rate * .34,
              "Precision handling does not reduce roll authority");
        check(slip > .2 && slip < .65, "Turn lost finite-thrust inertia or left excessive lateral slip");
        // The actual SDL input path calls this same resolver. Roll remains
        // available without mouse capture, and its modifier cannot fire strafe.
        HelmInput keys;
        keys.strafe = 1;
        keys.lift = -1;
        const auto bank = resolveHelmInput(keys, {}, 1. / 60, true);
        check(bank.roll == 1 && bank.strafe == 0 && bank.lift == -1,
              "Alt+D failed to roll or stole the Q/E lift axis");
        keys.strafe = -1;
        check(resolveHelmInput(keys, {}, 1. / 60, true).roll == -1, "Alt+A failed to roll left");
        check(resolveHelmInput(keys, {}, 1. / 60).strafe == -1 &&
                  resolveHelmInput(keys, {}, 1. / 60).roll == 0,
              "Unmodified A/D stopped strafing");
        keys = {};
        keys.roll = -1;
        check(resolveHelmInput(keys, {}, 1. / 60).roll == -1, "Dedicated U roll requires mouse capture");
        keys.strafe = 1;
        check(resolveHelmInput(keys, {}, 1. / 60, true).roll == 0,
              "Opposing dedicated and modified roll inputs failed to cancel");
        for (double dt : {1. / 30, 1. / 60, 1. / 144}) {
            const auto look = resolveHelmInput({}, {150 * dt, -80 * dt}, dt);
            check(std::abs(look.yaw - .225) < 1e-12 && std::abs(look.pitch - .12) < 1e-12,
                  "Mouse distance was treated as a frame-dependent angular command");
        }
        keys.roll = 1;
        const auto badMouse = resolveHelmInput(keys, {std::numeric_limits<double>::quiet_NaN(), 0}, 0);
        check(badMouse.roll == 1 && badMouse.yaw == 0 && badMouse.pitch == 0,
              "Invalid mouse sample poisoned the keyboard helm");
        // A full barrel roll must pass through inverted flight without an
        // attitude clamp or an assistance-induced return to a preferred up.
        Rig barrel(u);
        keys = {};
        keys.roll = 1;
        double angle = 0, minimumUp = 1;
        const auto initialUp = barrel.nav.orientation * glm::dvec3(0, 1, 0);
        const auto initialForward = barrel.nav.orientation * glm::dvec3(0, 0, -1);
        for (int i = 0; i < 480; ++i) {
            const auto oldRate = barrel.pilot.ship().angularVelocity.z;
            barrel.pilot.update(keys, 1. / 60);
            angle -= (oldRate + barrel.pilot.ship().angularVelocity.z) / 120;
            minimumUp =
                std::min(minimumUp, glm::dot(initialUp, barrel.nav.orientation * glm::dvec3(0, 1, 0)));
        }
        check(angle > 6.283185307 && minimumUp < -.99 &&
                  glm::dot(initialForward, barrel.nav.orientation * glm::dvec3(0, 0, -1)) > 1 - 1e-10,
              "Ship cannot complete an unrestricted 360-degree barrel roll");
        check(std::abs(barrel.pilot.instruments().rollRate + barrel.pilot.ship().angularVelocity.z) < 1e-12,
              "Cockpit roll-rate instrument disagrees with the ship");
        barrel.fly({}, .6);
        const auto heldAttitude = barrel.nav.orientation;
        barrel.fly({}, 1);
        check(std::abs(glm::dot(heldAttitude, barrel.nav.orientation)) > .999999,
              "Assistance levelled the attained bank instead of arresting roll");
        // Recovery must work even if the user is still holding rotation,
        // throttle, strafe and boost while pressing B. Compare against a clean
        // brake from the exact same moving/tumbling physical state.
        Rig panic(u), cleanBrake(u);
        panic.pilot.toggleAssist();
        panic.pilot.setVelocity({20, -10, -40});
        panic.fly(combinedInput, .5);
        const auto tumble = panic.pilot.ship().angularVelocity;
        check(glm::length(tumble) > 1, "Recovery fixture is not rotating significantly");
        check(cleanBrake.nav.restore(panic.nav.serialize()) &&
                  cleanBrake.pilot.restore(panic.pilot.serialize()),
              "Could not clone tumbling ship for recovery comparison");
        HelmInput recovery = combinedInput, plain;
        recovery.throttle = recovery.strafe = recovery.lift = 1;
        recovery.boost = recovery.brake = plain.brake = true;
        panic.pilot.update(recovery, 1. / 60);
        cleanBrake.pilot.update(plain, 1. / 60);
        check(glm::length(panic.pilot.ship().angularVelocity) > .1,
              "Recovery erased angular momentum instead of applying finite torque");
        panic.fly(recovery, 2);
        cleanBrake.fly(plain, 2);
        std::cout << "recovery_rate_after_2_s=" << glm::length(panic.pilot.ship().angularVelocity)
                  << " recovery_speed_after_2_s=" << glm::length(panic.pilot.ship().velocity) << '\n';
        check(glm::length(panic.pilot.ship().angularVelocity) < .001,
              "B failed to arrest a tumbling inertial ship while rotation controls are held");
        panic.fly(recovery, 4);
        cleanBrake.fly(plain, 4);
        check(panic.pilot.ship().assist && panic.pilot.ship().throttle == 0 &&
                  glm::length(panic.pilot.ship().angularVelocity) < .001 &&
                  glm::length(panic.pilot.ship().velocity) < .1,
              "B failed to recover a tumbling inertial ship with controls still held");
        check(glm::length(panic.pilot.ship().angularVelocity - cleanBrake.pilot.ship().angularVelocity) <
                      1e-12 &&
                  glm::length(panic.pilot.ship().velocity - cleanBrake.pilot.ship().velocity) < 1e-12 &&
                  std::abs(glm::dot(panic.nav.orientation, cleanBrake.nav.orientation)) > 1 - 1e-12,
              "Held controls overrode the recovery brake");
        // Inertial flight retains both SI momentum and body spin through every
        // hierarchy and render-unit change, including the atomic return trip.
        Rig inertial(u);
        inertial.pilot.toggleAssist();
        inertial.pilot.setVelocity({40, -13, 80});
        inertial.fly(keys, .5);
        const auto velocity = inertial.pilot.ship().velocity;
        const auto spin = inertial.pilot.ship().angularVelocity;
        inertial.fly({}, 1);
        check(glm::length(inertial.pilot.ship().velocity - velocity) < 1e-11 &&
                  glm::length(inertial.pilot.ship().angularVelocity - spin) < 1e-12,
              "Inertial flight silently damped roll or linear momentum");
        const auto attitude = inertial.nav.orientation;
        auto address = Address{};
        std::vector<Address> route{address};
        while (address.kind() != Kind::Lattice) {
            address = u.children(u.describe(address), 1).front();
            route.push_back(address);
        }
        auto visit = [&](const Address& next) {
            inertial.nav.changeFrame(next);
            inertial.nav.setRenderScale(u.describe(next).frame.logRadius + .5);
            inertial.pilot.synchronizeFrame();
            const auto rotation = u.relative(next, Address{}, 0).orientation;
            check(glm::length(inertial.pilot.ship().angularVelocity - spin) < 1e-12,
                  "Scale/frame change altered body-relative roll rate");
            check(std::abs(glm::dot(rotation * inertial.nav.orientation, attitude)) > 1 - 1e-11,
                  "Scale/frame change altered physical ship attitude");
        };
        for (const auto& next : route)
            visit(next);
        for (auto it = route.rbegin(); it != route.rend(); ++it)
            visit(*it);
        const auto saved = inertial.pilot.serialize();
        check(inertial.pilot.restore(saved) && inertial.pilot.serialize() == saved &&
                  inertial.pilot.ship().angularVelocity == spin,
              "Saving a rolling ship lost its angular state");
        Rig direct(u), rebased(u);
        for (auto* rig : {&direct, &rebased}) {
            rig->pilot.toggleAssist();
            rig->pilot.setVelocity(initial);
            rig->pilot.toggleAssist();
        }
        const auto galaxy = Address{}.child(Kind::Galaxy, 0);
        rebased.nav.changeFrame(galaxy);
        for (int i = 0; i < 60; ++i) {
            direct.pilot.update({}, 1. / 60);
            rebased.pilot.update({}, 1. / 60);
            const auto a =
                u.relative(direct.nav.anchor, Address{}, 0).orientation * direct.pilot.ship().velocity;
            const auto b =
                u.relative(rebased.nav.anchor, Address{}, 0).orientation * rebased.pilot.ship().velocity;
            check(glm::length(a - b) < 1e-7, "Reference-frame change kicked the assist-capture velocity");
        }
        Rig braking(u);
        braking.pilot.toggleAssist();
        braking.pilot.setVelocity(initial);
        braking.pilot.toggleAssist();
        braking.pilot.brake();
        braking.pilot.update({}, 1. / 60);
        check(glm::length(braking.pilot.ship().velocity - initial) > 1e-4 &&
                  glm::length(braking.pilot.ship().velocity - initial) < 1,
              "Assist capture delayed the explicit brake command");
        std::cout << "PASS: " << checks << " handling, roll and assist checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
