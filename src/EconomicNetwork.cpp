#include "infinite/Economy.hpp"
#include <algorithm>
#include <numeric>
namespace infinite {
namespace {
bool cancelled(const std::atomic<bool>* c) {
    return c && c->load();
}
void budget(EconomicSite& s) {
    const auto random = Random{s.seed};
    s.capacityKg = 30000 + int64_t(s.population) * 20;
    for (size_t g = 0; g < goodCount; g++) {
        s.target[g] = 80 + int64_t(s.population / 40);
        s.initial[g] = s.target[g] * (2 + int64_t(random.unit(g) * 4));
    }
    // Every funded economy starts at the same canonical epoch, with a disclosed endowment.
    // Productive specialisation and real consumption subsequently determine its stock.
    for (auto r : s.recipes)
        for (size_t g = 0; g < goodCount; g++) {
            s.target[g] += productionRecipes()[r].inputs[g] * 12;
            if (productionRecipes()[r].outputs[g])
                s.initial[g] += productionRecipes()[r].outputs[g] * 15;
        }
    s.target[size_t(Good::Food)] += int64_t(s.population / 100) * 4;
    s.target[size_t(Good::Water)] += int64_t(s.population / 35) * 4;
    // Inputs for consumer goods and replacement machinery are deliberately uneven, not random prices.
    if (!s.orbital && !s.recipes.empty()) {
        auto g = size_t(Good::Machinery);
        s.initial[g] = std::max<int64_t>(1, s.target[g] / 5);
    }
    int64_t total = std::accumulate(s.initial.begin(), s.initial.end(), int64_t(0));
    s.capacityKg = std::max(s.capacityKg, total * 2);
}
} // namespace
size_t EconomicNetwork::residentBytes() const {
    size_t n =
        sizeof(*this) + sites.capacity() * sizeof(EconomicSite) + vessels.capacity() * sizeof(ActivityId);
    for (const auto& s : sites)
        n += s.name.capacity() + s.role.capacity() + s.recipes.capacity() * sizeof(unsigned) +
             s.id.home.path.capacity() * sizeof(Segment);
    return n;
}
std::shared_ptr<const EconomicNetwork> generateEconomicNetwork(const Universe& u, const Address& address,
                                                               const std::atomic<bool>* cancel) {
    if (address.kind() != Kind::System)
        throw std::invalid_argument("Economy requires an existing star system");
    auto net = std::make_shared<EconomicNetwork>();
    net->system = address;
    net->universeSeed = u.seed();
    net->fingerprint = mix64(u.seed() ^ seedOf(address, u.seed()) ^ 0x45434f4e4f4d5931ULL);
    for (const auto& a : u.children(u.describe(address))) {
        if (cancelled(cancel))
            return {};
        const auto p = u.describe(a);
        // Existing orbital facilities are present only on primary worlds in the activity renderer.
        if (!isMoon(a)) {
            for (unsigned slot = 0; slot < 2; slot++) {
                const auto body = sampleActivity(p, slot, 0);
                EconomicSite s;
                s.id = body.id;
                s.name = body.name;
                s.role = slot ? "SERVICE / DISTRIBUTION" : "ORBITAL EXCHANGE";
                s.seed = mix64(p.seed ^ slot ^ 0x45434f4e4f4d5931ULL);
                s.metres = body.position;
                s.radius = body.radius;
                s.orbital = true;
                s.workers = 0;
                s.power = 0;
                budget(s);
                net->sites.push_back(std::move(s));
            }
            for (auto slot : activitySlots(p))
                if (slot >= 100 && slot < 200)
                    net->vessels.push_back({a, slot});
        }
        if (!hasSolidSurface(p))
            continue;
        auto world = generateSurfaceLife(u, p, cancel);
        if (!world)
            return {};
        for (const auto& town : world->settlements) {
            if (cancelled(cancel))
                return {};
            EconomicSite s;
            s.id = {a, surfaceActivitySlot + town.slot};
            s.name = town.name;
            s.metres = town.landingPoint;
            s.radius = town.radius;
            s.seed = mix64(town.seed ^ 0x45434f4e4f4d5931ULL);
            s.population = town.population;
            s.sealed = town.sealed;
            s.climate = world->climate;
            s.temperature = world->climate.temperatureK;
            s.moisture = world->climate.moisture;
            s.workers = std::max(2u, unsigned(town.population * 45 / 100));
            s.power = 8;
            unsigned industry = 0, agriculture = 0, utilities = 0, civic = 0, jobs = 0;
            for (const auto& b : town.buildings) {
                jobs += b.jobs;
                industry += b.use == BuildingUse::Industry || b.kind == SurfaceMesh::Industry;
                agriculture += b.use == BuildingUse::Agriculture || b.kind == SurfaceMesh::Greenhouse;
                utilities += b.use == BuildingUse::Utilities || b.kind == SurfaceMesh::Utility;
                civic += b.use == BuildingUse::Civic;
            }
            s.power += utilities * 8;
            s.workers = std::min(s.workers, std::max(2u, jobs + utilities * 2));
            // Economic capabilities occupy existing buildings; no new geometry or residents are generated.
            if (utilities)
                s.recipes = {0, 1, 2, 3, 4}; // extraction, water and oxygen
            if (agriculture) {
                s.recipes.push_back(8);
                s.recipes.push_back(9);
            }
            if (industry) {
                s.recipes.push_back(5);
                s.recipes.push_back(6);
                s.recipes.push_back(7);
                const unsigned special = 10 + unsigned(s.seed % 7);
                s.recipes.push_back(special);
                s.recipes.push_back(17);
            }
            if (civic && town.kind == SettlementKind::Research)
                s.recipes.push_back(16);
            s.role = town.kind == SettlementKind::Mining     ? "EXTRACTION / REFINING"
                     : town.kind == SettlementKind::Research ? "RESEARCH / INSTRUMENTS"
                     : agriculture > industry                ? "AGRICULTURE / LIFE SUPPORT"
                                                             : "INDUSTRY / HABITAT";
            s.reserves[size_t(Good::Ice)] = int64_t((.02 + p.environment.waterActivity) * 12000000);
            s.reserves[size_t(Good::Ore)] = int64_t(p.environment.chemistry.metal * 20000000);
            s.reserves[size_t(Good::Carbon)] = int64_t(p.environment.chemistry.carbon * 20000000);
            s.reserves[size_t(Good::Silicate)] = int64_t(p.environment.chemistry.silicate * 20000000);
            budget(s);
            net->sites.push_back(std::move(s));
        }
    }
    std::sort(net->sites.begin(), net->sites.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(net->vessels.begin(), net->vessels.end());
    for (auto& s : net->sites) {
        const auto home = u.describe(s.id.home);
        s.systemMetres =
            u.pointRelative(s.id.home, s.metres / std::pow(10., home.frame.logRadius), address, 0);
    }
    for (const auto& s : net->sites)
        net->fingerprint = mix64(net->fingerprint ^ s.seed ^ s.population ^ s.id.slot);
    return net;
}
} // namespace infinite
