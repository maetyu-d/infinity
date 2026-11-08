#include "infinity/Arrival.hpp"
#include "infinity/Economy.hpp"
#include "infinity/OrbitalDebris.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
namespace {
bool serves(const Address& destination, const ActivityId& id) {
    return destination == id.home || destination.ancestorOf(id.home);
}
}
std::vector<ArrivalEncounter> proceduralArrivals(const Universe& u, const Address& destination,
                                               const EconomySnapshot* s, std::span<const ActivityContact> contacts) {
    std::vector<ArrivalEncounter> result;
    const auto seed = u.describe(destination).seed;
    const bool ledger = s && !s->catchingUp && s->network &&
        (destination == s->network->system || s->network->system.ancestorOf(destination));
    const auto add = [&](ArrivalEncounter e, bool inbound = false) {
        if (!inbound && !serves(destination, e.target)) return;
        e.order = mix64(seed ^ u.describe(e.target.home).seed ^ mix64(e.target.slot) ^ 0x4152524956414cULL);
        const auto old = std::find_if(result.begin(), result.end(), [&](const auto& x) { return x.target == e.target; });
        if (old == result.end()) result.push_back(std::move(e));
        else if (e.kind < old->kind) *old = std::move(e);
    };
    if (ledger) {
        // Recovery status is authoritative. A repaired/rescued wreck must not
        // keep broadcasting an arrival distress call.
        for (const auto& f : s->flights) {
            if (f.disabledAt < 0 || !serves(destination, f.id)) continue;
            const auto receipt = std::find_if(s->wrecks.begin(), s->wrecks.end(), [&](const auto& w) { return w.id == f.id; });
            if (receipt == s->wrecks.end() || receipt->rescued || !receipt->survivor ||
                receipt->rescueDeadline < s->tick) continue;
            const auto aboard = std::any_of(s->player.rescued.begin(), s->player.rescued.end(),
                                           [&](const auto& p) { return p.source == f.id; });
            if (aboard) continue;
            const auto body = sampleActivity(u.describe(f.id.home), f.id.slot, 0);
            add({f.id, ArrivalKind::Distress, "DISTRESS / " + body.name,
                 "DISABLED FREIGHTER / J APPROACH / HOLD 0 CLOSE AND SLOW"});
        }
        for (const auto& f : s->flights) {
            if (f.disabledAt >= 0 || f.kg <= 0 ||
                (f.phase != FreightPhase::Transit && f.phase != FreightPhase::Holding) ||
                f.destination >= s->network->sites.size()) continue;
            const auto& port = s->network->sites[f.destination];
            if (!serves(destination, port.id)) continue;
            const auto body = sampleActivity(u.describe(f.id.home), f.id.slot, 0);
            add({f.id, ArrivalKind::Traffic, (f.weatherHold ? "WEATHER HOLD / " : "INBOUND FREIGHT / ") + body.name,
                 std::to_string(f.kg) + " KG " + std::string(goodInfo(f.good).name) +
                 " TO " + port.name + " / J APPROACH / I HAIL"}, true);
        }
        for (size_t i = 0; i < s->portSignals.size() && i < s->network->sites.size(); ++i) {
            const auto& signal = s->portSignals[i];
            const auto& site = s->network->sites[i];
            if (signal.missingKg > 0)
                add({site.id, ArrivalKind::Shortage, site.name + " / " + signal.brief,
                     "NEEDS " + std::to_string(signal.missingKg) + " KG " +
                     std::string(goodInfo(signal.shortage).name) + " / J APPROACH / F2 MARKET"});
            else if (signal.readiness >= .3)
                add({site.id, ArrivalKind::Traffic, "PORT ARRIVAL / " + site.name,
                     signal.brief + " / J APPROACH / F2 MARKET"});
        }
    }
    for (const auto& c : contacts) {
        if (!serves(destination, c.body.id) || c.occluded || c.distance > 200000 ||
            !std::isfinite(c.distance) || c.distance < 0) continue;
        const auto& b = c.body;
        ArrivalEncounter e{b.id, ArrivalKind::Traffic, b.name, b.status, c.distance};
        if (b.disabled) continue; // only the ledger can promise recovery work
        if (b.hostile) {
            if (c.distance > 30000) continue;
            e.kind = ArrivalKind::Hostile;
            e.title = "HOSTILE CONTACT / " + b.name;
            e.detail = b.status + " / H TARGET / B TAKE HELM";
        } else if (landableDebris(b)) {
            e.kind = ArrivalKind::Mining;
            e.title = "MINING LEAD / " + b.name;
            e.detail = "ORBITAL MATERIAL / J MATCH COURSE / L LAND / CTRL-SPACE MINE";
        } else if (b.kind == ActivityKind::Survey) {
            e.kind = ArrivalKind::Mining;
            e.title = "SURVEY ACTIVITY / " + b.name;
            e.detail = "RESOURCE SURVEY IN PROGRESS / J APPROACH / I HAIL";
        } else if (b.kind == ActivityKind::Patrol) {
            e.kind = ArrivalKind::Patrol;
            e.title = "LOCAL SECURITY / " + b.name;
            e.detail = "PATROL ON STATION / J APPROACH / I HAIL";
        } else if (b.kind == ActivityKind::Freighter || b.kind == ActivityKind::Ferry) {
            e.title = "ARRIVAL TRAFFIC / " + b.name;
            e.detail = b.status + " / J APPROACH / I HAIL";
        } else continue;
        add(std::move(e));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.order != b.order) return a.order < b.order;
        return a.target < b.target;
    });
    if (result.size() > 8) result.resize(8);
    return result;
}
bool ArrivalDirector::update(const Universe& u, const FlightInstruments& t, const EconomySnapshot* s,
                             std::span<const ActivityContact> contacts, double seconds, const Address* observer) {
    if (!std::isfinite(seconds) || seconds < 0) return false;
    if (t.warpArrivalSequence != observed_) {
        observed_ = t.warpArrivalSequence;
        pending_ = t.warpDestination;
        active_.reset();
        pendingAt_ = seconds;
        encounters_.clear(); selected_ = 0;
    }
    if (t.shrinking || t.landed || t.warp == WarpPhase::Route || t.warp == WarpPhase::Manual) {
        pending_.reset(); active_.reset(); encounters_.clear();
    }
    if (seconds >= expires_) { active_.reset(); encounters_.clear(); }
    if (active_ && observer) {
        auto system = *active_;
        while (system.kind() > Kind::System) system = system.parent();
        if (system != *observer && !system.ancestorOf(*observer)) {
            active_.reset(); encounters_.clear();
        }
    }
    if (active_ && seconds >= refreshAt_ && (!s || (!s->catchingUp && s->network &&
        (s->network->system == *active_ || s->network->system.ancestorOf(*active_))))) {
        const auto selected = current() ? std::optional<ActivityId>(current()->target) : std::nullopt;
        encounters_ = proceduralArrivals(u, *active_, s, contacts);
        selected_ = 0;
        if (selected)
            for (size_t i = 0; i < encounters_.size(); ++i)
                if (encounters_[i].target == *selected) selected_ = i;
        refreshAt_ = seconds + 2;
    }
    if (!pending_ || t.fieldActive || seconds - pendingAt_ < 2.8) return false;
    const bool synced = s && !s->catchingUp && s->network &&
        (s->network->system == *pending_ || s->network->system.ancestorOf(*pending_));
    if (!synced && seconds - pendingAt_ < 3) return false;
    auto next = proceduralArrivals(u, *pending_, synced ? s : nullptr, contacts);
    if (next.empty() && seconds - pendingAt_ < 12) return false;
    active_ = pending_;
    pending_.reset();
    encounters_ = std::move(next);
    expires_ = seconds + 60;
    refreshAt_ = seconds + 2;
    return !encounters_.empty();
}
const ArrivalEncounter* ArrivalDirector::current() const {
    return selected_ < encounters_.size() ? &encounters_[selected_] : nullptr;
}
const ArrivalEncounter* ArrivalDirector::select(std::optional<ActivityId> selected) {
    if (const auto* e = current(); e && selected && e->target == *selected)
        selected_ = (selected_ + 1) % encounters_.size();
    return current();
}
void ArrivalDirector::annotate(FlightInstruments& t) const {
    t.arrivalComms.clear();
    if (const auto* e = current())
        t.arrivalComms = e->title + " / SHIFT-I CONTACT " + std::to_string(selected_ + 1) +
                         "/" + std::to_string(encounters_.size());
}
} // namespace infinity
