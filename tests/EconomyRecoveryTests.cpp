#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> fixture(bool supplies = false) {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 430043;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite d;
        d.id = {n->system.child(Kind::Planet, i), 0};
        d.name = i ? "RECOVERY COLONY" : "SUPPLY PORT";
        d.orbital = !i;
        d.population = i ? 6000 : 0;
        d.sealed = i;
        d.workers = d.power = 20;
        d.capacityKg = 2000000;
        d.systemMetres = {double(i) * 1e10, 0, 0};
        if (!i)
            d.initial.fill(50000);
        else {
            d.initial[size_t(Good::Alloy)] = 100;
            d.initial[size_t(Good::Tools)] = 20;
            if (supplies)
                for (auto g : {Good::Food, Good::Water, Good::Oxygen})
                    d.initial[size_t(g)] = 10000;
            d.recipes = {1};
            for (auto g : {Good::Ore, Good::Silicate, Good::Carbon})
                d.target[size_t(g)] = 1000;
            d.reserves.fill(1000000);
        }
        n->sites.push_back(d);
    }
    return n;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.contracts = e.contracts();
    s.hauls = e.hauls();
    s.recoveries = e.recoveries();
    s.outcomes = e.outcomes();
    s.player = p;
    s.tick = e.tick();
    s.jobs = economicJobs(s);
    s.portSignals = economicPortSignals(s);
    return s;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Recovery lost or minted money");
    const auto a = e.initialTotal(), d = e.externalGoodsDelta(), s = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(a[g] + d[g] == s[g], "Recovery lost materials");
    for (const auto& s : e.sites())
        for (size_t g = 0; g < goodCount; ++g)
            check(s.reserved[g] >= 0 && s.reserved[g] <= s.stock[g], "Unbacked recovery kit");
    Economy loaded(std::make_shared<EconomicNetwork>(e.network()));
    check(loaded.restore(e.serialize()) && loaded.serialize() == e.serialize(),
          "Recovery save did not round trip");
}
std::string order(Economy& e, EconomyPlayer& p, EconomicAction action, uint64_t id = 0, unsigned site = 1) {
    EconomicCommand c;
    c.sequence = p.lastCommand + 1;
    c.port = e.network().sites[site].id;
    c.action = action;
    c.contract = id;
    return e.execute(c, p);
}
void source(Economy& e, EconomyPlayer& p, const Stock& goods) {
    for (size_t g = 0; g < goodCount; ++g)
        if (goods[g]) {
            EconomicCommand c{p.lastCommand + 1, e.network().sites[0].id, EconomicAction::Buy, Good(g),
                              goods[g]};
            c.limitCents = e.quote(0, Good(g), c.kg).buyCents;
            check(e.execute(c, p).starts_with("BOUGHT"), "Recovery supplies could not be bought");
        }
}
void damage(Economy& e, unsigned condition, unsigned health) {
    auto text = e.serialize();
    const auto first = text.find('\n'), second = text.find('\n', first + 1),
               third = text.find('\n', second + 1);
    auto line = text.substr(second + 1, third - second - 1);
    std::istringstream in(line);
    int64_t cash, households;
    unsigned oldCondition, oldHealth;
    in >> cash >> households >> oldCondition >> oldHealth;
    std::string tail;
    std::getline(in, tail);
    text.replace(second + 1, third - second - 1,
                 std::to_string(cash) + " " + std::to_string(households) + " " + std::to_string(condition) +
                     " " + std::to_string(health) + tail);
    check(e.restore(text), "Controlled damaged-plant fixture invalid");
}
} // namespace
int main() {
    try {
        auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        p.credits = 10000000;
        const auto money = e.moneyTotal() + p.credits;
        e.advanceTo(12);
        check(e.recoveries().size() == 1 && e.recoveries()[0].kind == RecoveryKind::Relief,
              "Actual shortage did not post relief");
        const auto relief = e.recoveries()[0];
        check(relief.required[size_t(Good::Oxygen)] > 0 && relief.escrow > 0,
              "Sealed colony relief omitted oxygen or funding");
        auto s = snapshot(e, p);
        check(s.jobs[0].recovery && economicPlanJob(s, s.jobs[0], {}).procurement,
              "Recovery bundle missing from jobs/planning");
        check(order(e, p, EconomicAction::AcceptRecovery, relief.id).starts_with("RECOVERY ACCEPTED"),
              "Remote recovery acceptance failed");
        check(
            order(e, p, EconomicAction::DeliverRecovery, relief.id, 0).starts_with("RECOVERY JOB NOT FOUND"),
            "Wrong port accepted a kit");
        Stock partial{};
        partial[size_t(Good::Food)] = 1;
        source(e, p, partial);
        check(order(e, p, EconomicAction::DeliverRecovery, relief.id).starts_with("RECOVERY DELIVERED"),
              "Partial relief failed");
        check(e.recoveries()[0].phase == RecoveryPhase::Accepted && e.outcomes().back().delivered == partial,
              "Partial relief completed entire job");
        auto remaining = relief.required;
        --remaining[size_t(Good::Food)];
        source(e, p, remaining);
        const auto health = e.sites()[1].health;
        check(order(e, p, EconomicAction::DeliverRecovery, relief.id).starts_with("RECOVERY DELIVERED"),
              "Relief completion failed");
        check(e.recoveries()[0].phase == RecoveryPhase::Complete && e.sites()[1].health == health,
              "Delivery instantly healed households");
        audit(e, p, money);
        const auto ledger = e.serialize(), wallet = p.serialize();
        EconomicCommand replay{p.lastCommand, n->sites[1].id, EconomicAction::DeliverRecovery};
        replay.contract = relief.id;
        e.execute(replay, p);
        check(e.serialize() == ledger && p.serialize() == wallet, "Replay duplicated relief payment/receipt");
        e.advanceTo(24);
        check(e.sites()[1].health > health && e.outcomes().back().after.health > health,
              "Report did not observe actual household recovery");
        audit(e, p, money);
        auto m = fixture(true);
        Economy repair(m);
        EconomyPlayer owner;
        owner.credits = 10000000;
        damage(repair, 400, 450);
        repair.outage(m->sites[1].id, 200);
        const auto repairMoney = repair.moneyTotal() + owner.credits;
        repair.advanceTo(12);
        auto job = *std::find_if(repair.recoveries().begin(), repair.recoveries().end(),
                                 [](const auto& j) { return j.kind == RecoveryKind::Maintenance; });
        check(job.required[size_t(Good::Machinery)] == job.pairs + 2, "Maintenance kit lacks service spares");
        order(repair, owner, EconomicAction::AcceptRecovery, job.id);
        source(repair, owner, job.required);
        check(order(repair, owner, EconomicAction::DeliverRecovery, job.id).starts_with("RECOVERY DELIVERED"),
              "Maintenance delivery failed");
        check(repair.recoveries()[0].phase == RecoveryPhase::Working && repair.outcomes().back().working,
              "Kit did not start crew work");
        check(repair.sites()[1].reserved[size_t(Good::Filters)] == job.pairs,
              "Other consumers can spend the maintenance kit");
        audit(repair, owner, repairMoney);
        repair.advanceTo(24);
        check(repair.recoveries()[0].progress == 0, "Unhealthy crew performed repairs");
        audit(repair, owner, repairMoney);
        while (repair.tick() < 80)
            repair.advanceTo(80);
        check(repair.recoveries()[0].phase == RecoveryPhase::Complete && !repair.outcomes().back().working &&
                  repair.outcomes().back().outageCleared &&
                  repair.outcomes().back().crewPaid == int64_t(job.pairs) * 100,
              "Crew repair did not restore power or pay real wages");
        check(repair.sites()[1].condition >= 750 && repair.sites()[1].reserved[size_t(Good::Machinery)] == 0,
              "Repair did not restore condition/release spares");
        check(repair.outcomes().back().after.power > 0 &&
                  repair.outcomes().back().after.outputKg > repair.outcomes().back().before.outputKg,
              "Report did not show actual power/output recovery");
        audit(repair, owner, repairMoney);
        // A later unrelated outage is not cleared by an older repair job.
        Economy interrupted(m);
        EconomyPlayer ip;
        ip.credits = 10000000;
        interrupted.outage(m->sites[1].id, 200);
        interrupted.advanceTo(12);
        auto ij = interrupted.recoveries()[0];
        order(interrupted, ip, EconomicAction::AcceptRecovery, ij.id);
        source(interrupted, ip, ij.required);
        order(interrupted, ip, EconomicAction::DeliverRecovery, ij.id);
        Stock turnover{};
        turnover[size_t(Good::Alloy)] = 20;
        source(interrupted, ip, turnover);
        const auto kitReport = interrupted.recoveries()[0].outcome;
        for (unsigned k = 0; k < 20; ++k) {
            EconomicCommand sale{ip.lastCommand + 1, m->sites[1].id, EconomicAction::Sell, Good::Alloy, 1};
            check(interrupted.execute(sale, ip).starts_with("SOLD"), "Turnover sale failed");
        }
        check(interrupted.outcomes().size() == 16 &&
                  std::any_of(interrupted.outcomes().begin(), interrupted.outcomes().end(),
                              [&](const auto& o) { return o.id == kitReport && o.working; }),
              "History eviction discarded an active crew report");
        Economy pinned(m);
        check(pinned.restore(interrupted.serialize()), "Pinned crew report failed reload");
        interrupted.outage(m->sites[1].id, 300);
        interrupted.advanceTo(24);
        check(interrupted.sites()[1].outageUntil == 300 &&
                  !std::find_if(interrupted.outcomes().begin(), interrupted.outcomes().end(),
                                [&](const auto& o) { return o.id == kitReport; })
                       ->outageCleared,
              "Repair cleared a newer outage");
        // Incomplete paid kit expires without losing its goods, refunding only unpaid escrow.
        Economy expired(m);
        EconomyPlayer ep;
        ep.credits = 10000000;
        expired.outage(m->sites[1].id, 200);
        expired.advanceTo(12);
        auto ej = expired.recoveries()[0];
        const auto expiredMoney = expired.moneyTotal() + ep.credits;
        order(expired, ep, EconomicAction::AcceptRecovery, ej.id);
        partial.fill(0);
        partial[size_t(Good::Machinery)] = 1;
        source(expired, ep, partial);
        order(expired, ep, EconomicAction::DeliverRecovery, ej.id);
        while (expired.tick() <= ej.deadline)
            expired.advanceTo(ej.deadline + 1);
        check(expired.recoveries()[0].phase == RecoveryPhase::Expired &&
                  expired.recoveries()[0].escrow == 0 &&
                  expired.sites()[1].reserved[size_t(Good::Machinery)] == 0,
              "Partial kit expiry lost refund/reservation");
        audit(expired, ep, expiredMoney);
        // Construction and recovery must share the settlement's finite workforce.
        auto small = fixture(true);
        small->sites[1].workers = 2;
        Economy sharedCrew(small);
        EconomyPlayer builder;
        builder.credits = 10000000;
        damage(sharedCrew, 290, 1000);
        const auto crewMoney = sharedCrew.moneyTotal() + builder.credits;
        sharedCrew.advanceTo(12);
        const auto cj = sharedCrew.recoveries()[0];
        order(sharedCrew, builder, EconomicAction::AcceptRecovery, cj.id);
        source(sharedCrew, builder, cj.required);
        order(sharedCrew, builder, EconomicAction::DeliverRecovery, cj.id);
        auto refit = facilityRefit(FacilityModule::Storage, 1);
        source(sharedCrew, builder, refit.materials);
        EconomicCommand build;
        build.port = small->sites[1].id;
        build.action = EconomicAction::Invest;
        build.limitCents = refit.labourCents;
        build.sequence = builder.lastCommand + 1;
        check(sharedCrew.execute(build, builder).starts_with("REFIT SUPPLIED"),
              "Shared-crew project did not fund");
        build.sequence++;
        sharedCrew.execute(build, builder);
        check(sharedCrew.sites()[1].project && sharedCrew.sites()[1].project->delivered == refit.materials,
              "Shared-crew project did not receive materials");
        sharedCrew.advanceTo(13);
        check(sharedCrew.recoveries()[0].progress == 1 && sharedCrew.sites()[1].project->progress == 0,
              "Plant restart double-booked recovery workers for construction");
        sharedCrew.advanceTo(14);
        check(sharedCrew.recoveries()[0].progress == 1 && sharedCrew.sites()[1].project->progress == 1,
              "Recovery and construction double-booked two workers");
        while (sharedCrew.sites()[1].project && sharedCrew.tick() < 50) {
            const auto repairBefore = sharedCrew.recoveries()[0].progress;
            const auto buildBefore = sharedCrew.sites()[1].project->progress;
            sharedCrew.advanceTo(sharedCrew.tick() + 1);
            const auto repairDelta = sharedCrew.recoveries()[0].progress - repairBefore;
            const auto buildAfter =
                sharedCrew.sites()[1].project ? sharedCrew.sites()[1].project->progress : refit.ticks;
            check(repairDelta + buildAfter - buildBefore <= 1,
                  "Periodic condition changes double-booked the construction and repair crew");
        }
        check(!sharedCrew.sites()[1].project, "Shared crew did not finish construction");
        const auto completedProgress = sharedCrew.recoveries()[0].progress;
        sharedCrew.advanceTo(sharedCrew.tick() + 1);
        check(sharedCrew.recoveries()[0].progress == completedProgress + 1,
              "Recovery did not resume when construction released workers");
        audit(sharedCrew, builder, crewMoney);
        // Generic sales retain reports too; bounded history survives reload and corrupt inputs fail
        // transactionally.
        Stock sale{};
        sale[size_t(Good::Alloy)] = 20;
        source(repair, owner, sale);
        for (unsigned k = 0; k < 20; ++k) {
            EconomicCommand c{owner.lastCommand + 1, m->sites[1].id, EconomicAction::Sell, Good::Alloy, 1};
            check(repair.execute(c, owner).starts_with("SOLD"), "Ordinary delivery failed");
        }
        check(repair.outcomes().size() == 16, "Delivery history grew without bound");
        audit(repair, owner, repairMoney);
        auto bad = repair.serialize();
        const auto pos = bad.find("OUTCOMES ");
        bad.replace(pos, 8, "CORRUPT!");
        const auto live = repair.serialize();
        check(!repair.restore(bad) && repair.serialize() == live, "Corrupt report save mutated state");
        s = snapshot(repair, owner);
        for (size_t k = 0; k < s.outcomes.size(); ++k) {
            auto v = economicMarketView(&s, {}, k, 10, false, MarketPage::Outcomes);
            check(v.ready && v.detail.size() <= 11 && v.detail[1].starts_with("PAID "),
                  "Outcome view omitted payment or overflowed rows");
            for (auto size : {glm::ivec2(1280, 800), glm::ivec2(640, 900)}) {
                const auto h = cockpitOverlay(FlightInstruments{}, double(size.x) / size.y, "", {}, {}, {},
                                              false, 1, &v);
                check(!h.lettering.vertices.empty(), "Outcome cockpit did not render");
            }
        }
        // A v4 ledger restores with no synthetic jobs or reports.
        auto old = Economy(m).serialize();
        old.erase(old.find("RECOVERY "));
        old.replace(17, 1, "4");
        Economy migrated(m);
        check(migrated.restore(old) && migrated.outcomes().empty() && migrated.recoveries().empty(),
              "v4 ledger migration failed");
        std::cout << checks << " recovery/outcome checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
