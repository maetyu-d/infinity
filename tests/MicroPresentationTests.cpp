#include "infinity/Cockpit.hpp"
#include "infinity/CockpitContext.hpp"
#include "infinity/MicroView.hpp"
#include "infinity/SurfaceShading.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace infinity;
static void check(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
static bool text(const CockpitOverlay& h, std::string_view s) {
    return std::any_of(h.textRuns.begin(), h.textRuns.end(),
                       [&](const auto& r) { return r.value.find(s) != r.value.npos; });
}
static void place(Navigation& nav, const Node& node, double unitsLog) {
    const double z = 3.1 * std::pow(10., node.frame.logRadius - unitsLog) - 1;
    std::ostringstream s;
    s.precision(17);
    s << node.address.str() << ' ' << unitsLog << " 1 0 0 0 0 0 " << z;
    check(nav.restore(s.str()), "Micro presentation camera invalid");
}
int main() {
    try {
        Universe u;
        Navigation nav(u);
        GeometryGenerator generator(u);
        const auto rock =
            u.describe(Address::parse("0,0,0/1:0/2:9/3:0/4:61516819435380/5:4/6:4611686018427387905"));
        const auto grain = u.describe(u.children(rock, 1).front());
        const auto lattice = u.describe(u.children(grain, 1).front());
        MicroView last;
        for (const auto& n : {rock, grain, lattice}) {
            const auto descriptor = descriptorFingerprint(n);
            const auto g = generator.generate(n, 1);
            const auto digest = geometryFingerprint(*g);
            place(nav, n, n.frame.logRadius);
            const auto camera = nav.serialize();
            const auto v =
                extractMicroView(u, nav, n.address.kind() == Kind::Lattice ? 1.2e-11 : .0001, g.get());
            check(v.active && v.address == n.address && v.name == n.label(),
                  "Micro display lost addressed object identity");
            const auto cell = crystalCell(n.environment.mineral, n.environment);
            check(v.formula == material(n.environment.mineral).formula && v.phase == processName(cell.phase),
                  "Micro HUD invented a mineral or phase");
            check(v.cellSites.size() == cell.sites.size() && v.cellSites.size() < 32,
                  "Unbounded or wrong cell schematic");
            check(std::abs(v.cellMetres - glm::length(cell.basisMetres[0])) < 1e-20,
                  "Reference cell lost physical dimensions");
            check(v.proximityKnown, "Local material proximity unavailable");
            if (v.atomic) {
                check(v.atomsKnown && v.atoms == g->lattice.atoms.size() &&
                          v.bonds == g->lattice.bonds.size(),
                      "Atomic HUD counted unrelated sites");
                double gap = 1e100;
                const double units = std::pow(10., g->logUnits);
                for (const auto& a : g->lattice.atoms)
                    gap =
                        std::min(gap, glm::length(glm::dvec3(0, 0, 3.1) - glm::dvec3(a.position)) - a.radius);
                check(std::abs(v.proximityMetres - std::max(0., gap * units - 1.2e-11 / 6)) < 1e-20,
                      "Atomic gap differs from resolved collision spheres");
                const auto pending = extractMicroView(u, nav, 1.2e-11);
                check(!pending.atomsKnown && !pending.proximityKnown,
                      "Loading atomic geometry displayed invented clearance");
                auto wrong = *g;
                wrong.descriptor ^= 1;
                check(!extractMicroView(u, nav, 1.2e-11, &wrong).atomsKnown,
                      "Stale geometry supplied atom counts");
            }
            for (double shift : {-1., 1.}) {
                place(nav, n, n.frame.logRadius + shift);
                const auto b = extractMicroView(u, nav, v.atomic ? 1.2e-11 : .0001, g.get());
                check(std::abs(b.fieldMetres - v.fieldMetres) < std::max(1e-24, v.fieldMetres * 1e-10) &&
                          std::abs(b.proximityMetres - v.proximityMetres) <
                              std::max(1e-24, std::max(v.proximityMetres, 1e-20) * 1e-8),
                      "Changing render units changed physical micro instruments");
            }
            check(descriptorFingerprint(u.describe(n.address)) == descriptor &&
                      geometryFingerprint(*g) == digest,
                  "Micro adapter changed generation or geometry");
            place(nav, n, n.frame.logRadius);
            check(nav.serialize() == camera, "Micro readout failed to preserve camera");
            last = v;
        }
        place(nav, grain, grain.frame.logRadius);
        const auto g = generator.generate(grain, 1);
        auto system = rock.address;
        while (system.kind() > Kind::System)
            system = system.parent();
        std::vector<CelestialContact> worlds(12);
        for (unsigned i = 0; i < worlds.size(); ++i) {
            auto& w = worlds[i];
            w.address = system.child(Kind::Planet, i);
            w.label = "PLANET " + std::to_string(i);
            w.distanceMetres = 1e9 + i;
            w.localDirection = {0, 0, -1};
            w.life = LifeDetection::Detected;
            w.population = 100;
        }
        for (bool atomic : {false, true}) {
            const auto view = atomic ? last : extractMicroView(u, nav, .0001, g.get());
            FlightInstruments t;
            t.shrinking = true;
            t.hullMetres = atomic ? 1.2e-11 : .0001;
            t.targetHullMetres = t.hullMetres;
            t.altitude = 100;
            for (double aspect : {1., 1.2, 4. / 3, 16. / 9, 2.4})
                for (bool expanded : {false, true}) {
                    t.contactsExpanded = expanded;
                    const auto h = cockpitOverlay(t, aspect, "HYPERION", {}, worlds, {}, false, 1, nullptr,
                                                  {1, 0, 0, 0}, nullptr, nullptr, &view);
                    check(cockpitTextConflicts(h).empty(), "Micro instruments overlap");
                    check(text(h, atomic ? "ATOMIC FLIGHT" : "MINERAL FLIGHT") && text(h, "REFERENCE CELL"),
                          "Micro scale and real cell schematic are not communicated");
                    check(text(h, atomic ? "LOCAL ATOM GAP" : "SURFACE ESTIMATE") &&
                              !text(h, "PARENT TERRAIN"),
                          "Parent planetary altitude displaced local clearance");
                    check(!text(h, "NO DESTINATION"), "Micro worksite lost the inspected object");
                    if (expanded)
                        check(std::any_of(h.contactHits.begin(), h.contactHits.end(),
                                          [](const auto& hit) { return hit.world.has_value(); }),
                              "Micro mode removed access to world contacts");
                    else
                        check(text(h, "LOCAL MATERIAL") && !text(h, "PLANET 0") &&
                                  std::any_of(h.contactHits.begin(), h.contactHits.end(),
                                              [](const auto& hit) { return hit.catalogue; }),
                              "Micro summary is cluttered with distant worlds or cannot open catalogue");
                    for (const auto& r : h.textRuns)
                        check(r.bounds.x >= 0 && r.bounds.y >= 0 &&
                                  r.bounds.x + r.bounds.w <= 900 * aspect + .01 &&
                                  r.bounds.y + r.bounds.h <= 900.01,
                              "Micro text left the screen");
                }
        }
        FlightInstruments tiny;
        tiny.shrinking = true;
        tiny.hullMetres = 1e-9;
        tiny.hostileContacts = 3;
        check(cockpitFocus(tiny).mode == CockpitMode::Surface,
              "Distant hostile contacts displaced the micro work context");
        tiny.incomingFire = .5;
        check(cockpitFocus(tiny).mode == CockpitMode::Combat,
              "Active fire failed to restore tactical priority while tiny");
        tiny = {};
        tiny.shrinking = true;
        tiny.phase = "SPECIMEN INSPECTION";
        const auto specimen = cockpitOverlay(tiny, 16. / 9, "HYPERION", {}, {}, {}, false, 1, nullptr,
                                             {1, 0, 0, 0}, nullptr, nullptr, &last);
        check(text(specimen, "SPECIMEN INSPECTION") && text(specimen, "SHIP PARKED"),
              "Material instruments hid the parked-ship specimen state");
        float previous = 0;
        for (double p : {0., .25, .75, 1., 2., 3., 5., 8., 16., 32., 100.}) {
            const float opacity = latticeBondOpacity(p);
            check(std::isfinite(opacity) && opacity >= previous && opacity <= .65f,
                  "Bond detail is unstable or exceeds brightness budget");
            previous = opacity;
        }
        check(latticeBondOpacity(.5) == 0 && latticeBondOpacity(100) > .6,
              "Bond detail did not filter unresolved links or retain resolved ones");
        std::cout << "PASS addressed micro instruments, physical unit invariance, cell phase, atomic gaps, "
                     "catalogue access, five screen shapes and bond detail\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
