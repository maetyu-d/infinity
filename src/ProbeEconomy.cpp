#include "infinity/Economy.hpp"
#include <numeric>
namespace infinity {
namespace {
Stock launcherParts() {
    Stock x{}; x[size_t(Good::Alloy)] = 14; x[size_t(Good::Electronics)] = 3;
    x[size_t(Good::Machinery)] = 3; x[size_t(Good::Tools)] = 2; return x;
}
Stock roundParts(unsigned n) {
    Stock x{}; x[size_t(Good::Alloy)] = 2*n; x[size_t(Good::Electronics)] = n;
    x[size_t(Good::Tools)] = n; return x;
}
}
WeaponQuote economicProbeQuote(const EconomySnapshot& s, size_t site, unsigned mount, ProbeService action) {
    WeaponQuote q;
    if (!s.network || site >= s.sites.size() || mount >= maxWeaponMounts) {
        q.status = "PROBE / CONNECT TO AN EQUIPMENT PORT"; return q;
    }
    const auto& p = s.exploration.probes; const auto& port = s.sites[site];
    if ((action == ProbeService::Fit) == p.launchers[mount]) {
        q.status = p.launchers[mount] ? "LAUNCHER ALREADY FITTED" : "NO PROBE LAUNCHER AT THIS BAY"; return q;
    }
    if (s.exploration.miningLevel && s.exploration.miningMount == mount) {
        q.status = "SELL THE MINING HEAD BEFORE REFITTING THIS BAY"; return q;
    }
    if (action == ProbeService::Reload && p.rounds[mount] == maxProbeRounds) {
        q.status = "PROBE MAGAZINE FULL / SIX ROUNDS"; return q;
    }
    q.materials = action == ProbeService::Reload
                      ? roundParts(std::min(3u, maxProbeRounds-p.rounds[mount])) : launcherParts();
    if (action != ProbeService::Reload) {
        const auto ammo = roundParts(action == ProbeService::Fit ? 1 : p.rounds[mount]);
        for (size_t g=0; g<goodCount; ++g) q.materials[g] += ammo[g];
    }
    if (action == ProbeService::Fit) {
        const auto old = weaponMaterials(s.player.weaponAt(mount));
        for (size_t g=0; g<goodCount; ++g) q.materials[g] -= old[g];
        q.labourCents = 18000;
    } else if (action == ProbeService::Reload) q.labourCents = 2000;
    bool stock = true; int64_t after = std::accumulate(port.stock.begin(), port.stock.end(), int64_t(0));
    for (size_t g=0; g<goodCount; ++g) if (q.materials[g]) {
        const auto price = s.quote(site, Good(g), std::abs(q.materials[g]));
        if (action == ProbeService::Remove) {
            q.sellCents += price.sellCents; after += q.materials[g];
        } else {
            stock &= q.materials[g] <= 0 || price.available >= q.materials[g];
            q.partsCents += q.materials[g] > 0 ? price.buyCents : -price.sellCents;
            after -= q.materials[g];
        }
    }
    const bool crew = port.outageUntil <= s.tick && port.health >= 500 && port.condition >= 300;
    const bool space = after <= economicCapacity(s.network->sites[site], port);
    const bool money = action == ProbeService::Remove ? port.cash >= q.sellCents
                     : s.player.credits >= q.totalCents() && port.cash + q.partsCents >= 0;
    q.available = crew && stock && space && money;
    q.status = !crew ? "EQUIPMENT CREW UNAVAILABLE" : !stock ? "PORT NEEDS PROBE PARTS"
             : !space ? "PORT WAREHOUSE FULL" : !money ? "INSUFFICIENT PLAYER OR PORT FUNDS"
             : action == ProbeService::Fit ? "REFIT EMPTY BAY OR TRADE IN TURRET / ONE PROBE INCLUDED"
             : action == ProbeService::Reload ? "LOAD UP TO THREE PROBES / SIX ROUND CAPACITY"
                                              : "RETURN LAUNCHER AND UNUSED PROBES TO PORT";
    return q;
}
std::string ExplorationLedger::executeProbe(const Universe& u, Economy& e, EconomyPlayer& player,
                                           const EconomicCommand& c) {
    const auto m = c.weaponMount;
    if (m >= maxWeaponMounts) return "INVALID PROBE BAY";
    auto& probes = state_.probes;
    if (c.action == EconomicAction::LaunchProbe) {
        if (!probes.launchers[m] || !probes.rounds[m]) return "PROBE BAY EMPTY / RELOAD AT PORT";
        if (!c.surveyTarget || c.surveyTarget->kind() != Kind::Planet ||
            c.surveyTarget->parent() != e.network_->system || !c.observer || !c.probeRay)
            return "SELECT A SOLID PLANET OR MOON FOR THE PROBE";
        const auto& o = *c.observer; const auto& ray = *c.probeRay;
        if (!std::isfinite(o.size) || o.size < .99 || o.size > 1 || o.field || o.landed ||
            !std::isfinite(o.hull) || o.hull <= 0 || !std::isfinite(glm::length(o.position)) ||
            ray.reference != o.reference || glm::length(ray.origin-o.position) > 25 ||
            ray.seconds != o.seconds || c.serviceUnits < 25 || c.serviceUnits > 150)
            return "PROBE / FULL SIZE / FLIGHT / DRIVE OFF / STAFF THE LAUNCH BAY";
        if (probes.holes.size() >= maxBoreholes) return "EXCAVATION REGISTER FULL / EXISTING CAVERNS RETAINED";
        for (const auto& b : probes.holes)
            if (probeActive(b,o.seconds)) return "PROBE ALREADY ACTIVE / ALT-P CAMERA";
        auto plan = planMiningProbe(u, *c.surveyTarget, ray, m,
                        mix64(seedOf(*c.surveyTarget,u.seed()) ^ mix64(c.sequence) ^ 0x50524f424558ULL));
        if (!plan.hole) return plan.status;
        if(c.probeValue) {
            std::string status;
            if(!controlProbe(*plan.hole,ProbeControl::Depth,c.probeValue,ray.seconds,status))return "PROBE DEPTH MUST BE 40 TO 120 M";
        }
        {std::string status;
            if(!controlProbe(*plan.hole,ProbeControl::Rate,c.probeRate,ray.seconds,status))return "PROBE FEED MUST BE 35 TO 150 PERCENT";}
        for (const auto& old:probes.holes)
            if (old.planet==plan.hole->planet) {
                const auto a=old.entry-glm::normalize(old.entry)*old.finalDepth();
                const auto b=plan.hole->entry-glm::normalize(plan.hole->entry)*plan.hole->finalDepth();
                if(glm::length(a-b)<old.roomRadius()+plan.hole->roomRadius()+5 ||
                   glm::length(old.entry-plan.hole->entry)<old.shaftRadius+plan.hole->shaftRadius+5)
                    return "EXISTING EXCAVATION NEARBY / SELECT CLEAR LAND / PROBE RETAINED";
            }
        if (std::any_of(probes.holes.begin(), probes.holes.end(), [&](const auto& h){return h.id==plan.hole->id;}))
            return "PROBE ALREADY REGISTERED";
        --probes.rounds[m]; probes.holes.push_back(*plan.hole);
        return plan.status;
    }
    auto site = e.siteIndex(c.port); if (!site) return "CONNECT TO AN EQUIPMENT PORT";
    EconomySnapshot snapshot; snapshot.network=e.network_; snapshot.sites=e.sites_; snapshot.tick=e.tick_;
    snapshot.player=player; snapshot.exploration=state_;
    const auto action = c.action == EconomicAction::FitProbeLauncher ? ProbeService::Fit
                      : c.action == EconomicAction::ReloadProbe ? ProbeService::Reload : ProbeService::Remove;
    const auto q = economicProbeQuote(snapshot,*site,m,action);
    if (!q.available) return q.status;
    if (action == ProbeService::Remove ? c.limitCents > q.sellCents : c.limitCents < q.totalCents())
        return "PROBE QUOTE CHANGED / REVIEW PRICE";
    auto& port = e.sites_[*site];
    const bool sell = action == ProbeService::Remove;
    for (size_t g=0; g<goodCount; ++g) {
        const auto change = (sell ? 1 : -1)*q.materials[g]; port.stock[g] += change;
        if (change >= 0) port.imported[g] += change; else port.exported[g] -= change;
    }
    if (sell) {
        port.cash -= q.sellCents; player.credits += q.sellCents; e.externalMoneyDelta_ -= q.sellCents;
        probes.launchers[m]=false; probes.rounds[m]=0;
        return "PROBE LAUNCHER SOLD / BAY EMPTY / EXISTING CAVERNS AND BEACONS RETAINED";
    }
    player.credits -= q.totalCents(); e.externalMoneyDelta_ += q.totalCents(); port.cash += q.partsCents;
    (e.network_->sites[*site].orbital ? e.carrierCash_ : port.householdCash) += q.labourCents;
    if (action == ProbeService::Fit) {
        const bool traded=player.weaponAt(m)>0;
        (m == 0 ? player.weaponLevel : player.extraWeapons[m-1])=0;
        probes.launchers[m]=true; probes.rounds[m]=1;
        return std::string("PROBE LAUNCHER FITTED / ")+(traded?"TURRET TRADED IN":"EMPTY BAY REFITTED")+
               " / CTRL-P LAUNCH / ALT-P FEED";
    }
    probes.rounds[m] += std::min(3u,maxProbeRounds-probes.rounds[m]);
    return "PROBES RELOADED / " + std::to_string(probes.rounds[m]) + " IN BAY / CTRL-P LAUNCH";
}
} // namespace infinity
