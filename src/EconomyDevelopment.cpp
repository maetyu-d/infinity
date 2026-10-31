#include "infinite/Economy.hpp"
#include <algorithm>
#include <numeric>
namespace infinite {
namespace {
int64_t total(const Stock& s) {
    return std::accumulate(s.begin(), s.end(), int64_t(0));
}
int64_t volume(const Stock& s) {
    int64_t n = 0;
    for (size_t g = 0; g < goodCount; g++)
        n += s[g] * goodInfo(Good(g)).litresPerKg;
    return n;
}
} // namespace
FacilityRefit facilityRefit(FacilityModule module, unsigned level) {
    if (size_t(module) >= facilityModuleCount || level < 1 || level > 3)
        return {};
    FacilityRefit r;
    r.labourCents = 30000 * level;
    r.ticks = 12 * level;
    auto add = [&](Good g, int64_t kg) { r.materials[size_t(g)] = kg * level; };
    add(Good::Alloy, 20);
    add(Good::Machinery, 5);
    add(Good::Electronics, 2);
    switch (module) {
    case FacilityModule::Storage:
        r.name = "WAREHOUSE REFIT";
        r.effect = "+25% BASE STORAGE / LEVEL";
        add(Good::HabitatParts, 10);
        break;
    case FacilityModule::Power:
        r.name = "UTILITY REFIT";
        r.effect = "+25% BASE POWER / LEVEL";
        add(Good::Silicon, 8);
        break;
    case FacilityModule::Automation:
        r.name = "AUTOMATION REFIT";
        r.effect = "+25% LABOUR PRODUCTIVITY / LEVEL";
        add(Good::Tools, 5);
        break;
    default:
        break;
    }
    return r;
}
int64_t economicPower(const EconomicSite& d, const EconomicSiteState& s, uint64_t tick) {
    int64_t power = int64_t((d.power * (4 + s.modules[size_t(FacilityModule::Power)]) / 4.) /
                            (1 + std::abs(d.temperature - 290) / 250 + (d.sealed ? .2 : 0)));
    if (d.climate.pressureBar > 0 && glm::length(d.metres) > 0) {
        auto weather = sampleWeather(d.climate, glm::normalize(d.metres), 0, tick * economyTickSeconds);
        power = power * int64_t(1000 - std::clamp(weather.storm, 0., 1.) * 350) / 1000;
    }
    return power;
}
bool economicConstructionActive(const EconomicSite& d, const EconomicSiteState& s, uint64_t tick) {
    if (!s.project || s.outageUntil > tick)
        return false;
    const auto& p = *s.project;
    return p.delivered == facilityRefit(p.module, s.modules[size_t(p.module)] + 1).materials &&
           (d.orbital || (int64_t(d.workers) * s.health / 1000 >= 2 && economicPower(d, s, tick) >= 1 &&
                          s.health >= 500 && s.condition >= 300));
}
Stock facilityMaterials(const EconomicSiteState& s) {
    Stock result{};
    for (size_t m = 0; m < facilityModuleCount; m++)
        for (unsigned level = 1; level <= s.modules[m]; level++) {
            auto r = facilityRefit(FacilityModule(m), level);
            for (size_t g = 0; g < goodCount; g++)
                result[g] += r.materials[g];
        }
    if (s.project)
        for (size_t g = 0; g < goodCount; g++)
            result[g] += s.project->delivered[g];
    return result;
}
int64_t economicPendingPayload(const EconomicCommand& c, const EconomicSiteState& s) {
    if (c.action == EconomicAction::Buy || c.action == EconomicAction::PickupHaul)
        return std::clamp<int64_t>(c.kg, 0, 4000);
    if (c.action == EconomicAction::CancelProject && s.project)
        return total(s.project->delivered);
    return 0;
}
std::string Economy::invest(const EconomicCommand& c, EconomyPlayer& player, size_t i) {
    auto& s = sites_[i];
    const auto& d = network_->sites[i];
    if (c.action == EconomicAction::CollectDividends) {
        if (!s.dividends)
            return "NO DIVIDENDS AVAILABLE / RETURNS REQUIRE CASH SURPLUS";
        const auto amount = s.dividends;
        player.credits += amount;
        externalMoneyDelta_ -= amount;
        s.dividendsPaid += amount;
        s.dividends = 0;
        return "COLLECTED " + std::to_string(amount / 100) + " CR / DIVIDENDS";
    }
    if (c.action == EconomicAction::CancelProject) {
        if (!s.project)
            return "NO ACTIVE REFIT";
        const auto& p = *s.project;
        if (player.massKg() + total(p.delivered) > player.capacityKg ||
            player.volumeLitres() + volume(p.delivered) > player.capacityLitres)
            return "MAKE CARGO SPACE FOR RETURNED MATERIALS";
        for (size_t g = 0; g < goodCount; g++) {
            player.cargo[g] += p.delivered[g];
            s.exported[g] += p.delivered[g];
        }
        player.credits += p.escrow;
        externalMoneyDelta_ -= p.escrow;
        s.project.reset();
        event(d.name + " / REFIT CANCELLED / unused labour refunded");
        return "REFIT CANCELLED / MATERIALS AND UNUSED ESCROW RETURNED";
    }
    const auto m = size_t(c.module);
    if (m >= facilityModuleCount)
        return "INVALID FACILITY MODULE";
    if (d.orbital && c.module != FacilityModule::Storage)
        return "NO SURFACE PLANT / WAREHOUSE REFIT ONLY";
    if (!s.project) {
        if (s.modules[m] >= 3)
            return "REFIT AT MAXIMUM LEVEL";
        auto r = facilityRefit(c.module, s.modules[m] + 1);
        if (c.limitCents < r.labourCents)
            return "REFIT QUOTE CHANGED / REVIEW COST";
        if (player.credits < r.labourCents)
            return "INSUFFICIENT CREDITS FOR LABOUR ESCROW";
        player.credits -= r.labourCents;
        externalMoneyDelta_ += r.labourCents;
        FacilityProject p;
        p.module = c.module;
        p.escrow = r.labourCents;
        s.project = p;
        event(d.name + " / PLAYER FUNDED " + std::string(r.name));
    }
    auto& p = *s.project;
    if (p.module != c.module)
        return "COMPLETE OR CANCEL THE EXISTING REFIT FIRST";
    auto r = facilityRefit(p.module, s.modules[m] + 1);
    int64_t delivered = 0;
    for (size_t g = 0; g < goodCount; g++) {
        auto kg = std::min(player.availableCargo(Good(g)), r.materials[g] - p.delivered[g]);
        p.delivered[g] += kg;
        player.cargo[g] -= kg;
        s.imported[g] += kg;
        delivered += kg;
    }
    return total(p.delivered) == total(r.materials)
               ? "REFIT SUPPLIED / CONSTRUCTION SCHEDULED"
               : "DELIVERED " + std::to_string(delivered) + " KG / REFIT AWAITS MATERIALS";
}
void Economy::development() {
    for (size_t i = 0; i < sites_.size(); i++) {
        auto& s = sites_[i];
        const auto& d = network_->sites[i];
        if (s.project) {
            auto& p = *s.project;
            const auto m = size_t(p.module);
            const auto r = facilityRefit(p.module, s.modules[m] + 1);
            // Existing orbital service crews handle warehouse work. Surface work depends on
            // the existing population, utility supply and equipment; it pauses during outages.
            if (economicConstructionActive(d, s, tick_)) {
                p.progress++;
                auto remaining = r.labourCents - r.labourCents * p.progress / r.ticks;
                auto wage = p.escrow - remaining;
                p.escrow = remaining;
                if (d.orbital)
                    carrierCash_ += wage;
                else
                    s.householdCash += wage;
                if (p.progress == r.ticks) {
                    s.modules[m]++;
                    s.project.reset();
                    event(d.name + " / " + std::string(r.name) + " COMMISSIONED");
                }
            }
        }
        if (tick_ % 12 == 0) {
            // A finite share of positive operating cash flow, after an endowment-sized reserve.
            // Capital funding goes directly to labour escrow, never into this profit measure.
            const auto levels = std::accumulate(s.modules.begin(), s.modules.end(), 0u);
            const auto reserve = 2000000 + int64_t(d.population) * 150;
            const auto gain = std::max<int64_t>(0, s.cash - s.operatingBaseline);
            const auto distribution =
                std::min(gain * (levels * 2) / 100, std::max<int64_t>(0, s.cash - reserve));
            s.cash -= distribution;
            s.dividends += distribution;
            s.operatingBaseline = s.cash;
        }
    }
}
std::vector<EconomySnapshot::TradeRoute> economicTradeRoutes(const EconomySnapshot& s) {
    std::vector<EconomySnapshot::TradeRoute> result;
    if (!s.network || s.catchingUp || s.sites.size() != s.network->sites.size())
        return result;
    const auto roomKg = std::max<int64_t>(0, s.player.capacityKg - s.player.massKg());
    const auto roomLitres = std::max<int64_t>(0, s.player.capacityLitres - s.player.volumeLitres());
    const auto count = s.sites.size();
    for (size_t g = 0; g < goodCount; g++) {
        std::vector<size_t> sellers, buyers;
        std::vector<EconomyQuote> spots;
        for (size_t i = 0; i < count; i++) {
            spots.push_back(s.quote(i, Good(g), 1));
            if (spots.back().available && spots.back().buyCents <= s.player.credits)
                sellers.push_back(i);
            if (s.sites[i].cash >= spots.back().sellCents &&
                economicCapacity(s.network->sites[i], s.sites[i]) > total(s.sites[i].stock))
                buyers.push_back(i);
        }
        std::stable_sort(sellers.begin(), sellers.end(),
                         [&](size_t a, size_t b) { return spots[a].buyCents < spots[b].buyCents; });
        std::stable_sort(buyers.begin(), buyers.end(),
                         [&](size_t a, size_t b) { return spots[a].sellCents > spots[b].sellCents; });
        sellers.resize(std::min<size_t>(4, sellers.size()));
        buyers.resize(std::min<size_t>(4, buyers.size()));
        for (auto a : sellers)
            for (auto b : buyers) {
                if (a == b || spots[a].buyCents >= spots[b].sellCents)
                    continue;
                int64_t high = std::min(
                    {int64_t(1000), roomKg, roomLitres / goodInfo(Good(g)).litresPerKg, spots[a].available,
                     economicCapacity(s.network->sites[b], s.sites[b]) - total(s.sites[b].stock)});
                // Find the largest affordable order with positive marginal gain. Integrated
                // quotes are then used for the displayed total, never spot-price multiplication.
                int64_t low = 0;
                while (low < high) {
                    auto kg = (low + high + 1) / 2;
                    auto ask = s.quote(a, Good(g), kg), bid = s.quote(b, Good(g), kg);
                    const auto previousAsk = kg > 1 ? s.quote(a, Good(g), kg - 1).buyCents : 0;
                    const auto previousBid = kg > 1 ? s.quote(b, Good(g), kg - 1).sellCents : 0;
                    if (ask.buyCents <= s.player.credits && bid.sellCents <= s.sites[b].cash &&
                        bid.sellCents - previousBid > ask.buyCents - previousAsk)
                        low = kg;
                    else
                        high = kg - 1;
                }
                if (!low)
                    continue;
                auto cost = s.quote(a, Good(g), low).buyCents;
                auto revenue = s.quote(b, Good(g), low).sellCents;
                result.push_back(
                    {unsigned(a), unsigned(b), Good(g), low, cost, revenue,
                     glm::length(s.network->sites[a].systemMetres - s.network->sites[b].systemMetres)});
            }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const auto gainA = a.revenueCents - a.costCents, gainB = b.revenueCents - b.costCents;
        return gainA != gainB ? gainA > gainB : a.distanceMetres < b.distanceMetres;
    });
    if (result.size() > 12)
        result.resize(12);
    return result;
}
} // namespace infinite
