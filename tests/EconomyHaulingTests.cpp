#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
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
    n->fingerprint = 945871;
    for (unsigned i = 0; i < 2; i++) {
        EconomicSite d;
        d.id = {n->system.child(Kind::Planet, i), 0};
        d.name = i ? "BUYER" : "SOURCE";
        d.orbital = true;
        d.capacityKg = 1000000;
        d.systemMetres = {double(i) * 1e10, 1000, 0};
        if (i)
            d.target[size_t(Good::Alloy)] = 1000;
        else
            d.initial[size_t(Good::Alloy)] = 5000;
        n->sites.push_back(d);
    }
    return n;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Hauling invented or lost money");
    auto initial = e.initialTotal(), delta = e.externalGoodsDelta(), stock = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; g++)
        check(initial[g] + delta[g] == stock[g], "Hauling duplicated or lost goods");
    for (const auto& site : e.sites())
        for (size_t g = 0; g < goodCount; g++)
            check(site.reserved[g] >= 0 && site.stock[g] >= site.reserved[g], "Unbacked hauling reservation");
}
EconomicCommand command(const Economy& e, const HaulContract& h, uint64_t sequence, EconomicAction action) {
    EconomicCommand c;
    c.sequence = sequence;
    c.action = action;
    c.contract = h.id;
    c.good = h.good;
    c.kg = h.kg;
    c.port = e.network().sites[action == EconomicAction::DeliverHaul ? h.destination : h.source].id;
    return c;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.hauls = e.hauls();
    s.contracts = e.contracts();
    s.player = p;
    s.tick = e.tick();
    s.jobs = economicJobs(s);
    return s;
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& a, uint64_t tick,
                                            uint64_t seq = 0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        auto s = r.snapshot();
        if (s && s->network && s->network->system == a && s->tick >= tick && !s->catchingUp &&
            s->player.lastCommand >= seq)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Hauling runtime failed to publish");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        const auto money = e.moneyTotal() + p.credits;
        e.advanceTo(12);
        check(!e.hauls().empty() && e.hauls().size() <= 8, "Shortage did not fund bounded hauling offers");
        auto h = e.hauls().front();
        check(h.escrow == h.cost + h.fee && e.sites()[h.source].reserved[size_t(h.good)] >= h.kg,
              "Offer has no physical stock or buyer funding");
        audit(e, p, money);
        auto accept = command(e, h, 1, EconomicAction::AcceptHaul);
        check(e.execute(accept, p).starts_with("HAUL ACCEPTED") && p.haul && !p.haul->onboard &&
                  p.massKg() == 0,
              "Remote acceptance loaded cargo or lost the job");
        auto ledger = e.serialize(), wallet = p.serialize();
        e.execute(accept, p);
        check(e.serialize() == ledger && p.serialize() == wallet, "Acceptance replay mutated the ledger");
        auto pickup = command(e, h, 2, EconomicAction::PickupHaul);
        check(economicPendingPayload(pickup, e.sites()[h.source]) == h.kg,
              "Pickup did not reserve pending ship payload");
        p.cargo[size_t(Good::Oxygen)] = 1000;
        check(e.execute(pickup, p).starts_with("MAKE ROOM") && !p.haul->onboard,
              "Oversized consignment bypassed hold volume");
        p.cargo.fill(0);
        pickup.sequence = 3;
        const auto cash = p.credits;
        const auto sellerCash = e.sites()[h.source].cash;
        check(e.execute(pickup, p).starts_with("SEALED CARGO LOADED") && p.sealedKg() == h.kg &&
                  p.availableCargo(h.good) == 0 && p.credits == cash,
              "Pickup charged the player or failed to seal cargo");
        check(e.sites()[h.source].cash == sellerCash && e.hauls().front().escrow == h.cost + h.fee,
              "Refundable cargo funds became spendable sale income before delivery");
        audit(e, p, money);
        ledger = e.serialize();
        wallet = p.serialize();
        e.execute(pickup, p);
        check(e.serialize() == ledger && p.serialize() == wallet,
              "Pickup replay duplicated cargo or source payment");
        check(e.restore(ledger) && p.restore(wallet) && e.reconcilePlayer(p),
              "Sealed cargo or paired state failed persistence");
        auto sell = command(e, h, 4, EconomicAction::Sell);
        sell.limitCents = 0;
        check(e.execute(sell, p).starts_with("INSUFFICIENT UNSEALED") && p.sealedKg() == h.kg,
              "A consignment was sold as ordinary inventory");
        auto s = snapshot(e, p);
        check(!s.jobs.empty() && s.jobs.front().haul &&
                  s.jobs.front().action == EconomicAction::DeliverHaul &&
                  s.jobs.front().target == n->sites[h.destination].id,
              "Active job is not first or has the wrong destination");
        // Sealed material cannot fulfil a second supply contract.
        auto supply = std::find_if(e.contracts().begin(), e.contracts().end(),
                                   [&](const auto& c) { return c.good == h.good; });
        check(supply != e.contracts().end(), "Missing parallel supply fixture");
        EconomicCommand contract{
            5, n->sites[supply->site].id, EconomicAction::Contract, h.good, supply->requested, 0, supply->id};
        e.execute(contract, p);
        contract.sequence = 6;
        check(e.execute(contract, p).starts_with("REQUIRED CARGO") && p.sealedKg() == h.kg,
              "A sealed consignment was spent on a supply contract");
        // Nor can it be installed in a capital project.
        EconomicCommand invest{7, n->sites[h.source].id, EconomicAction::Invest};
        invest.limitCents = 30000;
        e.execute(invest, p);
        check(e.sites()[h.source].project && e.sites()[h.source].project->delivered[size_t(h.good)] == 0,
              "A sealed consignment was used as construction material");
        EconomicCommand cancelProject{8, n->sites[h.source].id, EconomicAction::CancelProject};
        e.execute(cancelProject, p);
        // New unsealed stock of the same good remains freely tradable.
        auto buy = command(e, h, 9, EconomicAction::Buy);
        buy.kg = 10;
        buy.limitCents = e.quote(h.source, h.good, 10).buyCents;
        check(e.execute(buy, p).starts_with("BOUGHT") && p.availableCargo(h.good) == 10,
              "Seal blocked unrelated trade");
        sell.sequence = 10;
        sell.kg = 10;
        check(e.execute(sell, p).starts_with("SOLD") && p.availableCargo(h.good) == 0 && p.sealedKg() == h.kg,
              "Trading unsealed stock consumed the consignment");
        // Other systems must respect the global wallet seal too.
        auto foreign = fixture();
        foreign->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 10);
        for (size_t i = 0; i < foreign->sites.size(); i++)
            foreign->sites[i].id.home = foreign->system.child(Kind::Planet, i);
        Economy elsewhere(foreign);
        sell.port = foreign->sites[0].id;
        sell.sequence = 11;
        sell.kg = 1;
        check(elsewhere.execute(sell, p).starts_with("INSUFFICIENT UNSEALED"),
              "Cross-system resale bypassed the seal");
        auto deliver = command(e, h, 12, EconomicAction::DeliverHaul);
        deliver.port = n->sites[h.source].id;
        check(e.execute(deliver, p).starts_with("DELIVER AT") && p.sealedKg() == h.kg,
              "Wrong-port delivery succeeded");
        deliver.port = n->sites[h.destination].id;
        deliver.sequence = 13;
        const auto beforePay = p.credits;
        check(e.execute(deliver, p).starts_with("HAUL DELIVERED") && !p.haul && p.massKg() == 0 &&
                  p.credits == beforePay + h.fee && p.deliveries == 1 && p.freightEarnings == h.fee,
              "Delivery did not pay the funded fee exactly once");
        audit(e, p, money);
        ledger = e.serialize();
        wallet = p.serialize();
        e.execute(deliver, p);
        check(e.serialize() == ledger && p.serialize() == wallet, "Delivery replay paid twice");
        check(e.restore(ledger) && p.restore(wallet), "Delivery history failed to persist");
        // A full destination retains cargo, its seal and the funded fee until there is room.
        auto tightNetwork = fixture();
        tightNetwork->sites[1].capacityKg = 1000;
        Economy tight(tightNetwork);
        EconomyPlayer loader;
        const auto tightMoney = tight.moneyTotal() + loader.credits;
        tight.advanceTo(12);
        auto t = tight.hauls().front();
        tight.execute(command(tight, t, 1, EconomicAction::AcceptHaul), loader);
        tight.execute(command(tight, t, 2, EconomicAction::PickupHaul), loader);
        auto fill = command(tight, t, 3, EconomicAction::Buy);
        fill.kg = 1000;
        fill.limitCents = tight.quote(t.source, t.good, 1000).buyCents;
        check(tight.execute(fill, loader).starts_with("BOUGHT"), "Warehouse fixture could not buy stock");
        fill.sequence = 4;
        fill.action = EconomicAction::Sell;
        fill.port = tightNetwork->sites[1].id;
        fill.limitCents = 0;
        check(tight.execute(fill, loader).starts_with("SOLD"),
              "Warehouse fixture could not fill the destination");
        auto unload = command(tight, t, 5, EconomicAction::DeliverHaul);
        const auto unpaid = loader.credits;
        check(tight.execute(unload, loader).starts_with("DESTINATION WAREHOUSE FULL") &&
                  loader.sealedKg() == t.kg && loader.credits == unpaid,
              "A full warehouse deleted cargo or paid before unloading");
        fill.sequence = 6;
        fill.action = EconomicAction::Buy;
        fill.kg = t.kg;
        fill.limitCents = tight.quote(t.destination, t.good, t.kg).buyCents;
        tight.execute(fill, loader);
        unload.sequence = 7;
        check(tight.execute(unload, loader).starts_with("HAUL DELIVERED") && !loader.haul,
              "Consignment did not unload after warehouse space became available");
        audit(tight, loader, tightMoney);
        // Cancellation before pickup releases inventory and the entire buyer escrow.
        Economy unpicked(n);
        EconomyPlayer booking;
        auto bookingMoney = unpicked.moneyTotal() + booking.credits;
        unpicked.advanceTo(12);
        auto u = unpicked.hauls().front();
        unpicked.execute(command(unpicked, u, 1, EconomicAction::AcceptHaul), booking);
        check(unpicked.execute(command(unpicked, u, 2, EconomicAction::CancelHaul), booking)
                      .starts_with("HAUL WITHDRAWN") &&
                  !booking.haul && booking.returnedHauls == 1,
              "Pre-pickup withdrawal failed");
        audit(unpicked, booking, bookingMoney);
        // A loaded return requires the original source and unwinds reserved purchase funds.
        Economy returned(n);
        EconomyPlayer carrier;
        auto returnMoney = returned.moneyTotal() + carrier.credits;
        returned.advanceTo(12);
        auto r = returned.hauls().front();
        returned.execute(command(returned, r, 1, EconomicAction::AcceptHaul), carrier);
        returned.execute(command(returned, r, 2, EconomicAction::PickupHaul), carrier);
        auto back = command(returned, r, 3, EconomicAction::CancelHaul);
        back.port = n->sites[r.destination].id;
        check(returned.execute(back, carrier).starts_with("RETURN SEALED") && carrier.haul,
              "Cargo was discarded remotely");
        back.port = n->sites[r.source].id;
        back.sequence = 4;
        check(returned.execute(back, carrier).starts_with("HAUL WITHDRAWN") && !carrier.haul &&
                  carrier.massKg() == 0,
              "Physical return failed to reconcile source and buyer");
        audit(returned, carrier, returnMoney);
        // Pre-pickup expiry is safe even after its archived record is evicted.
        Economy expired(n);
        EconomyPlayer absent;
        const auto expiryMoney = expired.moneyTotal() + absent.credits;
        expired.advanceTo(12);
        auto x = expired.hauls().front();
        expired.execute(command(expired, x, 1, EconomicAction::AcceptHaul), absent);
        expired.advanceTo(x.deadline + 50, 1000);
        check(expired.reconcilePlayer(absent) && !absent.haul && absent.massKg() == 0 &&
                  absent.returnedHauls == 1,
              "Expired pickup left a permanent wallet/stock lock");
        audit(expired, absent, expiryMoney);
        // Overdue onboard goods stay aboard; reduced fee and unused escrow return are real transfers.
        Economy late(n);
        EconomyPlayer delayed;
        const auto lateMoney = late.moneyTotal() + delayed.credits;
        late.advanceTo(12);
        auto l = late.hauls().front();
        late.execute(command(late, l, 1, EconomicAction::AcceptHaul), delayed);
        late.execute(command(late, l, 2, EconomicAction::PickupHaul), delayed);
        late.advanceTo(l.deadline + 2, 1000);
        check(late.reconcilePlayer(delayed) && delayed.sealedKg() == l.kg,
              "Deadline deleted in-flight goods");
        auto overdueView = snapshot(late, delayed);
        check(overdueView.jobs.front().title.find("FEE " + std::to_string(l.fee / 200) + "CR") !=
                  std::string::npos,
              "Overdue job displayed an unreduced reward");
        late.advanceTo(l.deadline + 24, 1000);
        int64_t replacement = 0;
        for (const auto& other : late.hauls())
            if (other.destination == l.destination && other.good == l.good && other.phase == HaulPhase::Open)
                replacement += other.kg;
        check(replacement >= n->sites[l.destination].target[size_t(l.good)],
              "Overdue cargo permanently suppressed replacement supplies");
        auto credit = delayed.credits;
        late.execute(command(late, l, 3, EconomicAction::DeliverHaul), delayed);
        check(!delayed.haul && delayed.lateDeliveries == 1 && delayed.credits == credit + l.fee / 2,
              "Late delivery did not reduce the funded fee");
        audit(late, delayed, lateMoney);
        // Batch-independent dispatch and bounded archives across a long absent-player trace.
        Economy a(n), b(n);
        const auto traceMoney = a.moneyTotal() + EconomyPlayer{}.credits;
        for (unsigned t = 1; t <= 1200; t++) {
            a.advanceTo(t);
            if (t % 30 == 0)
                audit(a, EconomyPlayer{}, traceMoney);
        }
        b.advanceTo(1200, 1200);
        check(a.serialize() == b.serialize() && a.hauls().size() <= 32,
              "Haul offers depend on frame batching or grow unbounded");
        // Preserve version-two construction ledgers and version-one wallets without granting cargo/jobs.
        Economy legacy(n);
        EconomyPlayer legacyOwner;
        legacyOwner.cargo[size_t(Good::Alloy)] = 20;
        EconomicCommand legacyRefit{1, n->sites[0].id, EconomicAction::Invest};
        legacyRefit.limitCents = 30000;
        legacy.execute(legacyRefit, legacyOwner);
        auto versionTwo = legacy.serialize();
        const auto tail = versionTwo.rfind("1 0\n");
        check(tail != std::string::npos, "Missing legacy conversion boundary");
        versionTwo.erase(tail);
        versionTwo.replace(17, 1, "2");
        Economy migrated(n);
        check(migrated.restore(versionTwo) && migrated.moneyTotal() == legacy.moneyTotal() &&
                  migrated.inventoryTotal() == legacy.inventoryTotal() && migrated.sites()[0].project &&
                  migrated.sites()[0].project->delivered[size_t(Good::Alloy)] == 20 &&
                  migrated.hauls().empty(),
              "Version-two migration changed retained projects, goods or money");
        std::istringstream oldWallet(legacyOwner.serialize());
        std::vector<std::string> words;
        for (std::string word; oldWallet >> word;)
            words.push_back(word);
        words.resize(24);
        words[1] = "1";
        std::string v1;
        for (const auto& word : words)
            v1 += word + " ";
        EconomyPlayer migratedOwner;
        check(migratedOwner.restore(v1) && migratedOwner.credits == legacyOwner.credits &&
                  migratedOwner.cargo == legacyOwner.cargo && !migratedOwner.haul &&
                  !migratedOwner.deliveries,
              "Version-one wallet migration lost cargo or invented history");
        auto retained = delayed.serialize();
        check(!delayed.restore(retained + " corrupt") && delayed.serialize() == retained,
              "Corrupt hauling wallet mutated live state");
        // Terminal record corruption is rejected transactionally.
        auto invalid = late.serialize();
        const auto last = invalid.rfind(' ');
        invalid.replace(last + 1, invalid.size() - last - 1, "99\n");
        const auto live = late.serialize();
        check(!late.restore(invalid) && late.serialize() == live, "Invalid haul phase mutated the ledger");
        EconomyPlayer ranks;
        ranks.deliveries = 5;
        check(ranks.carrierRank() == "COURIER", "On-time service did not progress carrier rank");
        ranks.lateDeliveries = 1;
        check(ranks.carrierRank() == "NEW CARRIER", "Late service falsely counted as on-time reputation");
        // Jobs page stays inside the established cockpit at narrow and wide aspect ratios.
        s = snapshot(late, delayed);
        for (double aspect : {.6, 1., 1.6, 2.4}) {
            auto view = economicMarketView(&s, n->sites[0].id, 0, 10, true, MarketPage::Jobs);
            auto hud = cockpitOverlay({}, aspect, "", {}, {}, {}, false, 1, &view);
            auto rect = hud.hitRegions.back();
            check(!hud.lettering.vertices.empty() && rect.x >= 0 &&
                      rect.x + rect.w <= CockpitLayout(aspect).width &&
                      rect.y + rect.h < CockpitLayout(aspect).navigation.y,
                  "Job board escaped the fitted cockpit");
        }
        // Real worker persistence with a sealed consignment outside its origin network's cache residency.
        Universe universe;
        auto dir = std::filesystem::path(argc > 1 ? argv[1] : "build/economy/haul-test");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto system = n->system;
        int64_t savedCredits = 0;
        uint64_t jobId = 0;
        {
            EconomyRuntime runtime(universe, dir);
            runtime.request(system, 60);
            auto snap = wait(runtime, system, 12);
            check(!snap->hauls.empty(), "Real universe has no funded hauling opportunities");
            auto job = snap->hauls.front();
            jobId = job.id;
            EconomicCommand ac{
                1,     snap->network->sites[job.source].id, EconomicAction::AcceptHaul, job.good, job.kg, 0,
                job.id};
            runtime.command(ac);
            snap = wait(runtime, system, 12, 1);
            check(snap->player.haul && !snap->player.haul->onboard, "Worker acceptance failed");
            ac.sequence = 2;
            ac.action = EconomicAction::PickupHaul;
            runtime.command(ac);
            snap = wait(runtime, system, 12, 2);
            check(snap->player.haul && snap->player.haul->onboard && snap->player.sealedKg() == job.kg,
                  "Worker did not commit consignment and wallet atomically");
            savedCredits = snap->player.credits;
            for (unsigned star : {0u, 1u, 2u}) {
                auto other = Address{}.child(Kind::Galaxy, 0).child(Kind::System, star);
                runtime.request(other, 60);
                auto remote = wait(runtime, other, 12, 2);
                check(remote->player.haul && remote->player.sealedKg() == job.kg,
                      "Eviction lost the sealed cargo");
            }
            check(runtime.residentBytes() < 4 * 1024 * 1024, "Hauling exceeded the resident cache budget");
            runtime.flush();
        }
        {
            EconomyRuntime runtime(universe, dir);
            runtime.request(system, 60);
            auto snap = wait(runtime, system, 12, 2);
            check(snap->player.haul && snap->player.haul->id == jobId && snap->player.credits == savedCredits,
                  "Restart lost the active haul or changed credits");
            auto job = *std::find_if(snap->hauls.begin(), snap->hauls.end(),
                                     [&](const auto& q) { return q.id == jobId; });
            runtime.command({3, snap->network->sites[job.destination].id, EconomicAction::DeliverHaul,
                             job.good, job.kg, 0, job.id});
            snap = wait(runtime, system, 12, 3);
            check(!snap->player.haul && snap->player.deliveries == 1 &&
                      snap->player.credits == savedCredits + job.fee,
                  "Restarted haul did not deliver once");
            std::cout << "worker_bytes=" << runtime.residentBytes() << " job_rows=" << snap->jobs.size()
                      << '\n';
        }
        std::cout << "PASS: " << checks
                  << " hauling, seals, conservation, expiry, returns, history, UI and restart checks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
