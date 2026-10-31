#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) {
    checks++;
    if (!value)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 54321;
    for (unsigned i = 0; i < 3; i++) {
        EconomicSite d;
        d.id = {n->system.child(Kind::Planet, i), surfaceActivitySlot};
        d.name = "REFIT FIXTURE " + std::to_string(i);
        d.capacityKg = 1000000;
        d.population = 4000;
        d.workers = d.power = 1000;
        d.temperature = 290;
        d.initial.fill(500);
        d.target.fill(100);
        d.reserves.fill(10000000);
        d.systemMetres = {double(i) * 1e10, 0, 0};
        d.recipes = {0};
        d.initial[size_t(Good::Ice)] = 0;
        d.target[size_t(Good::Ice)] = 100000;
        if (i == 1) {
            d.initial[size_t(Good::Electronics)] = 0;
            d.target[size_t(Good::Electronics)] = 1000;
        }
        n->sites.push_back(d);
    }
    return n;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Refits/dividends created or lost money");
    const auto a = e.initialTotal(), b = e.externalGoodsDelta(), c = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; g++)
        check(a[g] + b[g] == c[g], "Construction duplicated or lost capital materials");
}
EconomicCommand command(const Economy& e, uint64_t seq, EconomicAction a, FacilityModule m) {
    EconomicCommand c;
    c.sequence = seq;
    c.port = e.network().sites[0].id;
    c.action = a;
    c.module = m;
    c.limitCents = facilityRefit(m, std::min(3u, e.sites()[0].modules[size_t(m)] + 1)).labourCents;
    return c;
}
std::string legacy(std::string text, size_t sites) {
    for (size_t i = 0; i < sites; i++) {
        text.pop_back();
        text.erase(text.rfind('\n') + 1);
    }
    text.replace(0, 18, "INFINITE_ECONOMY 1 ");
    return text;
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& a, uint64_t tick,
                                            uint64_t seq) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    do {
        auto s = r.snapshot();
        if (s && s->network && s->network->system == a && s->tick >= tick && !s->catchingUp &&
            s->player.lastCommand >= seq)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Development runtime timed out");
}
} // namespace
int main(int argc, char** argv) {
    try {
        Economy e(fixture());
        EconomyPlayer p;
        const auto initialMoney = e.moneyTotal() + p.credits;
        auto c = command(e, 1, EconomicAction::Invest, FacilityModule::Storage);
        check(e.execute(c, p).find("AWAITS MATERIALS") != std::string::npos && e.sites()[0].project,
              "Funding did not create a materials-backed project");
        e.advanceTo(5);
        check(e.sites()[0].project->progress == 0, "Unfunded materials built a warehouse");
        auto before = e.serialize(), wallet = p.serialize();
        e.execute(c, p);
        check(e.serialize() == before && p.serialize() == wallet, "Replayed investment charged twice");
        p.cargo[size_t(Good::Alloy)] = 10;
        c.sequence++;
        check(e.execute(c, p).starts_with("DELIVERED 10 KG") && p.massKg() == 0,
              "Partial material delivery failed");
        audit(e, p, initialMoney);
        auto save = e.serialize();
        check(e.restore(save) && e.serialize() == save, "Unfinished construction did not persist");
        auto cancel = command(e, ++c.sequence, EconomicAction::CancelProject, c.module);
        check(economicPendingPayload(cancel, e.sites()[0]) == 10,
              "Pending project cancellation did not reserve returning payload against shrinking");
        check(e.execute(cancel, p).starts_with("REFIT CANCELLED") && p.massKg() == 10 &&
                  p.credits == 250000 && !e.sites()[0].project,
              "Pre-work cancellation lost refund/cargo");
        audit(e, p, initialMoney);
        for (unsigned m = 0; m < facilityModuleCount; m++) {
            auto r = facilityRefit(FacilityModule(m), 1);
            p.cargo = r.materials;
            auto invest = command(e, ++c.sequence, EconomicAction::Invest, FacilityModule(m));
            check(e.execute(invest, p).find("CONSTRUCTION SCHEDULED") != std::string::npos,
                  "Complete materials failed to schedule refit");
            e.outage(invest.port, e.tick() + 4);
            e.advanceTo(e.tick() + 3);
            check(e.sites()[0].project->progress == 0, "Construction ignored outage");
            e.advanceTo(e.tick() + r.ticks + 2);
            check(!e.sites()[0].project && e.sites()[0].modules[m] == 1,
                  "Refit did not commission after recovery");
            audit(e, p, initialMoney);
            save = e.serialize();
            check(e.restore(save) && e.serialize() == save, "Commissioned refit failed persistence");
        }
        check(economicCapacity(e.network().sites[0], e.sites()[0]) == 1250000,
              "Warehouse refit did not affect effective capacity");
        // Returns require real sales and a protected cash reserve, never a passive money faucet.
        auto incomeNetwork = fixture();
        incomeNetwork->sites[0].population = 0;
        incomeNetwork->sites[0].recipes.clear();
        incomeNetwork->sites[0].initial[size_t(Good::HabitatParts)] = 0;
        incomeNetwork->sites[0].initial[size_t(Good::Instruments)] = 0;
        Economy income(incomeNetwork);
        EconomyPlayer investor;
        investor.cargo = facilityRefit(FacilityModule::Storage, 1).materials;
        auto cash = income.moneyTotal() + investor.credits;
        income.execute(command(income, 1, EconomicAction::Invest, FacilityModule::Storage), investor);
        income.advanceTo(12);
        check(income.sites()[0].dividends == 0, "An unprofitable plant minted returns");
        auto sale = command(income, 2, EconomicAction::Buy, FacilityModule::Storage);
        sale.good = Good::Medicine;
        sale.kg = 80;
        sale.limitCents = income.quote(0, sale.good, sale.kg).buyCents;
        check(income.execute(sale, investor).starts_with("BOUGHT"), "Cash-flow test purchase failed");
        income.advanceTo(24);
        check(income.sites()[0].dividends > 0, "Real sales did not fund a dividend");
        save = income.serialize();
        check(income.restore(save) && income.sites()[0].dividends > 0,
              "Uncollected dividends failed persistence");
        const auto earned = income.sites()[0].dividends, credits = investor.credits;
        auto collect = command(income, 3, EconomicAction::CollectDividends, FacilityModule::Storage);
        check(income.execute(collect, investor).starts_with("COLLECTED") &&
                  investor.credits == credits + earned && income.sites()[0].dividendsPaid == earned,
              "Collection did not transfer the existing dividend account");
        save = income.serialize();
        wallet = investor.serialize();
        income.execute(collect, investor);
        check(income.serialize() == save && investor.serialize() == wallet, "Dividend replay paid twice");
        audit(income, investor, cash);
        check(income.restore(save), "Uncollected/paid dividends failed restore");
        // Work already paid stays with workers; unused escrow and recoverable parts are refunded.
        auto partial = command(e, ++c.sequence, EconomicAction::Invest, FacilityModule::Power);
        p.cargo = facilityRefit(FacilityModule::Power, 2).materials;
        e.execute(partial, p);
        e.advanceTo(e.tick() + 4);
        check(e.sites()[0].project && e.sites()[0].project->progress > 0,
              "Second-level construction failed to progress");
        auto remaining = e.sites()[0].project->escrow;
        auto cashBeforeCancel = p.credits;
        p.cargo[size_t(Good::Ore)] = 4000;
        auto stop = command(e, ++c.sequence, EconomicAction::CancelProject, FacilityModule::Power);
        check(e.execute(stop, p).starts_with("MAKE CARGO SPACE") && e.sites()[0].project,
              "Cancellation discarded cargo when the hold was full");
        p.cargo.fill(0);
        stop.sequence = ++c.sequence;
        check(e.execute(stop, p).starts_with("REFIT CANCELLED") && p.credits == cashBeforeCancel + remaining,
              "Partial construction refunded spent labour");
        audit(e, p, initialMoney);
        save = e.serialize();
        auto bad = save;
        size_t tail = bad.size() - 1;
        for (size_t i = 0; i < e.sites().size(); i++)
            tail = bad.rfind('\n', tail - 1);
        bad[tail + 1] = '4';
        check(!e.restore(bad) && e.serialize() == save, "Invalid refit level mutated the live ledger");
        Economy batched(fixture());
        check(batched.restore(save), "Deterministic construction fixture failed restore");
        const auto goal = e.tick() + 50;
        while (e.tick() < goal)
            e.advanceTo(e.tick() + 1);
        batched.advanceTo(goal, 50);
        check(e.serialize() == batched.serialize(), "Refit history depends on frame batching");
        audit(e, p, initialMoney);
        // Actual production impact: retain the same recipes, geology and population.
        for (auto m : {FacilityModule::Power, FacilityModule::Automation}) {
            auto n = fixture();
            if (m == FacilityModule::Power)
                n->sites[0].power = 7;
            Economy upgraded(n), base(n);
            EconomyPlayer owner;
            owner.cargo = facilityRefit(m, 1).materials;
            upgraded.execute(command(upgraded, 1, EconomicAction::Invest, m), owner);
            upgraded.advanceTo(12);
            base.advanceTo(12);
            const auto u0 = upgraded.sites()[0].extracted[size_t(Good::Ice)];
            const auto b0 = base.sites()[0].extracted[size_t(Good::Ice)];
            upgraded.advanceTo(13);
            base.advanceTo(13);
            check(upgraded.sites()[0].extracted[size_t(Good::Ice)] - u0 >
                      base.sites()[0].extracted[size_t(Good::Ice)] - b0,
                  "Utility/automation commissioning did not improve real production");
        }
        // Version-one ledgers migrate without changing stock, money, history or wallet.
        Economy old(fixture());
        old.advanceTo(20);
        auto v1 = legacy(old.serialize(), old.sites().size());
        Economy migrated(fixture());
        check(migrated.restore(v1) && migrated.inventoryTotal() == old.inventoryTotal() &&
                  migrated.moneyTotal() == old.moneyTotal() && migrated.tick() == old.tick(),
              "Legacy economy migration lost existing state");
        // Expanded warehouse space is used by trading and survives reload above the old limit.
        auto warehouse = fixture();
        warehouse->sites[0].orbital = true;
        warehouse->sites[0].population = 0;
        warehouse->sites[0].initial.fill(0);
        warehouse->sites[0].initial[size_t(Good::Ore)] = 100;
        warehouse->sites[0].capacityKg = 100;
        Economy depot(warehouse);
        EconomyPlayer owner;
        owner.cargo = facilityRefit(FacilityModule::Storage, 1).materials;
        auto depotMoney = depot.moneyTotal() + owner.credits;
        depot.execute(command(depot, 1, EconomicAction::Invest, FacilityModule::Storage), owner);
        depot.advanceTo(12);
        owner.cargo[size_t(Good::Ore)] = 25;
        auto expandSale = command(depot, 2, EconomicAction::Sell, FacilityModule::Storage);
        expandSale.good = Good::Ore;
        expandSale.kg = 25;
        expandSale.limitCents = 0;
        check(depot.execute(expandSale, owner).starts_with("SOLD") &&
                  depot.sites()[0].stock[size_t(Good::Ore)] == 125,
              "Trade ignored the commissioned warehouse capacity");
        save = depot.serialize();
        check(depot.restore(save), "Expanded inventory was rejected against the old warehouse limit");
        audit(depot, owner, depotMoney);
        for (unsigned level : {2u, 3u}) {
            owner.cargo = facilityRefit(FacilityModule::Storage, level).materials;
            depot.execute(command(depot, level + 1, EconomicAction::Invest, FacilityModule::Storage), owner);
            depot.advanceTo(depot.tick() + facilityRefit(FacilityModule::Storage, level).ticks);
            check(depot.sites()[0].modules[0] == level, "Higher-level warehouse failed to commission");
            audit(depot, owner, depotMoney);
        }
        check(depot.execute(command(depot, 5, EconomicAction::Invest, FacilityModule::Storage), owner) ==
                  "REFIT AT MAXIMUM LEVEL",
              "Facility refits exceeded the bounded level cap");
        // A hot, sealed plant cannot construct using power it does not actually have.
        auto extreme = fixture();
        extreme->sites[0].power = 2;
        extreme->sites[0].temperature = 2000;
        extreme->sites[0].sealed = true;
        Economy furnace(extreme);
        EconomyPlayer builder;
        builder.cargo = facilityRefit(FacilityModule::Power, 1).materials;
        furnace.execute(command(furnace, 1, EconomicAction::Invest, FacilityModule::Power), builder);
        furnace.advanceTo(5);
        check(furnace.sites()[0].project->progress == 0 &&
                  economicPower(extreme->sites[0], furnace.sites()[0], 5) == 0,
              "Construction bypassed inherited environmental power constraints");
        EconomicSiteState paused;
        paused.project = FacilityProject{};
        paused.project->delivered = facilityRefit(FacilityModule::Storage, 1).materials;
        paused.health = 400;
        check(!economicConstructionActive(fixture()->sites[0], paused, 1),
              "Paused construction would reserve production resources from an unhealthy settlement");
        // A bounded shortlist uses executable bulk quotes, available funds and both cargo limits.
        EconomySnapshot s;
        s.network = fixture();
        s.sites = Economy(s.network).sites();
        auto start = std::chrono::steady_clock::now();
        s.routes = economicTradeRoutes(s);
        const auto routeMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        check(!s.routes.empty() && s.routes.size() <= 12, "No bounded profitable trade intelligence");
        for (const auto& r : s.routes) {
            check(r.source != r.destination && r.kg > 0 && r.kg <= 1000 &&
                      r.costCents == s.quote(r.source, r.good, r.kg).buyCents &&
                      r.revenueCents == s.quote(r.destination, r.good, r.kg).sellCents &&
                      r.revenueCents > r.costCents && r.costCents <= s.player.credits &&
                      r.revenueCents <= s.sites[r.destination].cash,
                  "Route overestimated profitable or affordable cargo");
        }
        const auto route = s.routes.front();
        Economy execution(s.network);
        EconomyPlayer merchant;
        const auto routeMoney = execution.moneyTotal() + merchant.credits;
        const auto cashBeforeRoute = merchant.credits;
        check(execution
                  .execute({1, s.network->sites[route.source].id, EconomicAction::Buy, route.good, route.kg,
                            route.costCents},
                           merchant)
                  .starts_with("BOUGHT"),
              "Recommended route could not be purchased at its quoted limit");
        check(execution
                      .execute({2, s.network->sites[route.destination].id, EconomicAction::Sell, route.good,
                                route.kg, route.revenueCents},
                               merchant)
                      .starts_with("SOLD") &&
                  merchant.credits == cashBeforeRoute + route.revenueCents - route.costCents &&
                  merchant.massKg() == 0,
              "Recommended route failed to realize its displayed gross margin");
        audit(execution, merchant, routeMoney);
        s.player.cargo[size_t(Good::Ore)] = 4000;
        check(economicTradeRoutes(s).empty(), "Full hold generated another cargo purchase route");
        s.player.cargo.fill(0);
        s.player.credits = 0;
        check(economicTradeRoutes(s).empty(), "Empty wallet generated a financed route");
        s.player = {};
        s.sites = e.sites();
        for (auto page : {MarketPage::Goods, MarketPage::Routes, MarketPage::Industry}) {
            auto view = economicMarketView(&s, s.network->sites[0].id, 0, 10, true, page);
            for (double aspect : {.6, 1., 1.6, 2.4}) {
                auto hud = cockpitOverlay({}, aspect, "", {}, {}, {}, false, 1, &view);
                auto rect = hud.hitRegions.back();
                check(!hud.lettering.vertices.empty() && rect.x >= 0 && rect.y >= 160 &&
                          rect.x + rect.w <= CockpitLayout(aspect).width &&
                          rect.y + rect.h < CockpitLayout(aspect).navigation.y,
                      "Economic page escaped fitted cockpit bounds");
            }
        }
        // Worker-backed materials, project, partial work, eviction and restart.
        Universe u;
        auto dir = std::filesystem::path(argc > 1 ? argv[1] : "build/economy/development-test");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        auto address = fixture()->system;
        Stock retainedCargo;
        int64_t retainedCredits;
        {
            EconomyRuntime runtime(u, dir);
            runtime.request(address, 0);
            auto snapshot = wait(runtime, address, 0, 0);
            auto id = snapshot->network->sites[0].id;
            auto r = facilityRefit(FacilityModule::Storage, 1);
            uint64_t seq = 0;
            for (size_t g = 0; g < goodCount; g++)
                if (r.materials[g]) {
                    auto q = snapshot->quote(0, Good(g), r.materials[g]);
                    runtime.command({++seq, id, EconomicAction::Buy, Good(g), r.materials[g], q.buyCents});
                    snapshot = wait(runtime, address, 0, seq);
                    check(snapshot->player.cargo[g] == r.materials[g], "Refit supply purchase failed");
                }
            EconomicCommand fund{++seq, id, EconomicAction::Invest};
            fund.limitCents = r.labourCents;
            runtime.command(fund);
            snapshot = wait(runtime, address, 0, seq);
            check(snapshot->sites[0].project && snapshot->player.massKg() == 0,
                  "Worker did not atomically commit the refit and cargo");
            runtime.request(address, 20);
            snapshot = wait(runtime, address, 4, seq);
            check(snapshot->sites[0].project->progress == 4, "Worker failed to build commissioned materials");
            std::vector<double> searchSamples;
            for (unsigned k = 0; k < 9; k++) {
                auto searchStart = std::chrono::steady_clock::now();
                auto routes = economicTradeRoutes(*snapshot);
                searchSamples.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - searchStart)
                        .count());
                check(routes.size() <= 12, "Real-network route search exceeded its bounded output");
            }
            std::sort(searchSamples.begin(), searchSamples.end());
            std::cout << "network_trade_search_median_ms=" << searchSamples[4]
                      << " max_ms=" << searchSamples.back() << '\n';
            retainedCredits = snapshot->player.credits;
            retainedCargo = snapshot->player.cargo;
            for (unsigned star : {0u, 1u, 2u}) {
                auto other = Address{}.child(Kind::Galaxy, 0).child(Kind::System, star);
                runtime.request(other, 20);
                wait(runtime, other, 4, seq);
            }
            runtime.request(address, 20);
            snapshot = wait(runtime, address, 4, seq);
            check(snapshot->sites[0].project && snapshot->sites[0].project->progress == 4,
                  "Eviction reset the facility project");
            runtime.flush();
        }
        {
            EconomyRuntime runtime(u, dir);
            runtime.request(address, 60);
            auto snapshot = wait(runtime, address, 12, 5);
            check(!snapshot->sites[0].project && snapshot->sites[0].modules[0] == 1 &&
                      snapshot->player.credits == retainedCredits && snapshot->player.cargo == retainedCargo,
                  "Restart lost or duplicated construction, wallet or cargo");
            check(runtime.residentBytes() < 4 * 1024 * 1024,
                  "Development exceeded the existing cache budget");
            std::cout << "worker_bytes=" << runtime.residentBytes() << " routes=" << snapshot->routes.size()
                      << '\n';
        }
        std::cout << "fixture_trade_search_ms=" << routeMs << '\n';
        std::cout << "PASS: " << checks
                  << " investment, capital conservation, construction, dividends, migration, trade and "
                     "persistence checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
