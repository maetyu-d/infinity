#include "infinite/Economy.hpp"
#include <algorithm>
#include <istream>
#include <ostream>
namespace infinite {
namespace {
void write(std::ostream& o, const Stock& s) {
    for (auto n : s)
        o << ' ' << n;
}
bool read(std::istream& in, Stock& s, int64_t max) {
    for (auto& n : s)
        if (!(in >> n) || n < 0 || n > max)
            return false;
    return true;
}
void write(std::ostream& o, const EconomicVitals& v) {
    o << ' ' << v.health << ' ' << v.condition << ' ' << v.power << ' ' << v.readiness << ' '
      << v.repairPoints << ' ' << v.outputKg;
}
bool read(std::istream& in, EconomicVitals& v) {
    return bool(in >> v.health >> v.condition >> v.power >> v.readiness >> v.repairPoints >> v.outputKg) &&
           v.health <= 1000 && v.condition <= 1000 && v.power <= 1000 && v.readiness <= 1000 &&
           v.repairPoints <= 10 && v.outputKg >= 0 && v.outputKg <= 40000000000000LL;
}
} // namespace
void Economy::writeRecovery(std::ostream& o) const {
    o << "RECOVERY " << nextRecovery_ << ' ' << recoveries_.size() << '\n';
    for (const auto& j : recoveries_) {
        o << j.id << ' ' << j.deadline << ' ' << j.outageUntil << ' ' << j.outcome << ' ' << j.site << ' '
          << j.pairs << ' ' << j.progress << ' ' << unsigned(j.kind) << ' ' << unsigned(j.phase) << ' '
          << j.escrow;
        write(o, j.required);
        write(o, j.delivered);
        write(o, j.rates);
        o << '\n';
    }
    o << "OUTCOMES " << nextOutcome_ << ' ' << outcomes_.size() << '\n';
    for (const auto& r : outcomes_) {
        o << r.id << ' ' << r.command << ' ' << r.tick << ' ' << r.updated << ' ' << r.observeUntil << ' '
          << r.site << ' ' << unsigned(r.kind) << ' ' << r.payment << ' ' << r.crewPaid << ' '
          << r.conditionRestored << ' ' << r.working << ' ' << r.outageCleared;
        write(o, r.delivered);
        write(o, r.before);
        write(o, r.after);
        o << '\n';
    }
}
bool Economy::readRecovery(std::istream& in) {
    std::string tag;
    size_t count;
    constexpr uint64_t max = 1000000000000ULL;
    if (!(in >> tag >> nextRecovery_ >> count) || tag != "RECOVERY" || !nextRecovery_ ||
        nextRecovery_ > max || count > 32)
        return false;
    unsigned active = 0;
    for (size_t n = 0; n < count; ++n) {
        RecoveryJob j;
        unsigned kind, phase;
        if (!(in >> j.id >> j.deadline >> j.outageUntil >> j.outcome >> j.site >> j.pairs >> j.progress >>
              kind >> phase >> j.escrow) ||
            !j.id || j.id >= nextRecovery_ || j.deadline > max || j.outageUntil > max ||
            j.site >= sites_.size() || j.pairs > 50 || j.progress > j.pairs || kind > 1 ||
            phase > unsigned(RecoveryPhase::Expired) || j.escrow < 0 || j.escrow > 1000000000 ||
            !read(in, j.required, 1000) || !read(in, j.delivered, 1000) || !read(in, j.rates, 100000))
            return false;
        j.kind = RecoveryKind(kind);
        j.phase = RecoveryPhase(phase);
        if (network_->sites[j.site].orbital || !network_->sites[j.site].population)
            return false;
        int64_t escrow = j.kind == RecoveryKind::Maintenance ? (j.pairs - j.progress) * 100 : 0;
        bool any = false;
        for (size_t g = 0; g < goodCount; ++g) {
            if (j.delivered[g] > j.required[g] || bool(j.rates[g]) != bool(j.required[g]))
                return false;
            any |= j.required[g] > 0;
            escrow += (j.required[g] - j.delivered[g]) * j.rates[g];
            if (j.kind == RecoveryKind::Relief && j.required[g] &&
                (Good(g) != Good::Food && Good(g) != Good::Water && Good(g) != Good::Oxygen))
                return false;
            if (j.kind == RecoveryKind::Relief && !network_->sites[j.site].sealed &&
                Good(g) == Good::Oxygen && j.required[g])
                return false;
            if (j.kind == RecoveryKind::Maintenance &&
                j.required[g] != (Good(g) == Good::Machinery ? int64_t(j.pairs + 2)
                                  : Good(g) == Good::Filters ? int64_t(j.pairs)
                                                             : 0))
                return false;
            if (j.phase == RecoveryPhase::Open && j.delivered[g])
                return false;
        }
        if (!any ||
            (j.kind == RecoveryKind::Relief &&
             (j.pairs || j.progress || j.phase == RecoveryPhase::Working)) ||
            (j.kind == RecoveryKind::Maintenance && j.pairs < 8) ||
            (j.progress && j.delivered != j.required) ||
            (j.phase == RecoveryPhase::Working && (j.delivered != j.required || j.progress == j.pairs)) ||
            (j.phase == RecoveryPhase::Complete && (j.delivered != j.required || j.progress != j.pairs)) ||
            (j.phase <= RecoveryPhase::Working ? j.escrow != escrow : j.escrow != 0))
            return false;
        if (j.phase <= RecoveryPhase::Working)
            ++active;
        if (std::any_of(recoveries_.begin(), recoveries_.end(), [&](const auto& x) {
                return x.id == j.id ||
                       (x.site == j.site && x.kind == j.kind && x.phase <= RecoveryPhase::Working &&
                        j.phase <= RecoveryPhase::Working);
            }))
            return false;
        recoveries_.push_back(j);
    }
    if (active > 8 || !(in >> tag >> nextOutcome_ >> count) || tag != "OUTCOMES" || !nextOutcome_ ||
        nextOutcome_ > max || count > 16)
        return false;
    for (size_t n = 0; n < count; ++n) {
        EconomicOutcome r;
        unsigned kind, working, cleared;
        if (!(in >> r.id >> r.command >> r.tick >> r.updated >> r.observeUntil >> r.site >> kind >>
              r.payment >> r.crewPaid >> r.conditionRestored >> working >> cleared) ||
            !r.id || r.id >= nextOutcome_ || !r.command || r.tick > tick_ || r.updated < r.tick ||
            r.updated > tick_ || r.observeUntil > max || r.observeUntil < r.tick || r.site >= sites_.size() ||
            kind > unsigned(OutcomeKind::Maintenance) || r.payment < 0 || r.payment > 1000000000 ||
            r.crewPaid < 0 || r.crewPaid > 5000 || r.crewPaid % 100 || r.conditionRestored > 750 ||
            working > 1 || cleared > 1 || !read(in, r.delivered, maxShipPayloadKg) || !read(in, r.before) ||
            !read(in, r.after))
            return false;
        r.kind = OutcomeKind(kind);
        r.working = working;
        r.outageCleared = cleared;
        if ((r.working || r.crewPaid || r.conditionRestored || r.outageCleared) &&
            r.kind != OutcomeKind::Maintenance)
            return false;
        if (std::any_of(outcomes_.begin(), outcomes_.end(),
                        [&](const auto& o) { return o.id == r.id || o.command == r.command; }))
            return false;
        if (!outcomes_.empty() && r.id <= outcomes_.back().id)
            return false;
        outcomes_.push_back(r);
    }
    for (const auto& j : recoveries_) {
        if (j.outcome >= nextOutcome_)
            return false;
        if (j.phase == RecoveryPhase::Working &&
            !std::any_of(outcomes_.begin(), outcomes_.end(), [&](const auto& o) {
                return o.id == j.outcome && o.site == j.site && o.kind == OutcomeKind::Maintenance &&
                       o.working && o.crewPaid == int64_t(j.progress) * 100;
            }))
            return false;
    }
    for (const auto& o : outcomes_)
        if (o.working && !std::any_of(recoveries_.begin(), recoveries_.end(), [&](const auto& j) {
                return j.outcome == o.id && j.phase == RecoveryPhase::Working;
            }))
            return false;
    return true;
}
} // namespace infinite
