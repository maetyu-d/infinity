#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
std::string personText(const CrewMember& p) {
    std::ostringstream o;
    writeCrewPerson(o, p);
    return o.str();
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->universeSeed = 47;
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 470047;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite s;
        s.id = {n->system.child(Kind::Planet, i), 0};
        s.name = "CREW EXCHANGE";
        s.orbital = true;
        s.seed = 100 + i;
        s.initial.fill(5000);
        s.capacityKg = 2000000;
        n->sites.push_back(s);
    }
    return n;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.player = p;
    s.tick = e.tick();
    s.crewMarkets = e.crewMarkets();
    return s;
}
void sync(ShipCrew& c, const EconomyPlayer& p) {
    c.reconcile(p.crew);
    c.reconcileMarket(p.crewRevision, p.crewTitles, p.crewBoarding);
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t total) {
    check(e.moneyTotal() + p.credits == total, "Crew trading created or lost money");
    auto a = e.initialTotal(), d = e.externalGoodsDelta(), b = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(a[g] + d[g] == b[g], "Crew trading changed goods accounting");
    Economy copy(std::make_shared<EconomicNetwork>(e.network()));
    check(copy.restore(e.serialize()) && copy.serialize() == e.serialize(),
          "Crew market ledger round trip failed");
    EconomyPlayer wallet;
    check(wallet.restore(p.serialize()) && wallet.serialize() == p.serialize(),
          "Crew wallet round trip failed");
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& system, uint64_t seq = 0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        auto s = r.snapshot();
        if (s && s->network && s->network->system == system && !s->catchingUp && s->player.lastCommand >= seq)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Crew trade worker timed out");
}
} // namespace
int main(int argc, char** argv) {
    try {
        auto net = fixture();
        Economy e(net);
        EconomyPlayer p;
        p.credits = 10000000;
        ShipCrew ship(47);
        const auto total = e.moneyTotal() + p.credits;
        uint64_t seq = 0;
        auto offers = economicCrewOffers(snapshot(e, p), 0);
        check(offers.size() == 3 &&
                  personText(offers[0]) == personText(economicCrewOffers(snapshot(e, p), 0)[0]),
              "Crew stock not finite and deterministic");
        check(offers[0].id != economicCrewOffers(snapshot(e, p), 1)[0].id,
              "Different ports cloned the same crew identity");
        auto actor = offers[0];
        const auto original = personText(actor);
        auto quote = economicCrewQuote(snapshot(e, p), 0, actor, true, ship.living());
        EconomicCommand c;
        c.sequence = ++seq;
        c.port = net->sites[0].id;
        c.action = EconomicAction::BuyCrew;
        c.crewId = actor.id;
        c.crewAboard = unsigned(ship.living());
        c.limitCents = quote.buyCents;
        check(e.execute(c, p).starts_with("CREW BOUGHT"), "Crew buy failed");
        sync(ship, p);
        check(ship.living() == 7 && economicCrewOffers(snapshot(e, p), 0).size() == 2,
              "Buy did not move one person between inventories");
        check(personText(ship.members().back()) == original, "Boarding replaced person attributes");
        audit(e, p, total);
        auto saved = ship.serialize();
        sync(ship, p);
        check(ship.serialize() == saved, "Receipt replay duplicated or reset crew");
        ShipCrew crash(47);
        sync(crash, p);
        check(crash.living() == 7 && personText(crash.members().back()) == original,
              "Committed crew purchase not recoverable from older ship save");
        const auto beforeSale = p.credits;
        quote = economicCrewQuote(snapshot(e, p), 0, actor, false, ship.living());
        c.sequence = ++seq;
        c.action = EconomicAction::SellCrew;
        c.person = ship.members().back();
        c.limitCents = quote.sellCents;
        check(e.execute(c, p).starts_with("CREW SOLD"), "Crew sell failed");
        sync(ship, p);
        check(ship.living() == 6 && p.credits == beforeSale + quote.sellCents,
              "Sale did not remove actor and pay correct bid");
        offers = economicCrewOffers(snapshot(e, p), 0);
        auto returned =
            std::find_if(offers.begin(), offers.end(), [&](const auto& m) { return m.id == actor.id; });
        check(returned != offers.end() && personText(*returned) == original,
              "Port sale lost skills, condition or identity");
        const auto credits = p.credits;
        c.sequence = ++seq;
        check(e.execute(c, p) == "CREW NO LONGER OWNED" && p.credits == credits,
              "New-sequence resale duplicated a person or payout");
        audit(e, p, total);
        quote = economicCrewQuote(snapshot(e, p), 0, *returned, true, ship.living());
        c.sequence = ++seq;
        c.action = EconomicAction::BuyCrew;
        c.person.reset();
        c.limitCents = quote.buyCents;
        c.crewAboard = unsigned(ship.living());
        check(e.execute(c, p).starts_with("CREW BOUGHT"), "Repurchase failed");
        sync(ship, p);
        check(ship.living() == 7 && personText(ship.members().back()) == original,
              "Repurchase regenerated sold crew");
        check(p.credits < credits, "Same-port buy/sell loop minted money");
        audit(e, p, total);
        auto skilled = ship.members()[0];
        auto raw = economicCrewQuote(snapshot(e, p), 0, skilled, false, 7).sellCents;
        skilled.experience.fill(3600);
        check(economicCrewQuote(snapshot(e, p), 0, skilled, false, 7).sellCents > raw,
              "Skills did not affect value");
        const auto healthy = economicCrewQuote(snapshot(e, p), 0, skilled, false, 7).sellCents;
        skilled.health = 10;
        check(economicCrewQuote(snapshot(e, p), 0, skilled, false, 7).sellCents < healthy,
              "Condition did not affect crew value");
        auto full = economicCrewQuote(snapshot(e, p), 0, economicCrewOffers(snapshot(e, p), 0)[0], true, 8);
        check(!full.available, "Full ship could buy crew");
        auto poor = snapshot(e, p);
        poor.player.credits = 0;
        check(!economicCrewQuote(poor, 0, economicCrewOffers(poor, 0)[0], true, 6).available,
              "Unfunded crew purchase offered");
        poor = snapshot(e, p);
        poor.sites[0].cash = 0;
        check(!economicCrewQuote(poor, 0, ship.members()[0], false, 7).available,
              "Insolvent port offered crew buyback");
        poor = snapshot(e, p);
        poor.sites[0].outageUntil = 100;
        check(!economicCrewQuote(poor, 0, ship.members()[0], false, 7).available,
              "Closed crew market traded");
        auto dead = ship.members()[1];
        dead.health = 0;
        c.sequence = ++seq;
        c.action = EconomicAction::SellCrew;
        c.crewId = dead.id;
        c.person = dead;
        c.limitCents = 0;
        check(e.execute(c, p).find("DECEASED") != std::string::npos, "Corpse sold as living crew");
        c.sequence = ++seq;
        c.action = EconomicAction::ReleaseCrew;
        const auto cash = p.credits;
        check(e.execute(c, p).starts_with("CREW RELEASED") && p.credits == cash, "Release paid money");
        sync(ship, p);
        check(std::none_of(ship.members().begin(), ship.members().end(),
                           [&](const auto& m) { return m.id == dead.id; }),
              "Released crew resurrected during reconciliation");
        audit(e, p, total);
        {
            // A full roster of previously purchased crew must remain replaceable
            // after casualties, without accumulating hidden boarding receipts.
            Economy replacement(net);
            EconomyPlayer wallet;
            wallet.credits = 10000000;
            ShipCrew roster(47);
            for (uint64_t id = 1; id <= 6; ++id)
                wallet.crewTitles.push_back({id, false});
            for (uint64_t i = 1; i <= 8; ++i) {
                auto m = roster.recruit(200 + i);
                m.id = (1ULL << 63) | i;
                wallet.crewTitles.push_back({m.id, true});
                wallet.crewBoarding.push_back({i, m});
            }
            wallet.crewRevision = 8;
            sync(roster, wallet);
            check(roster.living() == 8, "Purchased roster fixture did not board");
            const auto money = replacement.moneyTotal() + wallet.credits;
            for (unsigned cycle = 0; cycle < 3; ++cycle) {
                const auto available = economicCrewOffers(snapshot(replacement, wallet), 0);
                auto lost = roster.members().front();
                EconomicCommand order;
                order.sequence = cycle * 2 + 1;
                order.port = net->sites[0].id;
                order.action = EconomicAction::BuyCrew;
                order.crewId = available.front().id;
                order.crewAboard = 7;
                order.limitCents =
                    economicCrewQuote(snapshot(replacement, wallet), 0, available.front(), true, 7).buyCents;
                order.person = lost;
                check(replacement.execute(order, wallet) == "INVALID LOST CREW REPLACEMENT",
                      "Living crew could be silently retired by a purchase");
                lost.health = 0;
                order.person = lost;
                ++order.sequence;
                check(replacement.execute(order, wallet).starts_with("CREW BOUGHT"),
                      "Full boarding history blocked a casualty replacement");
                sync(roster, wallet);
                check(wallet.crewBoarding.size() == 8 && !crewOwned(wallet, lost.id) &&
                          roster.living() == 8 &&
                          std::none_of(roster.members().begin(), roster.members().end(),
                                       [&](const auto& m) { return m.id == lost.id; }),
                      "Replaced casualty retained a hidden receipt or berth");
                audit(replacement, wallet, money);
            }
            check(economicCrewOffers(snapshot(replacement, wallet), 0).empty(),
                  "Depleted crew stock regenerated");
            Economy restored(net);
            check(restored.restore(replacement.serialize()) &&
                      economicCrewOffers(snapshot(restored, wallet), 0).empty(),
                  "Empty crew market did not persist");
        }
        auto oldShip = ShipCrew(47).serialize();
        std::istringstream header(oldShip);
        std::string token;
        for (unsigned i = 0; i < 12; ++i)
            header >> token;
        auto begin = header.tellg();
        header >> token;
        auto end = header.tellg();
        oldShip.erase(size_t(begin), size_t(end - begin));
        oldShip.replace(0, 15, "INFINITE_CREW 1");
        ShipCrew migrated(47);
        check(migrated.restore(oldShip), "Crew v1 save no longer loads");
        auto legacy = e.serialize();
        legacy.erase(legacy.find("CREW_MARKETS"));
        legacy.replace(0, 18, "INFINITE_ECONOMY 6");
        Economy old(net);
        check(old.restore(legacy), "Legacy v6 economy stopped loading");
        auto view = snapshot(e, p);
        MarketContext context;
        context.crew = ship.members();
        auto market = economicMarketView(&view, net->sites[0].id, 2, 1, true, MarketPage::Crew, context);
        check(market.crewRows.size() == economicCrewOffers(view, 0).size() + ship.members().size(),
              "Crew commodity page omitted people");
        // Global ownership and per-port stock survive worker eviction and SQLite restart.
        const auto data = std::filesystem::path(argc > 1 ? argv[1] : "build/crew/trade-test");
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        Universe u;
        const auto system = net->system;
        ActivityId port;
        uint64_t id = 0;
        std::string identity;
        {
            EconomyRuntime r(u, data);
            r.request(system, 0);
            auto s = wait(r, system);
            size_t index = 0;
            CrewMember chosen;
            for (; index < s->sites.size(); ++index) {
                auto list = economicCrewOffers(*s, index);
                if (!list.empty() && economicCrewQuote(*s, index, list[0], true, 6).available) {
                    chosen = list[0];
                    break;
                }
            }
            check(index < s->sites.size(), "No affordable real crew market");
            port = s->network->sites[index].id;
            id = chosen.id;
            identity = personText(chosen);
            auto q = economicCrewQuote(*s, index, chosen, true, 6);
            EconomicCommand buy;
            buy.sequence = 1;
            buy.port = port;
            buy.action = EconomicAction::BuyCrew;
            buy.crewId = id;
            buy.crewAboard = 6;
            buy.limitCents = q.buyCents;
            check(r.command(buy), "Worker queue rejected purchase");
            s = wait(r, system, 1);
            check(crewOwned(s->player, id), "Worker failed ownership commit");
            auto bid = economicCrewQuote(*s, index, chosen, false, 7);
            buy.sequence = 2;
            buy.action = EconomicAction::SellCrew;
            buy.person = chosen;
            buy.limitCents = bid.sellCents;
            r.command(buy);
            s = wait(r, system, 2);
            check(!crewOwned(s->player, id), "Worker failed sale ownership commit");
            for (unsigned i = 10; i < 15; ++i) {
                auto other = Address{}.child(Kind::Galaxy, 0).child(Kind::System, i);
                r.request(other, 0);
                wait(r, other);
            }
            r.request(system, 0);
            s = wait(r, system, 2);
            auto i = *s->siteIndex(port);
            auto stock = economicCrewOffers(*s, i);
            check(std::any_of(stock.begin(), stock.end(),
                              [&](const auto& m) { return m.id == id && personText(m) == identity; }),
                  "LRU eviction lost sold individual");
            r.flush();
        }
        {
            EconomyRuntime r(u, data);
            r.request(system, 0);
            auto s = wait(r, system, 2);
            auto i = *s->siteIndex(port);
            auto stock = economicCrewOffers(*s, i);
            auto person = std::find_if(stock.begin(), stock.end(), [&](const auto& m) { return m.id == id; });
            check(person != stock.end() && personText(*person) == identity && !crewOwned(s->player, id),
                  "Restart changed crew stock or ownership");
        }
        std::cout << checks
                  << " crew stock, pricing, buy/sell, identity, condition, receipts, migration, accounting "
                     "and worker restart checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
