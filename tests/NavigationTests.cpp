#include "infinite/Navigation.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static void near(glm::dvec3 a, glm::dvec3 b, const char* message) {
    check(glm::length(a - b) < 2e-12, message);
}
static void place(Navigation& nav, const Universe& u, const Address& address) {
    std::ostringstream state;
    state.precision(17);
    state << address.str() << ' ' << u.describe(address).frame.logRadius + .5 << " 1 0 0 0 .17 -.21 .09";
    check(nav.restore(state.str()), "could not place camera at addressed scale");
}
int main() {
    try {
        for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
            Universe u(seed);
            Address address;
            for (unsigned scale = 0; scale < 9; ++scale) {
                Navigation nav(u);
                place(nav, u, address);
                const auto descriptor = descriptorFingerprint(u.describe(address));
                const auto eye = nav.cameraPosition();
                const auto log = nav.logDistance;
                for (int i = 0; i < 64; ++i) {
                    nav.rotate(9.5, i % 2 ? -2.75 : 2.75);
                    near(nav.cameraPosition(), eye, "mouse look moved the physical eye");
                    check(std::abs(glm::length(nav.orientation) - 1) < 2e-14,
                          "repeated mouse look lost unit orientation");
                }
                const auto heading = nav.orientation;
                const glm::dvec3 axes{1, 1, -1};
                for (int i = 0; i < 120; ++i)
                    nav.move(axes, 1. / 120, false);
                near(nav.cameraPosition(), eye + heading * glm::normalize(axes) * .32,
                     "flight did not follow the new heading at the expected speed");
                check(nav.anchor == address && nav.logDistance == log &&
                          descriptorFingerprint(u.describe(address)) == descriptor,
                      "flight changed object identity, scale or procedural descriptor");
                const auto saved = nav.serialize();
                Navigation restored(u);
                check(restored.restore(saved) && restored.serialize() == saved,
                      "flying camera did not survive save/restore");
                near(restored.cameraPosition(), nav.cameraPosition(), "save/restore moved the flying eye");

                // Independent frame rates and diagonal key combinations cover
                // equal physical distance, at every scale including atoms.
                Navigation slow(u), fast(u);
                place(slow, u, address);
                place(fast, u, address);
                for (int i = 0; i < 30; ++i)
                    slow.move({0, 0, -1}, 1. / 30, false);
                for (int i = 0; i < 144; ++i)
                    fast.move({1, 0, -1}, 1. / 144, false);
                check(std::abs(glm::length(slow.cameraPosition() - eye) -
                               glm::length(fast.cameraPosition() - eye)) < 2e-12,
                      "diagonal motion or frame rate changed flight speed");
                place(nav, u, address);
                nav.adjustSpeed(2);
                nav.move({0, 0, -1}, .05, true);
                near(nav.cameraPosition(), eye + glm::dvec3(0, 0, -.5),
                     "adjustable speed and boost did not combine");
                place(nav, u, address);
                nav.move({0, 0, -1}, .05, false, true);
                near(nav.cameraPosition(), eye + glm::dvec3(0, 0, -.008),
                     "precision flight did not reduce movement");

                // An interrupted zoom/focus must not resume on the next frame.
                place(nav, u, address);
                nav.zoom(-.5);
                nav.update(.02);
                const auto zoomEye = nav.cameraPosition();
                nav.rotate(35, -14);
                near(nav.cameraPosition(), zoomEye, "interrupting zoom teleported the eye");
                check(!nav.transitioning, "mouse look did not interrupt zoom");
                auto stopped = nav.serialize();
                nav.update(.1);
                check(nav.serialize() == stopped, "zoom resumed after manual takeover");
                if (scale < 8) {
                    auto child = u.children(u.describe(address), 1).front();
                    nav.focus(child);
                    nav.update(.02);
                    const auto focusEye = nav.cameraPosition();
                    const auto focusHeading = nav.orientation;
                    nav.move({0, 0, -1}, .01, false);
                    near(nav.cameraPosition(), focusEye + focusHeading * glm::dvec3(0, 0, -.0128),
                         "manual flight jumped when interrupting focus");
                    check(!nav.transitioning, "flight did not interrupt focus");
                    stopped = nav.serialize();
                    nav.update(.1);
                    check(nav.serialize() == stopped, "guided focus resumed after flight");
                    address = child;
                }
            }
        }
        Universe u;
        Navigation nav(u);
        nav.adjustSpeed(1000);
        check(nav.speedMultiplier() == 256, "maximum flight speed is not bounded");
        nav.adjustSpeed(-1000);
        nav.adjustSpeed(-1000);
        check(nav.speedMultiplier() == 1. / 64, "minimum flight speed is not bounded");
        nav.reset();
        check(nav.speedMultiplier() == 1, "reset did not restore normal speed");
        const auto eye = nav.cameraPosition();
        const auto q = nav.orientation;
        nav.move({0, 0, -1}, 100, false);
        near(nav.cameraPosition(), eye + q * glm::dvec3(0, 0, -.032), "frame stall caused a flight jump");
        nav.focus();
        const auto saved = nav.serialize();
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto inf = std::numeric_limits<double>::infinity();
        for (auto bad : {nan, inf, -inf}) {
            nav.rotate(bad, 1);
            nav.rotate(1, bad);
            nav.move({bad, 0, 0}, .01, false);
            nav.move({0, 0, -1}, bad, false);
            nav.adjustSpeed(bad);
        }
        nav.rotate(0, 0);
        nav.move({}, .01, false);
        nav.move({0, 0, -1}, 0, false);
        nav.move({0, 0, -1}, -1, false);
        check(nav.serialize() == saved && nav.transitioning && nav.speedMultiplier() == 1,
              "invalid or idle input corrupted the camera or interrupted a transition");
        std::cout << "PASS: " << checks
                  << " flight checks across all nine scales and three seeds; stationary free look, "
                     "camera-relative movement, frame rates, speed controls, manual takeover and saves.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " flight checks: " << e.what() << '\n';
        return 1;
    }
}
