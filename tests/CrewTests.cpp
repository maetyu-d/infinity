#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 460046;
    EconomicSite d;
    d.id = {n->system.child(Kind::Planet, 0), 0};
    d.name = "CREW PORT";
    d.orbital = true;
    d.capacityKg = 2000000;
    d.initial.fill(5000);
    n->sites.push_back(d);
    return n;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.player = p;
    s.tick = e.tick();
    return s;
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& system, uint64_t command = 0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        auto s = r.snapshot();
        if (s && s->network && s->network->system == system && !s->catchingUp &&
            s->player.lastCommand >= command)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Crew economic worker did not publish");
}
} // namespace
int main(int argc, char** argv) {
    try {
        ShipCrew c(46), same(46);
        check(c.serialize() == same.serialize() && c.living() == 6, "Starter crew not deterministic");
        std::set<uint64_t> ids;
        for (const auto& m : c.members())
            check(ids.insert(m.id).second, "Duplicate crew identity");
        std::set<std::string> names;
        for (const auto& m : c.members())
            check(names.insert(m.name).second, "Duplicate starter crew callsign");
        const auto ready = c.weaponReadiness();
        c.assign(2, CrewStation::Bunks);
        c.advance(.25);
        check(c.weaponReadiness() < ready, "Vacant gun station retained its bonus");
        c.assign(2, CrewStation::Weapons);
        for (unsigned i = 0; i < 100; ++i)
            c.advance(.25);
        check(c.weaponReadiness() > .99, "Gunner failed to reach station");
        c.toggleAutomatic();
        c.observeHull(100);
        c.observeHull(70);
        size_t damaged = 0;
        for (size_t i = 0; i < crewRoomCount; ++i)
            if (c.rooms()[i].fire > 0)
                damaged = i;
        check(c.rooms()[damaged].fire > 0 && c.rooms()[damaged].integrity < 100,
              "Hull damage did not damage compartments");
        check(
            std::any_of(c.members().begin(), c.members().end(), [](const auto& m) { return m.health < 100; }),
            "Hull hit caused no crew injury");
        c.order(3, CrewOrder::Firefight, CrewStation(damaged));
        c.order(4, CrewOrder::Repair, CrewStation(damaged));
        c.order(5, CrewOrder::Aid, CrewStation(damaged));
        for (unsigned i = 0; i < 360; ++i)
            c.advance(.25, 2, true);
        check(c.rooms()[damaged].fire == 0 && c.rooms()[damaged].integrity > 90 && c.spares() < 30 &&
                  c.medical() < 12,
              "Crew failed firefighting, repair or first aid");
        const auto text = c.serialize();
        ShipCrew restored(46);
        check(restored.restore(text) && restored.serialize() == text, "Crew save round trip lost state");
        check(!restored.restore(text + " junk") && restored.serialize() == text, "Corrupt save altered crew");
        ShipCrew wrong(17);
        check(!wrong.restore(text), "Crew state crossed universe seeds");
        ShipCrew walking(46);
        walking.toggleAutomatic();
        walking.assign(0, CrewStation::Bunks);
        walking.advance(.25);
        check(walking.members()[0].travel > 0, "Crew teleported instead of walking");
        ShipCrew moving(46);
        check(moving.restore(walking.serialize()) && moving.serialize() == walking.serialize(),
              "Movement did not survive reload");
        auto partial = walking.members()[0];
        walking.order(0, CrewOrder::Repair, CrewStation::Weapons);
        check(walking.members()[0].room == partial.room && walking.members()[0].travel == partial.travel,
              "Reassignment teleported walking crew");
        ShipCrew a(46), b(46);
        a.observeHull(100);
        b.observeHull(100);
        a.observeHull(75);
        b.observeHull(75);
        for (unsigned i = 0; i < 120; ++i)
            a.advance(.25, 3, true, true);
        for (unsigned i = 0; i < 600; ++i)
            b.advance(.05, 3, true, true);
        check(a.serialize() == b.serialize(), "Crew depends on rendering frame rate");
        ShipCrew vented(46);
        vented.toggleAutomatic();
        vented.vent(CrewStation::Weapons);
        for (unsigned i = 0; i < 240; ++i)
            vented.advance(.25);
        check(vented.rooms()[2].oxygen < 18 && vented.members()[2].health < 100,
              "Venting had no oxygen or crew consequence");
        vented.vent(CrewStation::Weapons);
        for (unsigned i = 0; i < 240; ++i)
            vented.advance(.25);
        check(vented.rooms()[2].oxygen > 50, "Sealed cabin failed to repressurise");
        vented.vent(CrewStation::Weapons);
        for (unsigned i = 0; i < 240; ++i)
            vented.advance(1);
        check(vented.members()[2].health == 0 && vented.living() == 5,
              "Sustained decompression caused no casualty");
        const auto casualty = vented.members()[2].id;
        vented.reconcile({0, 0, 1});
        check(vented.members()[2].health == 0, "Clinic resurrected lost crew");
        vented.reconcile({1, 0, 1});
        check(vented.living() == 6 && vented.members()[2].id != casualty,
              "Paid relief failed to replace casualty");
        ShipCrew training(46);
        auto xp = training.members()[2].experience[2];
        training.advance(10, 0, false, false);
        check(training.members()[2].experience[2] == xp, "Unfired guns farmed gunnery experience");
        training.advance(10, 0, true, true);
        check(training.members()[2].experience[2] > xp, "Firing failed to train gunner");
        ShipCrew autoCrew(46);
        autoCrew.observeHull(100);
        autoCrew.observeHull(70);
        autoCrew.advance(.25);
        check(std::any_of(autoCrew.members().begin(), autoCrew.members().end(),
                          [](const auto& m) { return m.automatic && m.order == CrewOrder::Firefight; }),
              "Automatic damage control ignored fire");
        ShipCrew receipts(46);
        receipts.reconcile({2, 1, 1});
        check(receipts.living() == 8 && receipts.receipts().hired == 2, "Paid recruits not added");
        auto saved = receipts.serialize();
        receipts.reconcile({2, 1, 1});
        check(receipts.serialize() == saved, "Replayed receipts duplicated crew or stores");
        receipts.reconcile({3, 1, 1});
        check(receipts.receipts().hired == 2 && receipts.living() == 8,
              "Full roster discarded paid recruit voucher");
        receipts.dismiss(7);
        receipts.reconcile({3, 1, 1});
        check(receipts.living() == 8 && receipts.receipts().hired == 3,
              "Queued paid recruit failed after berth available");
        for (size_t i = 0; i < 10000; ++i)
            receipts.reconcile({3, 1000000, 1000000});
        check(receipts.residentBytes() < 32768 && receipts.spares() <= 100 && receipts.medical() <= 50,
              "Crew memory or supplies unbounded");
        auto net = fixture();
        Economy econ(net);
        EconomyPlayer player;
        player.credits = 10000000;
        const auto money = econ.moneyTotal() + player.credits;
        uint64_t sequence = 0;
        for (auto service : {ShipService::CrewHire, ShipService::CrewSupplies, ShipService::CrewCare}) {
            const auto q = economicServiceQuote(snapshot(econ, player), 0, service, 1);
            check(q.available, "Crew service not purchasable at stocked port");
            EconomicCommand cmd;
            cmd.port = net->sites[0].id;
            cmd.serviceUnits = 1;
            cmd.limitCents = q.totalCents();
            cmd.sequence = ++sequence;
            cmd.action = service == ShipService::CrewHire       ? EconomicAction::HireCrew
                         : service == ShipService::CrewSupplies ? EconomicAction::SupplyCrew
                                                                : EconomicAction::TreatCrew;
            const auto result = econ.execute(cmd, player);
            check(result.find("PAID") != std::string::npos || result.find("SIGNED") != std::string::npos,
                  "Crew service order failed");
            check(econ.moneyTotal() + player.credits == money, "Crew purchase lost money");
            auto initial = econ.initialTotal(), delta = econ.externalGoodsDelta(),
                 total = econ.inventoryTotal();
            for (size_t g = 0; g < goodCount; ++g)
                check(initial[g] + delta[g] == total[g], "Crew purchase lost materials");
            const auto wallet = player.serialize();
            econ.execute(cmd, player);
            check(player.serialize() == wallet, "Replayed crew order charged twice");
        }
        check(player.crew.hired == 1 && player.crew.supplied == 1 && player.crew.treated == 1,
              "Missing durable crew receipts");
        EconomyPlayer loaded;
        check(loaded.restore(player.serialize()) && loaded.serialize() == player.serialize(),
              "Crew wallet failed persistence");
        auto legacy = player.serialize();
        legacy.replace(0, std::string("INFINITE_WALLET 5").size(), "INFINITE_WALLET 4");
        for (unsigned i = 0; i < 3; ++i)
            legacy.erase(legacy.find_last_of(' '));
        check(loaded.restore(legacy) && loaded.crew.hired == 0 && loaded.crew.supplied == 0,
              "Legacy wallet migration granted paid receipts");
        auto bad = player.serialize();
        bad += " corrupt";
        check(!loaded.restore(bad), "Corrupt wallet accepted");
        econ.outage(net->sites[0].id, 100);
        check(!economicServiceQuote(snapshot(econ, player), 0, ShipService::CrewHire, 1).available,
              "Outage left crew office open");
        EconomyPlayer poor;
        poor.credits = 0;
        check(!economicServiceQuote(snapshot(Economy(net), poor), 0, ShipService::CrewCare, 1).available,
              "Clinic granted unfunded treatment");
        // A real worker commits the wallet before the UI redeems its receipt.
        const auto data = std::filesystem::path(argc > 1 ? argv[1] : "build/crew/runtime-test");
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        Universe universe;
        auto actor = sampleActivity(universe.describe(net->sites[0].id.home), 300, 12);
        WeaponRay ray;
        ray.reference = actor.id.home;
        ray.origin = actor.position + glm::dvec3(0, 0, 1200);
        ray.direction = glm::normalize(actor.position - ray.origin);
        WeaponSystem plain, supported;
        supported.setCrewReadiness(1.2);
        check(plain.fire(universe, ray, std::span(&actor, 1)) &&
                  supported.fire(universe, ray, std::span(&actor, 1)),
              "Crew-supported gun failed to fire");
        check(supported.cooldown() < plain.cooldown(), "Gunner failed to improve actual reload");
        plain.advance(.1);
        supported.advance(.1);
        check(supported.heat() < plain.heat(), "Gunner failed to improve actual cooling");
        uint64_t hired = 0;
        {
            EconomyRuntime runtime(universe, data);
            runtime.request(net->system, 0);
            auto live = wait(runtime, net->system);
            size_t port = 0;
            while (port < live->sites.size() &&
                   !economicServiceQuote(*live, port, ShipService::CrewHire, 1).available)
                ++port;
            check(port < live->sites.size(), "No real stocked crew office");
            auto q = economicServiceQuote(*live, port, ShipService::CrewHire, 1);
            EconomicCommand hire;
            hire.sequence = live->player.lastCommand + 1;
            hire.port = live->network->sites[port].id;
            hire.action = EconomicAction::HireCrew;
            hire.serviceUnits = 1;
            hire.limitCents = q.totalCents();
            check(runtime.command(hire), "Worker rejected crew contract");
            live = wait(runtime, net->system, hire.sequence);
            hired = live->player.crew.hired;
            check(hired == 1, "Worker did not commit crew receipt");
            runtime.flush();
        }
        {
            EconomyRuntime runtime(universe, data);
            runtime.request(net->system, 0);
            auto live = wait(runtime, net->system);
            check(live->player.crew.hired == hired, "Crew receipt lost on SQLite restart");
            ShipCrew ship(universe.seed());
            ship.reconcile(live->player.crew);
            check(ship.living() == 7, "Reloaded paid crew contract not redeemable");
            auto text = ship.serialize();
            ship.reconcile(live->player.crew);
            check(ship.serialize() == text, "Reload duplicated paid recruit");
        }
        auto view = c.view();
        view.open = true;
        FlightInstruments t;
        for (double aspect : {1.6, 1.1, .75}) {
            const CrewPanelLayout l(aspect);
            for (size_t i = 0; i < crewRoomCount; ++i)
                for (size_t j = i + 1; j < crewRoomCount; ++j)
                    check(!l.rooms[i].overlaps(l.rooms[j]), "Crew room cards overlap");
            for (const auto& r : l.rooms)
                check(r.x >= l.panel.x && r.y >= l.panel.y && r.x + r.w <= l.panel.x + l.panel.w &&
                          r.y + r.h <= l.panel.y + l.panel.h,
                      "Crew compartment escaped panel");
            auto hud = cockpitOverlay(t, aspect, "", {}, {}, {}, false, 1, nullptr, {1, 0, 0, 0}, &view);
            check(!hud.lettering.vertices.empty(), "Crew console not drawn");
        }
        std::cout << checks
                  << " crew stations, walking, injuries, damage control, finite supplies, determinism, "
                     "receipts, accounting, migration and HUD checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
