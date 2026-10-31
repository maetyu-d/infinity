#include "infinite/Economy.hpp"
#include <algorithm>
#include <cmath>
namespace infinite {
std::vector<EconomicPortSignal> economicPortSignals(const EconomySnapshot& s) {
    std::vector<EconomicPortSignal> result;
    if (!s.network || s.sites.size() != s.network->sites.size())
        return result;
    result.reserve(s.sites.size());
    for (size_t i = 0; i < s.sites.size(); ++i) {
        const auto& d = s.network->sites[i];
        const auto& state = s.sites[i];
        EconomicPortSignal signal;
        auto free = [&](Good good) {
            const auto g = size_t(good);
            return state.stock[g] - state.reserved[g];
        };
        const bool offline = state.outageUntil > s.tick;
        signal.power =
            offline ? 0.f
            : d.orbital
                ? 1.f
                : float(std::clamp(double(economicPower(d, state, s.tick)) / std::max(1u, d.power), 0., 1.));
        float essentials = 1;
        if (d.population) {
            essentials = std::min(float(free(Good::Food)) / std::max<int64_t>(1, d.population / 500),
                                  float(free(Good::Water)) / std::max<int64_t>(1, d.population / 150));
            if (d.sealed)
                essentials = std::min(essentials,
                                      float(free(Good::Oxygen)) / std::max<int64_t>(1, d.population / 250));
        }
        signal.readiness =
            float(state.health) / 1000 * float(state.condition) / 1000 * std::clamp(essentials, 0.f, 1.f);
        signal.lighting = .06f + .94f * signal.power * (.25f + .75f * signal.readiness);
        double best = 0;
        for (size_t g = 0; g < goodCount; ++g) {
            const auto missing = d.target[g] - (state.stock[g] - state.reserved[g]);
            if (missing <= 0 || d.target[g] <= 0)
                continue;
            const bool essential = d.population && (Good(g) == Good::Food || Good(g) == Good::Water ||
                                                    (d.sealed && Good(g) == Good::Oxygen));
            const double score = double(missing) / d.target[g] * (essential ? 3. : 1.);
            if (score > best) {
                best = score;
                signal.shortage = Good(g);
                signal.missingKg = missing;
            }
        }
        const auto parts = std::clamp<int64_t>(
            std::min({free(Good::Alloy) / 2, free(Good::Machinery) * 5, free(Good::Tools) * 10}), 0, 10);
        const bool crew = !offline && state.health >= 500 && state.condition >= 300;
        signal.brief = offline            ? "PLANT OFFLINE"
                       : signal.missingKg ? "SHORT " + std::string(goodInfo(signal.shortage).name)
                                          : "STOCK BALANCED";
        signal.alert = offline ? 1.f : signal.missingKg ? .65f : 0.f;
        signal.report[0] = "POWER " + std::to_string(int(std::round(signal.power * 100))) + "% / READINESS " +
                           std::to_string(int(std::round(signal.readiness * 100))) + "%";
        signal.report[1] = !crew        ? "REPAIRS OFFLINE / CREW UNAVAILABLE"
                           : !parts     ? "NO REPAIR PARTS / F2 SERVICES"
                           : parts < 10 ? "LOW REPAIR PARTS / UP TO " + std::to_string(parts) + " POINTS"
                                        : "REPAIR PARTS READY / F2 SERVICES";
        signal.report[2] = signal.missingKg ? "NEEDS " + std::to_string(signal.missingKg) + "KG " +
                                                  std::string(goodInfo(signal.shortage).name)
                                            : "LOCAL TARGET STOCKS COVERED";
        const EconomySnapshot::TradeRoute* opportunity = nullptr;
        for (const auto& route : s.routes)
            if (route.source == i && (!opportunity || route.revenueCents - route.costCents >
                                                          opportunity->revenueCents - opportunity->costCents))
                opportunity = &route;
        if (opportunity)
            signal.report[2] += " / ROUTE " + std::string(goodInfo(opportunity->good).name) + " +" +
                                std::to_string((opportunity->revenueCents - opportunity->costCents) / 100) +
                                "CR";
        if (!crew && !offline && !signal.missingKg) {
            signal.brief = "SERVICE CREW UNAVAILABLE";
            signal.alert = .65f;
        }
        result.push_back(std::move(signal));
    }
    return result;
}
void annotateEconomicPort(ActivityBody& body, const EconomySnapshot& s) {
    body.economicBrief.clear();
    body.economicReport = {};
    body.economicAlert = 0;
    const auto i = s.siteIndex(body.id);
    if (!i || *i >= s.portSignals.size())
        return;
    const auto& signal = s.portSignals[*i];
    body.economicBrief = s.catchingUp ? "ECONOMY SYNCING" : signal.brief;
    body.economicReport = signal.report;
    body.economicAlert = s.catchingUp ? 0.f : signal.alert;
    if (s.catchingUp)
        body.economicReport[0] = "CATCHING UP / LAST PUBLISHED STATE";
}
} // namespace infinite
