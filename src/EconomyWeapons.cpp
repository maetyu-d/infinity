#include "infinite/Economy.hpp"
#include <algorithm>
#include <numeric>
namespace infinite {
Stock weaponMaterials(unsigned level) {
    Stock s{};
    if (level > maxWeaponLevel)
        return s;
    s[size_t(Good::Alloy)] = 8 * level;
    s[size_t(Good::Silicon)] = level;
    s[size_t(Good::Electronics)] = 2 * level;
    s[size_t(Good::Machinery)] = level;
    return s;
}
WeaponQuote economicWeaponQuote(const EconomySnapshot& s, size_t i, unsigned level, bool sell) {
    WeaponQuote q;
    if (!s.network || i >= s.sites.size() || !level || level > maxWeaponLevel) {
        q.status = "INVALID WEAPON GRADE";
        return q;
    }
    const auto& site = s.sites[i];
    const auto current = s.player.weaponLevel;
    if (sell && (!current || current != level)) {
        q.status = "NO MATCHING WEAPON FITTED";
        return q;
    }
    if (!sell && level <= current) {
        q.status = level == current ? "ALREADY FITTED" : "SELL FITTED WEAPON BEFORE DOWNGRADING";
        return q;
    }
    q.materials = weaponMaterials(level);
    if (!sell) {
        const auto installed = weaponMaterials(current);
        for (size_t g = 0; g < goodCount; ++g)
            q.materials[g] -= installed[g];
        q.labourCents = 10000 * level;
    }
    bool stock = true;
    int64_t incoming = 0;
    for (size_t g = 0; g < goodCount; ++g)
        if (q.materials[g]) {
            auto price = s.quote(i, Good(g), q.materials[g]);
            q.partsCents += price.buyCents;
            q.sellCents += price.sellCents;
            stock &= price.available >= q.materials[g];
            incoming += q.materials[g];
        }
    const bool space = std::accumulate(site.stock.begin(), site.stock.end(), int64_t(0)) + incoming <=
                       economicCapacity(s.network->sites[i], site);
    const bool crew = site.outageUntil <= s.tick && site.health >= 500 && site.condition >= 300;
    q.available =
        crew && (sell ? space && site.cash >= q.sellCents : stock && s.player.credits >= q.totalCents());
    q.status = !crew                             ? "ARMOURER UNAVAILABLE / PORT CONDITION OR OUTAGE"
               : sell && !space                  ? "PORT WAREHOUSE FULL"
               : sell && site.cash < q.sellCents ? "PORT CANNOT FUND WEAPON BUYBACK"
               : !sell && !stock                 ? "PORT NEEDS UNRESERVED WEAPON PARTS"
               : !sell && s.player.credits < q.totalCents() ? "INSUFFICIENT CREDITS"
               : sell                                       ? "SELL FITTED LASER / HARDWARE RETURNED TO PORT"
                                                            : "READY / PARTS AND INSTALLATION INCLUDED";
    return q;
}
std::string Economy::weaponTrade(const EconomicCommand& c, EconomyPlayer& player, size_t i) {
    EconomySnapshot snapshot;
    snapshot.network = network_;
    snapshot.sites = sites_;
    snapshot.tick = tick_;
    snapshot.player = player;
    const bool sell = c.action == EconomicAction::SellWeapon;
    const auto q = economicWeaponQuote(snapshot, i, c.serviceUnits, sell);
    if (!q.available)
        return q.status;
    if (sell ? q.sellCents < c.limitCents : q.totalCents() > c.limitCents)
        return "WEAPON QUOTE CHANGED / REVIEW PRICE";
    auto& site = sites_[i];
    for (size_t g = 0; g < goodCount; ++g) {
        site.stock[g] += (sell ? 1 : -1) * q.materials[g];
        (sell ? site.imported[g] : site.exported[g]) += q.materials[g];
    }
    if (sell) {
        site.cash -= q.sellCents;
        player.credits += q.sellCents;
        externalMoneyDelta_ -= q.sellCents;
        player.weaponLevel = 0;
        event(network_->sites[i].name + " / PLAYER WEAPON BUYBACK");
        return "WEAPON SOLD / " + std::to_string(q.sellCents / 100) + " CR / MOUNT EMPTY";
    }
    player.credits -= q.totalCents();
    externalMoneyDelta_ += q.totalCents();
    site.cash += q.partsCents;
    if (network_->sites[i].orbital)
        carrierCash_ += q.labourCents;
    else
        site.householdCash += q.labourCents;
    player.weaponLevel = c.serviceUnits;
    event(network_->sites[i].name + " / FITTED " + std::string(weaponSpec(player.weaponLevel).name));
    return "WEAPON FITTED / " + std::string(weaponSpec(player.weaponLevel).name) + " / SPACE FIRE";
}
std::string Economy::disableVessel(const EconomicCommand& c) {
    auto f = std::find_if(freight_.begin(), freight_.end(), [&](const auto& f) { return f.id == c.port; });
    if (f == freight_.end())
        return "VESSEL NOT IN THIS ECONOMIC NETWORK";
    if (f->disabledAt >= 0)
        return "VESSEL ALREADY DISABLED";
    if (!std::isfinite(c.impactSeconds) || c.impactSeconds < 0 || c.impactSeconds > 1e12 ||
        c.impactSeconds > (tick_ + 2) * economyTickSeconds)
        return "INVALID IMPACT TIME";
    if (f->phase == FreightPhase::Reposition || f->phase == FreightPhase::Loading) {
        sites_[f->source].reserved[size_t(f->good)] -= f->kg;
        f->kg = 0;
    }
    sites_[f->destination].cash += f->payment + f->fee;
    f->payment = f->fee = 0;
    f->disabledAt = c.impactSeconds;
    event("FREIGHTER DISABLED / onboard cargo retained / uncollected orders refunded");
    return "FREIGHTER DISABLED / CARGO REMAINS ABOARD THE WRECK";
}
} // namespace infinite
