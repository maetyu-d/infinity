#include "infinite/Economy.hpp"
#include <algorithm>
#include <cmath>
namespace infinite {
EconomicJobPlan economicPlanJob(const EconomySnapshot& s, const EconomySnapshot::Job& job,
                                const MarketContext& context) {
    EconomicJobPlan p;
    if (!s.network || job.source >= s.network->sites.size() || job.destination >= s.network->sites.size() ||
        job.kg < 0 || job.kg > maxShipPayloadKg || size_t(job.good) >= goodCount)
        return p;
    const auto& source = s.network->sites[job.source];
    const auto& destination = s.network->sites[job.destination];
    const bool loaded = job.haul && job.action == EconomicAction::DeliverHaul;
    const auto missing = std::max<int64_t>(0, job.kg - s.player.availableCargo(job.good));
    const auto incoming = job.haul ? (loaded ? 0 : job.kg) : missing;
    p.fits = s.player.massKg() + incoming <= s.player.capacityKg &&
             s.player.volumeLitres() + incoming * goodInfo(job.good).litresPerKg <= s.player.capacityLitres;
    p.procurement = !job.haul && missing > 0;
    p.deliveryMetres = job.haul && !loaded ? glm::length(destination.systemMetres - source.systemMetres) : 0;
    if (!context.systemPosition || !std::isfinite(glm::length(*context.systemPosition)) ||
        !std::isfinite(context.cruiseSpeed) || context.cruiseSpeed < 1e6 || p.procurement)
        return p;
    if (job.haul && !loaded)
        p.pickupMetres = glm::length(source.systemMetres - *context.systemPosition);
    else
        p.deliveryMetres = glm::length(destination.systemMetres - *context.systemPosition);
    // Flight-time estimate, not the NPC dispatch clock. Include acceleration,
    // field/proximity slow-down and port handling. It assumes N cruise between
    // ports; obstacles, alignment, weather and player choices can take longer.
    const double acceleration = 384000. / (12000. + s.player.massKg() + incoming);
    auto leg = [&](double distance, const EconomicSite& port) {
        const double cap = context.cruiseSpeed;
        const double localDistance = std::min(distance, port.orbital ? 1e5 : 2e5);
        const double local = 2 * std::sqrt(localDistance / acceleration);
        const double far = std::max(0., distance - localDistance);
        const double field = far > 0 ? far / cap + 2 * std::log1p(far / 1e5) / .32 : 0;
        return local + field + (port.orbital ? 30. : 120.);
    };
    p.seconds = leg(p.deliveryMetres, destination);
    if (job.haul && !loaded)
        p.seconds += leg(p.pickupMetres, source);
    p.slackSeconds = (double(job.deadline) - double(s.tick)) * economyTickSeconds - p.seconds;
    p.timed = std::isfinite(p.seconds) && std::isfinite(p.slackSeconds);
    return p;
}
} // namespace infinite
