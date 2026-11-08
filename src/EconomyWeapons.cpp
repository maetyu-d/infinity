#include "infinity/Economy.hpp"
#include <algorithm>
#include <numeric>
namespace infinity {
Stock weaponMaterials(unsigned level) {
    Stock s{};
    if (level > maxWeaponLevel)
        return s;
    if (level >= missileLauncher) {
        s[size_t(Good::Alloy)] = level == missileLauncher ? 18 : 10;
        s[size_t(Good::Silicon)] = 4;
        s[size_t(Good::Electronics)] = level == missileLauncher ? 6 : 9;
        s[size_t(Good::Machinery)] = 3;
        return s;
    }
    if (level == ionDisruptor) {
        s[size_t(Good::Alloy)] = 12;
        s[size_t(Good::Silicon)] = 3;
        s[size_t(Good::Electronics)] = 7;
        s[size_t(Good::Machinery)] = 2;
        return s;
    }
    s[size_t(Good::Alloy)] = 8 * level;
    s[size_t(Good::Silicon)] = level;
    s[size_t(Good::Electronics)] = 2 * level;
    s[size_t(Good::Machinery)] = level;
    return s;
}
WeaponQuote economicWeaponQuote(const EconomySnapshot& s, size_t i, unsigned level, bool sell,
                                unsigned mount) {
    WeaponQuote q;
    if (!s.network || i >= s.sites.size() || !level || level > maxWeaponLevel || mount >= maxWeaponMounts) {
        q.status = "INVALID WEAPON GRADE";
        return q;
    }
    if (!sell && level == pointDefence && mount == 0) {
        q.status = "POINT DEFENCE REQUIRES A CREW TURRET";
        return q;
    }
    const auto& site = s.sites[i];
    const auto current = s.player.weaponAt(mount);
    if (!sell && s.exploration.probes.launchers[mount]) {
        q.status = "PROBE LAUNCHER FITTED / REMOVE IN SURVEY SERVICES FIRST";
        return q;
    }
    if(!sell&&s.exploration.miningLevel&&s.exploration.miningMount==mount){
        q.status="MINING HEAD FITTED / F2 SURVEY TO SELL FIRST";return q;
    }
    if (sell && (!current || current != level)) {
        q.status = "NO MATCHING WEAPON FITTED";
        return q;
    }
    if (!sell && current && level != current && (level >= ionDisruptor || current >= ionDisruptor)) {
        q.status = "SELL CURRENT WEAPON BEFORE SWITCHING TYPE";
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
               : sell                                       ? "SELL FITTED WEAPON / HARDWARE RETURNED TO PORT"
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
    const auto q = economicWeaponQuote(snapshot, i, c.serviceUnits, sell, c.weaponMount);
    if (!q.available)
        return q.status;
    if (sell ? q.sellCents < c.limitCents : q.totalCents() > c.limitCents)
        return "WEAPON QUOTE CHANGED / REVIEW PRICE";
    auto& site = sites_[i];
    auto& fitted = c.weaponMount == 0 ? player.weaponLevel : player.extraWeapons[c.weaponMount - 1];
    for (size_t g = 0; g < goodCount; ++g) {
        site.stock[g] += (sell ? 1 : -1) * q.materials[g];
        (sell ? site.imported[g] : site.exported[g]) += q.materials[g];
    }
    if (sell) {
        site.cash -= q.sellCents;
        player.credits += q.sellCents;
        externalMoneyDelta_ -= q.sellCents;
        fitted = 0;
        event(network_->sites[i].name + " / PLAYER WEAPON BUYBACK");
        return "WEAPON SOLD / " + std::to_string(q.sellCents / 100) + " CR / " +
               std::string(weaponMountName(c.weaponMount)) + " EMPTY";
    }
    player.credits -= q.totalCents();
    externalMoneyDelta_ += q.totalCents();
    site.cash += q.partsCents;
    if (network_->sites[i].orbital)
        carrierCash_ += q.labourCents;
    else
        site.householdCash += q.labourCents;
    fitted = c.serviceUnits;
    event(network_->sites[i].name + " / " + std::string(weaponMountName(c.weaponMount)) + " FITTED " +
          std::string(weaponSpec(fitted).name));
    return "WEAPON FITTED / " + std::string(weaponMountName(c.weaponMount)) + " / " +
           std::string(weaponSpec(fitted).name) +
           (c.weaponMount ? " / ASSIGN GUNNER IN F9" : " / SPACE FIRE");
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
    openIncident(unsigned(f - freight_.begin()) + 1);
    event("FREIGHTER DISABLED / onboard cargo retained / uncollected orders refunded");
    return "FREIGHTER DISABLED / CARGO REMAINS ABOARD THE WRECK";
}
} // namespace infinity
