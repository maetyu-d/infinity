#include "infinity/Economy.hpp"
#include <algorithm>
#include <numeric>
namespace infinity {
std::vector<size_t> explorationJobs(const EconomySnapshot& s) {
    std::vector<size_t> out;
    if (!s.network)
        return out;
    for (size_t i = 0; i < s.exploration.contracts.size(); ++i)
        if (surveySystem(s.exploration.contracts[i].target) == std::optional<Address>(s.network->system))
            out.push_back(i);
    return out;
}
void explorationMarketView(MarketView& v, const EconomySnapshot& s, bool journal, size_t selected,
                           unsigned mount, size_t index) {
    const auto& x = s.exploration;
    v.ready = true;
    v.weaponMount = std::min(mount, maxWeaponMounts - 1);
    v.summary = "SURVEYS " + std::to_string(x.journal.size()) + " / DATA EARNED " +
                std::to_string(x.surveyEarnings / 100) + " CR / CONTRACTS " +
                std::to_string(x.surveysDelivered) + " / MINED " + std::to_string(x.minedKg) + " KG";
    if (journal) {
        v.role = "EXPLORATION JOURNAL";
        v.site = "DISCOVERIES / " + std::to_string(x.journal.size());
        v.selected = x.journal.empty() ? 0 : std::min(selected, x.journal.size() - 1);
        v.bottleneck =
            "UP-DOWN HISTORY / F4 VISIT / ENTER SELL DATA AT PORT / F12 RESURVEY FOR BETTER PRECISION";
        if (x.journal.empty()) {
            v.detail = {"NO SURVEYS RECORDED", "FACE A PLANET AND HOLD F12",
                        "ON SOLID GROUND, F12 SURVEYS THE LOCAL SURFACE PROSPECT",
                        "SHRINK WITH Z TO SURVEY ROCK, MINERAL AND ATOMIC STRUCTURE"};
            return;
        }
        const auto& r = x.journal[x.journal.size() - 1 - v.selected];
        v.detail = {r.name + " / GRADE " + std::to_string(r.grade) + " / " + std::to_string(v.selected + 1) +
                        " OF " + std::to_string(x.journal.size()),
                    r.formation,
                    r.mineral,
                    r.conditions,
                    r.composition,
                    "ADDRESS " + r.address.str(),
                    r.paidGrade >= r.grade ? "THIS DATA GRADE ALREADY SOLD"
                                           : "UNSOLD DATA / CONNECT TO A PORT AND PRESS ENTER"};
        if (r.prospect) {
            auto remaining = r.resources;
            const auto p = std::find_if(x.prospects.begin(), x.prospects.end(),
                                        [&](const auto& p) { return p.address == *r.prospect; });
            if (p != x.prospects.end())
                for (size_t g = 0; g < 4; ++g)
                    remaining[g] -= p->extracted[g];
            v.detail.push_back("RECOVERABLE KG: ICE " + std::to_string(remaining[0]) + " / ORE " +
                               std::to_string(remaining[1]) + " / SILICATE " + std::to_string(remaining[2]) +
                               " / CARBON " + std::to_string(remaining[3]));
            v.detail.push_back(
                "PROSPECT REMAINING " +
                std::to_string(std::accumulate(remaining.begin(), remaining.end(), int64_t(0))) +
                " KG / SHARED ACROSS FINER SCALES");
            for (const auto& seam : x.seams)
                if (seam.pool == *r.prospect &&
                    (seam.source.ancestorOf(r.address) || r.address.kind() <= Kind::Surface)) {
                    v.detail.push_back(std::string(seam.marked ? "MARKED " : "SEAM EXPEDITION / ") +
                                       (seam.material ? std::string(resourceName(seam.resource)) : "UNMAPPED") +
                                       " / RECOVERED " + std::to_string(seam.minedKg) + " KG");
                    v.detail.push_back("SHIFT-J ENTRY BEACON / SHIFT-Z FULL SIZE / CTRL-SPACE MINE MARK");
                    break;
                }
        }
        return;
    }
    const auto jobs = explorationJobs(s);
    v.selected = std::min(selected, size_t(5) + jobs.size());
    v.bottleneck =
        "F3 EXPEDITION PLANNER / LEFT-RIGHT MOUNT / ENTER EQUIP OR ACCEPT-CLAIM / BACKSPACE CANCEL / F4 TARGET";
    const auto scanner = economicExplorationQuote(s, index, false, v.weaponMount);
    const auto mining = economicExplorationQuote(s, index, true, v.weaponMount);
    const auto sale = economicExplorationQuote(s, index, true, v.weaponMount, true);
    std::vector<std::pair<std::string, std::string>> rows;
    rows.push_back({"SCANNER " + std::to_string(x.scannerLevel) + "/3 / UPGRADE " +
                        std::to_string(scanner.totalCents() / 100) + " CR",
                    scanner.status});
    rows.push_back({std::string(weaponMountName(v.weaponMount)) + " / MINING HEAD " +
                        std::to_string(x.miningLevel) + "/3 / FIT-UPGRADE " +
                        std::to_string(mining.totalCents() / 100) + " CR",
                    mining.status});
    rows.push_back({"SELL MINING HEAD / " + std::to_string(sale.sellCents / 100) + " CR", sale.status});
    for (auto i : jobs) {
        const auto& j = x.contracts[i];
        const auto phase = j.phase == SurveyPhase::Open       ? "OFFER"
                           : j.phase == SurveyPhase::Accepted ? "ACCEPTED"
                           : j.phase == SurveyPhase::Complete ? "COMPLETE"
                                                              : "CANCELLED";
        rows.push_back({std::string(phase) + " / SURVEY " + j.name + " / GRADE " + std::to_string(j.grade) +
                            " / " + std::to_string(j.reward / 100) + " CR",
                        j.supplyKg ? "F3 PLAN / FORECAST / DRILL THE RESOURCE REGION / SHRINK / MARK / MINE / DELIVER "+std::to_string(j.supplyKg)+" KG "+std::string(resourceName(j.supplyResource))
                        : j.cavern ? "SHRINK / CLOSE ASSAY / MARK / FULL-SIZE EXTRACT / DELIVER 10 KG TO ISSUER"
                        : j.phase == SurveyPhase::Accepted
                            ? "F4 TARGET / NEW OBSERVATION REQUIRED / RETURN TO ISSUER TO CLAIM"
                            : "REWARD RESERVED FROM ISSUER ON ACCEPTANCE"});
    }
    const auto probeFit = economicProbeQuote(s,index,v.weaponMount,ProbeService::Fit);
    const auto probeReload = economicProbeQuote(s,index,v.weaponMount,ProbeService::Reload);
    const auto probeRemove = economicProbeQuote(s,index,v.weaponMount,ProbeService::Remove);
    rows.push_back({std::string(weaponMountName(v.weaponMount))+" / FIT MINING PROBE / "+
                    std::to_string(probeFit.totalCents()/100)+" CR",probeFit.status});
    rows.push_back({"RELOAD PROBES / "+std::to_string(x.probes.rounds[v.weaponMount])+"/6 / "+
                    std::to_string(probeReload.totalCents()/100)+" CR",probeReload.status});
    rows.push_back({"SELL PROBE LAUNCHER / "+std::to_string(probeRemove.sellCents/100)+" CR",probeRemove.status});
    if (v.selected >= 3+jobs.size())
        v.bottleneck="LEFT-RIGHT BAY / ENTER REFIT / CTRL-P LAUNCH / ALT-P CAMERA / F4 BORE ENTRY";
    const auto first = v.selected >= 8 ? v.selected - 7 : 0;
    for (size_t i = first; i < std::min(rows.size(), first + 8); ++i) {
        v.detail.push_back(std::string(i == v.selected ? "> " : "  ") + rows[i].first);
        v.detail.push_back(rows[i].second);
    }
}
} // namespace infinity
