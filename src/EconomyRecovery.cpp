#include "infinity/Economy.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace infinity {
namespace {
int64_t total(const Stock& s) {
    return std::accumulate(s.begin(), s.end(), int64_t(0));
}
bool active(RecoveryPhase p) {
    return p <= RecoveryPhase::Working;
}
constexpr auto machinery = size_t(Good::Machinery), filters = size_t(Good::Filters);
} // namespace
EconomicVitals economicVitals(const EconomicSite& d, const EconomicSiteState& s, uint64_t tick) {
    EconomicVitals v;
    v.health = s.health;
    v.condition = s.condition;
    auto free = [&](Good g) { return s.stock[size_t(g)] - s.reserved[size_t(g)]; };
    const bool offline = s.outageUntil > tick;
    v.power = offline ? 0
              : d.orbital
                  ? 1000
                  : unsigned(std::clamp(double(economicPower(d, s, tick)) / std::max(1u, d.power) * 1000., 0.,
                                        1000.));
    double essentials = 1;
    if (d.population) {
        essentials = std::min(double(free(Good::Food)) / std::max<uint64_t>(1, d.population / 500),
                              double(free(Good::Water)) / std::max<uint64_t>(1, d.population / 150));
        if (d.sealed)
            essentials =
                std::min(essentials, double(free(Good::Oxygen)) / std::max<uint64_t>(1, d.population / 250));
    }
    v.readiness = unsigned(double(s.health) * s.condition / 1000 * std::clamp(essentials, 0., 1.));
    if (!offline && s.health >= 500 && s.condition >= 300)
        v.repairPoints = unsigned(std::clamp<int64_t>(
            std::min({free(Good::Alloy) / 2, free(Good::Machinery) * 5, free(Good::Tools) * 10}), 0, 10));
    v.outputKg = total(s.produced) + total(s.extracted);
    return v;
}
EconomicOutcome& Economy::recordOutcome(const EconomicCommand& c, size_t i, OutcomeKind kind,
                                        const Stock& delivered, int64_t payment, EconomicVitals before) {
    if (outcomes_.size() >= 16) {
        // Active repairs retain their report until the crew finishes. At most eight can be active.
        auto old = std::find_if(outcomes_.begin(), outcomes_.end(), [](const auto& o) { return !o.working; });
        outcomes_.erase(old);
    }
    EconomicOutcome o;
    o.id = nextOutcome_++;
    o.command = c.sequence;
    o.site = unsigned(i);
    o.kind = kind;
    o.tick = o.updated = tick_;
    o.observeUntil = tick_ + 12;
    o.delivered = delivered;
    o.payment = payment;
    o.before = before;
    o.after = economicVitals(network_->sites[i], sites_[i], tick_);
    outcomes_.push_back(o);
    return outcomes_.back();
}
void Economy::observeOutcomes() {
    for (auto& o : outcomes_)
        if (o.working || tick_ <= o.observeUntil) {
            o.after = economicVitals(network_->sites[o.site], sites_[o.site], tick_);
            o.updated = tick_;
        }
}
bool Economy::recoveryCrew(size_t i) const {
    const auto& d = network_->sites[i];
    const auto& s = sites_[i];
    const int64_t workers =
        int64_t(d.workers) * s.health / 1000 * (4 + s.modules[size_t(FacilityModule::Automation)]) / 4;
    const auto available = workers - (economicConstructionActive(d, s, tick_) ? 2 : 0);
    return !d.orbital && available >= 2 && s.health >= 500 &&
           std::any_of(recoveries_.begin(), recoveries_.end(),
                       [&](const auto& j) { return j.site == i && j.phase == RecoveryPhase::Working; });
}
void Economy::recoveryWork(std::span<unsigned> crewUsed) {
    for (auto& j : recoveries_) {
        auto& s = sites_[j.site];
        if ((j.phase == RecoveryPhase::Open || j.phase == RecoveryPhase::Accepted) && tick_ > j.deadline) {
            if (j.kind == RecoveryKind::Maintenance)
                for (size_t g = 0; g < goodCount; ++g)
                    s.reserved[g] -= j.delivered[g];
            s.cash += j.escrow;
            j.escrow = 0;
            j.phase = RecoveryPhase::Expired;
            event(network_->sites[j.site].name + " / RECOVERY EXPIRED / unused escrow and kit released");
        }
        if (j.phase != RecoveryPhase::Working || !recoveryCrew(j.site))
            continue;
        for (auto g : {machinery, filters}) {
            --s.stock[g];
            --s.reserved[g];
            ++s.consumed[g];
        }
        crewUsed[j.site] += 2;
        s.householdCash += 100; // two existing workers paid 50 cents each
        j.escrow -= 100;
        const auto restored = std::min(15u, 1000 - s.condition);
        s.condition += restored;
        ++j.progress;
        auto receipt = std::find_if(outcomes_.begin(), outcomes_.end(),
                                    [&](const auto& o) { return o.id == j.outcome; });
        if (receipt != outcomes_.end()) {
            receipt->crewPaid += 100;
            receipt->conditionRestored += restored;
        }
        if (j.progress == j.pairs) {
            // Spare machinery becomes available to ordinary port services.
            s.reserved[machinery] -= j.required[machinery] - j.progress;
            const bool clear = j.outageUntil > tick_ && s.outageUntil == j.outageUntil;
            if (clear)
                s.outageUntil = tick_;
            if (receipt != outcomes_.end()) {
                receipt->working = false;
                receipt->outageCleared = clear;
                receipt->observeUntil = tick_ + 12;
            }
            s.cash += j.escrow;
            j.escrow = 0;
            j.phase = RecoveryPhase::Complete;
            event(network_->sites[j.site].name + " / MAINTENANCE COMPLETE / " +
                  (clear ? "POWER RESTORED" : "PLANT SERVICED"));
        }
    }
}
void Economy::recoveryOffers(std::span<const unsigned> stalledRecipes) {
    std::erase_if(recoveries_, [&](const auto& j) { return !active(j.phase) && tick_ > j.deadline + 24; });
    if (tick_ % 12)
        return;
    const auto count =
        std::count_if(recoveries_.begin(), recoveries_.end(), [](const auto& j) { return active(j.phase); });
    auto restarts = std::count_if(recoveries_.begin(), recoveries_.end(), [](const auto& j) {
        return active(j.phase) && j.kind == RecoveryKind::Restart;
    });
    auto ordinary = count - restarts;
    std::vector<Stock> inbound;
    if (restarts < 2 && std::any_of(stalledRecipes.begin(), stalledRecipes.end(), [](unsigned r) { return r; })) {
        inbound.resize(sites_.size());
        for (const auto& f : freight_)
            if (f.disabledAt < 0 && f.phase != FreightPhase::Idle)
                inbound[f.destination][size_t(f.good)] += f.kg;
        for (const auto& h : hauls_)
            if (h.phase <= HaulPhase::InTransit && h.deadline >= tick_)
                inbound[h.destination][size_t(h.good)] += h.kg;
        for (const auto& c : contracts_)
            if (c.phase <= ContractPhase::Accepted && c.deadline >= tick_)
                inbound[c.site][size_t(c.good)] += c.requested - c.delivered;
    }
    unsigned offered = 0;
    bool restartPosted = false;
    for (size_t i = 0; i < sites_.size() && recoveries_.size() < 32; ++i) {
        if ((ordinary >= 8 || offered >= 2) && (inbound.empty() || restarts >= 2 || restartPosted))
            break;
        const auto& d = network_->sites[i];
        auto& s = sites_[i];
        if (d.orbital || !d.population)
            continue;
        for (auto kind : {RecoveryKind::Relief, RecoveryKind::Maintenance, RecoveryKind::Restart}) {
            if (recoveries_.size() >= 32)
                break;
            if (kind == RecoveryKind::Restart) {
                if (inbound.empty() || restarts >= 2 || restartPosted || !stalledRecipes[i])
                    continue;
            } else if (ordinary >= 8 || offered >= 2) {
                continue;
            }
            if (std::any_of(recoveries_.begin(), recoveries_.end(),
                            [&](const auto& j) { return j.site == i && j.kind == kind && active(j.phase); }))
                continue;
            RecoveryJob j;
            j.site = unsigned(i);
            j.kind = kind;
            j.deadline = tick_ + 288;
            if (kind == RecoveryKind::Relief) {
                bool emergency = s.health < 600;
                for (auto good : {Good::Food, Good::Water, Good::Oxygen}) {
                    const auto need = householdRation(d, good);
                    if (!need)
                        continue;
                    const auto g = size_t(good);
                    const auto free = s.stock[g] - s.reserved[g];
                    emergency |= free < need * 2;
                    j.required[g] = std::clamp<int64_t>(need * 12 - free, 0, 1000);
                }
                if (!emergency || !total(j.required))
                    continue;
            } else if (kind == RecoveryKind::Maintenance) {
                if ((s.condition >= 600 && s.outageUntil <= tick_) || d.workers < 2)
                    continue;
                j.pairs = std::clamp((800 - std::min(s.condition, 800u) + 14) / 15, 8u, 50u);
                j.required[machinery] = j.pairs + 2;
                j.required[filters] = j.pairs;
                j.outageUntil = s.outageUntil;
                j.escrow = j.pairs * 100;
            } else {
                j.recipe = stalledRecipes[i];
                const auto& recipe = productionRecipes()[j.recipe - 1];
                bool uncovered = false;
                for (size_t g = 0; g < goodCount; ++g) {
                    const auto available = s.stock[g] - s.reserved[g] + inbound[i][g];
                    uncovered |= recipe.inputs[g] > available;
                    j.required[g] = std::max<int64_t>(0, recipe.inputs[g] * 6 - available);
                }
                if (!uncovered)
                    continue;
                j.deadline = tick_ + 144;
            }
            for (size_t g = 0; g < goodCount; ++g)
                if (j.required[g]) {
                    j.rates[g] = unitPrice(i, Good(g), s.stock[g] - s.reserved[g], true) * 13 / 10;
                    j.escrow += j.required[g] * j.rates[g];
                }
            if (j.escrow > s.cash || total(s.stock) + total(j.required) > economicCapacity(d, s))
                continue;
            j.id = nextRecovery_++;
            s.cash -= j.escrow;
            recoveries_.push_back(j);
            if (kind == RecoveryKind::Restart) {
                ++restarts;
                restartPosted = true;
            } else {
                ++ordinary;
                ++offered;
            }
            const bool weatherDelay = kind == RecoveryKind::Relief && std::any_of(freight_.begin(), freight_.end(),
                [&](const auto& f) { return f.disabledAt < 0 && f.destination == i &&
                                           f.phase == FreightPhase::Holding && j.required[size_t(f.good)] > 0; }) &&
                economicWeatherHold(d, tick_);
            event(d.name + (weatherDelay ? " / FUNDED HOUSEHOLD RELIEF / WEATHER HOLD"
                           : kind == RecoveryKind::Relief ? " / FUNDED HOUSEHOLD RELIEF"
                           : kind == RecoveryKind::Maintenance ? " / FUNDED PLANT RECOVERY"
                                                               : " / FUNDED PRODUCTION RESTART"));
        }
    }
}
std::string Economy::recoveryCommand(const EconomicCommand& c, EconomyPlayer& player, size_t i) {
    auto it = std::find_if(recoveries_.begin(), recoveries_.end(),
                           [&](const auto& j) { return j.id == c.contract && j.site == i; });
    if (it == recoveries_.end())
        return "RECOVERY JOB NOT FOUND AT THIS PORT";
    auto& j = *it;
    if (c.action == EconomicAction::AcceptRecovery) {
        if (j.phase != RecoveryPhase::Open || tick_ > j.deadline)
            return "RECOVERY OFFER CLOSED";
        j.phase = RecoveryPhase::Accepted;
        return "RECOVERY ACCEPTED / SOURCE LISTED SUPPLIES / F4 DESTINATION";
    }
    if (j.phase != RecoveryPhase::Accepted || tick_ > j.deadline)
        return j.phase == RecoveryPhase::Working ? "CREW WORKING / F5 OUTCOMES" : "RECOVERY JOB CLOSED";
    auto& s = sites_[i];
    Stock delivery{};
    int64_t space = economicCapacity(network_->sites[i], s) - total(s.stock), payment = 0;
    for (size_t g = 0; g < goodCount; ++g) {
        delivery[g] = std::min({j.required[g] - j.delivered[g], player.availableCargo(Good(g)), space});
        space -= delivery[g];
        payment += delivery[g] * j.rates[g];
    }
    if (!total(delivery))
        return "NO REQUIRED UNSEALED CARGO OR WAREHOUSE SPACE / CARGO RETAINED";
    const auto before = economicVitals(network_->sites[i], s, tick_);
    for (size_t g = 0; g < goodCount; ++g) {
        player.cargo[g] -= delivery[g];
        s.stock[g] += delivery[g];
        s.imported[g] += delivery[g];
        j.delivered[g] += delivery[g];
        if (j.kind == RecoveryKind::Maintenance)
            s.reserved[g] += delivery[g];
        else if (delivery[g])
            incidentCargo(i, Good(g), delivery[g], true);
    }
    player.credits += payment;
    externalMoneyDelta_ -= payment;
    j.escrow -= payment;
    const auto outcome = j.kind == RecoveryKind::Relief ? OutcomeKind::Relief
                       : j.kind == RecoveryKind::Maintenance ? OutcomeKind::Maintenance : OutcomeKind::Restart;
    auto& receipt = recordOutcome(c, i, outcome, delivery, payment, before);
    j.outcome = receipt.id;
    if (j.delivered == j.required) {
        j.phase = j.kind == RecoveryKind::Maintenance ? RecoveryPhase::Working : RecoveryPhase::Complete;
        receipt.working = j.phase == RecoveryPhase::Working;
    }
    event(network_->sites[i].name + " / PLAYER RECOVERY DELIVERY");
    return "RECOVERY DELIVERED " + std::to_string(total(delivery)) + " KG / " +
           std::to_string(payment / 100) + " CR / F5 OUTCOMES";
}
void economicOutcomeView(MarketView& v, const EconomySnapshot& s, size_t selected) {
    if (s.outcomes.empty()) {
        v.detail = {"NO DELIVERY REPORTS IN THIS SYSTEM YET", "DELIVER SUPPLIES OR COMPLETE A FREIGHT JOB"};
        v.selected = 0;
        return;
    }
    v.selected = std::min(selected, s.outcomes.size() - 1);
    const auto& o = s.outcomes[s.outcomes.size() - 1 - v.selected];
    v.site = s.network->sites[o.site].name;
    v.role = "DELIVERY OUTCOME " + std::to_string(v.selected + 1) + "/" + std::to_string(s.outcomes.size());
    const auto pct = [](unsigned a, unsigned b) {
        return std::to_string(a / 10) + "% TO " + std::to_string(b / 10) + "%";
    };
    v.detail = {std::string(o.kind == OutcomeKind::Restart       ? "PRODUCTION RESTART"
                            : o.kind == OutcomeKind::Relay      ? "EMERGENCY FREIGHT RELAY"
                            : o.kind == OutcomeKind::Rescue     ? "DISTRESS RESCUE / SURVIVOR HANDED OVER"
                            : o.kind == OutcomeKind::Relief     ? "HOUSEHOLD RELIEF"
                            : o.kind == OutcomeKind::Maintenance ? "PLANT RECOVERY"
                            : o.kind == OutcomeKind::Bounty      ? "BOUNTY / WRECK SALVAGE"
                            : o.kind == OutcomeKind::Escort      ? "LANE ESCORT COMPLETE"
                            : o.kind == OutcomeKind::Haul        ? "FREIGHT DELIVERY"
                            : o.kind == OutcomeKind::Supply      ? "SUPPLY CONTRACT"
                                                                 : "MARKET SALE") +
                    " / ECON TICK " + std::to_string(o.tick),
                "PAID " + std::to_string(o.payment / 100) + "." + (o.payment % 100 < 10 ? "0" : "") +
                    std::to_string(o.payment % 100) + " CR"};
    if (o.kind == OutcomeKind::Rescue) {
        v.detail.push_back("SURVIVOR RECEIVED BY PORT CREW OFFICE");
        v.summary = "UP-DOWN REPORTS / F4 TARGET THIS PORT / F5 OTHER MARKET PAGES";
        return;
    }
    v.detail.back() += " / " + std::to_string(total(o.delivered)) + " KG";
    std::string goods = "CARGO: ";
    for (size_t g = 0; g < goodCount; ++g)
        if (o.delivered[g])
            goods += std::string(goodInfo(Good(g)).name) + " " + std::to_string(o.delivered[g]) + "KG  ";
    v.detail.push_back(goods);
    v.detail.push_back("HOUSEHOLD HEALTH " + pct(o.before.health, o.after.health));
    v.detail.push_back("PLANT CONDITION " + pct(o.before.condition, o.after.condition));
    v.detail.push_back("POWER " + pct(o.before.power, o.after.power) + " / READINESS " +
                       pct(o.before.readiness, o.after.readiness));
    v.detail.push_back("AVAILABLE HULL REPAIRS " + std::to_string(o.before.repairPoints) + " TO " +
                       std::to_string(o.after.repairPoints) + " POINTS");
    v.detail.push_back("SITE OUTPUT SINCE HANDOVER " +
                       std::to_string(std::max<int64_t>(0, o.after.outputKg - o.before.outputKg)) +
                       " KG / OBSERVED, ALL CAUSES");
    if (o.kind == OutcomeKind::Bounty)
        v.detail.push_back("BOUNTY SETTLED / PHYSICAL WRECK RECOVERY: APPROACH CLOSE AND HOLD 0");
    if (o.kind == OutcomeKind::Escort)
        v.detail.push_back("CIVILIAN PROTECTED / 90S WITHIN 5KM / FUNDED REWARD SETTLED");
    if (o.kind == OutcomeKind::Maintenance)
        v.detail.push_back("CREW PAID " + std::to_string(o.crewPaid / 100) + " CR / CONDITION REPAIRED +" +
                           std::to_string(o.conditionRestored / 10) + "%" +
                           (o.outageCleared ? " / OUTAGE CLEARED" : ""));
    const auto job = std::find_if(s.recoveries.begin(), s.recoveries.end(),
                                  [&](const auto& j) { return j.outcome == o.id; });
    if (job != s.recoveries.end() && job->phase == RecoveryPhase::Accepted) {
        std::string remaining = "STILL NEEDED: ";
        for (size_t g = 0; g < goodCount; ++g)
            if (job->required[g] > job->delivered[g])
                remaining += std::string(goodInfo(Good(g)).name) + " " +
                             std::to_string(job->required[g] - job->delivered[g]) + "KG  ";
        v.detail.push_back(remaining);
        v.detail.push_back("PARTIAL DELIVERY PAID / RETURN WITH REMAINING SUPPLIES");
    } else if (job != s.recoveries.end() && job->phase == RecoveryPhase::Expired)
        v.detail.push_back("INCOMPLETE JOB EXPIRED / PAID GOODS RETAINED / UNUSED ESCROW REFUNDED");
    else
        v.detail.push_back(o.working ? "KIT SUPPLIED / CREW WORKING OR AWAITING HEALTHY WORKERS"
                           : o.kind == OutcomeKind::Maintenance && o.crewPaid
                               ? "MAINTENANCE COMPLETE / PAID CREW FINISHED THE KIT"
                           : s.tick <= o.observeUntil ? "SUPPLIES RECEIVED / OBSERVING RECOVERY FOR 12 TICKS"
                                                      : "OBSERVATION COMPLETE / REPORT SAVED");
    v.summary = "UP-DOWN REPORTS / F4 TARGET THIS PORT / F5 OTHER MARKET PAGES";
    v.bottleneck = "BEFORE HANDOVER / LAST OBSERVATION / RESTOCKING DOES NOT INSTANTLY HEAL HOUSEHOLDS";
}
} // namespace infinity
