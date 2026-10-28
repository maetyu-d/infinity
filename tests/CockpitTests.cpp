#include "infinite/Cockpit.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace infinite;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        check(flightQuantity(.012, true) == "12.0 mm/s", "Millimetre units are ambiguous");
        check(flightQuantity(1.2e-11) == "12.0 pm", "Atomic hull units are wrong");
        check(flightQuantity(85000, true) == "85.0 km/s", "Flight speed units are wrong");
        std::vector<ActivityContact> traffic(80);
        const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
        for (size_t i = 0; i < traffic.size(); ++i) {
            auto& c = traffic[i];
            c.body.id = {system.child(Kind::Planet, 0), uint32_t(i)};
            c.body.name = "ORBITAL HARBOUR " + std::to_string(i);
            c.body.status = "STATION KEEPING / BERTHS OPEN";
            c.body.kind = ActivityKind::Harbour;
            c.localDirection = glm::normalize(glm::dvec3((int(i % 12) - 6) * .035, .18, -1));
            c.distance = 1000 + i * 100;
        }
        traffic[77].selected = true;
        const auto rows = cockpitActivityRows(traffic);
        check(rows.size() == 6 && rows.back() == 77, "Selected distant craft is missing from the display");
        std::set<size_t> unique(rows.begin(), rows.end());
        check(unique.size() == rows.size(), "A contact row is duplicated");
        for (size_t i = 0; i < traffic.size(); ++i)
            check(traffic[i].body.id.slot == i, "Display changed stable K-cycle order");
        check(cockpitActivityRows({}).empty(), "Empty contact display is invalid");
        std::vector<CelestialContact> worlds(70);
        for (size_t i = 0; i < worlds.size(); ++i) {
            auto& c = worlds[i];
            c.address = system.child(Kind::Planet, i);
            c.localDirection = glm::normalize(glm::dvec3((int(i % 10) - 5) * .06, -.14, -1));
            c.distanceMetres = 1e7 + i;
            c.angularRadius = .0001;
            c.label = "MOON " + std::to_string(i);
            c.moon = true;
        }
        FlightInstruments t;
        worlds[54].life = worlds[61].life = LifeDetection::Detected;
        worlds[54].population = 34000;
        worlds[61].population = 5100;
        worlds[61].occluded = true;
        worlds[63].life = LifeDetection::Scanning;
        worlds[68].selected = true;
        worlds[69].life = LifeDetection::Detected; // No population: never a positive signal.
        const auto livingRows = cockpitCelestialRows(worlds);
        check(livingRows.size() == 8 && livingRows[0] == 68 && livingRows[1] == 54 && livingRows[2] == 61,
              "Life-bearing distant worlds or the selected world disappeared behind nearby moons");
        check(std::set<size_t>(livingRows.begin(), livingRows.end()).size() == 8,
              "Prioritized celestial rows contain duplicates");
        check(nextLivingWorld(worlds, {}) == worlds[54].address &&
                  nextLivingWorld(worlds, worlds[54].address) == worlds[61].address &&
                  nextLivingWorld(worlds, worlds[61].address) == worlds[54].address,
              "Life targeting does not wrap through confirmed worlds");
        check(!hasSurfaceLife(worlds[63]) && !hasSurfaceLife(worlds[69]) &&
                  cockpitCelestialRows({}).empty() && !nextLivingWorld({}, {}),
              "Pending, empty or zero-population observations produced a life target");
        for (size_t i = 0; i < worlds.size(); i++)
            check(worlds[i].address == system.child(Kind::Planet, i),
                  "HUD prioritization mutated authoritative contact order or identity");
        t.speed = 85000;
        t.localVelocity = {0, 0, -85000};
        t.targetVisible = true;
        t.localTarget = {.13, .11, -1};
        t.range = 2e10;
        t.destination = "Moon 13145 / J NAVIGATE";
        for (double aspect : {1., 4. / 3, 16. / 9, 2.4}) {
            const CockpitLayout l(aspect);
            for (auto r : {l.traffic, l.celestial, l.life, l.navigation, l.flight, l.scope, l.systems}) {
                check(r.x >= 0 && r.y >= 0 && r.x + r.w <= l.width && r.y + r.h <= 900,
                      "Instrument panel escaped the viewport");
                check(r.w > 100 && r.h > 0, "Instrument collapsed in a narrow window");
            }
            for (auto a : {l.navigation, l.flight, l.scope, l.systems})
                for (auto b : {l.navigation, l.flight, l.scope, l.systems})
                    if (a.x != b.x)
                        check(!a.overlaps(b), "Lower-deck instruments overlap");
            check(l.trafficRow({l.traffic.x + 20, 217}, 80) == 0, "First traffic row cannot be clicked");
            check(l.celestialRow({l.celestial.x + 20, 217}, 70) == 0, "First world row cannot be clicked");
            check(!l.trafficRow({l.traffic.x + l.traffic.w + 1, 217}, 80), "Outside click selected traffic");
            check(!l.trafficRow({l.traffic.x + 20, 422}, 80), "Footer click selected hidden traffic");
            check(!l.celestialRow({l.celestial.x + 20, 217}, 0), "Empty list accepted a click");
            const auto projected = cockpitTargetMarker({.1, .05, -1}, aspect);
            check(!projected.offscreen && !projected.aft &&
                      glm::length(projected.point - glm::dvec2(l.width * .5 + .1 * 450 / .7002075,
                                                               450 - .05 * 450 / .7002075)) < 1e-9,
                  "Onscreen target does not match the camera projection");
            for (auto bearing : {glm::dvec3(0, 0, 1), glm::dvec3(1, 2, -.0001), glm::dvec3(-3, -8, 1),
                                 glm::dvec3(0, 0, -1)}) {
                const auto marker = cockpitTargetMarker(bearing, aspect);
                check(l.windscreen.contains(marker.point), "Offscreen cue escaped the readable windscreen");
                check(marker.aft == (bearing.z >= 0), "Offscreen cue lost front/back direction");
            }
            for (int regime = 0; regime < 4; ++regime) {
                t.altitude = regime == 1 ? 32 : regime == 2 ? 1200 : -1;
                t.gear = regime == 1;
                t.roll = regime == 1 ? 1.57079632679 : 0;
                t.pitch = regime == 1 ? .15 : 0;
                t.descent = regime == 1 ? 1 : 800;
                t.shrinking = regime == 3;
                t.hullMetres = regime == 3 ? 1.2e-11 : 12;
                t.logMetres = regime == 3 ? -10 : 7;
                t.localTarget = regime % 2 ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 0, -1);
                const auto h = cockpitOverlay(t, aspect, "HYPERION", "DESTINATION SELECTED", worlds, traffic);
                check(h.worldLabels.size() <= 4, "Crowded scene produced too many labels");
                for (size_t i = 0; i < h.worldLabels.size(); ++i) {
                    const auto r = h.worldLabels[i];
                    check(l.windscreen.contains({r.x, r.y}) &&
                              r.x + r.w <= l.windscreen.x + l.windscreen.w + 1e-6 &&
                              r.y + r.h <= l.windscreen.y + l.windscreen.h + 1e-6,
                          "World label escaped the windscreen");
                    check(!r.overlaps({l.width * .5 - 42, 423, 84, 54}), "World label covered the reticle");
                    for (size_t j = 0; j < i; ++j)
                        check(!r.overlaps(h.worldLabels[j]), "World labels overlap");
                }
                for (const auto* mesh : {&h.panels, &h.strokes, &h.lettering})
                    for (auto v : mesh->vertices)
                        check(std::isfinite(v.x) && std::isfinite(v.y) && v.x >= 0 && v.x <= l.width + .01 &&
                                  v.y >= 0 && v.y <= 900,
                              "Cockpit drawing is clipped or non-finite");
                const auto guide = cockpitOverlay(t, aspect, "HYPERION", {}, worlds, traffic, true);
                check(guide.worldLabels.empty(), "Guide permits distracting world labels");
            }
        }
        std::cout
            << "PASS: " << checks
            << " cockpit layout, target projection, decluttering, stable selection and SI unit checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
