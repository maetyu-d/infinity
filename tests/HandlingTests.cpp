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
Response response(const Universe& u, double dt, double direction = 1, bool precision = false) {
    Rig r(u);
    HelmInput h;
    h.roll = direction;
    h.precision = precision;
    const auto forward = r.nav.orientation * glm::dvec3(0, 0, -1);
    r.fly(h, 1, dt);
    Response result;
    result.rate = std::abs(r.pilot.ship().angularVelocity.z);
    check(glm::dot(forward, r.nav.orientation * glm::dvec3(0, 0, -1)) > 1 - 1e-10,
          "Pure roll changed the physical forward heading");
    h.roll = 0;
    double previous = result.rate;
    while (result.settle < 2 && std::abs(r.pilot.ship().angularVelocity.z) > .02) {
        r.pilot.update(h, dt);
        const auto rate = std::abs(r.pilot.ship().angularVelocity.z);
        result.overshoot += (previous + rate) * .5 * dt;
        previous = rate;
        result.settle += dt;
    }
    h.roll = direction;
    r.fly(h, 1, dt);
    h.roll = -direction;
    while (result.reverse < 2 && r.pilot.ship().angularVelocity.z * direction < 0) {
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
                  << ",\"assist_residual_drift_m_s\":" << residual << ",\"turn_slip_rad\":" << slip << "}\n";
        if (argc > 1)
            return 0; // --measure records the baseline without the new handling thresholds.
        check(r.rate > 1.7 && r.rate < 1.81, "Roll did not reach the assisted barrel-roll rate");
        check(r.settle <= .45 && r.overshoot < .25, "Released roll feels loose or overshoots excessively");
        check(r.reverse < .3, "Roll reversal is too slow");
        check(assistKick < .2 && residual < 65, "Flight-assist capture kicks or fails to settle");
        for (double dt : {1. / 30, 1. / 144}) {
            const auto other = response(u, dt);
            check(std::abs(other.rate - r.rate) < .01 && std::abs(other.settle - r.settle) < .04 &&
                      std::abs(other.overshoot - r.overshoot) < .02,
                  "Rotational handling depends on display frame rate");
        }
        const auto left = response(u, 1. / 60, -1);
        check(std::abs(left.rate - r.rate) < 1e-10 && std::abs(left.settle - r.settle) < 1e-10,
              "Left and right roll are asymmetric");
        const auto fine = response(u, 1. / 60, 1, true);
        check(fine.rate < r.rate * .36 && fine.rate > r.rate * .34,
              "Precision handling does not reduce roll authority");
        check(slip < .65, "High-throttle steering has excessive lateral slide");
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
            check(std::abs(look.yaw - .375) < 1e-12 && std::abs(look.pitch - .2) < 1e-12,
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
        for (int i = 0; i < 240; ++i) {
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
        check(glm::length(braking.pilot.ship().velocity - initial) > .2,
              "Assist capture delayed the explicit brake command");
        std::cout << "PASS: " << checks << " handling, roll and assist checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
