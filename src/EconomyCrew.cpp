#include "infinite/Economy.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
namespace infinite {
namespace {
std::vector<CrewMember> initialOffers(const EconomicNetwork& n, size_t site) {
    const auto& d = n.sites.at(site);
    ShipCrew generator(n.universeSeed);
    const auto seed = mix64(seedOf(d.id.home, n.universeSeed) ^ mix64(d.id.slot) ^ 0x435245574d41524bULL);
    std::vector<CrewMember> offers;
    for (unsigned i = 0; i < 3; ++i) {
        const auto h = mix64(seed ^ mix64(i));
        auto m = generator.recruit(h % 1000000 + 1);
        m.id = (1ULL << 63) | (h & ~(1ULL << 63));
        m.health = 80 + double((h >> 8) % 21);
        m.fatigue = double((h >> 16) % 21);
        m.morale = 70 + double((h >> 24) % 31);
        if (std::any_of(offers.begin(), offers.end(), [&](const auto& p) { return p.name == m.name; }))
            m.name += " " + std::to_string(i + 1);
        offers.push_back(std::move(m));
    }
    return offers;
}
bool validPerson(const CrewMember& m) {
    std::ostringstream out;
    writeCrewPerson(out, m);
    std::istringstream in(out.str());
    CrewMember parsed;
    return readCrewPerson(in, parsed);
}
} // namespace
std::string crewMoney(int64_t cents) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2) << cents / 100.;
    return o.str();
}
bool crewOwned(const EconomyPlayer& p, uint64_t id) {
    const auto title =
        std::find_if(p.crewTitles.begin(), p.crewTitles.end(), [&](const auto& t) { return t.id == id; });
    if (title != p.crewTitles.end())
        return title->aboard;
    return (id >= 1 && id <= 6) || (id >= 101 && id <= 100 + p.crew.hired);
}
std::vector<CrewMember> economicCrewOffers(const EconomySnapshot& s, size_t site) {
    if (!s.network || site >= s.network->sites.size())
        return {};
    const auto found = std::find_if(s.crewMarkets.begin(), s.crewMarkets.end(),
                                    [&](const auto& p) { return p.site == site; });
    return found == s.crewMarkets.end() ? initialOffers(*s.network, site) : found->offers;
}
CrewTradeQuote economicCrewQuote(const EconomySnapshot& s, size_t site, const CrewMember& person, bool buy,
                                 size_t aboard) {
    CrewTradeQuote q;
    if (!s.network || site >= s.sites.size() || !validPerson(person)) {
        q.status = "INVALID CREW OR PORT";
        return q;
    }
    const auto& d = s.network->sites[site];
    const auto& state = s.sites[site];
    const auto offers = economicCrewOffers(s, site);
    const auto specialty = size_t(std::max_element(person.experience.begin(), person.experience.end()) -
                                  person.experience.begin());
    const auto demand = d.sealed ? 4u : !d.recipes.empty() ? 3u : d.orbital ? unsigned(1 + d.seed % 2) : 5u;
    double base = 30000;
    for (auto xp : person.experience)
        base += xp * 35;
    const double condition = (.2 + .8 * person.health / 100.) * (.7 + .3 * person.morale / 100.) *
                             (1 - .35 * person.fatigue / 100.);
    const double local = (.7 + double(d.seed % 61) / 100.) * (specialty == demand ? 1.6 : .85) *
                         (1.3 - .05 * std::min<size_t>(offers.size(), 12));
    q.buyCents = std::max<int64_t>(1000, int64_t(std::ceil(base * condition * local * 1.18)) + 1000);
    q.sellCents = std::max<int64_t>(100, int64_t(std::floor(base * condition * local * .72)));
    const bool titleSpace = s.player.crewTitles.size() < maxCrewTitles ||
                            std::any_of(
                                s.player.crewTitles.begin(), s.player.crewTitles.end(),
                                [&](const auto& t) { return t.id == person.id; });
    const bool exists =
        std::any_of(offers.begin(), offers.end(), [&](const auto& p) { return p.id == person.id; });
    q.status = s.catchingUp ? "ECONOMY CATCHING UP"
               : state.outageUntil > s.tick || state.health < 500 || state.condition < 300
                   ? "CREW OFFICE UNAVAILABLE"
               : person.health <= 0                                       ? "DECEASED / RELEASE AT PORT"
               : !titleSpace || s.player.crewRevision >= 1000000000000ULL ? "CREW REGISTRY FULL"
               : buy && !exists                                           ? "OFFER NO LONGER AVAILABLE"
               : buy && crewOwned(s.player, person.id)                    ? "ALREADY ABOARD"
               : buy && aboard >= maxShipCrew                             ? "ALL EIGHT BERTHS FILLED"
               : buy && s.player.credits < q.buyCents                     ? "INSUFFICIENT CREDITS"
               : !buy && !crewOwned(s.player, person.id)                  ? "CREW NO LONGER OWNED"
               : !buy && offers.size() >= maxPortCrew                     ? "PORT CREW BERTHS FULL"
               : !buy && state.cash < q.sellCents                         ? "PORT CANNOT FUND CREW PURCHASE"
                                                                          : "READY";
    q.available = q.status == "READY";
    if (q.available && ((buy && state.cash > 1000000000000000LL - q.buyCents) ||
                        (!buy && s.player.credits > 1000000000000000LL - q.sellCents))) {
        q.available = false;
        q.status = "BALANCE LIMIT / CREW TRANSFER PAUSED";
    }
    return q;
}
std::vector<CrewTradeRow> economicCrewRows(const EconomySnapshot& s, size_t site,
                                           std::span<const CrewMember> aboard) {
    std::vector<CrewTradeRow> rows;
    size_t count = std::count_if(aboard.begin(), aboard.end(), [](const auto& m) { return m.health > 0; }) +
                   s.player.rescued.size();
    for (const auto& m : economicCrewOffers(s, site))
        rows.push_back({m, true, economicCrewQuote(s, site, m, true, count)});
    for (const auto& m : aboard)
        rows.push_back({m, false, economicCrewQuote(s, site, m, false, count)});
    return rows;
}
std::string Economy::crewTrade(const EconomicCommand& c, EconomyPlayer& player, size_t site) {
    const bool buy = c.action == EconomicAction::BuyCrew, release = c.action == EconomicAction::ReleaseCrew;
    EconomySnapshot s;
    s.network = network_;
    s.sites = sites_;
    s.player = player;
    s.tick = tick_;
    s.crewMarkets = crewMarkets_;
    const auto offers = economicCrewOffers(s, site);
    CrewMember person;
    const CrewMember* lost = nullptr;
    if (buy) {
        auto m = std::find_if(offers.begin(), offers.end(), [&](const auto& m) { return m.id == c.crewId; });
        if (m == offers.end())
            return "OFFER NO LONGER AVAILABLE";
        person = *m;
        // Buying can fill a casualty's berth. Retire that person's durable
        // boarding receipt together with the purchase, even if its ship slot
        // would otherwise have been overwritten before a later release.
        if (c.person) {
            const auto& dead = *c.person;
            if (!validPerson(dead) || dead.health != 0 || dead.id == person.id || !crewOwned(player, dead.id))
                return "INVALID LOST CREW REPLACEMENT";
            const auto known = std::find_if(player.crewBoarding.begin(), player.crewBoarding.end(),
                                            [&](const auto& b) { return b.member.id == dead.id; });
            if (known != player.crewBoarding.end() &&
                (known->member.name != dead.name || known->member.trait != dead.trait))
                return "CREW IDENTITY MISMATCH";
            lost = &dead;
        }
    } else {
        if (!c.person || c.person->id != c.crewId || !validPerson(*c.person))
            return "INVALID CREW TRANSFER";
        person = *c.person;
    }
    auto q = economicCrewQuote(s, site, person, buy, c.crewAboard);
    if (release) {
        if (!crewOwned(player, person.id))
            return "CREW NO LONGER OWNED";
        if (sites_[site].outageUntil > tick_ || sites_[site].health < 500 || sites_[site].condition < 300)
            return "CREW OFFICE UNAVAILABLE";
        if (player.crewRevision >= 1000000000000ULL)
            return "CREW RECEIPT LIMIT";
    } else {
        if (!q.available)
            return q.status;
        if (buy ? q.buyCents > c.limitCents : q.sellCents < c.limitCents)
            return "CREW QUOTE CHANGED / REVIEW PRICE";
    }
    auto title = std::find_if(player.crewTitles.begin(), player.crewTitles.end(),
                              [&](const auto& t) { return t.id == person.id; });
    const bool lostNeedsTitle = lost && std::none_of(player.crewTitles.begin(), player.crewTitles.end(),
                                                     [&](const auto& t) { return t.id == lost->id; });
    if (player.crewTitles.size() + size_t(title == player.crewTitles.end()) + size_t(lostNeedsTitle) >
        maxCrewTitles)
        return "CREW REGISTRY FULL";
    const bool retiresBoarding = lost && std::any_of(player.crewBoarding.begin(), player.crewBoarding.end(),
                                                     [&](const auto& b) { return b.member.id == lost->id; });
    if (buy && player.crewBoarding.size() >= maxShipCrew && !retiresBoarding)
        return "RELEASE LOST CREW FIRST";
    if (!buy) {
        auto known = std::find_if(player.crewBoarding.begin(), player.crewBoarding.end(),
                                  [&](const auto& b) { return b.member.id == person.id; });
        if (known != player.crewBoarding.end() &&
            (known->member.name != person.name || known->member.trait != person.trait))
            return "CREW IDENTITY MISMATCH";
    }
    auto market =
        std::find_if(crewMarkets_.begin(), crewMarkets_.end(), [&](const auto& p) { return p.site == site; });
    if (!release && market == crewMarkets_.end()) {
        if (crewMarkets_.size() >= maxCrewMarkets)
            return "SYSTEM CREW MARKETS FULL";
        crewMarkets_.push_back({unsigned(site), offers});
        market = std::prev(crewMarkets_.end());
    }
    if (title == player.crewTitles.end()) {
        player.crewTitles.push_back({person.id, buy});
    } else
        title->aboard = buy;
    if (lost) {
        const auto old = std::find_if(player.crewTitles.begin(), player.crewTitles.end(),
                                      [&](const auto& t) { return t.id == lost->id; });
        if (old == player.crewTitles.end())
            player.crewTitles.push_back({lost->id, false});
        else
            old->aboard = false;
        std::erase_if(player.crewBoarding, [&](const auto& b) { return b.member.id == lost->id; });
    }
    person.duty = person.room = person.target = person.walkTo = CrewStation::Bunks;
    person.weaponMount = noWeaponMount;
    person.order = CrewOrder::Duty;
    person.travel = person.travelDuration = 0;
    person.automatic = false;
    ++player.crewRevision;
    std::erase_if(player.crewBoarding, [&](const auto& b) { return b.member.id == person.id; });
    if (buy) {
        std::erase_if(market->offers, [&](const auto& m) { return m.id == person.id; });
        player.crewBoarding.push_back({player.crewRevision, person});
        player.credits -= q.buyCents;
        sites_[site].cash += q.buyCents;
        externalMoneyDelta_ += q.buyCents;
    } else if (!release) {
        market->offers.push_back(person);
        player.credits += q.sellCents;
        sites_[site].cash -= q.sellCents;
        externalMoneyDelta_ -= q.sellCents;
    }
    const std::string action = buy ? "CREW BOUGHT" : release ? "CREW RELEASED" : "CREW SOLD";
    event(network_->sites[site].name + " / " + action + " / " + person.name);
    return action + " / " + person.name +
           (release ? " / NO PAYMENT" : " / " + crewMoney(buy ? q.buyCents : q.sellCents) + " CR");
}
void Economy::writeCrewMarkets(std::ostream& out) const {
    out << "CREW_MARKETS " << crewMarkets_.size() << '\n';
    for (const auto& p : crewMarkets_) {
        out << p.site << ' ' << p.offers.size() << '\n';
        for (const auto& m : p.offers) {
            writeCrewPerson(out, m);
            out << '\n';
        }
    }
}
bool Economy::readCrewMarkets(std::istream& in) {
    std::string tag;
    size_t count;
    if (!(in >> tag >> count) || tag != "CREW_MARKETS" || count > maxCrewMarkets)
        return false;
    std::set<unsigned> sites;
    std::set<uint64_t> ids;
    for (size_t i = 0; i < count; ++i) {
        PortCrewMarket p;
        size_t people;
        if (!(in >> p.site >> people) || p.site >= sites_.size() || people > maxPortCrew ||
            !sites.insert(p.site).second)
            return false;
        for (size_t j = 0; j < people; ++j) {
            CrewMember m;
            if (!readCrewPerson(in, m) || m.health <= 0 || !ids.insert(m.id).second)
                return false;
            p.offers.push_back(std::move(m));
        }
        crewMarkets_.push_back(std::move(p));
    }
    return true;
}
} // namespace infinite
