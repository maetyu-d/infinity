#include "infinity/SaveTag.hpp"
#include "infinity/Economy.hpp"
#include "infinity/Planetology.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace infinity {
namespace {
constexpr std::array resources{Good::Ice, Good::Ore, Good::Silicate, Good::Carbon};
bool linked(const Universe& u, const SurveyObserver& o, const EconomicSite& s) {
    if (!std::isfinite(o.size) || !std::isfinite(o.hull) || o.size < .99 || o.size > 1.01 || o.hull <= 0 ||
        o.field || !std::isfinite(glm::length(o.velocity)) || glm::length(o.velocity) > 3)
        return false;
    const auto r = u.describe(o.reference).frame.logRadius;
    const auto eye = u.pointRelative(o.reference, o.position / std::pow(10., r), s.id.home, 0);
    const double distance = glm::length(eye - s.metres);
    return std::isfinite(distance) &&
           (s.orbital ? distance < s.radius * 5 + 250 : o.landed && distance < 250);
}
Stock parts(const MaterialExpedition& j) {
    return facilityRefit(FacilityModule::Power, j.powerLevel).materials;
}
Address sourceAt(const Universe& u, const EconomicSite& site) {
    return samplingSourceNear(u, u.describe(site.id.home), site.metres, site.radius);
}
} // namespace
const MaterialExpedition* selectedExpedition(const ExpeditionState& s, size_t i) {
    return i < s.jobs.size() ? &s.jobs[i] : nullptr;
}
const MaterialExpedition* activeExpedition(const ExpeditionState& s, const Address& system) {
    for (const auto& j : s.jobs)
        if (j.phase >= ExpeditionPhase::Survey && j.phase <= ExpeditionPhase::Return &&
            surveySystem(j.source) == system)
            return &j;
    return nullptr;
}
std::string expeditionObjective(const MaterialExpedition& j) {
    switch (j.phase) {
    case ExpeditionPhase::Offered:
        return "LAND AT COLONY / ENTER DIAGNOSE AND ACCEPT";
    case ExpeditionPhase::Survey:
        return "F4 CORE SITE / J APPROACH / HOLD F12 / CTRL-SPACE CORE";
    case ExpeditionPhase::Core:
        return "RETURN TO A PORT / ENTER OPEN SPECIMEN LAB";
    case ExpeditionPhase::Assay:
        return "PROBE: F4 NEXT STRUCTURE / J MAGNIFY / F12 ASSAY / SHIFT-F11 EXIT";
    case ExpeditionPhase::Return:
        return "RETURN TO ORIGINAL COLONY / ENTER FIT ADAPTED EXCHANGER";
    case ExpeditionPhase::Complete:
        return "EXCHANGER RESTORED / POWER UPGRADED / SAVED OUTCOME";
    case ExpeditionPhase::Cancelled:
        return "CANCELLED / COLONY FUNDS AND PARTS RETURNED";
    }
    return {};
}
void ExpeditionLedger::offer(const Universe& u, Economy& e) {
    if (state_.jobs.size() >= maxMaterialExpeditions ||
        std::any_of(state_.jobs.begin(), state_.jobs.end(),
                    [&](const auto& j) { return surveySystem(j.source) == e.network_->system; }))
        return;
    for (size_t i = 0; i < e.network_->sites.size(); ++i) {
        const auto& d = e.network_->sites[i];
        const auto& s = e.sites_[i];
        if (d.orbital || !d.population || !hasSolidSurface(u.describe(d.id.home)) ||
            s.modules[size_t(FacilityModule::Power)] >= 3)
            continue;
        MaterialExpedition j;
        j.id = mix64(seedOf(d.id.home, u.seed()) ^ mix64(d.id.slot) ^ 0x4558504544495431ULL);
        if (!j.id)
            j.id = 1;
        j.colony = d.id;
        j.colonyName = d.name;
        j.source = sourceAt(u, d);
        j.descriptor = descriptorFingerprint(u.describe(j.source));
        const auto reading = surveyReading(u, j.grain(), 3, 0);
        j.materialDescription = reading.mineral + " / " + reading.formation;
        j.powerLevel = s.modules[size_t(FacilityModule::Power)] + 1;
        state_.jobs.push_back(j);
        return;
    }
}
void ExpeditionLedger::reconcilePlayer(EconomyPlayer& p) const {
    p.specimenKg = 0;
    p.specimenLitres = 0;
    for (const auto& j : state_.jobs)
        if (j.carrying()) {
            ++p.specimenKg;
            p.specimenLitres += goodInfo(resources[j.resource]).litresPerKg;
        }
}
std::string ExpeditionLedger::execute(const Universe& u, Economy& e, ExplorationLedger& exploration,
                                      EconomyPlayer& p, const EconomicCommand& c) {
    if (!c.sequence || c.sequence <= p.lastCommand)
        return "ORDER ALREADY PROCESSED";
    p.lastCommand = c.sequence;
    auto it = std::find_if(state_.jobs.begin(), state_.jobs.end(),
                           [&](const auto& j) { return j.id == c.contract; });
    if (it == state_.jobs.end() || surveySystem(it->source) != e.network_->system)
        return "EXPEDITION OUTSIDE ACTIVE SYSTEM";
    auto& j = *it;
    const auto index = e.siteIndex(j.colony);
    if (!index)
        return "ORIGINAL COLONY UNAVAILABLE";
    auto& s = e.sites_[*index];
    const auto& d = e.network_->sites[*index];
    auto at = [&](const ActivityId& port) {
        const auto i = e.siteIndex(port);
        return i && c.observer && linked(u, *c.observer, e.network_->sites[*i]);
    };
    const auto materialParts = parts(j);
    const auto labour = facilityRefit(FacilityModule::Power, j.powerLevel).labourCents;
    if (c.action == EconomicAction::DiagnoseExpedition) {
        if (j.phase != ExpeditionPhase::Offered)
            return "DIAGNOSIS ALREADY RECORDED";
        if (activeExpedition(state_, e.network_->system))
            return "FINISH THE ACTIVE MATERIAL EXPEDITION FIRST";
        if (!at(j.colony))
            return "LAND AND STOP AT THE ORIGINAL COLONY TO DIAGNOSE";
        if (s.project || s.modules[size_t(FacilityModule::Power)] + 1 != j.powerLevel)
            return "COLONY UTILITY REFIT CHANGED / OFFER UNAVAILABLE";
        if (s.cash < j.reward + labour)
            return "COLONY IS RAISING REPAIR FUNDS";
        for (size_t g = 0; g < goodCount; ++g)
            if (s.stock[g] - s.reserved[g] < materialParts[g])
                return "COLONY NEEDS EXCHANGER PARTS / SUPPLY ITS MARKET";
        s.cash -= j.reward + labour;
        j.escrow = j.reward + labour;
        e.externalMoneyDelta_ -= j.escrow;
        for (size_t g = 0; g < goodCount; ++g) {
            s.stock[g] -= materialParts[g];
            s.exported[g] += materialParts[g];
        }
        j.acceptedRevision = exploration.state_.revision;
        j.phase = ExpeditionPhase::Survey;
        // A simulated component failure; no generator, city layout or terrain is modified.
        s.condition = std::min(s.condition, 850u);
        e.event("THERMAL EXCHANGER DIAGNOSED / " + d.name + " / CORE EXPEDITION FUNDED");
        return "EXPEDITION FUNDED / PARTS RESERVED / SURVEY THE MARKED CORE SITE";
    }
    if (c.action == EconomicAction::CancelExpedition) {
        if (j.phase < ExpeditionPhase::Survey || j.phase > ExpeditionPhase::Return)
            return "EXPEDITION ALREADY CLOSED";
        int64_t returned = 0, stored = 0;
        for (size_t g = 0; g < goodCount; ++g) {
            returned += materialParts[g];
            stored += s.stock[g];
        }
        if (stored + returned > economicCapacity(d, s))
            return "COLONY NEEDS WAREHOUSE ROOM TO RETURN ITS REPAIR KIT";
        // A recovered sample is retained as a bulk mineral, never silently destroyed.
        if (j.carrying())
            p.cargo[size_t(resources[j.resource])]++;
        s.cash += j.escrow;
        e.externalMoneyDelta_ += j.escrow;
        j.escrow = 0;
        for (size_t g = 0; g < goodCount; ++g) {
            s.stock[g] += materialParts[g];
            s.imported[g] += materialParts[g];
        }
        j.phase = ExpeditionPhase::Cancelled;
        j.lab.reset();
        reconcilePlayer(p);
        return "EXPEDITION CANCELLED / ESCROW AND PARTS RETURNED / CORE RETAINED AS MINERAL CARGO";
    }
    if (c.action == EconomicAction::RecoverCore) {
        if (j.phase != ExpeditionPhase::Survey || !c.observer || !c.surveyTarget ||
            !std::isfinite(c.workSeconds) || c.workSeconds < 6 || !c.combatPresent)
            return "CORE REQUIRES SURVEY AND SIX SECONDS OF UNOBSTRUCTED SAMPLING";
        const auto key = prospectAddress(u, j.source);
        auto& x = exploration.state_;
        const bool surveyed = std::any_of(x.journal.begin(), x.journal.end(), [&](const auto& r) {
            return r.revision > j.acceptedRevision && r.prospect == key;
        });
        if (!surveyed)
            return "HOLD F12 AT THE CORE SITE AFTER DIAGNOSIS";
        if (prospectAddress(u, *c.surveyTarget) != key ||
            !miningAccess(u, *c.observer, *c.surveyTarget, 0).allowed)
            return "APPROACH THE MARKED CORE SITE AT FULL SIZE / STOP NEAR THE GROUND";
        const auto home = u.describe(j.colony.home);
        const auto point = u.pointRelative(
            c.observer->reference,
            c.observer->position / std::pow(10., u.describe(c.observer->reference).frame.logRadius),
            j.colony.home, 0);
        const auto direction = u.describe(j.source).geographicDirection;
        const auto core =
            direction * (std::pow(10., home.frame.logRadius) + u.terrainHeight(home, direction));
        if (glm::length(point - core) > 80)
            return "CORE SITE MORE THAN 80 M AWAY / F4 TARGET";
        auto prospect = std::find_if(x.prospects.begin(), x.prospects.end(),
                                     [&](const auto& a) { return a.address == *key; });
        if (prospect == x.prospects.end() && x.prospects.size() >= maxProspects)
            return "PROSPECT REGISTER FULL";
        const auto available = prospect == x.prospects.end() ? prospectResources(u, *key) : prospect->capacity;
        unsigned slot = 0;
        int64_t most = -1;
        for (unsigned k = 0; k < 4; ++k) {
            const auto remaining =
                available[k] - (prospect == x.prospects.end() ? 0 : prospect->extracted[k]);
            if (remaining > most) {
                most = remaining;
                slot = k;
            }
        }
        if (most <= 0)
            return "SHARED SURFACE PROSPECT DEPLETED";
        const auto g = size_t(resources[slot]);
        if (p.massKg() + 1 > p.capacityKg ||
            p.volumeLitres() + goodInfo(resources[slot]).litresPerKg > p.capacityLitres)
            return "MAKE ROOM FOR A ONE KG SEALED CORE";
        if (prospect == x.prospects.end()) {
            x.prospects.push_back({*key, {}, available});
            prospect = std::prev(x.prospects.end());
        }
        ++prospect->extracted[slot];
        ++x.minedKg;
        ++s.imported[g];
        ++s.exported[g];
        j.resource = slot;
        j.phase = ExpeditionPhase::Core;
        reconcilePlayer(p);
        return "SEALED CORE ABOARD / 1 KG / ORIGINAL SOURCE ID RETAINED / RETURN TO A PORT LAB";
    }
    if (c.action == EconomicAction::OpenSpecimen) {
        if (!j.carrying() || j.phase == ExpeditionPhase::Return)
            return "NO UNFINISHED CORE TO INSPECT";
        const auto lab = e.siteIndex(c.port);
        if (!lab || !at(c.port))
            return "PARK THE FULL SIZE SHIP AT A PORT TO OPEN ITS SPECIMEN LAB";
        const auto& l = e.sites_[*lab];
        if (l.health < 500 || l.outageUntil > e.tick_)
            return "PORT LAB HAS NO POWER OR CREW";
        j.lab = c.port;
        j.phase = ExpeditionPhase::Assay;
        return "SPECIMEN LAB READY / PARKED SHIP / CANONICAL SOURCE RECONSTRUCTION";
    }
    if (c.action == EconomicAction::AssaySpecimen) {
        if (j.phase != ExpeditionPhase::Assay || !j.lab || c.specimenLab != j.lab || !c.observer ||
            !c.surveyTarget || !std::isfinite(c.workSeconds) || c.workSeconds < 8 ||
            !surveyAccess(u, *c.observer, *c.surveyTarget))
            return "ASSAY NEEDS OWNED CORE AND COMPLETED PROBE OBSERVATION";
        const auto lab = e.siteIndex(*j.lab);
        if (!lab || e.sites_[*lab].outageUntil > e.tick_)
            return "LAB POWER LOST / CORE AND PROGRESS RETAINED";
        unsigned bit = *c.surveyTarget == j.source      ? 1
                       : *c.surveyTarget == j.grain()   ? 2
                       : *c.surveyTarget == j.lattice() ? 4
                                                        : 0;
        if (!bit)
            return "STRUCTURE IS OUTSIDE THIS CORE'S REGISTERED MATERIAL LINEAGE";
        if ((bit == 2 && !(j.assays & 1)) || (bit == 4 && !(j.assays & 2)))
            return "ASSAY ROCK, THEN MINERAL, THEN ATOMIC STRUCTURE";
        j.assays |= bit;
        if (j.assays == 7) {
            j.phase = ExpeditionPhase::Return;
            return "ATOMIC ASSAY COMPLETE / ADAPTED EXCHANGER SPECIFIED / SHIFT-F11 RETURN TO SHIP";
        }
        return "SOURCE ASSAY RECORDED / F4 NEXT STRUCTURE / J MAGNIFY";
    }
    if (c.action == EconomicAction::RepairExpedition) {
        if (j.phase != ExpeditionPhase::Return)
            return "COMPLETE ALL THREE CORE ASSAYS FIRST";
        if (!at(j.colony))
            return "RETURN TO THE ORIGINAL COLONY TO FIT THE EXCHANGER";
        if (s.project || s.modules[size_t(FacilityModule::Power)] >= j.powerLevel)
            return "COLONY UTILITIES CHANGED / CANCEL TO RETURN RESERVED FUNDS AND CORE";
        if (s.health < 500 || d.workers < 2)
            return "COLONY FITTING CREW NEEDS RELIEF SUPPLIES FIRST";
        const auto before = economicVitals(d, s, e.tick_);
        // Installed refit parts remain physical capital in facilityMaterials().
        for (size_t g = 0; g < goodCount; ++g)
            s.imported[g] += materialParts[g];
        const auto g = size_t(resources[j.resource]);
        ++s.imported[g];
        ++s.consumed[g];
        s.modules[size_t(FacilityModule::Power)] = j.powerLevel;
        s.condition = std::min(1000u, s.condition + 150);
        s.health = std::min(1000u, s.health + 25);
        p.credits += j.reward;
        s.householdCash += j.escrow - j.reward;
        e.externalMoneyDelta_ += j.escrow - j.reward;
        j.escrow = 0;
        auto& outcome = e.recordOutcome(c, *index, OutcomeKind::Maintenance, materialParts, j.reward, before);
        outcome.conditionRestored = outcome.after.condition - before.condition;
        outcome.crewPaid = labour;
        j.outcome = outcome.id;
        j.phase = ExpeditionPhase::Complete;
        j.lab.reset();
        reconcilePlayer(p);
        e.event("CORE EXPEDITION COMPLETE / " + d.name + " / EXCHANGER ONLINE / +25% BASE POWER");
        return "EXCHANGER FITTED / +25% BASE POWER / " + std::to_string(j.reward / 100) +
               " CR PAID / OUTCOME SAVED";
    }
    return "UNKNOWN MATERIAL EXPEDITION ORDER";
}
std::string ExpeditionLedger::serialize() const {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "INFINITY_EXPEDITIONS 1 " << state_.jobs.size();
    for (const auto& j : state_.jobs)
        o << ' ' << j.id << ' ' << std::quoted(j.colony.home.str()) << ' ' << j.colony.slot << ' '
          << std::quoted(j.source.str()) << ' ' << unsigned(j.phase) << ' ' << j.reward << ' ' << j.escrow
          << ' ' << j.assays << ' ' << j.resource << ' ' << j.powerLevel << ' ' << j.descriptor << ' '
          << j.acceptedRevision << ' ' << j.outcome << ' ' << bool(j.lab) << ' '
          << std::quoted(j.lab ? j.lab->home.str() : "-") << ' ' << (j.lab ? j.lab->slot : 0) << ' '
          << std::quoted(j.colonyName);
    return o.str();
}
bool ExpeditionLedger::restore(std::string_view text, const Universe& u) {
    try {
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string tag;
        unsigned version;
        size_t count;
        if (!(in >> tag >> version >> count) || !matchesSaveTag(tag, "INFINITY_EXPEDITIONS") || version != 1 ||
            count > maxMaterialExpeditions)
            return false;
        ExpeditionState candidate;
        std::set<uint64_t> ids;
        std::set<Address> systems;
        for (size_t i = 0; i < count; ++i) {
            MaterialExpedition j;
            std::string home, source, lab;
            unsigned phase, slot;
            bool hasLab;
            if (!(in >> j.id >> std::quoted(home) >> j.colony.slot >> std::quoted(source) >> phase >>
                  j.reward >> j.escrow >> j.assays >> j.resource >> j.powerLevel >> j.descriptor >>
                  j.acceptedRevision >> j.outcome >> hasLab >> std::quoted(lab) >> slot >>
                  std::quoted(j.colonyName)))
                return false;
            j.colony.home = Address::parse(home);
            j.source = Address::parse(source);
            if (phase > unsigned(ExpeditionPhase::Cancelled) || !j.id || !ids.insert(j.id).second ||
                j.source.kind() != Kind::Rock || j.colony.home.kind() != Kind::Planet ||
                !j.colony.home.ancestorOf(j.source) || j.reward != 90000 || j.assays > 7 || j.resource > 3 ||
                j.powerLevel < 1 || j.powerLevel > 3 || j.colonyName.size() > 128 ||
                j.acceptedRevision > 1000000000000ULL ||
                descriptorFingerprint(u.describe(j.source)) != j.descriptor ||
                !systems.insert(*surveySystem(j.source)).second)
                return false;
            j.phase = ExpeditionPhase(phase);
            const bool active = j.phase >= ExpeditionPhase::Survey && j.phase <= ExpeditionPhase::Return;
            if (j.escrow !=
                (active ? j.reward + facilityRefit(FacilityModule::Power, j.powerLevel).labourCents : 0))
                return false;
            if ((j.assays & 2) && !(j.assays & 1))
                return false;
            if ((j.assays & 4) && !(j.assays & 2))
                return false;
            if ((j.phase == ExpeditionPhase::Return || j.phase == ExpeditionPhase::Complete) && j.assays != 7)
                return false;
            if ((j.phase == ExpeditionPhase::Assay && j.assays == 7) ||
                ((j.phase == ExpeditionPhase::Complete) != (j.outcome != 0)))
                return false;
            if (j.phase <= ExpeditionPhase::Core && j.assays)
                return false;
            if (hasLab) {
                j.lab = ActivityId{Address::parse(lab), slot};
                if (j.lab->home.kind() != Kind::Planet || surveySystem(j.lab->home) != surveySystem(j.source))
                    return false;
            }
            if ((j.phase == ExpeditionPhase::Assay || j.phase == ExpeditionPhase::Return) != hasLab)
                return false;
            const auto reading = surveyReading(u, j.grain(), 3, 0);
            j.materialDescription = reading.mineral + " / " + reading.formation;
            candidate.jobs.push_back(j);
        }
        in >> std::ws;
        if (!in.eof())
            return false;
        state_ = std::move(candidate);
        return true;
    } catch (...) {
        return false;
    }
}
size_t ExpeditionLedger::residentBytes() const {
    size_t bytes = sizeof(*this) + state_.jobs.capacity() * sizeof(MaterialExpedition);
    for (const auto& j : state_.jobs)
        bytes += j.colonyName.capacity() + j.materialDescription.capacity() +
                 j.source.path.capacity() * sizeof(Segment) +
                 j.colony.home.path.capacity() * sizeof(Segment) +
                 (j.lab ? j.lab->home.path.capacity() * sizeof(Segment) : 0);
    return bytes;
}
} // namespace infinity
