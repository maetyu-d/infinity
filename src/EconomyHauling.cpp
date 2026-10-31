#include "infinite/Economy.hpp"
#include <algorithm>
#include <numeric>
namespace infinite {
namespace {
int64_t total(const Stock& s) {
    return std::accumulate(s.begin(), s.end(), int64_t(0));
}
bool waiting(HaulPhase p) {
    return p == HaulPhase::Open || p == HaulPhase::Reserved;
}
bool active(HaulPhase p) {
    return waiting(p) || p == HaulPhase::InTransit;
}
} // namespace
int64_t EconomyPlayer::sealedKg() const {
    return haul && haul->onboard ? haul->kg : 0;
}
int64_t EconomyPlayer::availableCargo(Good good) const {
    return cargo.at(size_t(good)) - (haul && haul->onboard && haul->good == good ? haul->kg : 0);
}
std::string_view EconomyPlayer::carrierRank() const {
    const auto onTime = deliveries - lateDeliveries;
    return onTime >= 50   ? "VETERAN CARRIER"
           : onTime >= 20 ? "TRUSTED CARRIER"
           : onTime >= 5  ? "COURIER"
                          : "NEW CARRIER";
}
bool Economy::reconcilePlayer(EconomyPlayer& player) const {
    const auto owned = std::count_if(hauls_.begin(), hauls_.end(), [](const auto& h) {
        return h.phase == HaulPhase::Reserved || h.phase == HaulPhase::InTransit;
    });
    if (!player.haul || player.haul->system != network_->system)
        return owned == 0;
    auto& p = *player.haul;
    auto h = std::find_if(hauls_.begin(), hauls_.end(), [&](const auto& x) { return x.id == p.id; });
    if (h == hauls_.end()) {
        if (!p.onboard && p.id < nextHaul_ && tick_ > p.deadline && owned == 0) {
            player.haul.reset();
            player.returnedHauls++;
            return true;
        }
        return false;
    }
    if (p.source != network_->sites[h->source].id || p.destination != network_->sites[h->destination].id ||
        p.good != h->good || p.kg != h->kg || p.deadline != h->deadline)
        return false;
    if (!p.onboard && (h->phase == HaulPhase::Expired || h->phase == HaulPhase::Cancelled)) {
        player.haul.reset();
        player.returnedHauls++;
        return owned == 0;
    }
    return owned == 1 && (p.onboard ? h->phase == HaulPhase::InTransit && player.cargo[size_t(p.good)] >= p.kg
                                    : h->phase == HaulPhase::Reserved);
}
void Economy::haulOffers() {
    for (auto& h : hauls_) {
        if (waiting(h.phase) && tick_ > h.deadline) {
            sites_[h.source].reserved[size_t(h.good)] -= h.kg;
            sites_[h.destination].cash += h.escrow;
            h.escrow = 0;
            h.phase = HaulPhase::Expired;
            event("HAUL EXPIRED / stock released and buyer escrow refunded");
        } else if (h.phase == HaulPhase::InTransit && tick_ == h.deadline + 1)
            event("HAUL OVERDUE / cargo remains sealed / late fee reduced");
    }
    std::erase_if(hauls_, [&](const auto& h) { return !active(h.phase) && tick_ > h.deadline + 24; });
    if (tick_ % 12 || sites_.empty())
        return;
    // At most eight funded waiting offers, 32 retained records and two new offers per batch.
    size_t open = std::count_if(hauls_.begin(), hauls_.end(), [](const auto& h) { return waiting(h.phase); });
    if (open >= 8 || hauls_.size() >= 32)
        return;
    std::vector<Stock> inbound(sites_.size());
    for (const auto& f : freight_)
        if (f.phase != FreightPhase::Idle)
            inbound[f.destination][size_t(f.good)] += f.kg;
    for (const auto& h : hauls_)
        if (active(h.phase) && tick_ <= h.deadline)
            inbound[h.destination][size_t(h.good)] += h.kg;
    std::array<std::vector<size_t>, goodCount> sellers;
    for (size_t g = 0; g < goodCount; g++) {
        for (size_t i = 0; i < sites_.size(); i++)
            if (sites_[i].stock[g] - sites_[i].reserved[g] - network_->sites[i].target[g] >= 10)
                sellers[g].push_back(i);
        std::stable_sort(sellers[g].begin(), sellers[g].end(), [&](size_t a, size_t b) {
            return unitPrice(a, Good(g), sites_[a].stock[g] - sites_[a].reserved[g], true) <
                   unitPrice(b, Good(g), sites_[b].stock[g] - sites_[b].reserved[g], true);
        });
        if (sellers[g].size() > 4)
            sellers[g].resize(4);
    }
    for (unsigned offered = 0; offered < 2 && open < 8 && hauls_.size() < 32; offered++) {
        double best = 0;
        HaulContract chosen;
        for (size_t b = 0; b < sites_.size(); b++)
            for (size_t g = 0; g < goodCount; g++) {
                auto need = network_->sites[b].target[g] - sites_[b].stock[g] - inbound[b][g];
                if (need < 10)
                    continue;
                for (auto a : sellers[g]) {
                    if (a == b)
                        continue;
                    const auto surplus =
                        sites_[a].stock[g] - sites_[a].reserved[g] - network_->sites[a].target[g];
                    const auto unit = unitPrice(a, Good(g), sites_[a].stock[g] - sites_[a].reserved[g], true);
                    const int64_t fee = 30 + int64_t(travelTicks(a, b)) * 2;
                    auto kg =
                        std::min({int64_t(300), surplus, need,
                                  economicCapacity(network_->sites[b], sites_[b]) - total(sites_[b].stock),
                                  sites_[b].cash / (unit * 2 + fee)});
                    if (kg < 10)
                        continue;
                    const double score = double(need) / (1 + travelTicks(a, b));
                    if (score <= best)
                        continue;
                    // Integrate the locked source price; reduce until the buyer can fund it.
                    auto cost = quote(a, Good(g), kg).buyCents;
                    while (kg >= 10 && cost + kg * fee > sites_[b].cash) {
                        kg /= 2;
                        cost = kg >= 10 ? quote(a, Good(g), kg).buyCents : 0;
                    }
                    if (kg < 10)
                        continue;
                    best = score;
                    chosen.source = unsigned(a);
                    chosen.destination = unsigned(b);
                    chosen.good = Good(g);
                    chosen.kg = kg;
                    chosen.cost = cost;
                    chosen.fee = kg * fee;
                }
            }
        if (!chosen.kg)
            break;
        chosen.id = nextHaul_++;
        chosen.deadline = tick_ + std::max<uint64_t>(144, travelTicks(chosen.source, chosen.destination) * 4);
        chosen.escrow = chosen.cost + chosen.fee;
        sites_[chosen.source].reserved[size_t(chosen.good)] += chosen.kg;
        sites_[chosen.destination].cash -= chosen.escrow;
        inbound[chosen.destination][size_t(chosen.good)] += chosen.kg;
        hauls_.push_back(chosen);
        open++;
        event(network_->sites[chosen.destination].name + " / FUNDED PLAYER HAUL / " +
              std::to_string(chosen.kg) + " KG " + std::string(goodInfo(chosen.good).name));
    }
}
std::string Economy::haulCommand(const EconomicCommand& c, EconomyPlayer& player) {
    auto h = std::find_if(hauls_.begin(), hauls_.end(), [&](const auto& x) { return x.id == c.contract; });
    if (h == hauls_.end())
        return "HAUL NO LONGER AVAILABLE";
    auto& source = sites_[h->source];
    auto& destination = sites_[h->destination];
    const auto g = size_t(h->good);
    if (c.good != h->good || c.kg != h->kg)
        return "CONSIGNMENT CHANGED / REVIEW JOB";
    if (c.action == EconomicAction::AcceptHaul) {
        if (h->phase != HaulPhase::Open || tick_ > h->deadline)
            return "HAUL OFFER CLOSED";
        if (player.haul)
            return "FINISH OR RETURN YOUR ACTIVE HAUL FIRST";
        if (c.port != network_->sites[h->source].id)
            return "SELECT THE HAUL PICKUP PORT";
        h->phase = HaulPhase::Reserved;
        player.haul = PlayerHaul{network_->system,
                                 h->id,
                                 h->deadline,
                                 network_->sites[h->source].id,
                                 network_->sites[h->destination].id,
                                 h->good,
                                 h->kg,
                                 false};
        return "HAUL ACCEPTED / F4 PICKUP / CARGO AND FEE RESERVED";
    }
    if (!player.haul || player.haul->system != network_->system || player.haul->id != h->id)
        return "THIS IS NOT YOUR ACTIVE HAUL";
    auto& p = *player.haul;
    if (c.action == EconomicAction::PickupHaul) {
        if (h->phase != HaulPhase::Reserved || p.onboard)
            return "HAUL IS NOT AWAITING PICKUP";
        if (c.port != p.source)
            return "PICK UP AT THE CONTRACT SOURCE";
        if (player.massKg() + h->kg > player.capacityKg ||
            player.volumeLitres() + h->kg * goodInfo(h->good).litresPerKg > player.capacityLitres)
            return "MAKE ROOM FOR THE SEALED CONSIGNMENT";
        source.stock[g] -= h->kg;
        source.reserved[g] -= h->kg;
        source.exported[g] += h->kg;
        player.cargo[g] += h->kg;
        h->phase = HaulPhase::InTransit;
        p.onboard = true;
        event("PLAYER PICKUP / " + std::to_string(h->kg) + " KG SEALED " +
              std::string(goodInfo(h->good).name));
        return "SEALED CARGO LOADED / F4 DELIVERY / NO PURCHASE CHARGE";
    }
    if (c.action == EconomicAction::DeliverHaul) {
        if (h->phase != HaulPhase::InTransit || !p.onboard)
            return "PICK UP THE CONSIGNMENT FIRST";
        if (c.port != p.destination)
            return "DELIVER AT THE CONTRACT DESTINATION";
        if (economicCapacity(network_->sites[h->destination], destination) - total(destination.stock) < h->kg)
            return "DESTINATION WAREHOUSE FULL / CARGO RETAINED";
        const bool late = tick_ > h->deadline;
        const auto payment = late ? h->fee / 2 : h->fee;
        destination.stock[g] += h->kg;
        destination.imported[g] += h->kg;
        player.cargo[g] -= h->kg;
        player.credits += payment;
        source.cash += h->cost;
        destination.cash += h->escrow - h->cost - payment;
        externalMoneyDelta_ -= payment;
        h->escrow = 0;
        h->phase = HaulPhase::Complete;
        player.deliveries++;
        player.lateDeliveries += late;
        player.freightEarnings += payment;
        player.haul.reset();
        event("PLAYER HAUL DELIVERED / " + std::to_string(payment / 100) + " CR" +
              (late ? " / LATE" : " / ON TIME"));
        return "HAUL DELIVERED / " + std::to_string(payment / 100) + " CR" +
               (late ? " / LATE FEE" : " / ON TIME");
    }
    if (c.action == EconomicAction::CancelHaul) {
        if (h->phase == HaulPhase::Reserved) {
            source.reserved[g] -= h->kg;
            destination.cash += h->escrow;
        } else if (h->phase == HaulPhase::InTransit && p.onboard) {
            if (c.port != p.source)
                return "RETURN SEALED CARGO TO ITS SOURCE / F4 IN JOBS";
            if (economicCapacity(network_->sites[h->source], source) - total(source.stock) < h->kg)
                return "SOURCE WAREHOUSE FULL / DELIVER TO THE BUYER";
            source.stock[g] += h->kg;
            source.imported[g] += h->kg;
            player.cargo[g] -= h->kg;
            destination.cash += h->escrow;
        } else
            return "HAUL ALREADY CLOSED";
        h->escrow = 0;
        h->phase = HaulPhase::Cancelled;
        player.haul.reset();
        player.returnedHauls++;
        event("PLAYER HAUL WITHDRAWN / goods and buyer funds reconciled");
        return "HAUL WITHDRAWN / SEALED CARGO AND ESCROW RETURNED";
    }
    return "INVALID HAUL ACTION";
}
std::vector<EconomySnapshot::Job> economicJobs(const EconomySnapshot& s) {
    std::vector<EconomySnapshot::Job> jobs;
    if (!s.network)
        return jobs;
    auto time = [&](uint64_t deadline) {
        return deadline < s.tick ? std::string("OVERDUE")
                                 : std::to_string((deadline - s.tick) * 5 / 60) + " ECON H LEFT";
    };
    auto addHaul = [&](const HaulContract& h) {
        const auto fee = s.tick > h.deadline ? h.fee / 2 : h.fee;
        const bool mine =
            s.player.haul && s.player.haul->system == s.network->system && s.player.haul->id == h.id;
        EconomySnapshot::Job job;
        job.haul = true;
        job.id = h.id;
        job.good = h.good;
        job.kg = h.kg;
        job.action = h.phase == HaulPhase::Open       ? EconomicAction::AcceptHaul
                     : h.phase == HaulPhase::Reserved ? EconomicAction::PickupHaul
                                                      : EconomicAction::DeliverHaul;
        job.target = s.network->sites[h.phase == HaulPhase::InTransit ? h.destination : h.source].id;
        job.title =
            std::string(mine ? h.phase == HaulPhase::InTransit ? "DELIVER SEALED " : "PICKUP " : "HAUL ") +
            std::to_string(h.kg) + "KG " + std::string(goodInfo(h.good).name) + " / FEE " +
            std::to_string(fee / 100) + "CR / " + time(h.deadline);
        job.route = s.network->sites[h.source].name + " > " + s.network->sites[h.destination].name;
        jobs.push_back(std::move(job));
    };
    for (const auto& h : s.hauls)
        if (h.phase == HaulPhase::Reserved || h.phase == HaulPhase::InTransit)
            addHaul(h);
    auto addSupply = [&](const EconomicContract& c) {
        EconomySnapshot::Job job;
        job.id = c.id;
        job.good = c.good;
        job.kg = c.requested - c.delivered;
        job.target = s.network->sites[c.site].id;
        job.title = std::string(c.phase == ContractPhase::Accepted ? "SUPPLY ACTIVE " : "SUPPLY ") +
                    std::to_string(job.kg) + "KG " + std::string(goodInfo(c.good).name) + " / " +
                    std::to_string(c.unitCents / 100) + "CR/KG / " + time(c.deadline);
        job.route = s.network->sites[c.site].name + " / PLAYER SOURCES THE GOODS";
        jobs.push_back(std::move(job));
    };
    for (const auto& c : s.contracts)
        if (c.phase == ContractPhase::Accepted && jobs.size() < 12)
            addSupply(c);
    for (const auto& h : s.hauls)
        if (h.phase == HaulPhase::Open && jobs.size() < 12)
            addHaul(h);
    for (const auto& c : s.contracts)
        if (c.phase == ContractPhase::Open && jobs.size() < 12)
            addSupply(c);
    return jobs;
}
} // namespace infinite
