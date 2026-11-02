#include "infinite/Economy.hpp"
#include <algorithm>
#include <numeric>

namespace infinite {
namespace {
constexpr std::array resources{Good::Ice, Good::Ore, Good::Silicate, Good::Carbon};
Stock hardware(unsigned level, bool mining) {
    Stock out{};
    if (!level)
        return out;
    out[size_t(Good::Electronics)] = mining ? 2 * level : 3 * (level - 1);
    out[size_t(Good::Silicon)] = mining ? level : 2 * (level - 1);
    out[size_t(Good::Alloy)] = mining ? 6 * level : 0;
    out[size_t(Good::Tools)] = mining ? 2 * level : level - 1;
    return out;
}
bool activePort(const EconomySnapshot& s, size_t i) {
    return i < s.sites.size() && s.sites[i].outageUntil <= s.tick && s.sites[i].health >= 500 &&
           s.sites[i].condition >= 300;
}
} // namespace
std::array<unsigned, maxWeaponMounts> ExplorationState::stationLoadout(const EconomyPlayer& p) const {
    auto levels = p.weaponLoadout();
    if (miningLevel && miningMount < maxWeaponMounts)
        levels[miningMount] = miningLevel;
    return levels;
}
WeaponQuote economicExplorationQuote(const EconomySnapshot& s, size_t i, bool mining, unsigned mount,
                                     bool sell) {
    WeaponQuote q;
    if (!s.network || !activePort(s, i) || mount >= maxWeaponMounts) {
        q.status = "PORT EQUIPMENT CREW UNAVAILABLE";
        return q;
    }
    const auto& x = s.exploration;
    const unsigned current = mining ? x.miningLevel : x.scannerLevel;
    if (sell && (!mining || !current || x.miningMount != mount)) {
        q.status = "NO MINING HEAD AT THIS MOUNT";
        return q;
    }
    if (!sell && current >= 3) {
        q.status = "EQUIPMENT AT MAXIMUM GRADE III";
        return q;
    }
    if (mining && !sell && (s.player.weaponAt(mount) || (current && x.miningMount != mount))) {
        q.status = "EMPTY THIS MOUNT FIRST / SELL LASER OR EXISTING MINING HEAD";
        return q;
    }
    const unsigned next = sell ? current : current + 1;
    q.materials = hardware(next, mining);
    if (!sell) {
        const auto installed = hardware(current, mining);
        for (size_t g = 0; g < goodCount; ++g)
            q.materials[g] -= installed[g];
        q.labourCents = (mining ? 20000 : 35000) * next;
    }
    bool stock = true;
    int64_t kg = 0;
    for (size_t g = 0; g < goodCount; ++g)
        if (q.materials[g]) {
            const auto price = s.quote(i, Good(g), q.materials[g]);
            q.partsCents += price.buyCents;
            q.sellCents += price.sellCents;
            stock &= price.available >= q.materials[g];
            kg += q.materials[g];
        }
    const auto& site = s.sites[i];
    const bool room = std::accumulate(site.stock.begin(), site.stock.end(), int64_t(0)) + kg <=
                      economicCapacity(s.network->sites[i], site);
    q.available = sell ? (room && site.cash >= q.sellCents) : (stock && s.player.credits >= q.totalCents());
    q.status = q.available
                   ? (sell ? "RETURN HARDWARE / PORT-FUNDED BUYBACK" : "READY / PORT PARTS AND INSTALLATION")
               : sell   ? "PORT NEEDS SPACE OR BUYBACK FUNDS"
               : !stock ? "PORT NEEDS UNRESERVED EQUIPMENT PARTS"
                        : "INSUFFICIENT CREDITS";
    return q;
}
void ExplorationLedger::offer(const Universe& u, Economy& e) {
    if (e.network_->sites.empty() || state_.contracts.size() >= maxSurveyContracts)
        return;
    const auto system = e.network_->system;
    if (std::any_of(state_.contracts.begin(), state_.contracts.end(),
                    [&](const auto& j) { return surveySystem(j.target) == system; }))
        return;
    const auto parent = u.describe(system);
    const auto count = u.planetCount(parent);
    if (!count)
        return;
    for (unsigned k = 0; k < 3 && state_.contracts.size() < maxSurveyContracts; ++k) {
        const auto& issuer = e.network_->sites[std::min<size_t>(k, e.network_->sites.size() - 1)];
        SurveyContract j;
        j.issuer = issuer.id;
        j.grade = k + 1;
        j.id = mix64(seedOf(system, u.seed()) ^ 0x5355525645593533ULL ^ mix64(k));
        if (!j.id)
            j.id = 1;
        if (std::any_of(state_.contracts.begin(), state_.contracts.end(),
                        [&](const auto& x) { return x.id == j.id; }))
            continue;
        j.target = system.child(Kind::Planet, (mix64(j.id) % count + k) % count);
        j.name = u.describe(j.target).label();
        j.reward = 30000 * (k + 1);
        state_.contracts.push_back(j);
    }
}
std::string ExplorationLedger::execute(const Universe& u, Economy& e, EconomyPlayer& p,
                                       const EconomicCommand& c) {
    if (!c.sequence || c.sequence <= p.lastCommand)
        return "ORDER ALREADY PROCESSED";
    p.lastCommand = c.sequence;
    const auto targetSystem = c.surveyTarget ? surveySystem(*c.surveyTarget) : std::nullopt;
    if (c.action == EconomicAction::ScanSurvey || c.action == EconomicAction::ExtractProspect) {
        if (!c.surveyTarget || !c.observer || targetSystem != std::optional<Address>(e.network_->system) ||
            !std::isfinite(c.workSeconds))
            return "EXPLORATION TARGET OUTSIDE ACTIVE SYSTEM";
        if (c.action == EconomicAction::ScanSurvey) {
            if (c.workSeconds < 8 || !surveyAccess(u, *c.observer, *c.surveyTarget))
                return "SCAN REQUIRES COMPLETED OBSERVATION / APPROACH AND FACE TARGET";
            auto found = std::find_if(state_.journal.begin(), state_.journal.end(),
                                      [&](const auto& r) { return r.address == *c.surveyTarget; });
            if (found == state_.journal.end() && state_.journal.size() >= maxSurveyRecords)
                return "SURVEY JOURNAL FULL / RECORDS PRESERVED";
            if (state_.revision >= 1000000000000ULL)
                return "SURVEY REVISION LIMIT";
            const auto precision =
                std::min(3u, state_.scannerLevel + unsigned(c.serviceUnits >= 110 && c.serviceUnits <= 150));
            if (found != state_.journal.end() && precision < found->grade)
                return "LOWER SCAN PRECISION / HIGHER-GRADE RECORD RETAINED / REST CREW OR UPGRADE SCANNER";
            auto record = surveyReading(u, *c.surveyTarget, precision, c.observer->seconds);
            if (found != state_.journal.end()) {
                record.grade = std::max(record.grade, found->grade);
                record = surveyReading(u, *c.surveyTarget, record.grade, c.observer->seconds);
                record.paidGrade = found->paidGrade;
            }
            record.revision = ++state_.revision;
            const bool first = found == state_.journal.end();
            if (first)
                state_.journal.push_back(record);
            else
                *found = record;
            return std::string(first ? "DISCOVERY RECORDED / " : "SURVEY UPDATED / ") + record.name +
                   " / F11 JOURNAL";
        }
        if (!state_.miningLevel || c.weaponMount != state_.miningMount || c.workSeconds < 6)
            return "FIT AND OPERATE MINING HEAD FIRST";
        auto access = miningAccess(u, *c.observer, *c.surveyTarget, state_.miningMount);
        if (!access.allowed || !c.combatPresent)
            return "MINING ACCESS LOST / NO CARGO TRANSFER";
        bool known = false;
        for (const auto& r : state_.journal) {
            auto key = prospectAddress(u, r.address);
            if (key && *key == access.deposit) {
                known = true;
                break;
            }
        }
        if (!known)
            return "UNSURVEYED PROSPECT / HOLD F12 FIRST";
        auto found = std::find_if(state_.prospects.begin(), state_.prospects.end(),
                                  [&](const auto& a) { return a.address == access.deposit; });
        if (found == state_.prospects.end()) {
            if (state_.prospects.size() >= maxProspects)
                return "PROSPECT REGISTER FULL / DEPLETION RECORDS RETAINED";
            state_.prospects.push_back({access.deposit});
            found = std::prev(state_.prospects.end());
        }
        const auto total = prospectResources(u, access.deposit);
        int64_t taken = 0, budget = 10 * state_.miningLevel;
        for (size_t slot = 0; slot < resources.size(); ++slot) {
            const auto good = resources[slot];
            const auto g = size_t(good);
            const auto kg = std::max<int64_t>(
                0, std::min({total[slot] - found->extracted[slot], budget, p.capacityKg - p.massKg(),
                             (p.capacityLitres - p.volumeLitres()) / goodInfo(good).litresPerKg}));
            found->extracted[slot] += kg;
            p.cargo[g] += kg;
            taken += kg;
            budget -= kg;
            // Physical extraction enters the player economy and is exported immediately.
            if (!e.sites_.empty()) {
                e.sites_[0].imported[g] += kg;
                e.sites_[0].exported[g] += kg;
            }
        }
        state_.minedKg += uint64_t(taken);
        if (!taken) {
            bool exhausted = true;
            for (size_t g = 0; g < 4; ++g)
                exhausted &= found->extracted[g] >= total[g];
            return exhausted ? "PROSPECT DEPLETED / JOURNAL RETAINS SITE"
                             : "HOLD FULL / CARGO AND PROSPECT RETAINED";
        }
        return "EXTRACTED " + std::to_string(taken) + " KG / REAL MATERIAL IN CARGO / F2 SELL";
    }
    const auto site = e.siteIndex(c.port);
    if (!site)
        return "RETURN TO A LOCAL EXPLORATION PORT";
    if (c.action != EconomicAction::AcceptSurvey && c.action != EconomicAction::CancelSurvey) {
        if (!c.observer || c.observer->field || !std::isfinite(c.observer->size) || c.observer->size < .99 ||
            c.observer->hull <= 0 || !std::isfinite(glm::length(c.observer->velocity)) ||
            glm::length(c.observer->velocity) > 3)
            return "EXPLORATION SERVICES REQUIRE A PHYSICAL PORT LINK";
        const auto& d = e.network_->sites[*site];
        const auto& o = *c.observer;
        const auto eye =
            o.reference == d.id.home
                ? o.position
                : u.pointRelative(o.reference,
                                  o.position / std::pow(10., u.describe(o.reference).frame.logRadius),
                                  d.id.home, 0);
        const auto range = glm::length(eye - d.metres);
        if (!std::isfinite(range) || !(d.orbital ? range < d.radius * 5 + 250 : o.landed && range < 250))
            return "APPROACH AND STOP AT THE ISSUING PORT / LAND AT SURFACE PORTS";
    }
    if (c.action == EconomicAction::UpgradeScanner || c.action == EconomicAction::FitMiningHead ||
        c.action == EconomicAction::SellMiningHead) {
        EconomySnapshot s;
        s.network = e.network_;
        s.sites = e.sites_;
        s.tick = e.tick_;
        s.player = p;
        s.exploration = state_;
        const bool mining = c.action != EconomicAction::UpgradeScanner,
                   sell = c.action == EconomicAction::SellMiningHead;
        const auto q = economicExplorationQuote(s, *site, mining, c.weaponMount, sell);
        if (!q.available)
            return q.status;
        if (sell ? c.limitCents > q.sellCents : c.limitCents < q.totalCents())
            return "EQUIPMENT QUOTE CHANGED / REVIEW PRICE";
        auto& port = e.sites_[*site];
        for (size_t g = 0; g < goodCount; ++g) {
            port.stock[g] += (sell ? 1 : -1) * q.materials[g];
            (sell ? port.imported[g] : port.exported[g]) += q.materials[g];
        }
        if (sell) {
            port.cash -= q.sellCents;
            p.credits += q.sellCents;
            e.externalMoneyDelta_ -= q.sellCents;
            state_.miningLevel = 0;
            state_.miningMount = noWeaponMount;
            return "MINING HEAD SOLD / HARDWARE RETURNED TO PORT";
        }
        p.credits -= q.totalCents();
        e.externalMoneyDelta_ += q.totalCents();
        port.cash += q.partsCents;
        (e.network_->sites[*site].orbital ? e.carrierCash_ : port.householdCash) += q.labourCents;
        if (mining) {
            ++state_.miningLevel;
            state_.miningMount = c.weaponMount;
            return "MINING HEAD FITTED / " + std::string(weaponMountName(c.weaponMount)) +
                   " / CTRL-SPACE / F9 STAFF EXTRA MOUNT";
        }
        ++state_.scannerLevel;
        return "SCANNER UPGRADED / FASTER OBSERVATIONS / GREATER PRECISION";
    }
    if (c.action == EconomicAction::SellSurvey) {
        if (!c.surveyTarget)
            return "SELECT A JOURNAL ENTRY";
        auto r = std::find_if(state_.journal.begin(), state_.journal.end(),
                              [&](const auto& x) { return x.address == *c.surveyTarget; });
        if (r == state_.journal.end() || r->paidGrade >= r->grade)
            return "SURVEY DATA ALREADY SOLD / IMPROVE GRADE FOR MORE";
        const int64_t price = (r->grade - r->paidGrade) * (r->address.kind() >= Kind::Grain ? 10000 : 5000);
        auto& port = e.sites_[*site];
        if (port.cash < price || port.outageUntil > e.tick_ || port.health < 500 || port.condition < 300)
            return "SURVEY OFFICE NEEDS FUNDS OR REPAIRS / DATA RETAINED";
        if (p.credits > 1000000000000000LL - price)
            return "BALANCE LIMIT / DATA RETAINED";
        port.cash -= price;
        p.credits += price;
        e.externalMoneyDelta_ -= price;
        state_.surveyEarnings += price;
        r->paidGrade = r->grade;
        return "FIRST-SURVEY DATA SOLD / " + std::to_string(price / 100) + " CR / SAME GRADE PAID ONCE";
    }
    auto job = std::find_if(state_.contracts.begin(), state_.contracts.end(),
                            [&](const auto& j) { return j.id == c.contract && j.issuer == c.port; });
    if (job == state_.contracts.end())
        return "SURVEY CONTRACT NOT FOUND AT THIS PORT";
    auto& port = e.sites_[*site];
    if (c.action == EconomicAction::AcceptSurvey) {
        if (job->phase != SurveyPhase::Open)
            return "SURVEY OFFER CLOSED";
        if (port.cash < job->reward)
            return "ISSUER CANNOT RESERVE SURVEY REWARD";
        port.cash -= job->reward;
        e.externalMoneyDelta_ -= job->reward;
        job->escrow = job->reward;
        job->phase = SurveyPhase::Accepted;
        job->acceptedRevision = state_.revision;
        return "SURVEY ACCEPTED / REWARD IN ESCROW / F4 TARGET / HOLD F12";
    }
    if (c.action == EconomicAction::CancelSurvey) {
        if (job->phase != SurveyPhase::Accepted)
            return "NO ACCEPTED SURVEY TO CANCEL";
        port.cash += job->escrow;
        e.externalMoneyDelta_ += job->escrow;
        job->escrow = 0;
        job->phase = SurveyPhase::Cancelled;
        return "SURVEY CANCELLED / RESERVED FUNDS RETURNED TO ISSUER";
    }
    if (c.action != EconomicAction::ClaimSurvey || job->phase != SurveyPhase::Accepted)
        return "SURVEY CONTRACT ALREADY CLOSED";
    auto report = std::find_if(state_.journal.begin(), state_.journal.end(), [&](const auto& r) {
        return r.address == job->target && r.grade >= job->grade && r.revision > job->acceptedRevision;
    });
    if (report == state_.journal.end())
        return "NEW SURVEY REQUIRED / VISIT TARGET AFTER ACCEPTING / CHECK GRADE";
    if (p.credits > 1000000000000000LL - job->escrow)
        return "BALANCE LIMIT / ESCROW RETAINED";
    p.credits += job->escrow;
    state_.surveyEarnings += job->escrow;
    job->escrow = 0;
    job->phase = SurveyPhase::Complete;
    ++state_.surveysDelivered;
    return "SURVEY CONTRACT COMPLETE / " + std::to_string(job->reward / 100) + " CR / JOURNAL RETAINED";
}
} // namespace infinite
