#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <algorithm>
#include <iostream>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 420042;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite d;
        d.id = {n->system.child(Kind::Planet, i), surfaceActivitySlot};
        d.name = i ? "BUYER COLONY" : "SOURCE CITY";
        d.capacityKg = 100000;
        d.population = 1000;
        d.power = 8;
        d.temperature = 290;
        d.initial.fill(100);
        d.target.fill(100);
        d.initial[size_t(Good::Water)] = i ? 0 : 1000;
        d.systemMetres = {double(i) * 1e10, 0, 0};
        n->sites.push_back(d);
    }
    return n;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.player = p;
    s.tick = e.tick();
    s.routes = economicTradeRoutes(s);
    s.portSignals = economicPortSignals(s);
    return s;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Economic reporting changed money");
    auto initial = e.initialTotal(), delta = e.externalGoodsDelta(), stock = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(initial[g] + delta[g] == stock[g], "Economic reporting changed material accounting");
}
} // namespace
int main() {
    try {
        const auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        const auto money = e.moneyTotal() + p.credits;
        const auto ledger = e.serialize(), wallet = p.serialize();
        auto s = snapshot(e, p);
        check(s.portSignals.size() == n->sites.size(), "Port signals lost site alignment");
        check(s.portSignals[0].lighting == 1 && s.portSignals[1].lighting < .4f,
              "Essential shortages do not affect city lighting");
        check(s.portSignals[1].shortage == Good::Water && s.portSignals[1].missingKg == 100,
              "Port warning did not identify the real missing supply");
        check(s.portSignals[0].report[2].find("ROUTE") != std::string::npos,
              "Affordable profitable trade was hidden from its source port");
        auto broke = s;
        broke.player.credits = 0;
        auto reports = economicPortSignals(broke);
        check(reports[0].report[1].starts_with("REPAIR PARTS READY"),
              "Empty player wallet falsely made port repair stock unavailable");
        auto reserved = s;
        reserved.sites[0].reserved[size_t(Good::Machinery)] =
            reserved.sites[0].stock[size_t(Good::Machinery)];
        check(economicPortSignals(reserved)[0].report[1].starts_with("NO REPAIR PARTS"),
              "Port report offered spare parts committed to freight");
        auto low = s;
        low.sites[0].stock[size_t(Good::Alloy)] = 4;
        check(economicPortSignals(low)[0].report[1].find("UP TO 2 POINTS") != std::string::npos,
              "Limited spare parts were reported as a full repair batch");
        auto unhealthy = s;
        unhealthy.sites[0].health = 400;
        check(economicPortSignals(unhealthy)[0].report[1].find("CREW UNAVAILABLE") != std::string::npos,
              "Unhealthy crew was reported as available");
        for (unsigned i = 0; i < 100; ++i)
            (void)economicPortSignals(s);
        check(e.serialize() == ledger && p.serialize() == wallet,
              "Signal extraction mutated live simulation");
        ActivityBody port;
        port.id = n->sites[1].id;
        port.kind = ActivityKind::SurfacePort;
        port.position = {1, 2, 3};
        port.velocity = {4, 5, 6};
        port.radius = 99;
        port.seed = 123;
        const auto original = port;
        annotateEconomicPort(port, s);
        check(port.position == original.position && port.velocity == original.velocity &&
                  port.seed == original.seed && port.id == original.id && port.radius == original.radius,
              "Port annotation changed physical identity or geometry");
        check(port.economicBrief.starts_with("SHORT WATER") &&
                  hailActivity(port).find("NEEDS 100KG WATER") != std::string::npos,
              "HUD and hail lost the shortage report");
        auto catching = s;
        catching.catchingUp = true;
        annotateEconomicPort(port, catching);
        check(port.economicBrief == "ECONOMY SYNCING" &&
                  port.economicReport[0].find("CATCHING UP") != std::string::npos,
              "Catch-up state presented stale availability as live");
        auto quote = e.quote(0, Good::Water, 100);
        check(e.execute({1, n->sites[0].id, EconomicAction::Buy, Good::Water, 100, quote.buyCents}, p)
                  .starts_with("BOUGHT"),
              "Could not source real supplies for recovery test");
        check(
            e.execute({2, n->sites[1].id, EconomicAction::Sell, Good::Water, 100, 0}, p).starts_with("SOLD"),
            "Could not deliver real supplies for recovery test");
        check(snapshot(e, p).portSignals[1].lighting == 1,
              "Actual delivered supplies did not restore settlement lighting");
        e.outage(n->sites[1].id, 12);
        auto outage = snapshot(e, p);
        check(outage.portSignals[1].power == 0 && outage.portSignals[1].lighting == .06f &&
                  outage.portSignals[1].alert == 1 && outage.portSignals[1].brief == "PLANT OFFLINE",
              "Outage failed to dim lights and warn approaching ships");
        e.advanceTo(12);
        check(snapshot(e, p).portSignals[1].power == 1 && snapshot(e, p).portSignals[1].lighting > .95f,
              "Restored power failed to recover settlement lighting");
        audit(e, p, money);
        Universe universe;
        EconomicFlight f;
        f.id = {n->system.child(Kind::Planet, 0), 100};
        f.source = 0;
        f.destination = 1;
        f.location = 0;
        f.good = Good::Water;
        f.from = {1e9, 0, 0};
        f.to = {1.2e9, 0, 0};
        f.sourceRadius = f.destinationRadius = 1e6;
        f.stellarRadius = 1e6;
        f.startSeconds = 0;
        f.endSeconds = 60;
        ActivityBody transit;
        for (auto phase : {FreightPhase::Idle, FreightPhase::Reposition, FreightPhase::Loading,
                           FreightPhase::Transit, FreightPhase::Holding}) {
            f.phase = phase;
            f.kg = phase == FreightPhase::Transit || phase == FreightPhase::Holding ? 42 : 0;
            f.reservedKg = phase == FreightPhase::Loading || phase == FreightPhase::Reposition ? 42 : 0;
            auto b = sampleEconomicFreight(universe, *n, f, 15);
            if (phase == FreightPhase::Idle) {
                check(b.economicReport[2] == "NO ASSIGNED DESTINATION" &&
                          b.economicBrief.starts_with("EMPTY"),
                      "Idle freight displayed a stale destination or cargo");
            } else {
                auto hail = hailActivity(b);
                check(hail.find(n->sites[0].name) != std::string::npos &&
                          hail.find(n->sites[1].name) != std::string::npos,
                      "Assigned freight lost source/destination names");
                if (phase == FreightPhase::Loading)
                    check(b.economicReport[0].find("RESERVED 42KG WATER / DEPARTS 45S") != std::string::npos,
                          "Loading cargo was not distinguished from cargo already aboard");
                if (phase == FreightPhase::Reposition)
                    check(b.economicBrief.starts_with("EMPTY") &&
                              b.economicReport[2].find("PICKUP") != std::string::npos,
                          "Empty repositioning flight presented loaded goods");
                if (phase == FreightPhase::Holding)
                    check(b.economicReport[0].find("WAREHOUSE FULL") != std::string::npos,
                          "Waiting freighter did not explain its blocked destination");
                if (phase == FreightPhase::Transit) {
                    check(b.economicReport[0].find("ARRIVES 45S") != std::string::npos,
                          "Freight arrival countdown used the wrong clock");
                    transit = b;
                }
            }
            auto renamed = *n;
            renamed.sites[0].name = "OTHER SOURCE";
            renamed.sites[1].name = "OTHER BUYER";
            auto other = sampleEconomicFreight(universe, renamed, f, 15);
            check(b.id == other.id && b.seed == other.seed && b.position == other.position &&
                      b.velocity == other.velocity && b.orientation == other.orientation &&
                      b.tint == other.tint,
                  "Freight manifest changed the established flight path or appearance");
        }
        std::vector<ActivityContact> contacts(80);
        for (size_t i = 0; i < contacts.size(); ++i) {
            contacts[i].body.kind = ActivityKind::Freighter;
            contacts[i].distance = 1000 + i;
            contacts[i].localDirection = glm::normalize(glm::dvec3(.1, .1, -1));
        }
        contacts[45].body = original;
        annotateEconomicPort(contacts[45].body, s);
        contacts[69].body = transit;
        contacts[69].selected = true;
        auto rows = cockpitActivityRows(contacts);
        check(rows.size() == 6 && std::find(rows.begin(), rows.end(), 45) != rows.end() &&
                  std::find(rows.begin(), rows.end(), 69) != rows.end(),
              "Busy traffic list lost its useful port or selected freight");
        for (double aspect : {.6, 1., 1.6, 2.4}) {
            auto h = cockpitOverlay({}, aspect, "", "", {}, contacts);
            check(!h.hitRegions.empty(), "Economic contact has no persistent HUD report");
            const auto card = h.hitRegions.back();
            check(card.x >= 0 && card.x + card.w <= CockpitLayout(aspect).width &&
                      card.y + card.h < CockpitLayout(aspect).navigation.y && h.worldLabels.size() <= 4,
                  "Economic report escaped the fitted cockpit or label budget");
        }
        std::cout << "PASS: " << checks
                  << " economic HUD, power feedback, real supply recovery, freight manifests and "
                     "preservation checks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
