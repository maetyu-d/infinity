#include "infinite/CombatActors.hpp"
#include "infinite/Economy.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
namespace infinite {
namespace {
bool active(CombatJobPhase p) {
    return p == CombatJobPhase::Accepted || p == CombatJobPhase::Claimable;
}
bool funded(CombatJobPhase p) {
    return p == CombatJobPhase::Open || active(p);
}
} // namespace
Stock combatSalvage(const CombatContract& c) {
    Stock s{};
    if (c.kind == CombatJobKind::Bounty) {
        s[size_t(Good::Alloy)] = 20;
        s[size_t(Good::Electronics)] = 2;
    }
    return s;
}
bool Economy::reconcileCombat(EconomyPlayer& p) const {
    const auto count = std::count_if(combatContracts_.begin(), combatContracts_.end(),
                                     [](const auto& c) { return active(c.phase); });
    if (!p.combatJob || p.combatJob->system != network_->system)
        return count == 0;
    const auto it = std::find_if(combatContracts_.begin(), combatContracts_.end(),
                                 [&](const auto& c) { return c.id == p.combatJob->id; });
    if (it == combatContracts_.end())
        return false;
    if (it->phase == CombatJobPhase::Failed || it->phase == CombatJobPhase::Expired) {
        p.combatJob.reset();
        ++p.combatFailed;
        return count == 0;
    }
    return count == 1 && active(it->phase);
}
void Economy::combatOffers() {
    for (auto& c : combatContracts_) {
        if ((c.phase == CombatJobPhase::Open || c.phase == CombatJobPhase::Accepted) && tick_ > c.deadline) {
            sites_[c.site].cash += c.escrow;
            c.escrow = 0;
            c.phase = CombatJobPhase::Expired;
            event("SECURITY CONTRACT EXPIRED / REWARD REFUNDED");
        } else if (c.phase == CombatJobPhase::Accepted && c.kind == CombatJobKind::Escort &&
                   c.protectedSeconds > 0 && tick_ * economyTickSeconds - c.lastPresent > 35) {
            sites_[c.site].cash += c.escrow;
            c.escrow = 0;
            c.phase = CombatJobPhase::Failed;
            event("ESCORT ABANDONED / REWARD REFUNDED");
        }
    }
    if (tick_ % 12 || combatContracts_.size() >= 32)
        return;
    unsigned offered = 0;
    std::set<Address> homes;
    for (unsigned i = 0; i < network_->sites.size() && offered < 2; ++i) {
        const auto& site = network_->sites[i];
        if (!site.orbital || isMoon(site.id.home) || !homes.insert(site.id.home).second)
            continue;
        for (auto kind : {CombatJobKind::Escort, CombatJobKind::Bounty}) {
            ActivityId target{site.id.home, kind == CombatJobKind::Escort ? 200u : raiderActivitySlot};
            if (std::any_of(combatContracts_.begin(), combatContracts_.end(),
                            [&](const auto& c) { return c.target == target && c.kind == kind; }))
                continue;
            const int64_t reward = kind == CombatJobKind::Escort ? 60000 : 90000;
            if (sites_[i].cash < reward * 2 || offered >= 2 || combatContracts_.size() >= 32)
                continue;
            CombatContract c;
            c.id = nextCombat_++;
            c.deadline = tick_ + 720;
            c.site = i;
            c.target = target;
            c.kind = kind;
            c.reward = c.escrow = reward;
            sites_[i].cash -= reward;
            combatContracts_.push_back(c);
            ++offered;
            event(site.name +
                  (kind == CombatJobKind::Escort ? " / FUNDED 90S LANE ESCORT" : " / FUNDED NAMED BOUNTY"));
        }
    }
}
std::string Economy::combatCommand(const EconomicCommand& c, EconomyPlayer& p) {
    if (!reconcileCombat(p))
        return "SECURITY SAVE MISMATCH / ORDER PAUSED";
    auto it = std::find_if(combatContracts_.begin(), combatContracts_.end(),
                           [&](const auto& x) { return x.id == c.contract; });
    if (it == combatContracts_.end())
        return "SECURITY CONTRACT NOT FOUND";
    auto& job = *it;
    const auto issuer = network_->sites[job.site].id;
    auto fail = [&](const char* reason) {
        sites_[job.site].cash += job.escrow;
        job.escrow = 0;
        job.phase = CombatJobPhase::Failed;
        p.combatJob.reset();
        ++p.combatFailed;
        event(reason);
        return std::string(reason);
    };
    if (c.action == EconomicAction::AcceptCombat) {
        if (job.phase != CombatJobPhase::Open || tick_ > job.deadline)
            return "SECURITY OFFER CLOSED";
        if (p.combatJob)
            return "FINISH OR CANCEL YOUR ACTIVE SECURITY CONTRACT FIRST";
        if (c.port != issuer)
            return "SELECT ISSUING SECURITY PORT";
        if (!std::isfinite(c.impactSeconds) || std::abs(c.impactSeconds - tick_ * economyTickSeconds) > 10 ||
            !c.combatTarget || *c.combatTarget != job.target || !std::isfinite(c.targetHull) ||
            c.targetHull <= 0)
            return "TARGET ALREADY DISABLED OR CONTACT UNVERIFIED";
        job.phase = CombatJobPhase::Accepted;
        job.acceptedSeconds = job.lastReport = job.lastPresent = c.impactSeconds;
        p.combatJob = PlayerCombatJob{network_->system, job.id};
        return job.kind == CombatJobKind::Escort
                   ? "ESCORT ACCEPTED / F4 CONTACT / PROTECT WITHIN 5KM FOR 90S"
                   : "BOUNTY ACCEPTED / F4 NAMED HOSTILE / RETURN HERE TO CLAIM";
    }
    if (!p.combatJob || p.combatJob->system != network_->system || p.combatJob->id != job.id ||
        !active(job.phase))
        return "THIS IS NOT YOUR ACTIVE SECURITY CONTRACT";
    if (c.action == EconomicAction::CancelCombat)
        return fail("SECURITY CONTRACT CANCELLED / ESCROW REFUNDED");
    if (c.action == EconomicAction::ReportCombat) {
        if (job.phase != CombatJobPhase::Accepted)
            return "SECURITY OBJECTIVE ALREADY VERIFIED / RETURN TO ISSUER";
        if (!c.combatTarget || *c.combatTarget != job.target || !std::isfinite(c.targetHull) ||
            c.targetHull < 0 || c.targetHull > 100 || !std::isfinite(c.targetDistance) ||
            c.targetDistance < 0 || !std::isfinite(c.impactSeconds) || c.impactSeconds <= job.lastReport ||
            std::abs(c.impactSeconds - tick_ * economyTickSeconds) > 10)
            return "INVALID SECURITY TELEMETRY";
        const double elapsed = c.impactSeconds - job.lastReport;
        job.lastReport = c.impactSeconds;
        if (c.serviceUnits == 0)
            return fail("SECURITY FAILED / PLAYER SHIP LOST");
        if (job.kind == CombatJobKind::Bounty) {
            // Only the main-thread player weapon ledger can submit this receipt.
            if (c.targetHull > 0 || !c.combatPresent)
                return "BOUNTY ACTIVE / NAMED HOSTILE STILL FLYING";
            job.phase = CombatJobPhase::Claimable;
        } else {
            if (c.targetHull <= 0)
                return fail("ESCORT FAILED / CIVILIAN LOST");
            if (c.combatPresent && c.targetDistance <= 5000) {
                if (elapsed <= 2.5)
                    job.protectedSeconds += std::min(elapsed, 1.25);
                job.lastPresent = c.impactSeconds;
            }
            if (job.protectedSeconds >= 90)
                job.phase = CombatJobPhase::Claimable;
            else if (job.protectedSeconds > 0 && c.impactSeconds - job.lastPresent > 30)
                return fail("ESCORT FAILED / CONTACT ABANDONED FOR 30S");
            else
                return "ESCORT / " + std::to_string(int(job.protectedSeconds)) + "/90S / KEEP WITHIN 5KM";
        }
        event("SECURITY OBJECTIVE VERIFIED / RETURN TO ISSUING PORT");
        return "OBJECTIVE COMPLETE / F4 ISSUER / BERTH AND ENTER TO CLAIM";
    }
    if (c.action != EconomicAction::ClaimCombat || job.phase != CombatJobPhase::Claimable || c.port != issuer)
        return "COMPLETE OBJECTIVE THEN CLAIM AT ISSUING PORT";
    const auto salvage = combatSalvage(job);
    int64_t kg = 0, litres = 0;
    for (size_t g = 0; g < goodCount; ++g) {
        kg += salvage[g];
        litres += salvage[g] * goodInfo(Good(g)).litresPerKg;
    }
    if (p.massKg() + kg > p.capacityKg || p.volumeLitres() + litres > p.capacityLitres)
        return "MAKE ROOM FOR 22KG WRECK SALVAGE / REWARD RETAINED";
    const auto before = economicVitals(network_->sites[job.site], sites_[job.site], tick_);
    p.credits += job.escrow;
    p.combatEarnings += job.escrow;
    externalMoneyDelta_ -= job.escrow;
    // A single finite wreck receipt introduces recovered ship material and
    // exports it directly to the player's hold. No port stock is synthesized.
    for (size_t g = 0; g < goodCount; ++g) {
        sites_[job.site].imported[g] += salvage[g];
        sites_[job.site].exported[g] += salvage[g];
        p.cargo[g] += salvage[g];
    }
    recordOutcome(c, job.site, job.kind == CombatJobKind::Escort ? OutcomeKind::Escort : OutcomeKind::Bounty,
                  {}, job.escrow, before);
    job.escrow = 0;
    job.phase = CombatJobPhase::Complete;
    p.combatJob.reset();
    ++p.combatCompleted;
    event("SECURITY PAID / " + std::to_string(job.reward / 100) + "CR / SALVAGE " + std::to_string(kg) +
          "KG");
    return "SECURITY PAID / " + std::to_string(job.reward / 100) + "CR / " + std::to_string(kg) +
           "KG SALVAGE";
}
void Economy::writeCombat(std::ostream& o) const {
    o << "SECURITY " << nextCombat_ << ' ' << combatContracts_.size() << '\n' << std::setprecision(17);
    for (const auto& c : combatContracts_)
        o << c.id << ' ' << c.deadline << ' ' << c.site << ' ' << std::quoted(c.target.str()) << ' '
          << unsigned(c.kind) << ' ' << unsigned(c.phase) << ' ' << c.reward << ' ' << c.escrow << ' '
          << c.acceptedSeconds << ' ' << c.lastReport << ' ' << c.protectedSeconds << ' ' << c.lastPresent
          << '\n';
}
bool Economy::readCombat(std::istream& in) {
    std::string h, id;
    size_t count;
    if (!(in >> h >> nextCombat_ >> count) || h != "SECURITY" || !nextCombat_ || count > 32)
        return false;
    std::set<uint64_t> ids;
    std::set<ActivityId> targets;
    unsigned accepted = 0;
    for (size_t i = 0; i < count; ++i) {
        CombatContract c;
        unsigned kind, phase;
        if (!(in >> c.id >> c.deadline >> c.site >> std::quoted(id) >> kind >> phase >> c.reward >>
              c.escrow >> c.acceptedSeconds >> c.lastReport >> c.protectedSeconds >> c.lastPresent) ||
            !c.id || c.id >= nextCombat_ || !ids.insert(c.id).second || c.site >= sites_.size() || kind > 1 ||
            phase > unsigned(CombatJobPhase::Expired) || c.reward < 1 || c.reward > 1000000 ||
            c.deadline > 1000000000000ULL)
            return false;
        try {
            c.target = ActivityId::parse(id);
        } catch (...) {
            return false;
        }
        c.kind = CombatJobKind(kind);
        c.phase = CombatJobPhase(phase);
        if (c.target.home != network_->sites[c.site].id.home ||
            c.target.slot != (c.kind == CombatJobKind::Escort ? 200u : raiderActivitySlot) ||
            !targets.insert(c.target).second || c.escrow != (funded(c.phase) ? c.reward : 0))
            return false;
        for (auto t : {c.acceptedSeconds, c.lastReport, c.protectedSeconds, c.lastPresent})
            if (!std::isfinite(t) || t < 0 || t > 1e12)
                return false;
        if (c.protectedSeconds > 91.25 || c.lastReport < c.acceptedSeconds || c.lastPresent > c.lastReport)
            return false;
        accepted += active(c.phase);
        combatContracts_.push_back(c);
    }
    return accepted <= 1;
}
} // namespace infinite
