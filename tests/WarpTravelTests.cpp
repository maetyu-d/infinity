#include "infinity/Arrival.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Economy.hpp"
#include "infinity/WarpTravel.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
}
int main() { try {
    check(warpExitEstimate(12000, 12000, 5400, 1e9) == 0, "Exit threshold has a nonzero countdown");
    check(warpExitEstimate(120000, 12000, 0, 1e9) < 0, "Stationary ship promises an arrival time");
    check(std::abs(warpExitEstimate(120000, 12000, 54000, 1e9) - std::log(10.) / .45) < 1e-10,
          "Countdown ignores the actual proportional braking leg");
    check(warpExitEstimate(120000, 12000, -100, 1e9) < 0, "Opening course has an arrival time");
    check(std::abs(warpExitEstimate(120000, 12000, 18000, 1e9, 0) - std::log(10.) / .15) < 1e-10,
          "Countdown ignores a declining clearance envelope");
    check(warpExitEstimate(120000, 12000, 18000, 1e9, 8000) < warpExitEstimate(120000, 12000, 18000, 1e9, 0),
          "Surface stand-off is missing from the clearance countdown");
    check(warpExitEstimate(1e22, 1e16, 1e18, 1e18) > 10000, "Long-range cruise countdown overflowed");
    check(warpExitEstimate(std::numeric_limits<double>::infinity(), 1, 1, 1) < 0,
          "Nonfinite countdown reached the HUD");
    for (double radius : {1e5, 6e6, 7e7, 1e11, 1e20}) {
        const double drop = std::max(12000., radius * 1e-6);
        double previous = 0;
        for (double fraction : {1e5, 1e3, 100., 36., 10., 2., 1.}) {
            const auto reveal = warpDestinationReveal(drop * fraction, drop, radius);
            check(reveal >= previous && reveal <= 1 && std::isfinite(reveal), "Reveal reverses across a scale range");
            previous = reveal;
        }
        check(previous == 1, "Destination does not fully resolve at exit");
    }
    check(warpTunnelAperture(0, 1, 1.2) == .21 &&
          warpTunnelAperture(1, .5, 1.2) > warpTunnelAperture(.5, .5, 1.2),
          "Tunnel opening fails to track the real body's angular size");
    Universe u; Navigation nav(u); RegionCache cache(u); FlightController flight(u, nav, cache); Pilot pilot(u, nav, flight);
    pilot.startInSystem();
    const auto planet = nav.anchor;
    const auto fingerprint = descriptorFingerprint(u.describe(planet));
    auto net = std::make_shared<EconomicNetwork>(); net->system = planet.parent();
    EconomicSite port; port.id = {planet, 0}; port.name = "DESTINATION HARBOUR";
    net->sites.push_back(port);
    EconomySnapshot snapshot; snapshot.network = net;
    EconomicPortSignal signal; signal.missingKg = 90; signal.shortage = Good::Water; signal.brief = "SHORT WATER";
    snapshot.portSignals.push_back(signal);
    EconomicFlight freight; freight.id = {planet, 100}; freight.kg = 80; freight.destination = 0;
    freight.phase = FreightPhase::Transit; freight.disabledAt = 4;
    snapshot.flights.push_back(freight);
    WreckRecovery wreck; wreck.id = freight.id; wreck.survivor = CrewMember{}; wreck.rescueDeadline = 100;
    snapshot.wrecks.push_back(wreck);
    std::vector<ActivityContact> contacts;
    for (const auto slot : {101u, 300u, 400u}) {
        ActivityContact c; c.body = sampleActivity(u.describe(planet), slot, 20); c.distance = 5000 + slot;
        contacts.push_back(c);
    }
    ActivityContact hostile = contacts[0]; hostile.body.id.slot = 600; hostile.body.hostile = true;
    hostile.body.name = "EXISTING RAIDER"; contacts.push_back(hostile);
    auto offers = proceduralArrivals(u, planet, &snapshot, contacts);
    check(offers.size() >= 5 && offers.front().kind == ArrivalKind::Distress, "Live distress does not lead arrival calls");
    auto reverse = contacts; std::reverse(reverse.begin(), reverse.end());
    const auto again = proceduralArrivals(u, planet, &snapshot, reverse);
    check(offers.size() == again.size(), "Arrival choice depends on renderer submission order");
    for (size_t i = 0; i < offers.size(); ++i) {
        check(offers[i].target == again[i].target && offers[i].order == again[i].order,
              "Arrival encounters are nondeterministic");
        check(std::count_if(offers.begin(), offers.end(), [&](const auto& e) { return e.target == offers[i].target; }) == 1,
              "Arrival duplicated an existing actor");
    }
    snapshot.wrecks[0].rescued = true;
    auto resolved = proceduralArrivals(u, planet, &snapshot, contacts);
    check(std::none_of(resolved.begin(), resolved.end(), [](const auto& e) { return e.kind == ArrivalKind::Distress; }),
          "Rescued survivor keeps transmitting distress");
    snapshot.catchingUp = true;
    auto syncing = proceduralArrivals(u, planet, &snapshot, contacts);
    check(std::none_of(syncing.begin(), syncing.end(), [](const auto& e) { return e.kind == ArrivalKind::Shortage; }),
          "Stale economy creates shortage encounters");
    check(proceduralArrivals(u, planet.child(Kind::Surface, 0), &snapshot, contacts).empty(),
          "Arrival leaked another world's activity into a surface address");
    snapshot.catchingUp = false;
    auto quiet = snapshot;
    quiet.flights.clear(); quiet.wrecks.clear(); quiet.portSignals[0].missingKg = 0;
    quiet.portSignals[0].readiness = 1; quiet.portSignals[0].brief = "SERVICES AVAILABLE";
    auto ports = proceduralArrivals(u, planet, &quiet, {});
    check(ports.size() == 1 && ports[0].target == port.id && ports[0].kind == ArrivalKind::Traffic,
          "Quiet inhabited arrival has no actionable existing port");
    auto concealed = contacts;
    for (auto& c : concealed) c.occluded = true;
    check(proceduralArrivals(u, planet, nullptr, concealed).empty(), "Arrival radio reveals occluded ships");
    ArrivalDirector director; FlightInstruments t;
    t.warp = WarpPhase::Drop; t.warpDestination = planet; t.warpArrivalSequence = 1; t.fieldActive = true;
    check(!director.update(u, t, &snapshot, contacts, 10) && !director.current(), "Encounter fired inside the active field");
    t.fieldActive = false;
    check(!director.update(u, t, &snapshot, contacts, 11), "Encounter interrupts destination reveal");
    check(director.update(u, t, &snapshot, contacts, 13), "Successful exit did not expose live encounters");
    const auto id = director.current()->target;
    check(director.select({})->target == id && director.select(id)->target != id, "Arrival contact selection cannot cycle");
    director.annotate(t);
    check(t.arrivalComms.find("SHIFT-I") != t.arrivalComms.npos, "Arrival radio has no usable action");
    check(!director.update(u, t, &snapshot, contacts, 14), "One exit repeatedly re-rolls its encounters");
    auto empty = snapshot; empty.portSignals.clear(); empty.flights.clear(); empty.wrecks.clear();
    director.update(u, t, &empty, {}, 16); director.annotate(t);
    check(!director.current() && t.arrivalComms.empty(), "Resolved arrival calls remain on the HUD");
    director.update(u, t, &snapshot, contacts, 18);
    check(director.current(), "Arrival radio cannot receive newly relevant local activity");
    const auto elsewhere = planet.parent().parent();
    director.update(u, t, &snapshot, contacts, 19, &elsewhere);
    check(!director.current(), "Arrival radio follows the ship into an unrelated system frame");
    director.update(u, t, &snapshot, contacts, 75);
    check(!director.current(), "Arrival calls never expire");
    t.warp = WarpPhase::Manual;
    director.update(u, t, &snapshot, contacts, 76);
    check(!director.current(), "Manual warp manufactured a guided encounter");
    check(descriptorFingerprint(u.describe(planet)) == fingerprint, "Arrival layer edited the procedural world");
    t.targetVisible = true; t.localTarget = {0, 0, -1}; t.localWarpDestination = {0, 0, -2};
    t.warp = WarpPhase::Route; t.warpReveal = .8; t.warpExitSeconds = 8; t.warpExitRange = 30000;
    t.warpExitSafe = true; t.arrivalComms.clear();
    for (const auto aspect : {1., 1.2, 4./3, 16./9, 2.4}) {
        t.warp = WarpPhase::Route; t.warpDestination = planet; t.arrivalComms.clear();
        auto h = cockpitOverlay(t, aspect, "HYPERION");
        check(cockpitTextConflicts(h).empty(), "Travel instruments overlap on a narrow display");
        check(std::any_of(h.textRuns.begin(), h.textRuns.end(), [](const auto& r) { return r.value.starts_with("DROP ~"); }),
              "Travel instruments hide the drop countdown");
        t.warp = WarpPhase::Off; t.warpDestination.reset();
        director = {}; t.warpArrivalSequence = 2; t.warpDestination = planet; t.warp = WarpPhase::Drop;
        director.update(u, t, &snapshot, contacts, 80);
        director.update(u, t, &snapshot, contacts, 84); director.annotate(t);
        h = cockpitOverlay(t, aspect, "HYPERION");
        check(cockpitTextConflicts(h).empty(), "Arrival calls overlap the cockpit");
    }
    std::cout << checks << " warp and arrival checks passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
