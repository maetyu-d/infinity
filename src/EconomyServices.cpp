#include "infinite/Economy.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace infinite {
namespace {
template <class Price>
ShipServiceQuote quoteService(const EconomicSiteState& site, const EconomyPlayer& player, uint64_t tick,
                              ShipService service, unsigned units, Price price) {
    ShipServiceQuote q;
    q.units = units;
    auto add = [&](Good good, int64_t kg) { q.materials[size_t(good)] = kg; };
    if (service == ShipService::Repair) {
        if (!units || units > 10) {
            q.status = units ? "INVALID REPAIR BATCH / MAXIMUM 10 POINTS" : "HULL FULL / NO REPAIR REQUIRED";
            return q;
        }
        if (player.repairPoints > 1000000000000ULL - units) {
            q.status = "REPAIR RECEIPT LIMIT REACHED";
            return q;
        }
        add(Good::Alloy, units * 2);
        add(Good::Machinery, (units + 4) / 5);
        add(Good::Tools, (units + 9) / 10);
        q.labourCents = units * 300;
    } else if (service == ShipService::CargoHold) {
        if (player.cargoLevel >= maxCargoLevel) {
            q.status = "CARGO HOLD AT MAXIMUM LEVEL";
            return q;
        }
        if (units != player.cargoLevel + 1) {
            q.status = "CARGO REFIT CHANGED / REVIEW NEXT LEVEL";
            return q;
        }
        add(Good::Alloy, 80 * units);
        add(Good::Polymers, 20 * units);
        add(Good::Machinery, 10 * units);
        add(Good::Tools, 5 * units);
        q.labourCents = 100000 * units;
    } else if (service == ShipService::CrewHire || service == ShipService::CrewSupplies ||
               service == ShipService::CrewCare) {
        const auto receipts = service == ShipService::CrewHire       ? player.crew.hired
                              : service == ShipService::CrewSupplies ? player.crew.supplied
                                                                     : player.crew.treated;
        if (units != 1 || receipts >= 1000000) {
            q.status = "INVALID CREW SERVICE OR RECEIPT LIMIT";
            return q;
        }
        if (service == ShipService::CrewHire) {
            add(Good::Food, 4);
            add(Good::Oxygen, 4);
            q.labourCents = 40000;
        } else if (service == ShipService::CrewSupplies) {
            add(Good::Tools, 2);
            add(Good::Machinery, 1);
            add(Good::Alloy, 2);
            add(Good::Medicine, 1);
            q.labourCents = 2500;
        } else {
            add(Good::Medicine, 5);
            add(Good::Food, 10);
            add(Good::Oxygen, 5);
            q.labourCents = 5000;
        }
    } else {
        q.status = "UNKNOWN SHIP SERVICE";
        return q;
    }
    bool stock = true;
    for (size_t g = 0; g < goodCount; g++) {
        if (!q.materials[g])
            continue;
        auto parts = price(Good(g), q.materials[g]);
        q.partsCents += parts.buyCents;
        stock &= parts.available >= q.materials[g];
    }
    q.status = site.outageUntil > tick || site.health < 500 || site.condition < 300
                   ? "SERVICE CREW UNAVAILABLE / PORT OUTAGE OR CONDITION"
               : !stock                          ? "PORT NEEDS UNRESERVED SPARE PARTS"
               : player.credits < q.totalCents() ? "INSUFFICIENT CREDITS"
                                                 : "READY / PORT PARTS INCLUDED IN PRICE";
    q.available = site.outageUntil <= tick && site.health >= 500 && site.condition >= 300 && stock &&
                  player.credits >= q.totalCents();
    return q;
}
} // namespace
ShipServiceQuote economicServiceQuote(const EconomySnapshot& s, size_t i, ShipService service,
                                      unsigned units) {
    if (!s.network || i >= s.sites.size() || i >= s.network->sites.size())
        return {};
    return quoteService(s.sites[i], s.player, s.tick, service, units,
                        [&](Good good, int64_t kg) { return s.quote(i, good, kg); });
}
std::string Economy::service(const EconomicCommand& c, EconomyPlayer& player, size_t i) {
    const bool repair = c.action == EconomicAction::RepairHull;
    const auto kind = repair                                     ? ShipService::Repair
                      : c.action == EconomicAction::UpgradeCargo ? ShipService::CargoHold
                      : c.action == EconomicAction::HireCrew     ? ShipService::CrewHire
                      : c.action == EconomicAction::SupplyCrew   ? ShipService::CrewSupplies
                                                                 : ShipService::CrewCare;
    auto q = quoteService(sites_[i], player, tick_, kind, c.serviceUnits,
                          [&](Good good, int64_t kg) { return quote(i, good, kg); });
    if (!q.available)
        return q.status;
    if (c.limitCents < q.totalCents())
        return "SERVICE QUOTE CHANGED / REVIEW TOTAL COST";
    auto& site = sites_[i];
    for (size_t g = 0; g < goodCount; g++) {
        site.stock[g] -= q.materials[g];
        // Repair parts are final maintenance consumption. Refit parts leave the
        // port as installed ship capital, represented by the persistent level.
        (repair || kind == ShipService::CrewHire || kind == ShipService::CrewCare ? site.consumed[g]
                                                                                  : site.exported[g]) +=
            q.materials[g];
    }
    player.credits -= q.totalCents();
    externalMoneyDelta_ += q.totalCents();
    site.cash += q.partsCents;
    if (network_->sites[i].orbital)
        carrierCash_ += q.labourCents; // existing orbital cooperative pays its crews
    else
        site.householdCash += q.labourCents;
    if (repair) {
        player.repairPoints += q.units;
        event(network_->sites[i].name + " / PAID HULL REPAIR / " + std::to_string(q.units) + " POINTS");
        std::ostringstream receipt;
        receipt << "HULL REPAIR PAID / +" << q.units << " POINTS / " << std::fixed << std::setprecision(2)
                << q.totalCents() / 100. << " CR";
        return receipt.str();
    }
    if (kind == ShipService::CrewHire || kind == ShipService::CrewSupplies || kind == ShipService::CrewCare) {
        auto& receipt = kind == ShipService::CrewHire       ? player.crew.hired
                        : kind == ShipService::CrewSupplies ? player.crew.supplied
                                                            : player.crew.treated;
        ++receipt;
        const std::string message = kind == ShipService::CrewHire ? "CREW CONTRACT SIGNED / F8 ASSIGN RELIEF"
                                    : kind == ShipService::CrewSupplies
                                        ? "CREW STORES PAID / +10 SPARES / +5 MEDICAL"
                                        : "SHORE CLINIC PAID / SURVIVING CREW TREATED AND RESTED";
        event(network_->sites[i].name + " / " + message);
        return message;
    }
    player.cargoLevel = q.units;
    player.capacityKg = holdCapacityKg(q.units);
    player.capacityLitres = holdCapacityLitres(q.units);
    event(network_->sites[i].name + " / INSTALLED PLAYER CARGO REFIT / LEVEL " + std::to_string(q.units));
    return "CARGO REFIT INSTALLED / " + std::to_string(player.capacityKg) + " KG / " +
           std::to_string(player.capacityLitres) + " L";
}
} // namespace infinite
