#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include "infinite/Piloting.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
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
    n->fingerprint = 410041;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite d;
        d.id = {n->system.child(Kind::Planet, i), 0};
        d.name = i ? "BULK BUYER" : "SERVICE PORT";
        d.orbital = i != 0;
        d.population = i ? 0 : 4000;
        d.capacityKg = 1000000;
        d.initial.fill(5000);
        d.systemMetres = {double(i) * 1e10, 0, 0};
        d.initial[size_t(Good::Water)] = i ? 0 : 30000;
        d.target[size_t(Good::Water)] = i ? 5000 : 0;
        n->sites.push_back(d);
    }
    return n;
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
EconomicCommand service(const Economy& e, const EconomyPlayer& p, uint64_t sequence, ShipService kind,
                        unsigned units) {
    auto q = economicServiceQuote(snapshot(e, p), 0, kind, units);
    EconomicCommand c;
    c.sequence = sequence;
    c.port = e.network().sites[0].id;
    c.action = kind == ShipService::Repair ? EconomicAction::RepairHull : EconomicAction::UpgradeCargo;
    c.serviceUnits = units;
    c.limitCents = q.totalCents();
    return c;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Services created or destroyed money");
    auto initial = e.initialTotal(), delta = e.externalGoodsDelta(), stock = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(initial[g] + delta[g] == stock[g], "Services lost material accounting");
    for (const auto& site : e.sites())
        for (size_t g = 0; g < goodCount; ++g)
            check(site.stock[g] >= site.reserved[g], "Services consumed reserved freight");
}
std::string hullSave(const Pilot& p, double hull) {
    std::istringstream input(p.serialize());
    std::string result, word;
    for (unsigned i = 0; input >> word; ++i)
        result += (i == 17 ? std::to_string(hull) : word) + " ";
    return result;
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& runtime, const Address& system, uint64_t seq) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        auto s = runtime.snapshot();
        if (s && s->network && s->network->system == system && !s->catchingUp && s->player.lastCommand >= seq)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Service worker failed to publish");
}
} // namespace
int main(int argc, char** argv) {
    try {
        const auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        // A bulk bill must equal successive one-kilogram marginal quotes,
        // including scarce stock, reserved inventory and saturated prices.
        for (int64_t target : {int64_t(0), int64_t(137), int64_t(5000)})
            for (size_t g = 0; g < goodCount; ++g) {
                auto network = std::make_shared<EconomicNetwork>(*n);
                network->sites[0].target[g] = target;
                EconomySnapshot prices;
                prices.network = network;
                prices.sites = e.sites();
                prices.sites[0].stock[g] = target ? 9000 : 0;
                prices.sites[0].reserved[g] = target ? 1700 : 0;
                const auto initialStock = prices.sites[0].stock[g];
                auto bulk = prices.quote(0, Good(g), maxShipPayloadKg);
                int64_t ask = 0, bid = 0;
                for (int64_t kg = 0; kg < maxShipPayloadKg; ++kg) {
                    prices.sites[0].stock[g] = std::max(prices.sites[0].reserved[g], initialStock - kg);
                    ask += prices.quote(0, Good(g), 1).buyCents;
                    prices.sites[0].stock[g] = initialStock + kg;
                    bid += prices.quote(0, Good(g), 1).sellCents;
                }
                check(ask == bulk.buyCents && bid == bulk.sellCents,
                      "Bulk billing changed the established marginal prices");
            }
        const auto money = e.moneyTotal() + p.credits;
        auto repair = service(e, p, 1, ShipService::Repair, 10);
        auto q = economicServiceQuote(snapshot(e, p), 0, ShipService::Repair, 10);
        check(q.available && q.totalCents() > 0, "Funded port cannot offer a repair");
        const auto partsBefore = e.sites()[0].stock;
        const auto crewBefore = e.sites()[0].householdCash;
        check(e.execute(repair, p).starts_with("HULL REPAIR PAID") && p.repairPoints == 10,
              "Paid repair has no durable receipt");
        check(e.sites()[0].householdCash == crewBefore + q.labourCents,
              "Repair crew was not paid from the player");
        for (size_t g = 0; g < goodCount; ++g)
            check(e.sites()[0].stock[g] == partsBefore[g] - q.materials[g],
                  "Repair did not use quoted real parts");
        audit(e, p, money);
        auto ledger = e.serialize(), wallet = p.serialize();
        e.execute(repair, p);
        check(e.serialize() == ledger && p.serialize() == wallet, "Repair replay charged twice");
        Universe universe;
        Navigation nav(universe);
        RegionCache cache(universe);
        FlightController flight(universe, nav, cache);
        Pilot pilot(universe, nav, flight);
        check(pilot.restore(hullSave(pilot, 83)), "Could not restore a damaged ship fixture");
        const auto beforeApplication = pilot.serialize();
        pilot.applyPortRepairs(p.repairPoints);
        check(pilot.ship().hull == 93 && pilot.ship().appliedRepairPoints == 10,
              "Repair receipt did not restore paid hull points");
        const auto afterApplication = pilot.serialize();
        pilot.applyPortRepairs(p.repairPoints);
        check(pilot.serialize() == afterApplication, "Receipt replay healed the ship twice");
        check(pilot.restore(beforeApplication), "Pre-application ship restore failed");
        pilot.applyPortRepairs(p.repairPoints);
        check(pilot.serialize() == afterApplication, "Crash between payment and application lost repair");
        check(pilot.restore(afterApplication), "Applied receipt failed restart");
        check(pilot.restore(hullSave(pilot, 71)), "Could not restore post-repair damage");
        pilot.applyPortRepairs(p.repairPoints);
        check(pilot.ship().hull == 71, "Old paid work healed later damage");
        auto oldShip = beforeApplication.substr(0, beforeApplication.rfind(' '));
        oldShip.replace(14, 1, "4");
        check(pilot.restore(oldShip) && pilot.ship().appliedRepairPoints == 0 && pilot.ship().hull == 83,
              "Version-four ship migration changed hull state");
        pilot.applyPortRepairs(p.repairPoints);
        check(pilot.ship().hull == 93, "Old ship did not recover committed repair work");
        auto stale = service(e, p, 2, ShipService::CargoHold, 1);
        stale.limitCents = 0;
        const auto beforeRefit = p.credits;
        check(e.execute(stale, p).starts_with("SERVICE QUOTE CHANGED") && p.cargoLevel == 0 &&
                  p.credits == beforeRefit,
              "Stale refit quote spent funds");
        auto upgrade = service(e, p, 3, ShipService::CargoHold, 1);
        check(e.execute(upgrade, p).starts_with("CARGO REFIT INSTALLED") && p.cargoLevel == 1 &&
                  p.capacityKg == 6000 && p.capacityLitres == 16000,
              "Cargo refit did not expand both limits");
        audit(e, p, money);
        ledger = e.serialize();
        wallet = p.serialize();
        e.execute(upgrade, p);
        check(e.serialize() == ledger && p.serialize() == wallet, "Cargo upgrade replay spent twice");
        check(e.restore(ledger) && p.restore(wallet) && p.capacityKg == 6000 && p.capacityLitres == 16000,
              "Refit capacity or repairs were lost on restore");
        e.advanceTo(12);
        const auto bulk =
            std::find_if(e.hauls().begin(), e.hauls().end(), [](const auto& h) { return h.kg > 4000; });
        check(bulk != e.hauls().end() && bulk->kg <= maxShipPayloadKg,
              "Real shortage did not create a larger funded job");
        const auto h = *bulk;
        EconomicCommand ac{4, n->sites[h.source].id, EconomicAction::AcceptHaul, h.good, h.kg, 0, h.id};
        e.execute(ac, p);
        ac.sequence = 5;
        ac.action = EconomicAction::PickupHaul;
        check(economicPendingPayload(ac, e.sites()[0]) == h.kg,
              "Large pickup lost its provisional cargo lock");
        check(e.execute(ac, p).starts_with("SEALED CARGO LOADED") && p.massKg() == h.kg,
              "Expanded hold could not collect a bulk consignment");
        check(p.restore(p.serialize()) && e.restore(e.serialize()) && e.reconcilePlayer(p),
              "Large sealed manifest failed save/restart");
        pilot.setCargoMass(p.massKg());
        check(pilot.cargoMass() == h.kg && shipProperties(1, 0, h.kg).massKg == 12000 + h.kg,
              "Expanded payload was clipped to the old physics limit");
        pilot.toggleShrink();
        check(!pilot.ship().shrinking, "Expanded payload bypassed shrink lock");
        pilot.adjustSize(-3);
        check(!pilot.ship().shrinking, "Large payload bypassed manual shrink lock");
        auto s = snapshot(e, p);
        MarketContext context;
        context.systemPosition = n->sites[h.source].systemMetres;
        auto job = s.jobs.front();
        auto plan = economicPlanJob(s, job, context);
        check(plan.timed && plan.fits && plan.pickupMetres == 0 && plan.deliveryMetres == 1e10,
              "Loaded route doubled its own cargo or included a second pickup");
        check(std::abs(plan.slackSeconds -
                       ((double(h.deadline) - s.tick) * economyTickSeconds - plan.seconds)) < 1e-8,
              "Deadline margin mixed economic minutes with flight seconds");
        auto slow = context;
        slow.cruiseSpeed = 1e6;
        check(economicPlanJob(s, job, slow).seconds > plan.seconds,
              "Planner ignored configured cruise speed");
        auto near = context;
        near.systemPosition = n->sites[h.destination].systemMetres;
        check(economicPlanJob(s, job, near).seconds < plan.seconds,
              "Moving to destination did not reduce estimated travel");
        auto oldLedger = e.serialize(), oldWallet = p.serialize(), oldCamera = nav.serialize(),
             oldPilot = pilot.serialize();
        for (unsigned i = 0; i < 100; ++i)
            (void)economicPlanJob(s, job, context);
        check(e.serialize() == oldLedger && p.serialize() == oldWallet && nav.serialize() == oldCamera &&
                  pilot.serialize() == oldPilot,
              "Read-only job planning mutated the universe, ship or ledger");
        s.tick = h.deadline + 1;
        check(economicPlanJob(s, job, context).slackSeconds < 0, "Overdue job has positive deadline margin");
        auto uncollected = job;
        uncollected.action = EconomicAction::PickupHaul;
        auto small = snapshot(e, p);
        small.player.cargo.fill(0);
        small.player.haul.reset();
        small.player.capacityKg = 4000;
        small.player.capacityLitres = 8000;
        check(!economicPlanJob(small, uncollected, context).fits, "Planner hid an undersized hold");
        auto procurement = job;
        procurement.haul = false;
        check(economicPlanJob(small, procurement, context).procurement &&
                  !economicPlanJob(small, procurement, context).timed,
              "Supply job invented a procurement route");
        check(!economicPlanJob(small, procurement, context).fits,
              "Supply planning ignored the space needed for missing goods");
        auto unknown = context;
        unknown.systemPosition.reset();
        check(!economicPlanJob(s, job, unknown).timed, "Planner invented a remote ship position");
        for (double aspect : {.6, 1., 1.6, 2.4})
            for (auto page : {MarketPage::Services, MarketPage::Jobs}) {
                auto view = economicMarketView(&s, n->sites[0].id, 0, 10, true, page, context);
                auto hud = cockpitOverlay({}, aspect, "", {}, {}, {}, false, 1, &view);
                auto panel = hud.hitRegions.back();
                check(panel.x >= 0 && panel.x + panel.w <= CockpitLayout(aspect).width &&
                          panel.y + panel.h < CockpitLayout(aspect).navigation.y &&
                          !hud.lettering.vertices.empty(),
                      "Services or planning escaped the fitted cockpit");
                check(view.capacityKg == p.capacityKg && view.capacityLitres == p.capacityLitres,
                      "Cockpit still displayed the old fixed hold limits");
            }
        ac.sequence = 6;
        ac.action = EconomicAction::DeliverHaul;
        ac.port = n->sites[h.destination].id;
        check(e.execute(ac, p).starts_with("HAUL DELIVERED") && p.massKg() == 0,
              "Bulk haul could not deliver");
        audit(e, p, money);
        auto noParts = fixture();
        noParts->sites[0].initial[size_t(Good::Machinery)] = 0;
        Economy empty(noParts);
        EconomyPlayer customer;
        auto refusal = service(empty, customer, 1, ShipService::Repair, 10);
        const auto emptyMoney = empty.moneyTotal() + customer.credits;
        check(empty.execute(refusal, customer).starts_with("PORT NEEDS") && customer.repairPoints == 0,
              "A port repaired without spare parts");
        audit(empty, customer, emptyMoney);
        auto reserved = snapshot(e, p);
        reserved.sites[0].reserved[size_t(Good::Machinery)] =
            reserved.sites[0].stock[size_t(Good::Machinery)];
        check(!economicServiceQuote(reserved, 0, ShipService::Repair, 10).available,
              "Repair quote spent machinery already committed to freight");
        customer.credits = 0;
        check(!economicServiceQuote(snapshot(e, customer), 0, ShipService::CargoHold, 1).available,
              "A penniless ship received a refit");
        Economy outage(n);
        EconomyPlayer grounded;
        outage.outage(n->sites[0].id, 10);
        check(!economicServiceQuote(snapshot(outage, grounded), 0, ShipService::Repair, 10).available,
              "Port outage did not stop service work");
        Economy levels(n);
        EconomyPlayer owner;
        owner.credits = 5000000;
        const auto levelMoney = levels.moneyTotal() + owner.credits;
        for (unsigned level = 1; level <= 3; ++level) {
            auto c = service(levels, owner, level, ShipService::CargoHold, level);
            check(levels.execute(c, owner).starts_with("CARGO REFIT INSTALLED"),
                  "Funded cargo tier did not install");
            audit(levels, owner, levelMoney);
        }
        check(owner.capacityKg == maxShipPayloadKg && owner.capacityLitres == 32000 &&
                  !economicServiceQuote(snapshot(levels, owner), 0, ShipService::CargoHold, 4).available,
              "Maximum hold tier was bypassed");
        auto badWallet = owner.serialize();
        const auto tail = badWallet.rfind("3 0");
        check(tail != std::string::npos, "Missing capacity migration boundary");
        badWallet.replace(tail, 1, "4");
        auto retained = owner.serialize();
        check(!owner.restore(badWallet) && owner.serialize() == retained,
              "Invalid capacity tier mutated wallet");
        EconomyPlayer old;
        std::ostringstream v2;
        v2 << "INFINITE_WALLET 2 250000 0";
        for (size_t g = 0; g < goodCount; ++g)
            v2 << " 0";
        v2 << " 0 0 0 0 0";
        check(old.restore(v2.str()) && old.cargoLevel == 0 && old.capacityKg == 4000 && !old.repairPoints,
              "Version-two migration granted upgrades or lost default capacity");
        // Upgrades and unconsumed repair receipts survive LRU eviction and worker restart.
        auto dir = std::filesystem::path(argc > 1 ? argv[1] : "build/economy/services-test");
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        std::optional<ActivityId> realPort;
        int64_t credits = 0;
        {
            EconomyRuntime runtime(universe, dir);
            runtime.request(n->system, 0);
            auto view = wait(runtime, n->system, 0);
            for (size_t i = 0; i < view->sites.size(); ++i)
                if (economicServiceQuote(*view, i, ShipService::CargoHold, 1).available &&
                    economicServiceQuote(*view, i, ShipService::Repair, 10).available) {
                    realPort = view->network->sites[i].id;
                    break;
                }
            check(bool(realPort), "Real universe has no stocked service port");
            EconomicCommand command;
            command.sequence = 1;
            command.port = *realPort;
            command.action = EconomicAction::UpgradeCargo;
            command.serviceUnits = 1;
            command.limitCents =
                economicServiceQuote(*view, *view->siteIndex(*realPort), ShipService::CargoHold, 1)
                    .totalCents();
            runtime.command(command);
            view = wait(runtime, n->system, 1);
            check(view->player.cargoLevel == 1, "Worker did not commit cargo upgrade");
            command.sequence = 2;
            command.action = EconomicAction::RepairHull;
            command.serviceUnits = 10;
            command.limitCents =
                economicServiceQuote(*view, *view->siteIndex(*realPort), ShipService::Repair, 10)
                    .totalCents();
            runtime.command(command);
            view = wait(runtime, n->system, 2);
            check(view->player.repairPoints == 10, "Worker lost the pending hull repair receipt");
            credits = view->player.credits;
            for (unsigned star : {0u, 1u, 2u}) {
                auto other = Address{}.child(Kind::Galaxy, 0).child(Kind::System, star);
                runtime.request(other, 0);
                (void)wait(runtime, other, 2);
            }
            check(runtime.residentBytes() < 4 * 1024 * 1024, "Services exceeded economic cache budget");
        }
        {
            EconomyRuntime runtime(universe, dir);
            runtime.request(n->system, 0);
            auto view = wait(runtime, n->system, 2);
            check(view->player.cargoLevel == 1 && view->player.capacityKg == 6000 &&
                      view->player.capacityLitres == 16000 && view->player.repairPoints == 10 &&
                      view->player.credits == credits,
                  "Restart lost paid ship services or altered credits");
        }
        std::cout
            << "PASS: " << checks
            << " port services, repair receipts, cargo tiers, planning, conservation and restart checks\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
