#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include "infinite/Piloting.hpp"
#include <chrono>
#include <iostream>
#include <numeric>
#include <sstream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    checks++;
    if (!b)
        throw std::runtime_error(why);
}
int64_t sum(const Stock& s) {
    return std::accumulate(s.begin(), s.end(), int64_t(0));
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 123456;
    n->universeSeed = 0x494e46494e495445ULL;
    auto planet = n->system.child(Kind::Planet, 0);
    for (unsigned i = 0; i < 4; i++) {
        EconomicSite d;
        d.id = {planet, i == 3 ? 0u : surfaceActivitySlot + i};
        d.name = "ECONOMIC FIXTURE " + std::to_string(i);
        d.capacityKg = 1000000;
        d.workers = 1000;
        d.power = 1000;
        d.population = i == 3 ? 0 : 1000;
        d.seed = i + 1;
        d.orbital = i == 3;
        d.systemMetres = {double(i) * 10000, 100, 0};
        d.metres = {0, 10000000 + double(i) * 1000, 0};
        d.initial.fill(400);
        d.target.fill(100);
        d.reserves.fill(10000000);
        if (i == 0) {
            d.recipes = {0, 1, 4};
            d.initial[size_t(Good::Ore)] = 3000;
        }
        if (i == 1) {
            d.recipes = {2, 3, 8, 9};
            d.initial[size_t(Good::Food)] = 3000;
        }
        if (i == 2)
            d.recipes = {5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17};
        if (i == 3) {
            d.initial[size_t(Good::Food)] = 0;
            d.target[size_t(Good::Food)] = 1000;
        }
        n->sites.push_back(d);
    }
    for (unsigned i = 0; i < 8; i++)
        n->vessels.push_back({planet, 100 + i});
    return n;
}
void audit(const Economy& e, int64_t money) {
    check(e.moneyTotal() == money, "Money was created/lost by trade, wages, shipping or escrow");
    auto inventory = e.inventoryTotal(), initial = e.initialTotal(), delta = e.externalGoodsDelta();
    for (size_t g = 0; g < goodCount; g++)
        check(inventory[g] == initial[g] + delta[g],
              "Cargo duplicated or vanished across reservations, production or delivery");
    for (const auto& s : e.sites())
        for (size_t g = 0; g < goodCount; g++)
            check(s.stock[g] >= s.reserved[g] && s.reserved[g] >= 0, "Invalid inventory reservation");
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& a, uint64_t tick,
                                            uint64_t command = 0) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        auto s = r.snapshot();
        if (s && s->network && s->network->system == a && s->tick >= tick &&
            s->player.lastCommand >= command && !s->catchingUp)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    auto s = r.snapshot();
    throw std::runtime_error("Economic worker failed to publish: " + (s ? s->message : "no snapshot"));
}
} // namespace
int main(int argc, char** argv) {
    try {
        for (const auto& r : productionRecipes())
            if (sum(r.inputs))
                check(sum(r.inputs) == sum(r.outputs) + r.wasteKg,
                      "Manufacturing recipe creates physical mass");
        Economy a(fixture()), b(fixture());
        const auto money = a.moneyTotal();
        for (unsigned t = 1; t <= 600; t++) {
            a.advanceTo(t);
            audit(a, money);
        }
        b.advanceTo(600, 600);
        check(a.serialize() == b.serialize(), "Economic history depends on frame/tick batching");
        check(std::any_of(a.freight().begin(), a.freight().end(), [](const auto& f) { return f.trips > 0; }),
              "Real surplus/demand generated no completed freight");
        const auto saved = a.serialize();
        check(a.restore(saved) && a.serialize() == saved, "Active freight/escrow persistence changed state");
        check(!a.restore("INFINITE_ECONOMY 1 broken") && a.serialize() == saved,
              "Failed restore mutated live economy");
        Economy market(fixture());
        EconomyPlayer player;
        const auto fullMoney = market.moneyTotal() + player.credits;
        auto q = market.quote(0, Good::Ore, 100);
        auto port = market.network().sites[0].id;
        EconomicCommand c{1, port, EconomicAction::Buy, Good::Ore, 100, q.buyCents, 0};
        check(market.execute(c, player).starts_with("BOUGHT") && player.massKg() == 100,
              "Player could not buy real inventory");
        auto once = market.serialize(), wallet = player.serialize();
        market.execute(c, player);
        check(market.serialize() == once && player.serialize() == wallet,
              "Replayed order duplicated cargo/payment");
        check(market.moneyTotal() + player.credits == fullMoney,
              "Player purchase broke financial conservation");
        auto beforeCredits = player.credits;
        c.sequence = 2;
        c.action = EconomicAction::Sell;
        c.limitCents = market.quote(0, Good::Ore, 100).sellCents;
        check(market.execute(c, player).starts_with("SOLD") && player.massKg() == 0, "Player sale failed");
        check(player.credits < 250000 && player.credits > beforeCredits,
              "Immediate buy/resell generated money or failed payment");
        check(market.moneyTotal() + player.credits == fullMoney, "Player sale broke financial conservation");
        c.sequence = 3;
        c.action = EconomicAction::Buy;
        c.limitCents = 0;
        once = market.serialize();
        market.execute(c, player);
        check(market.serialize() == once, "Stale quote executed above its price limit");
        auto contractFixture = fixture();
        contractFixture->vessels.clear();
        Economy contracts(contractFixture);
        contracts.advanceTo(12, 12);
        auto offered = std::find_if(contracts.contracts().begin(), contracts.contracts().end(),
                                    [](const auto& c) { return c.site == 3; });
        check(offered != contracts.contracts().end() && offered->escrow > 0,
              "Real shortage did not generate funded contract");
        EconomyPlayer courier;
        courier.cargo[size_t(offered->good)] = offered->requested;
        auto contractMoney = contracts.moneyTotal() + courier.credits;
        EconomicCommand delivery{1,
                                 contracts.network().sites[3].id,
                                 EconomicAction::Contract,
                                 offered->good,
                                 offered->requested,
                                 0,
                                 offered->id};
        check(contracts.execute(delivery, courier).starts_with("CONTRACT ACCEPTED"),
              "Contract acceptance failed");
        EconomySnapshot mission;
        mission.network = contractFixture;
        mission.contracts = contracts.contracts();
        check(mission.contractDestination() == contractFixture->sites[3].id,
              "Accepted contract has no persistent destination target");
        delivery.sequence = 2;
        delivery.kg = std::max<int64_t>(1, offered->requested / 2);
        check(contracts.execute(delivery, courier).starts_with("DELIVERED") && courier.massKg() > 0,
              "Partial delivery did not preserve remaining cargo and escrow");
        delivery.sequence = 3;
        delivery.kg = offered->requested;
        check(contracts.execute(delivery, courier).starts_with("DELIVERED") && courier.massKg() == 0,
              "Escrow-funded delivery failed");
        check(contracts.moneyTotal() + courier.credits == contractMoney, "Contract reward created credits");
        auto expiryMoney = contracts.moneyTotal();
        contracts.advanceTo(200, 200);
        audit(contracts, expiryMoney);
        Economy expiring(contractFixture);
        const auto expiryTotal = expiring.moneyTotal();
        expiring.advanceTo(157, 157);
        check(
            expiring.moneyTotal() == expiryTotal &&
                std::any_of(expiring.contracts().begin(), expiring.contracts().end(),
                            [](const auto& c) { return c.phase == ContractPhase::Expired && c.escrow == 0; }),
            "Expired contract did not refund funded escrow");
        auto walletSave = player.serialize();
        check(player.restore(walletSave) && player.serialize() == walletSave && !player.restore("corrupt"),
              "Wallet persistence failed");
        Universe u;
        Navigation nav(u);
        RegionCache cache(u);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        pilot.setCargoMass(500);
        pilot.toggleShrink();
        check(!pilot.ship().shrinking, "Bulk cargo could be shrunk");
        pilot.adjustSize(-3);
        check(!pilot.ship().shrinking, "Manual size adjustment bypassed cargo rule");
        const auto empty = shipProperties(), loaded = shipProperties(1, 0, 4000);
        check(loaded.massKg == 16000 && loaded.forceCaps == empty.forceCaps &&
                  loaded.inertia.x > empty.inertia.x,
              "Cargo did not increase mass/inertia at fixed thrust");
        auto start = std::chrono::steady_clock::now();
        auto address = fixture()->system;
        const auto before = descriptorFingerprint(u.describe(address));
        auto network = generateEconomicNetwork(u, address);
        check(network && network->sites.size() > 10, "Universe adapter produced no actual ports");
        check(descriptorFingerprint(u.describe(address)) == before, "Economy extraction altered generation");
        check(std::all_of(network->sites.begin(), network->sites.end(),
                          [&](const auto& s) { return s.orbital || hasSolidSurface(u.describe(s.id.home)); }),
              "Surface economy occupied a gas giant");
        Economy real(network);
        auto realMoney = real.moneyTotal();
        real.advanceTo(100, 100);
        audit(real, realMoney);
        auto realSave = real.serialize();
        check(real.restore(realSave), "Real network state failed persistence");
        auto adapterMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        auto simulationStart = std::chrono::steady_clock::now();
        real.advanceTo(1100, 1000);
        const auto thousandTickMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - simulationStart)
                .count();
        audit(real, realMoney);
        auto outageFixture = fixture();
        outageFixture->sites[1].initial[size_t(Good::Food)] = 50;
        Economy healthy(outageFixture), disrupted(outageFixture);
        disrupted.outage(disrupted.network().sites[1].id, 30);
        healthy.advanceTo(20, 20);
        disrupted.advanceTo(20, 20);
        check(healthy.sites()[1].produced[size_t(Good::Food)] >
                  disrupted.sites()[1].produced[size_t(Good::Food)],
              "Facility outage did not interrupt downstream production");
        disrupted.advanceTo(40, 40);
        check(disrupted.sites()[1].produced[size_t(Good::Food)] > 0,
              "Production failed to resume after outage");
        // The same ledger persists outside GPU/geometry cache residency and process lifetime.
        auto dir = std::filesystem::path(argc > 1 ? argv[1] : "build/economy/runtime-test");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        int64_t retainedCredits;
        Stock retainedCargo;
        {
            EconomyRuntime runtime(u, dir);
            runtime.request(address, 50);
            auto s = wait(runtime, address, 10);
            auto p = std::find_if(s->sites.begin(), s->sites.end(), [](const auto& x) {
                return x.stock[size_t(Good::Water)] - x.reserved[size_t(Good::Water)] > 10;
            });
            check(p != s->sites.end(), "No water inventory available for runtime trade");
            auto i = size_t(p - s->sites.begin());
            auto quote = s->quote(i, Good::Water, 10);
            runtime.command(
                {1, s->network->sites[i].id, EconomicAction::Buy, Good::Water, 10, quote.buyCents, 0});
            s = wait(runtime, address, 10, 1);
            check(s->player.cargo[size_t(Good::Water)] == 10, "Worker did not commit the queued trade");
            retainedCredits = s->player.credits;
            retainedCargo = s->player.cargo;
            runtime.flush();
        }
        {
            EconomyRuntime runtime(u, dir);
            runtime.request(address, 50);
            auto s = wait(runtime, address, 10, 1);
            check(s->player.credits == retainedCredits && s->player.cargo == retainedCargo,
                  "SQLite restart lost or duplicated wallet/cargo");
            auto view = economicMarketView(s.get(), s->network->sites[0].id, 1, 10, true);
            FlightInstruments instruments;
            for (double aspect : {1., 1.6, 2.4}) {
                auto hud = cockpitOverlay(instruments, aspect, "HYPERION", {}, {}, {}, false, 1, &view);
                check(!hud.lettering.vertices.empty() && hud.hitRegions.size() > 3,
                      "Market overlay missing fitted geometry or input blocking");
                const auto panel = hud.hitRegions.back();
                const CockpitLayout layout(aspect);
                check(panel.y >= 160 && panel.y + panel.h < layout.navigation.y && panel.x >= 0 &&
                          panel.x + panel.w <= layout.width,
                      "Market overlaps top instruments, bottom controls or screen bounds");
            }
            for (unsigned star : {0u, 1u, 2u}) {
                auto other = Address{}.child(Kind::Galaxy, 0).child(Kind::System, star);
                runtime.request(other, 50);
                wait(runtime, other, 10, 1);
            }
            runtime.request(address, 50);
            s = wait(runtime, address, 10, 1);
            check(s->player.credits == retainedCredits && s->player.cargo == retainedCargo,
                  "Evicting and reloading systems changed player inventory");
            check(runtime.residentBytes() < 4 * 1024 * 1024,
                  "Dormant economic systems escaped the three-network memory budget");
            const auto commandPort = s->network->sites.front().id;
            for (unsigned k = 2; k <= 13; k++) {
                runtime.request(Address{}.child(Kind::Galaxy, 0).child(Kind::System, k % 4), 50);
                check(runtime.command({k, commandPort, EconomicAction::Sell, Good::Water, 4000, 0, 0}),
                      "Command queue rejected a bounded cancellation stress trace");
            }
            runtime.request(address, 50);
            s = wait(runtime, address, 10, 13);
            check(s->player.cargo == retainedCargo && s->player.credits == retainedCredits,
                  "Cancelled/context-stale orders lost acknowledgement or changed the wallet");
            runtime.flush();
        }
        std::cout << "network_sites=" << network->sites.size() << " vessels=" << network->vessels.size()
                  << " adapter_and_100_ticks_ms=" << adapterMs << " thousand_ticks_ms=" << thousandTickMs
                  << " network_bytes=" << real.residentBytes() << '\n';
        std::cout << "PASS: " << checks
                  << " economic accounting, freight, contracts, replay, cargo physics, preservation and "
                     "SQLite checks\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
