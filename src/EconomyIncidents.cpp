#include "infinity/Economy.hpp"
#include <algorithm>
#include <cmath>
#include <istream>
#include <limits>
#include <ostream>
#include <set>

namespace infinity {
namespace {
constexpr size_t maxIncidents = 128;

const WreckRecovery* wreckFor(const EconomySnapshot& s, const ActivityId& id) {
    const auto it = std::find_if(s.wrecks.begin(), s.wrecks.end(), [&](const auto& w) { return w.id == id; });
    return it == s.wrecks.end() ? nullptr : &*it;
}

bool passengerAboard(const EconomySnapshot& s, const ActivityId& id) {
    return std::any_of(s.player.rescued.begin(), s.player.rescued.end(), [&](const auto& p) { return p.source == id; });
}

bool activeRelief(const RecoveryJob& j, unsigned site, Good good) {
    return j.site == site && j.kind == RecoveryKind::Relief && j.phase <= RecoveryPhase::Accepted &&
           j.required[size_t(good)] > 0;
}

std::string duration(double seconds) {
    const auto value = uint64_t(std::ceil(std::clamp(seconds, 0., 1e12)));
    if (value >= 3600)
        return std::to_string(value / 3600) + "H " + std::to_string(value % 3600 / 60) + "M";
    return std::to_string(value / 60) + "M " + std::to_string(value % 60) + "S";
}

std::string deadline(const EconomySnapshot& s, uint64_t tick) {
    return tick < s.tick ? "DEADLINE PASSED" : "DUE IN " + duration((tick - s.tick) * economyTickSeconds);
}
} // namespace

int64_t householdRation(const EconomicSite& site, Good good) {
    if (!site.population)
        return 0;
    const int divisor = good == Good::Food ? 500 : good == Good::Water ? 150
                       : good == Good::Oxygen && site.sealed ? 250 : 0;
    return divisor ? std::max<int64_t>(1, int64_t(site.population) / divisor) : 0;
}

const FreightIncident* Economy::incidentFor(unsigned flight) const {
    const auto it = std::find_if(incidents_.begin(), incidents_.end(), [&](const auto& i) { return i.flight == flight; });
    return it == incidents_.end() ? nullptr : &*it;
}

void Economy::openIncident(unsigned flight) {
    const auto& f = freight_[flight - 1];
    if (f.kg <= 0 || (f.phase != FreightPhase::Transit && f.phase != FreightPhase::Holding))
        return;
    if (incidents_.size() == maxIncidents) {
        const auto old = std::find_if(incidents_.begin(), incidents_.end(), [&](const auto& i) {
            return tick_ > i.opened + freightIncidentWindow;
        });
        if (old == incidents_.end())
            return;
        incidents_.erase(old);
    }
    const auto health = sites_[f.destination].health;
    FreightIncident incident;
    incident.flight = flight;
    incident.opened = tick_;
    incident.displacedKg = f.kg;
    incident.initialHealth = incident.lowestHealth = health;
    const auto ration = householdRation(network_->sites[f.destination], f.good);
    if (ration && sites_[f.destination].stock[size_t(f.good)] - sites_[f.destination].reserved[size_t(f.good)] < ration * 2)
        incident.shortage = tick_;
    incidents_.push_back(incident);
}

void Economy::incidentCargo(size_t destination, Good good, int64_t kg, bool player, unsigned preferred) {
    auto credit = [&](FreightIncident& incident) {
        const auto& f = freight_[incident.flight - 1];
        if (f.destination != destination || f.good != good)
            return;
        const auto replacement = std::min(kg, incident.remainingKg());
        (player ? incident.playerKg : incident.carrierKg) += replacement;
        kg -= replacement;
    };
    if (preferred) {
        const auto it = std::find_if(incidents_.begin(), incidents_.end(), [&](const auto& i) { return i.flight == preferred; });
        if (it != incidents_.end())
            credit(*it);
    }
    for (auto& incident : incidents_) {
        if (!kg)
            break;
        if (incident.flight != preferred && tick_ <= incident.opened + freightIncidentWindow)
            credit(incident);
    }
}

void Economy::observeIncidents() {
    for (auto& incident : incidents_) {
        if (tick_ > incident.opened + freightIncidentWindow)
            continue;
        const auto& f = freight_[incident.flight - 1];
        const auto& state = sites_[f.destination];
        const auto ration = householdRation(network_->sites[f.destination], f.good);
        if (!ration)
            continue;
        const auto stock = state.stock[size_t(f.good)] - state.reserved[size_t(f.good)];
        incident.lowestHealth = std::min(incident.lowestHealth, state.health);
        if (!incident.shortage && stock < ration * 2)
            incident.shortage = tick_;
        if (incident.shortage && !incident.reliefRequested && std::any_of(recoveries_.begin(), recoveries_.end(),
                [&](const auto& j) { return activeRelief(j, f.destination, f.good); }))
            incident.reliefRequested = tick_;
        if (incident.shortage && !incident.restored && stock >= ration * 2 && state.health >= incident.initialHealth)
            incident.restored = tick_;
    }
}

void Economy::writeIncidents(std::ostream& out) const {
    out << "FREIGHT_INCIDENTS " << incidents_.size() << '\n';
    for (const auto& i : incidents_)
        out << i.flight << ' ' << i.opened << ' ' << i.shortage << ' ' << i.reliefRequested << ' ' << i.restored
            << ' ' << i.displacedKg << ' ' << i.carrierKg << ' ' << i.playerKg << ' ' << i.initialHealth
            << ' ' << i.lowestHealth << '\n';
}

bool Economy::readIncidents(std::istream& in) {
    std::string tag;
    size_t count;
    if (!(in >> tag >> count) || tag != "FREIGHT_INCIDENTS" || count > maxIncidents)
        return false;
    std::set<unsigned> flights;
    for (size_t n = 0; n < count; ++n) {
        FreightIncident i;
        if (!(in >> i.flight >> i.opened >> i.shortage >> i.reliefRequested >> i.restored >> i.displacedKg
                 >> i.carrierKg >> i.playerKg >> i.initialHealth >> i.lowestHealth) ||
            !i.flight || i.flight > freight_.size() || !flights.insert(i.flight).second || i.opened > tick_ ||
            (!incidents_.empty() && i.opened < incidents_.back().opened) ||
            i.displacedKg <= 0 || i.displacedKg > 1000 || i.carrierKg < 0 || i.carrierKg > i.displacedKg ||
            i.playerKg < 0 || i.playerKg > i.displacedKg - i.carrierKg || i.initialHealth > 1000 ||
            i.lowestHealth > i.initialHealth)
            return false;
        const auto& f = freight_[i.flight - 1];
        if (f.disabledAt < 0 || f.kg > i.displacedKg ||
            (f.phase != FreightPhase::Transit && f.phase != FreightPhase::Holding) ||
            (i.shortage && (i.shortage < i.opened || i.shortage > tick_ ||
                            !householdRation(network_->sites[f.destination], f.good))))
            return false;
        for (const auto when : {i.reliefRequested, i.restored})
            if (when && (!i.shortage || when < i.shortage || when > tick_ || when > i.opened + freightIncidentWindow))
                return false;
        incidents_.push_back(i);
    }
    return true;
}

const FreightIncident* selectedIncident(const EconomySnapshot& s, size_t selected) {
    return selected < s.incidents.size() ? &s.incidents[s.incidents.size() - 1 - selected] : nullptr;
}

std::optional<EconomySnapshot::Job> economicIncidentJob(const EconomySnapshot& s, size_t selected,
                                                        unsigned response) {
    const auto* incident = selectedIncident(s, selected);
    if (!s.network || !incident || !incident->flight || incident->flight > s.flights.size())
        return {};
    const auto& flight = s.flights[incident->flight - 1];
    EconomySnapshot::Job job;
    if (response == 0) {
        const auto* wreck = wreckFor(s, flight.id);
        if (!wreck || !wreck->survivor)
            return {};
        const bool aboard = passengerAboard(s, flight.id);
        if (wreck->rescued && !aboard)
            return {};
        job.distress = flight.id;
        job.source = job.destination = wreck->accountingSite;
        job.issuer = s.network->sites[job.destination].id;
        job.target = aboard ? job.issuer : flight.id;
        job.action = aboard ? EconomicAction::DisembarkRescued : EconomicAction::RecoverWreck;
        job.deadline = wreck->rescueDeadline;
        return job;
    }
    if (response == 1) {
        const auto h = std::find_if(s.hauls.begin(), s.hauls.end(), [&](const auto& x) {
            return x.interruptedFlight == incident->flight && x.phase <= HaulPhase::InTransit;
        });
        if (h == s.hauls.end() || (h->phase == HaulPhase::Open && !incident->remainingKg()))
            return {};
        job.haul = true;
        job.source = h->source;
        job.destination = h->destination;
        job.id = h->id;
        job.good = h->good;
        job.kg = h->kg;
        job.deadline = h->deadline;
        job.action = h->phase == HaulPhase::Open ? EconomicAction::AcceptHaul
                     : h->phase == HaulPhase::Reserved ? EconomicAction::PickupHaul : EconomicAction::DeliverHaul;
        job.target = s.network->sites[h->phase == HaulPhase::InTransit ? h->destination : h->source].id;
        return job;
    }
    if (!incident->reliefRequested)
        return {};
    const auto j = std::find_if(s.recoveries.begin(), s.recoveries.end(), [&](const auto& x) {
        constexpr uint64_t reliefOfferWindow = 288;
        return activeRelief(x, flight.destination, flight.good) &&
               x.deadline <= incident->opened + freightIncidentWindow + reliefOfferWindow;
    });
    if (j == s.recoveries.end())
        return {};
    job.recovery = true;
    job.source = job.destination = j->site;
    job.id = j->id;
    job.good = flight.good;
    job.deadline = j->deadline;
    job.action = j->phase == RecoveryPhase::Open ? EconomicAction::AcceptRecovery : EconomicAction::DeliverRecovery;
    job.target = s.network->sites[j->site].id;
    for (size_t g = 0; g < goodCount; ++g) {
        job.required[g] = j->required[g] - j->delivered[g];
        job.kg += job.required[g];
    }
    return job;
}

void economicIncidentView(MarketView& v, const EconomySnapshot& s, size_t selected, const MarketContext& context) {
    v.selected = std::min(selected, s.incidents.empty() ? 0 : s.incidents.size() - 1);
    const auto* incident = selectedIncident(s, v.selected);
    if (!incident || !incident->flight || incident->flight > s.flights.size()) {
        v.site = "LOCAL INCIDENTS";
        v.role = "INCIDENT BOARD";
        v.detail = {"NO LOADED FREIGHT INCIDENTS IN THIS SYSTEM", "INCIDENTS FOLLOW ACTUAL DISABLED CARGO RUNS"};
        return;
    }
    const auto& flight = s.flights[incident->flight - 1];
    const auto& port = s.network->sites[flight.destination];
    const auto& state = s.sites[flight.destination];
    const auto good = std::string(goodInfo(flight.good).name);
    const auto ration = householdRation(port, flight.good);
    const auto free = state.stock[size_t(flight.good)] - state.reserved[size_t(flight.good)];
    const auto* wreck = wreckFor(s, flight.id);
    const bool aboard = passengerAboard(s, flight.id);
    const bool closed = s.tick > incident->opened + freightIncidentWindow;
    const bool rescuePending = aboard || (wreck && wreck->rescueEscrow && !wreck->rescued);
    const bool settled = !rescuePending && !incident->remainingKg() && (!incident->shortage || incident->restored);
    v.role = std::string(closed || settled
                         ? "INCIDENT DEBRIEF " : "FREIGHT INCIDENT ") + std::to_string(v.selected + 1) +
             "/" + std::to_string(s.incidents.size());
    v.site = port.name;
    v.detail = {
        "FREIGHTER DISABLED / " + std::to_string(incident->displacedKg) + " KG " + good + " INTERRUPTED",
        s.network->sites[flight.source].name + " > " + port.name,
        ration ? good + " RESERVE / " + std::to_string(free) + " KG / " +
                     duration(double(free) / ration * economyTickSeconds) + " AT NORMAL USE"
               : good + " IN STORE / " + std::to_string(free) + " KG",
        ration ? "HOUSEHOLD HEALTH " + std::to_string(state.health / 10) + "% / LOW " +
                     std::to_string(incident->lowestHealth / 10) + "% / BEFORE " +
                     std::to_string(incident->initialHealth / 10) + "%"
               : "LOCAL INVENTORY TRACKED FROM ACTUAL DELIVERIES",
        "REPLACED " + std::to_string(incident->playerKg) + " KG PLAYER / " +
            std::to_string(incident->carrierKg) + " KG CARRIERS / " + std::to_string(incident->remainingKg()) + " KG LEFT",
        incident->restored ? "RECOVERY OBSERVED / STOCK AND HEALTH RESTORED"
            : incident->shortage ? "RESERVE RAN LOW / " + std::string(closed ? "OBSERVATION WINDOW CLOSED" : "RECOVERY PENDING")
                                  : "NO CRITICAL RESERVE LOSS OBSERVED",
    };
    v.detail.push_back(wreck && wreck->survivor ? "RESCUE / " + wreck->survivor->name +
        (aboard ? " ABOARD / RETURN TO PORT" : wreck->rescued ? " HANDED OVER" : " / SIGNAL CONFIRMED")
        : "RESCUE / NO CONFIRMED SURVIVOR");
    const auto relay = economicIncidentJob(s, v.selected, 1);
    int64_t inbound = 0;
    double earliest = std::numeric_limits<double>::infinity();
    for (const auto& carrier : s.flights) {
        if (carrier.disabledAt >= 0 || carrier.destination != flight.destination || carrier.good != flight.good ||
            carrier.phase == FreightPhase::Idle)
            continue;
        inbound += carrier.kg + carrier.reservedKg;
        if (carrier.phase == FreightPhase::Transit)
            earliest = std::min(earliest, carrier.endSeconds);
    }
    const std::string incoming = inbound > 0 ? std::to_string(inbound) + " KG WITH OTHER CARRIERS" : "";
    v.detail.push_back(!relay ? (!incident->remainingKg() ? "RELAY / SHIPMENT COVERED"
        : inbound > 0 ? "RELAY / " + incoming : "RELAY / NO FUNDED ROUTE AVAILABLE")
        : relay->action == EconomicAction::PickupHaul ? "RELAY / ACCEPTED / AWAITING PICKUP"
        : relay->action == EconomicAction::DeliverHaul ? "RELAY / SEALED CARGO ABOARD" : "RELAY / REPLACEMENT ROUTE AVAILABLE");
    v.detail.push_back(economicIncidentJob(s, v.selected, 2) ? "RELIEF / COLONY REQUESTING SUPPLIES"
        : incident->reliefRequested ? "RELIEF / EARLIER REQUEST CLOSED" : "RELIEF / NO LINKED REQUEST");

    const auto job = economicIncidentJob(s, v.selected, context.incidentResponse);
    v.summary = context.incidentResponse == 0 ? "RESCUE" : context.incidentResponse == 1 ? "RELAY" : "RELIEF";
    if (!job) {
        v.plan = "NO ACTIVE RESPONSE / LEFT-RIGHT TO REVIEW OPTIONS";
        if (context.incidentResponse == 1 && incident->remainingKg() && std::isfinite(earliest))
            v.planTiming = "NEXT CARRIER ETA " + duration(earliest - s.tick * economyTickSeconds);
        return;
    }
    if (job->distress) {
        const auto& issuer = s.network->sites[job->destination];
        v.plan = aboard ? "F4 " + issuer.name + " / BERTH AND ENTER TO HAND OVER"
                        : "F4 WRECK / WITHIN 120 M / BELOW 3 M/S / HOLD 0 / FREE ONE BERTH";
        if (context.systemPosition && (aboard || context.wreckPosition)) {
            const auto mass = s.player.massKg();
            double seconds = economicFlightLeg(glm::length(issuer.systemMetres - *context.systemPosition),
                                               context.cruiseSpeed, mass, issuer.orbital);
            if (!aboard) {
                const auto location = *context.wreckPosition;
                seconds = economicFlightLeg(glm::length(location - *context.systemPosition), context.cruiseSpeed, mass, true)
                    + economicFlightLeg(glm::length(issuer.systemMetres - location), context.cruiseSpeed, mass, issuer.orbital) + 6.;
            }
            if (std::isfinite(seconds))
                v.planTiming = "EST " + duration(seconds) + " / ";
        }
        v.planTiming += wreck->rescueEscrow ? deadline(s, job->deadline) : "STANDARD PORT HANDOVER";
        return;
    }
    v.plan = std::to_string(job->kg) + " KG / F4 " + s.network->sites[job->action == EconomicAction::AcceptHaul ||
        job->action == EconomicAction::PickupHaul ? job->source : job->destination].name + " / ENTER " +
        (job->action == EconomicAction::AcceptHaul || job->action == EconomicAction::AcceptRecovery ? "ACCEPT"
         : job->action == EconomicAction::PickupHaul ? "LOAD" : "DELIVER");
    const auto plan = economicPlanJob(s, *job, context);
    v.planTiming = (plan.timed ? "EST " + duration(plan.seconds) + " / "
                  : plan.procurement ? "SOURCE LISTED GOODS / " : "") + deadline(s, job->deadline);
    if (!plan.fits)
        v.planTiming += " / HOLD TOO SMALL";
    else if (plan.timed && ration && plan.seconds > double(free) / ration * economyTickSeconds)
        v.planTiming += " / ARRIVAL AFTER RESERVE ESTIMATE";
    if (job->recovery) {
        std::string goods = "RELIEF / ";
        for (size_t g = 0; g < goodCount; ++g)
            if (job->required[g])
                goods += std::to_string(job->required[g]) + " KG " + std::string(goodInfo(Good(g)).name) + "  ";
        v.detail[8] = std::move(goods);
    }
}

std::string economicIncidentRadio(const EconomySnapshot& before, const EconomySnapshot& after) {
    if (!before.network || !after.network || before.network->system != after.network->system || after.catchingUp)
        return {};
    for (auto it = after.incidents.rbegin(); it != after.incidents.rend(); ++it) {
        const auto& i = *it;
        if (!i.flight || i.flight > after.flights.size() || after.tick > i.opened + freightIncidentWindow)
            continue;
        const auto& f = after.flights[i.flight - 1];
        const auto& port = after.network->sites[f.destination].name;
        const auto old = std::find_if(before.incidents.begin(), before.incidents.end(), [&](const auto& x) { return x.flight == i.flight; });
        if (old == before.incidents.end())
            return "PORT COMMS / FREIGHTER DISABLED / " + port + " WAITING FOR " + std::string(goodInfo(f.good).name);
        if (i.restored && !old->restored)
            return "PORT COMMS / " + port + " REPORTS RESERVES AND HEALTH RESTORED";
        if (!i.remainingKg() && old->remainingKg())
            return "PORT COMMS / REPLACEMENT SHIPMENT RECEIVED AT " + port;
        if (i.reliefRequested && !old->reliefRequested)
            return "PORT COMMS / " + port + " REQUESTS HOUSEHOLD RELIEF";
        if (i.shortage && !old->shortage)
            return "PORT COMMS / " + port + " RUNNING LOW ON " + std::string(goodInfo(f.good).name);
        const auto* wreck = wreckFor(after, f.id);
        if (wreck && wreck->survivor && wreck->rescued && !passengerAboard(after, f.id) && passengerAboard(before, f.id))
            return "PORT COMMS / " + wreck->survivor->name + " SAFELY HANDED OVER";
    }
    return {};
}
} // namespace infinity
