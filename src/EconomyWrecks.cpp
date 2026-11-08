#include "infinity/CombatActors.hpp"
#include "infinity/Economy.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
namespace infinity {
void Economy::expireDistressCalls() {
    for (auto& w : wrecks_) {
        if (w.rescueEscrow && tick_ > w.rescueDeadline) {
            sites_[w.accountingSite].cash += w.rescueEscrow;
            w.rescueEscrow = 0;
            event("DISTRESS CALL EXPIRED / PORT REWARD RELEASED");
        }
    }
}
std::string Economy::wreckCommand(const EconomicCommand& c, EconomyPlayer& p) {
    if (c.action == EconomicAction::DisembarkRescued) {
        const auto index = siteIndex(c.port);
        if (!index)
            return "RETURN SURVIVORS TO A PORT";
        if (p.rescued.empty())
            return "NO SURVIVORS ABOARD";
        const bool namedHandover = c.combatTarget && c.combatTarget->slot >= 100 && c.combatTarget->slot < 300;
        if (namedHandover && std::none_of(p.rescued.begin(), p.rescued.end(), [&](const auto& person) {
                return person.source == *c.combatTarget;
            }))
            return "NAMED SURVIVOR IS NOT ABOARD";
        auto& site = sites_[*index];
        if (site.outageUntil > tick_ || site.health < 500 || site.condition < 300)
            return "RESCUE OFFICE UNAVAILABLE";
        auto market = std::find_if(crewMarkets_.begin(), crewMarkets_.end(),
                                   [&](const auto& x) { return x.site == *index; });
        if (market == crewMarkets_.end()) {
            if (crewMarkets_.size() >= maxCrewMarkets)
                return "PORT CREW REGISTRY FULL";
            EconomySnapshot s;
            s.network = network_;
            s.sites = sites_;
            s.player = p;
            s.tick = tick_;
            crewMarkets_.push_back({unsigned(*index), economicCrewOffers(s, *index)});
            market = std::prev(crewMarkets_.end());
        }
        const auto before = economicVitals(network_->sites[*index], site, tick_);
        size_t count = 0;
        int64_t payment = 0;
        for (auto it = p.rescued.begin(); it != p.rescued.end() && market->offers.size() < maxPortCrew;) {
            if (namedHandover && it->source != *c.combatTarget) {
                ++it;
                continue;
            }
            auto wreck = std::find_if(wrecks_.begin(), wrecks_.end(), [&](const auto& w) { return w.id == it->source; });
            const bool funded = wreck != wrecks_.end() && wreck->rescueEscrow && wreck->accountingSite == *index;
            if (!funded && site.cash < rescueReward) {
                ++it;
                continue;
            }
            if (p.credits > 1000000000000000LL - payment - rescueReward) {
                if (!count)
                    return "BALANCE LIMIT / RESCUE PAYMENT RETAINED";
                break;
            }
            if (!funded)
                site.cash -= rescueReward;
            if (wreck != wrecks_.end() && wreck->rescueEscrow) {
                if (!funded)
                    sites_[wreck->accountingSite].cash += wreck->rescueEscrow;
                wreck->rescueEscrow = 0;
            }
            market->offers.push_back(it->person);
            it = p.rescued.erase(it);
            ++count;
            payment += rescueReward;
        }
        if (!count)
            return "PORT NEEDS RESCUE BERTHS OR REWARD FUNDS / PASSENGERS RETAINED";
        p.rescuesDelivered += count;
        p.credits += payment;
        externalMoneyDelta_ -= payment;
        recordOutcome(c, *index, OutcomeKind::Rescue, {}, payment, before);
        event("SURVIVORS DELIVERED / " + std::to_string(count) + " / PORT CARE / " +
              std::to_string(payment / 100) + "CR");
        return "SURVIVORS DISEMBARKED / " + std::to_string(count) + " / " + std::to_string(payment / 100) +
               "CR / CREW OFFICE";
    }
    if (!c.combatTarget || c.combatTarget->home.kind() != Kind::Planet ||
        c.combatTarget->home.parent() != network_->system)
        return "WRECK OUTSIDE THIS ECONOMIC SYSTEM";
    const auto id = *c.combatTarget;
    auto found = std::find_if(wrecks_.begin(), wrecks_.end(), [&](const auto& w) { return w.id == id; });
    if (c.action == EconomicAction::RegisterWreck) {
        if (found != wrecks_.end())
            return "WRECK ALREADY REGISTERED";
        if (wrecks_.size() >= 128 || sites_.empty())
            return "RECOVERY REGISTER FULL";
        if (!c.wreck || c.wreck->id != id || !std::isfinite(c.wreck->hull) || c.wreck->hull < 0 ||
            c.wreck->hull > 100 || (c.wreck->hull > 0 && c.wreck->systems[size_t(ShipSystem::Engines)] > 0) ||
            !std::isfinite(c.wreck->hitSeconds) || c.wreck->hitSeconds < 0 ||
            c.wreck->hitSeconds > (tick_ + 2) * economyTickSeconds)
            return "UNVERIFIED DISABLED SHIP";
        if (!isRaider(id) && !(id.slot >= 100 && id.slot < 404))
            return "SHIP HAS NO RECOVERABLE MANIFEST";
        WreckRecovery w;
        w.id = id;
        for (unsigned i = 0; i < network_->sites.size(); ++i)
            if (network_->sites[i].id.home == id.home) {
                w.accountingSite = i;
                break;
            }
        bool reserved = false;
        for (auto& job : combatContracts_)
            if (job.target == id && job.kind == CombatJobKind::Bounty && !job.physicalSalvage) {
                if (job.phase == CombatJobPhase::Claimable || job.phase == CombatJobPhase::Complete ||
                    job.phase == CombatJobPhase::Accepted)
                    reserved = true;
                else
                    job.physicalSalvage = true;
            }
        if (!reserved) {
            w.scrap[size_t(Good::Alloy)] = 20;
            w.scrap[size_t(Good::Electronics)] = 2;
        }
        const auto seed =
            mix64(seedOf(id.home, network_->universeSeed) ^ mix64(id.slot) ^ 0x5245534355453532ULL);
        if (c.wreck->systems[size_t(ShipSystem::LifeSupport)] > 0 && seed % 4 != 0) {
            auto m = ShipCrew(network_->universeSeed).recruit(seed % 1000000 + 1);
            m.id = (1ULL << 62) | (seed & ((1ULL << 62) - 1));
            m.health = 45 + double((seed >> 8) % 26);
            m.fatigue = 40;
            m.morale = 55;
            m.duty = m.room = m.target = m.walkTo = CrewStation::Bunks;
            m.weaponMount = noWeaponMount;
            w.survivor = m;
        }
        for (size_t g = 0; g < goodCount; ++g)
            sites_[w.accountingSite].imported[g] += w.scrap[g];
        auto freight =
            std::find_if(freight_.begin(), freight_.end(), [&](const auto& f) { return f.id == id; });
        if (freight != freight_.end() && freight->disabledAt < 0) {
            EconomicCommand disable = c;
            disable.port = id;
            disable.impactSeconds = c.wreck->hitSeconds;
            disableVessel(disable);
        }
        const auto& issuer = network_->sites[w.accountingSite];
        auto& funds = sites_[w.accountingSite];
        if (w.survivor && id.slot >= 100 && id.slot < 300 && issuer.orbital && funds.cash >= rescueReward &&
            funds.outageUntil <= tick_ && funds.health >= 500 && funds.condition >= 300) {
            w.rescueDeadline = tick_ + 360;
            w.rescueEscrow = rescueReward;
            funds.cash -= w.rescueEscrow;
            event(issuer.name + " / FUNDED CIVILIAN DISTRESS CALL");
        }
        wrecks_.push_back(w);
        event("FINITE WRECK MANIFEST REGISTERED / " + id.str());
        return "WRECK REGISTERED / HOLD 0 CLOSE AND SLOW";
    }
    if (c.action != EconomicAction::RecoverWreck || found == wrecks_.end())
        return "WRECK MANIFEST UNAVAILABLE";
    if (!c.combatPresent || !std::isfinite(c.targetDistance) || c.targetDistance < 0 ||
        c.targetDistance > 120 || !std::isfinite(c.relativeSpeed) || c.relativeSpeed < 0 ||
        c.relativeSpeed > 3)
        return "RECOVERY REQUIRES CLOSE, SLOW, FULL-SIZE FLIGHT";
    auto& w = *found;
    int64_t kg = 0;
    auto take = [&](Good good, int64_t& remaining, unsigned site) {
        const auto g = size_t(good);
        const auto amount = std::max<int64_t>(
            0, std::min({remaining, p.capacityKg - p.massKg(),
                         (p.capacityLitres - p.volumeLitres()) / goodInfo(good).litresPerKg}));
        remaining -= amount;
        p.cargo[g] += amount;
        sites_[site].exported[g] += amount;
        kg += amount;
    };
    auto freight = std::find_if(freight_.begin(), freight_.end(), [&](const auto& f) { return f.id == id; });
    if (freight != freight_.end() && freight->disabledAt >= 0 &&
        (freight->phase == FreightPhase::Transit || freight->phase == FreightPhase::Holding))
        take(freight->good, freight->kg, freight->source);
    for (size_t g = 0; g < goodCount; ++g)
        take(Good(g), w.scrap[g], w.accountingSite);
    bool rescued = false;
    if (w.survivor && !w.rescued && c.crewAboard + p.rescued.size() < maxShipCrew) {
        if (std::none_of(p.rescued.begin(), p.rescued.end(),
                         [&](const auto& x) { return x.person.id == w.survivor->id; })) {
            p.rescued.push_back({w.id, *w.survivor});
            w.rescued = true;
            rescued = true;
        }
    }
    p.recoveredKg += uint64_t(kg);
    if (!kg && !rescued)
        return w.survivor && !w.rescued ? "NO ROOM / FREE HOLD SPACE OR A CREW BERTH"
                                        : "WRECK EMPTY / NOTHING REMAINING";
    event("WRECK RECOVERY / " + std::to_string(kg) + "KG / " +
          (rescued ? "SURVIVOR ABOARD" : "CARGO TRANSFERRED"));
    return "RECOVERED " + std::to_string(kg) + "KG" +
           (rescued ? " / SURVIVOR ABOARD / F2 SERVICES" : " / CARGO IN HOLD");
}
void Economy::writeWrecks(std::ostream& out) const {
    out << "WRECK_RECOVERY " << wrecks_.size() << '\n';
    for (const auto& w : wrecks_) {
        out << std::quoted(w.id.str()) << ' ' << w.accountingSite << ' ' << w.rescued << ' '
            << bool(w.survivor);
        for (auto n : w.scrap)
            out << ' ' << n;
        if (w.survivor) {
            out << ' ';
            writeCrewPerson(out, *w.survivor);
        }
        out << ' ' << w.rescueDeadline << ' ' << w.rescueEscrow << '\n';
    }
}
bool Economy::readWrecks(std::istream& in, bool dynamicJobs) {
    std::string h, id;
    size_t count;
    if (!(in >> h >> count) || h != "WRECK_RECOVERY" || count > 128)
        return false;
    std::set<ActivityId> ids;
    std::set<uint64_t> people;
    for (size_t i = 0; i < count; ++i) {
        WreckRecovery w;
        unsigned rescued, person;
        if (!(in >> std::quoted(id) >> w.accountingSite >> rescued >> person) ||
            w.accountingSite >= sites_.size() || rescued > 1 || person > 1)
            return false;
        try {
            w.id = ActivityId::parse(id);
        } catch (...) {
            return false;
        }
        if (w.id.home.kind() != Kind::Planet || w.id.home.parent() != network_->system ||
            (!isRaider(w.id) && !(w.id.slot >= 100 && w.id.slot < 404)) || !ids.insert(w.id).second)
            return false;
        for (size_t g = 0; g < goodCount; ++g)
            if (!(in >> w.scrap[g]) || w.scrap[g] < 0 ||
                w.scrap[g] > (Good(g) == Good::Alloy         ? 20
                              : Good(g) == Good::Electronics ? 2
                                                             : 0))
                return false;
        w.rescued = rescued;
        if (person) {
            CrewMember m;
            if (!readCrewPerson(in, m) || m.health <= 0 || !people.insert(m.id).second)
                return false;
            w.survivor = m;
        }
        if (rescued && !person)
            return false;
        if (dynamicJobs && (!(in >> w.rescueDeadline >> w.rescueEscrow) || w.rescueDeadline > 1000000000000ULL ||
            (w.rescueDeadline && (!w.survivor || w.id.slot < 100 || w.id.slot >= 300 || !network_->sites[w.accountingSite].orbital)) ||
            (w.rescueEscrow && (w.rescueEscrow != rescueReward || !w.rescueDeadline || w.rescueDeadline < tick_))))
            return false;
        wrecks_.push_back(w);
    }
    return true;
}
} // namespace infinity
