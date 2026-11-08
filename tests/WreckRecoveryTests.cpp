#include "infinity/CombatActors.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Economy.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace infinity;
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
EconomySnapshot incidentSnapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.tick = e.tick();
    s.sites = e.sites();
    s.incidents = e.incidents();
    s.hauls = e.hauls();
    s.recoveries = e.recoveries();
    s.wrecks = e.wrecks();
    s.player = p;
    for (const auto& f : e.freight()) {
        EconomicFlight flight;
        flight.id = f.id;
        flight.source = f.source;
        flight.destination = f.destination;
        flight.good = f.good;
        flight.phase = f.phase;
        flight.kg = f.kg;
        flight.disabledAt = f.disabledAt;
        s.flights.push_back(flight);
    }
    return s;
}

void checkIncident(bool alternateCarrier) {
    auto n = fixture();
    auto& colony = n->sites[1];
    colony.orbital = false;
    colony.sealed = true;
    colony.population = 250;
    colony.initial[size_t(Good::Oxygen)] = 8;
    colony.target.fill(0);
    colony.target[size_t(Good::Oxygen)] = 100;
    if (alternateCarrier)
        n->vessels.push_back({n->sites[0].id.home, 101});
    Economy e(n);
    EconomyPlayer p;
    const auto money = e.moneyTotal() + p.credits;
    e.advanceTo(5);
    const auto original = e.freight()[0];
    check(original.phase == FreightPhase::Transit && original.good == Good::Oxygen,
          "Fixture did not dispatch real oxygen cargo");
    const auto before = incidentSnapshot(e, p);
    auto c = command(e, p, original.id, EconomicAction::RegisterWreck);
    check(e.execute(c, p).starts_with("WRECK REGISTERED") && e.incidents().size() == 1 &&
              e.incidents()[0].displacedKg == original.kg, "Freight damage did not create a manifest-linked incident");
    const auto opened = incidentSnapshot(e, p);
    check(economicIncidentRadio(before, opened).find("FREIGHTER DISABLED") != std::string::npos &&
              economicIncidentRadio(opened, opened).empty(), "Incident radio omitted or repeated the same event");
    auto legacy = e.serialize();
    legacy.erase(legacy.find("FREIGHT_INCIDENTS "));
    legacy.replace(17, legacy.find(' ', 17) - 17, "10");
    Economy migrated(n);
    check(migrated.restore(legacy) && migrated.incidents().size() == 1 &&
              migrated.incidents()[0].displacedKg == original.kg && migrated.incidents()[0].playerKg == 0,
          "Legacy disabled cargo did not migrate without invented receipts");
    e.advanceTo(12);
    auto current = incidentSnapshot(e, p);
    check(e.incidents()[0].shortage > 0 && e.incidents()[0].lowestHealth < e.incidents()[0].initialHealth,
          "Households did not actually draw down the interrupted oxygen stock");
    if (alternateCarrier) {
        check(e.incidents()[0].carrierKg == original.kg && e.incidents()[0].playerKg == 0 &&
                  !economicIncidentJob(current, 0, 1), "Actual NPC delivery did not cover the interrupted cargo");
        check(economicIncidentRadio(opened, current).find("SHIPMENT RECEIVED") != std::string::npos,
              "NPC delivery did not announce the replacement");
        audit(e, p, money);
        return;
    }

    const auto relay = economicIncidentJob(current, 0, 1);
    const auto relief = economicIncidentJob(current, 0, 2);
    check(relay && relay->haul && relay->target == n->sites[0].id && relief && relief->recovery &&
              relief->target == colony.id && relief->required[size_t(Good::Oxygen)] > 0,
          "Incident did not expose the matching relay and stock-driven relief actions");
    check(economicIncidentRadio(opened, current).find("REQUESTS HOUSEHOLD RELIEF") != std::string::npos,
          "Relief did not produce a radio update");
    MarketContext context;
    context.systemPosition = n->sites[0].systemMetres;
    context.incidentResponse = 1;
    const auto card = economicMarketView(&current, {}, 0, 10, false, MarketPage::Incidents, context);
    check(card.detail.size() == 9 && card.detail[2].find("OXYGEN RESERVE") != std::string::npos &&
              card.planTiming.starts_with("EST "), "Incident card omitted reserve or travel estimate");
    for (int width : {1440, 576}) {
        const CockpitViewport viewport(width, 900, {0, 0, double(width), 900});
        auto overlay = cockpitOverlay({}, viewport.aspect(), "", "", {}, {}, false, viewport.projectionScale, &card);
        const auto conflicts = cockpitTextConflicts(overlay);
        if (!conflicts.empty()) {
            const auto [a, b] = conflicts.front();
            std::cerr << "Incident viewport " << width << ": " << overlay.textRuns[a].value
                      << " overlaps " << overlay.textRuns[b].value << '\n';
        }
        check(conflicts.empty(), "Incident card text overlaps in the cockpit");
    }
    if (const auto rescue = economicIncidentJob(current, 0, 0)) {
        check(rescue->target == original.id, "Incident rescue did not target the disabled carrier");
        c = command(e, p, original.id, EconomicAction::RecoverWreck);
        c.crewAboard = 7;
        check(e.execute(c, p).find("SURVIVOR ABOARD") != std::string::npos, "Incident survivor could not board");
        const auto boarding = incidentSnapshot(e, p);
        const auto handover = economicIncidentJob(boarding, 0, 0);
        check(handover && handover->action == EconomicAction::DisembarkRescued && handover->target == n->sites[0].id,
              "Incident rescue did not switch to the issuer");
        c = command(e, p, handover->target, handover->action);
        c.combatTarget = original.id;
        check(e.execute(c, p).starts_with("SURVIVORS DISEMBARKED"), "Incident rescue handover failed");
    }
    auto issue = [&](const EconomySnapshot::Job& job, EconomicAction action, const ActivityId& port) {
        EconomicCommand order;
        order.sequence = p.lastCommand + 1;
        order.port = port;
        order.action = action;
        order.good = job.good;
        order.kg = job.kg;
        order.contract = job.id;
        return e.execute(order, p);
    };
    check(issue(*relief, EconomicAction::AcceptRecovery, colony.id).starts_with("RECOVERY ACCEPTED"),
          "Incident relief could not be accepted");
    for (size_t g = 0; g < goodCount; ++g) {
        const auto good = Good(g);
        const auto need = relief->required[g] - p.availableCargo(good);
        if (need <= 0)
            continue;
        EconomicCommand buy{p.lastCommand + 1, n->sites[0].id, EconomicAction::Buy, good, need};
        buy.limitCents = e.quote(0, good, need).buyCents;
        check(e.execute(buy, p).starts_with("BOUGHT"), "Incident relief supplies could not be sourced");
    }
    check(issue(*relief, EconomicAction::DeliverRecovery, colony.id).starts_with("RECOVERY DELIVERED") &&
              e.incidents()[0].playerKg > 0 && e.incidents()[0].restored == 0,
          "Relief was not credited or restored health instantaneously");
    check(issue(*relay, EconomicAction::AcceptHaul, n->sites[0].id).starts_with("HAUL ACCEPTED") &&
              issue(*relay, EconomicAction::PickupHaul, n->sites[0].id).starts_with("SEALED CARGO LOADED") &&
              issue(*relay, EconomicAction::DeliverHaul, colony.id).starts_with("HAUL DELIVERED"),
          "Incident relay did not use the normal sealed-cargo loop");
    check(e.incidents()[0].playerKg == original.kg && e.incidents()[0].carrierKg == 0,
          "Incident double-counted relief and replacement cargo");
    e.advanceTo(55);
    current = incidentSnapshot(e, p);
    check(e.incidents()[0].restored && e.sites()[1].health >= e.incidents()[0].initialHealth,
          "Colony did not recover from delivered stock over time");
    const auto result = economicMarketView(&current, {}, 0, 10, false, MarketPage::Incidents);
    check(result.role.starts_with("INCIDENT DEBRIEF") && result.detail[5].starts_with("RECOVERY OBSERVED"),
          "Debrief did not report observed colony recovery");
    audit(e, p, money);
}
} // namespace
int main(int argc, char** argv) {
    try {
        checkIncident(false);
        checkIncident(true);
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
        e.advanceTo(12);
        const auto relay = std::find_if(e.hauls().begin(), e.hauls().end(),
                                        [](const auto& h) { return h.interruptedFlight == 1; });
        check(relay != e.hauls().end() && relay->good == e.freight()[0].good &&
                  relay->destination == e.freight()[0].destination && relay->kg <= e.freight()[0].kg,
              "Disabled canonical cargo run did not generate a matched relay");
        const auto offer = *relay;
        audit(e, p, money);
        for (const auto action : {EconomicAction::AcceptHaul, EconomicAction::PickupHaul, EconomicAction::DeliverHaul}) {
            EconomicCommand transfer;
            transfer.sequence = p.lastCommand + 1;
            transfer.port = n->sites[action == EconomicAction::DeliverHaul ? offer.destination : offer.source].id;
            transfer.action = action;
            transfer.good = offer.good;
            transfer.kg = offer.kg;
            transfer.contract = offer.id;
            e.execute(transfer, p);
        }
        check(p.deliveries == 1 && e.outcomes().back().kind == OutcomeKind::Relay &&
                  e.outcomes().back().payment == offer.fee, "Relay did not pay once through the sealed delivery");
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
        check(record.rescueEscrow == rescueReward && record.rescueDeadline > e.tick(),
              "Confirmed civilian survivor did not reserve a reward");
        EconomySnapshot board;
        board.network = n;
        board.tick = e.tick();
        board.wrecks = e.wrecks();
        const auto jobs = economicJobs(board);
        check(std::any_of(jobs.begin(), jobs.end(), [&](const auto& j) {
                  return j.distress == rescue && j.target == *rescue && j.action == EconomicAction::RecoverWreck;
              }), "Distress offer did not target the real wreck");
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
        board.player = p;
        board.wrecks = e.wrecks();
        const auto aboard = economicJobs(board);
        check(!aboard.empty() && aboard.front().distress == rescue && aboard.front().target == n->sites[0].id &&
                  aboard.front().action == EconomicAction::DisembarkRescued, "Rescue board did not switch to handover");
        c = command(e, p, n->sites[0].id, EconomicAction::DisembarkRescued);
        c.combatTarget = rescue;
        const auto credits = p.credits;
        check(e.execute(c, p).starts_with("SURVIVORS DISEMBARKED") && p.rescued.empty() &&
                  p.rescuesDelivered == 1 && p.credits == credits + rescueReward &&
                  e.outcomes().back().kind == OutcomeKind::Rescue && record.rescueEscrow == 0,
              "Port rescue handover failed");
        const auto& market = *std::find_if(e.crewMarkets().begin(), e.crewMarkets().end(),
                                           [](const auto& x) { return x.site == 0; });
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
        auto q = economicCrewQuote(s, 0, person, true, 7);
        check(q.available, "Rescued person did not enter existing crew market");
        c = {};
        c.sequence = p.lastCommand + 1;
        c.port = n->sites[0].id;
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
            int64_t savedInterrupted = 0;
            ActivityId interrupted;
            uint64_t incidentTick = 12;
            ActivityId id;
            auto wait = [](EconomyRuntime& runtime, uint64_t sequence, uint64_t minimumTick = 12) {
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(40);
                while (std::chrono::steady_clock::now() < until) {
                    auto s = runtime.snapshot();
                    if (s && s->network && !s->catchingUp && s->tick >= minimumTick &&
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
                const auto hasCargo = [](const auto& f) {
                    return f.disabledAt < 0 && f.kg > 0;
                };
                for (const uint64_t tick : {24u, 36u, 60u, 96u}) {
                    if (std::any_of(s->flights.begin(), s->flights.end(), hasCargo))
                        break;
                    runtime.request(n->system, tick * economyTickSeconds);
                    s = wait(runtime, c.sequence, tick);
                }
                const auto loaded = std::find_if(s->flights.begin(), s->flights.end(), hasCargo);
                check(loaded != s->flights.end(), "Generated system has no loaded carrier for incident test");
                incidentTick = s->tick;
                savedInterrupted = loaded->kg;
                interrupted = loaded->id;
                EconomicCommand hit;
                hit.sequence = s->player.lastCommand + 1;
                hit.port = interrupted;
                hit.action = EconomicAction::DisableVessel;
                hit.impactSeconds = s->tick * economyTickSeconds;
                check(runtime.command(hit), "Worker rejected generated freight disruption");
                s = wait(runtime, hit.sequence);
                check(s->incidents.size() == 1 && s->incidents[0].displacedKg == savedInterrupted,
                      "Worker did not publish the real incident manifest");
                runtime.flush();
            }
            EconomyRuntime resumed(universe, path);
            resumed.request(n->system, incidentTick * economyTickSeconds);
            auto s = wait(resumed, 0, incidentTick);
            check(s->player.cargo == savedCargo && s->player.rescued.size() == savedPeople &&
                      s->wrecks.size() == 1,
                  "SQLite restart lost cargo, survivor or consumed wreck receipt");
            check(s->incidents.size() == 1 && s->incidents[0].displacedKg == savedInterrupted &&
                      s->flights[s->incidents[0].flight - 1].id == interrupted,
                  "SQLite restart lost the incident or changed its target");
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
