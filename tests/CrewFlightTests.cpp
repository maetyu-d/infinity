#include "infinite/Cockpit.hpp"
#include <algorithm>
#include <iostream>
#include <set>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
ShipCrew specialists(unsigned fit = 8) {
    ShipCrew c(48);
    std::vector<CrewTitle> titles;
    std::vector<CrewBoarding> incoming;
    for (uint64_t i = 1; i <= 6; ++i)
        titles.push_back({i, false});
    for (uint64_t i = 0; i < 8; ++i) {
        auto m = c.recruit(100 + i);
        m.id = (1ULL << 63) | (100 + i);
        m.experience.fill(0);
        if (i < 6)
            m.experience[(i + 3) % 6] = 360;
        m.health = i < fit ? 100 : 10;
        titles.push_back({m.id, true});
        incoming.push_back({i + 1, m});
    }
    c.reconcileMarket(8, titles, incoming);
    c.toggleAutomatic();
    return c;
}
} // namespace
int main() {
    try {
        auto c = specialists();
        auto original = c.members();
        c.command(CrewCommand::BestStations, 0, CrewStation::Bridge);
        std::set<CrewStation> duties;
        for (size_t i = 0; i < 8; ++i) {
            const auto& m = c.members()[i];
            check(m.id == original[i].id && m.name == original[i].name &&
                      m.experience == original[i].experience,
                  "Station matching replaced crew identities or skills");
            if (i < 6) {
                check(m.duty == CrewStation((i + 3) % 6), "Specialist assigned to an inferior station");
                check(duties.insert(m.duty).second, "Matching duplicated a duty station");
            } else
                check(m.duty == CrewStation::Bunks && m.order == CrewOrder::Rest,
                      "Relief crew were not sent to rest");
            check(m.room == CrewStation::Bunks && m.travel == 0, "Matching teleported a crew member");
        }
        c.advance(.25);
        auto moving = c.members()[0];
        check(moving.travel > 0 && c.task(0).travelSeconds >= moving.travel, "Missing real movement ETA");
        c.command(CrewCommand::Return, 0, CrewStation::Bridge);
        check(c.members()[0].travel == moving.travel && c.members()[0].room == moving.room,
              "A new order cancelled an in-flight corridor traversal");
        const auto unchanged = c.serialize();
        for (unsigned i = 0; i < 100; ++i)
            c.view(i % 8, CrewStation(i % crewRoomCount));
        check(c.serialize() == unchanged, "Crew instruments mutated the simulation");
        ShipCrew loaded(48);
        check(loaded.restore(unchanged) && loaded.serialize() == unchanged,
              "Orders or walking lost on reload");
        auto shortCrew = specialists(4);
        shortCrew.bestStations();
        check(std::count_if(shortCrew.members().begin(), shortCrew.members().end(),
                            [](const auto& m) { return m.duty != CrewStation::Bunks; }) == 4,
              "Incapacitated crew assigned to active stations");
        check(shortCrew.command(CrewCommand::Repair, 7, CrewStation::Bridge).find("NO FIT") !=
                  std::string::npos,
              "Incapacitated crew accepted an operational command");
        check(shortCrew.task(7).blocked && shortCrew.task(7).status == "INCAPACITATED",
              "Incapacitated crew had no clear task status");
        ShipCrew emergency(48);
        emergency.toggleAutomatic();
        emergency.observeHull(100);
        emergency.observeHull(70);
        CrewStation damaged = CrewStation::Bridge;
        for (size_t i = 0; i < crewRoomCount; ++i)
            if (emergency.rooms()[i].fire > 0)
                damaged = CrewStation(i);
        emergency.command(CrewCommand::Repair, 0, damaged);
        check(emergency.task(0).blocked && emergency.task(0).detail.find("FIRE") != std::string::npos,
              "Repair task did not explain fire blocking work");
        emergency.command(CrewCommand::Return, 0, damaged);
        emergency.command(CrewCommand::Emergency, 0, damaged);
        auto firefighters = [&] {
            return std::count_if(emergency.members().begin(), emergency.members().end(), [&](const auto& m) {
                return m.order == CrewOrder::Firefight && m.target == damaged;
            });
        };
        check(firefighters() == 1, "Emergency command did not dispatch a firefighter");
        emergency.respond();
        check(firefighters() == 1, "Repeated emergency command duplicated an active assignment");
        const auto spares = emergency.spares(), medical = emergency.medical();
        for (unsigned i = 0; i < 600; ++i) {
            if (i % 20 == 0)
                emergency.respond();
            emergency.advance(.25, 2, true);
        }
        const auto& repaired = emergency.rooms()[size_t(damaged)];
        check(repaired.fire <= .01 && repaired.integrity > 99 && repaired.breach <= .01,
              "Dispatched crew did not extinguish and repair real damage");
        check(emergency.spares() < spares && emergency.medical() < medical,
              "Damage control failed to consume real stores");
        check(std::none_of(emergency.members().begin(), emergency.members().end(),
                           [](const auto& m) {
                               return m.order == CrewOrder::Firefight || m.order == CrewOrder::Repair ||
                                      m.order == CrewOrder::Aid;
                           }),
              "Completed manual tasks did not return crew to duty");
        ShipCrew emptyTask(48);
        emptyTask.toggleAutomatic();
        for (auto job : {CrewCommand::Repair, CrewCommand::Firefight, CrewCommand::Aid}) {
            emptyTask.command(job, 0, CrewStation::Bridge);
            emptyTask.advance(.25);
            check(emptyTask.members()[0].order == CrewOrder::Duty, "Already completed task stranded crew");
        }
        emptyTask.command(CrewCommand::Rest, 0, CrewStation::Bunks);
        for (unsigned i = 0; i < 100; ++i)
            emptyTask.advance(.25);
        check(emptyTask.members()[0].order == CrewOrder::Rest, "Manual rest ended before captain recall");
        emptyTask.command(CrewCommand::Return, 0, CrewStation::Bridge);
        check(emptyTask.members()[0].order == CrewOrder::Duty, "Return-to-duty command failed");
        const auto automatic = emptyTask.automatic();
        emptyTask.command(CrewCommand::Automatic, 0, CrewStation::Bridge);
        check(emptyTask.automatic() != automatic, "Auto-response control failed");
        ShipCrew a(48), b(48);
        for (auto* ship : {&a, &b}) {
            ship->observeHull(100);
            ship->observeHull(70);
            ship->respond();
        }
        for (unsigned i = 0; i < 120; ++i)
            a.advance(.25);
        for (unsigned i = 0; i < 600; ++i)
            b.advance(.05);
        check(a.serialize() == b.serialize(), "Crew response depends on rendering frame rate");
        auto view = c.view(0, CrewStation::Bridge);
        view.flightOpen = true;
        FlightInstruments instruments;
        for (double aspect : {2.4, 1.6, 1., .75}) {
            const CrewFlightLayout l(aspect);
            const CockpitLayout flight(aspect);
            check(!l.weapon.overlaps(l.panel) && !l.weapon.overlaps(l.notice),
                  "Flight notice or crew console covered the live weapon readout");
            check(!l.notice.overlaps(l.panel) && l.weapon.x + l.weapon.w <= 900 * aspect - 24,
                  "Crew flight notice or weapon card escaped the usable layout");
            std::vector<CockpitRect> controls{l.automatic};
            controls.insert(controls.end(), l.roster.begin(), l.roster.end());
            controls.insert(controls.end(), l.rooms.begin(), l.rooms.end());
            controls.insert(controls.end(), l.actions.begin(), l.actions.end());
            for (const auto& r : controls) {
                check(r.x >= l.panel.x && r.y >= l.panel.y && r.x + r.w <= l.panel.x + l.panel.w &&
                          r.y + r.h <= l.panel.y + l.panel.h,
                      "Crew control escaped panel");
                for (const auto& other :
                     {flight.navigation, flight.flight, flight.scope, flight.systems, flight.celestial})
                    check(!r.overlaps(other), "Crew console covered a flight instrument");
            }
            for (size_t i = 0; i < controls.size(); ++i)
                for (size_t j = i + 1; j < controls.size(); ++j)
                    check(!controls[i].overlaps(controls[j]), "Crew command controls overlap");
            check(!l.panel.contains({900 * aspect * .5, 450}), "Crew console obscured the aim point");
            auto hud =
                cockpitOverlay(instruments, aspect, "", {}, {}, {}, false, 1, nullptr, {1, 0, 0, 0}, &view);
            check(!hud.lettering.vertices.empty() &&
                      std::any_of(hud.hitRegions.begin(), hud.hitRegions.end(),
                                  [&](const auto& r) { return r.contains({l.panel.x + 2, l.panel.y + 2}); }),
                  "Crew console missing drawing or mouse-hit protection");
        }
        check(c.residentBytes() < 32768, "Crew management exceeded the bounded ship-state budget");
        std::cout << checks
                  << " in-flight crew station matching, dispatch, task completion, status, movement, "
                     "persistence and UI checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
