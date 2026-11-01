#include "infinite/CombatActors.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->universeSeed = Universe{}.seed();
    n->fingerprint = 520052;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite s;
        s.id = {n->system.child(Kind::Planet, i), 0};
        s.orbital = true;
        s.name = "RESCUE PORT";
        s.capacityKg = 1000000;
        s.initial.fill(2000);
        s.workers = s.power = 20;
        s.systemMetres = {double(i) * 6e9, 0, 0};
        if (i) {
            s.initial[size_t(Good::Ore)] = 0;
            s.target[size_t(Good::Ore)] = 1000;
        }
        n->sites.push_back(s);
    }
    n->vessels.push_back({n->sites[0].id.home, 100});
    return n;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Recovery minted or lost money");
    auto a = e.initialTotal(), d = e.externalGoodsDelta(), v = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(a[g] + d[g] == v[g], "Recovery lost finite freight or scrap");
    Economy restored(std::make_shared<EconomicNetwork>(e.network()));
    check(restored.restore(e.serialize()) && restored.serialize() == e.serialize(),
          "Recovery ledger restart failed");
    EconomyPlayer wallet;
    check(wallet.restore(p.serialize()) && wallet.serialize() == p.serialize(),
          "Rescued passenger wallet restart failed");
}
EconomicCommand command(const Economy& e, const EconomyPlayer& p, ActivityId id, EconomicAction action) {
    EconomicCommand c;
    c.sequence = p.lastCommand + 1;
    c.port = id;
    c.combatTarget = id;
    c.action = action;
    WeaponDamage d;
    d.id = id;
    d.hull = 0;
    d.hitSeconds = e.tick() * economyTickSeconds;
    c.wreck = d;
    c.combatPresent = true;
    c.targetDistance = 50;
    c.relativeSpeed = 0;
    c.crewAboard = 8;
    return c;
}
} // namespace
int main(int argc, char** argv) {
    try {
        auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        const auto money = e.moneyTotal() + p.credits;
        e.advanceTo(5);
        check(e.freight()[0].kg > 0, "No canonical freight to recover");
        const auto freightId = n->vessels[0];
        auto c = command(e, p, freightId, EconomicAction::RegisterWreck);
        check(e.execute(c, p).starts_with("WRECK REGISTERED"), "Destroyed freight not registered");
        check(e.freight()[0].disabledAt >= 0, "Recovery failed to pause disabled freight dispatch");
        audit(e, p, money);
        const auto before = e.serialize();
        c.sequence = ++p.lastCommand; // replay guard must leave the ledger unchanged
        check(e.execute(c, p) == "ORDER ALREADY PROCESSED" && e.serialize() == before,
              "Duplicate wreck registration changed inventory");
        c = command(e, p, freightId, EconomicAction::RecoverWreck);
        c.relativeSpeed = 4;
        check(e.execute(c, p).starts_with("RECOVERY REQUIRES"), "Fast recovery accepted");
        c = command(e, p, freightId, EconomicAction::RecoverWreck);
        c.targetDistance = 121;
        check(e.execute(c, p).starts_with("RECOVERY REQUIRES"), "Remote recovery accepted");
        const auto freightBefore = e.freight()[0].kg;
        p.capacityKg = 10;
        p.capacityLitres = 100;
        c = command(e, p, freightId, EconomicAction::RecoverWreck);
        check(e.execute(c, p).starts_with("RECOVERED 10KG") && e.freight()[0].kg == freightBefore - 10,
              "Partial recovery did not preserve remaining freight");
        p.capacityKg = 4000;
        p.capacityLitres = 8000;
        audit(e, p, money);
        c = command(e, p, freightId, EconomicAction::RecoverWreck);
        e.execute(c, p);
        audit(e, p, money);
        const auto cargo = p.cargo;
        c = command(e, p, freightId, EconomicAction::RecoverWreck);
        e.execute(c, p);
        check(p.cargo == cargo, "An empty wreck produced more cargo");
        std::optional<ActivityId> rescue;
        for (unsigned slot = 200; slot < 208 && !rescue; ++slot) {
            ActivityId id{n->sites[0].id.home, slot};
            c = command(e, p, id, EconomicAction::RegisterWreck);
            e.execute(c, p);
            if (e.wrecks().back().survivor)
                rescue = id;
        }
        check(rescue.has_value(), "Deterministic fixtures contained no survivors");
        const auto& record = *std::find_if(e.wrecks().begin(), e.wrecks().end(),
                                           [&](const auto& w) { return w.id == *rescue; });
        const auto person = *record.survivor;
        c = command(e, p, *rescue, EconomicAction::RecoverWreck);
        c.crewAboard = 7;
        check(e.execute(c, p).find("SURVIVOR ABOARD") != std::string::npos && p.rescued.size() == 1,
              "Survivor did not use an available berth");
        check(p.rescued[0].person.id == person.id && p.rescued[0].person.health == person.health,
              "Recovery replaced identity or healed injuries");
        c = command(e, p, *rescue, EconomicAction::RecoverWreck);
        c.crewAboard = 7;
        e.execute(c, p);
        check(p.rescued.size() == 1, "Repeated pickup cloned the survivor");
        audit(e, p, money);
        c = command(e, p, n->sites[1].id, EconomicAction::DisembarkRescued);
        check(e.execute(c, p).starts_with("SURVIVORS DISEMBARKED") && p.rescued.empty() &&
                  p.rescuesDelivered == 1,
              "Port rescue handover failed");
        const auto& market = *std::find_if(e.crewMarkets().begin(), e.crewMarkets().end(),
                                           [](const auto& x) { return x.site == 1; });
        check(std::any_of(market.offers.begin(), market.offers.end(),
                          [&](const auto& m) { return m.id == person.id && m.health == person.health; }),
              "Port office lost rescued identity or injuries");
        audit(e, p, money);
        EconomySnapshot s;
        s.network = n;
        s.sites = e.sites();
        s.player = p;
        s.tick = e.tick();
        s.crewMarkets = e.crewMarkets();
        auto q = economicCrewQuote(s, 1, person, true, 7);
        check(q.available, "Rescued person did not enter existing crew market");
        c = {};
        c.sequence = p.lastCommand + 1;
        c.port = n->sites[1].id;
        c.action = EconomicAction::BuyCrew;
        c.crewId = person.id;
        c.crewAboard = 7;
        c.limitCents = q.buyCents;
        check(e.execute(c, p).starts_with("CREW BOUGHT") && crewOwned(p, person.id),
              "Rescued person could not join through existing crew trade");
        audit(e, p, money);
        auto rootAddress = e.serialize();
        const auto recoveryRows = rootAddress.find("WRECK_RECOVERY ");
        const auto beginId = rootAddress.find('"', recoveryRows), endId = rootAddress.find('"', beginId + 1);
        rootAddress.replace(beginId + 1, endId - beginId - 1, "0,0,0@activity1:100");
        check(!e.restore(rootAddress), "Malformed recovery root address was accepted");
        const auto good = e.serialize();
        check(!e.restore(good + " corrupt") && e.serialize() == good,
              "Malformed recovery save changed live state");
        if (argc > 1) {
            const auto path = std::filesystem::path(argv[1]);
            if (path.filename() != "runtime-test" || path.parent_path().filename() != "wreck-recovery")
                throw std::runtime_error("Recovery fixture requires isolated runtime-test directory");
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
            Universe universe;
            Stock savedCargo{};
            size_t savedPeople = 0;
            ActivityId id;
            auto wait = [](EconomyRuntime& runtime, uint64_t sequence) {
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(40);
                while (std::chrono::steady_clock::now() < until) {
                    auto s = runtime.snapshot();
                    if (s && s->network && !s->catchingUp && s->tick >= 12 &&
                        s->player.lastCommand >= sequence)
                        return s;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                throw std::runtime_error("Recovery SQLite worker timed out");
            };
            {
                EconomyRuntime runtime(universe, path);
                runtime.request(n->system, 60);
                auto s = wait(runtime, 0);
                id = {s->network->sites[0].id.home, raiderActivitySlot};
                EconomicCommand c;
                c.sequence = s->player.lastCommand + 1;
                c.action = EconomicAction::RegisterWreck;
                c.combatTarget = id;
                c.port = id;
                WeaponDamage d;
                d.id = id;
                d.hull = 0;
                d.hitSeconds = 60;
                c.wreck = d;
                check(runtime.command(c), "Worker rejected wreck registration");
                s = wait(runtime, c.sequence);
                c.sequence++;
                c.action = EconomicAction::RecoverWreck;
                c.combatPresent = true;
                c.targetDistance = 50;
                c.relativeSpeed = 0;
                c.crewAboard = 0;
                check(runtime.command(c), "Worker rejected physical recovery");
                s = wait(runtime, c.sequence);
                savedCargo = s->player.cargo;
                savedPeople = s->player.rescued.size();
                check(s->player.massKg() == 22, "Worker did not publish finite salvage");
                runtime.flush();
            }
            EconomyRuntime resumed(universe, path);
            resumed.request(n->system, 60);
            auto s = wait(resumed, 0);
            check(s->player.cargo == savedCargo && s->player.rescued.size() == savedPeople &&
                      s->wrecks.size() == 1,
                  "SQLite restart lost cargo, survivor or consumed wreck receipt");
            EconomicCommand c;
            c.sequence = s->player.lastCommand + 1;
            c.action = EconomicAction::RecoverWreck;
            c.combatTarget = id;
            c.port = id;
            c.combatPresent = true;
            c.targetDistance = 50;
            c.relativeSpeed = 0;
            c.crewAboard = 0;
            check(resumed.command(c), "Worker rejected repeated recovery check");
            s = wait(resumed, c.sequence);
            check(s->player.cargo == savedCargo && s->player.rescued.size() == savedPeople,
                  "SQLite restart allowed duplicate cargo or survivor pickup");
            resumed.flush();
        }
        std::cout << "PASS: " << checks
                  << " finite cargo, partial holds, rescue berths, identity, port rewards, crew integration "
                     "and restart checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
