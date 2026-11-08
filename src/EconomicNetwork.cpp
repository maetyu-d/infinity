#include "infinity/Economy.hpp"
#include "infinity/Urban.hpp"
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
namespace infinity {
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

void specialize(EconomicSite& s, const Node& world, const SurfaceSettlement& town,
                unsigned utilities, unsigned industry, unsigned agriculture, unsigned civic) {
    const auto local = localCatchment(world, town.direction);
    constexpr std::array raw{Good::Ice, Good::Ore, Good::Silicate, Good::Carbon};
    constexpr std::array<std::string_view, 4> geology{"ICE FRACTURES", "METAL VEINS", "SILICATE BEDROCK", "CARBON SEAMS"};
    s.originalReserves = s.reserves;
    s.reserves.fill(0);
    for (unsigned r = 0; r < raw.size(); ++r) {
        const auto g = size_t(raw[r]);
        if (local.grade[r] >= .018)
            s.reserves[g] = std::min(s.originalReserves[g], int64_t(900 + 36000 * local.grade[r]));
    }
    if (local.water >= .038)
        s.reserves[size_t(Good::Water)] = int64_t(2400 + 25000 * local.water);
    for (const auto r : s.recipes) {
        s.originalRecipes |= uint32_t(1) << r;
        for (size_t g = 0; g < goodCount; ++g)
            s.target[g] -= productionRecipes()[r].inputs[g] * 12;
    }
    s.recipes.clear();
    const auto add = [&](unsigned recipe) {
        if (std::find(s.recipes.begin(), s.recipes.end(), recipe) == s.recipes.end())
            s.recipes.push_back(recipe);
    };
    const bool waterWorks = utilities || industry || (town.sealed && agriculture);
    if (waterWorks && s.reserves[size_t(Good::Ice)] && local.grade[0] > .032) {
        add(0);
        add(2);
    } else if (waterWorks && s.reserves[size_t(Good::Water)]) {
        add(20);
    }
    if (waterWorks && s.sealed)
        add(3);
    if (industry || town.kind == SettlementKind::Mining) {
        unsigned rawMaterial = local.dominant;
        if (!rawMaterial)
            rawMaterial = unsigned(std::max_element(local.grade.begin() + 1, local.grade.end()) - local.grade.begin());
        if ((utilities || industry) && town.kind == SettlementKind::Mining && s.reserves[size_t(raw[rawMaterial])]) {
            constexpr std::array<unsigned, 4> extraction{0, 18, 19, 4};
            add(extraction[rawMaterial]);
        }
        if (industry) {
            constexpr std::array<unsigned, 4> refining{2, 5, 6, 7};
            add(refining[local.dominant]);
            const unsigned finishing = town.kind == SettlementKind::Research ? 16
                                     : local.dominant == 0 ? 13
                                     : local.dominant == 1 ? 12
                                     : local.dominant == 2 ? 11 : 15;
            add(finishing);
            add(17);
        }
    }
    if (agriculture && (town.sealed || s.moisture > .25)) {
        add(8);
        add(9);
    }
    if (civic && town.kind == SettlementKind::Research)
        add(16);
    for (const auto r : s.recipes)
        for (size_t g = 0; g < goodCount; ++g)
            s.target[g] += productionRecipes()[r].inputs[g] * 12;
    const bool localWater = std::find(s.recipes.begin(), s.recipes.end(), 20) != s.recipes.end() ||
                            std::find(s.recipes.begin(), s.recipes.end(), 0) != s.recipes.end();
    if (s.sealed && !localWater)
        s.target[size_t(Good::Water)] += householdRation(s, Good::Water) * 12;
    s.localWorks = std::string(geology[local.dominant]) + (localWater ? " / LOCAL WATER" : " / IMPORT-DEPENDENT WATER");
    if (town.kind == SettlementKind::Research)
        s.role = "RESEARCH / INSTRUMENTS";
    else if (town.kind == SettlementKind::Mining || industry > agriculture)
        s.role = std::string(geology[local.dominant]) + " / " + (town.kind == SettlementKind::Mining ? "EXTRACTION" : "REFINING");
    else if (agriculture)
        s.role = town.sealed ? "GREENHOUSE / LIFE SUPPORT" : "AGRICULTURE / LIFE SUPPORT";
}

EconomicSite surfaceSite(const Node& p,const SurfaceSettlement& town,const PlanetClimate& climate,bool includeRegion){
    EconomicSite s;
    s.id = {p.address, surfaceActivitySlot + town.slot};
    s.name = includeRegion?settlementDisplayName(town):town.name;
    s.metres = town.landingPoint;
    s.radius = includeRegion?settlementExtent(town):town.radius;
    s.seed = mix64(town.seed ^ 0x45434f4e4f4d5931ULL);
    s.population = includeRegion?settlementPopulation(town):town.population;
    s.sealed = town.sealed;
    s.climate = climate;
    s.temperature = climate.temperatureK;
    s.moisture = climate.moisture;
    s.workers = std::max(2u, unsigned(uint64_t(s.population)*45/100));
    s.power = 8;
    unsigned industry = 0, agriculture = 0, utilities = 0, civic = 0, jobs = 0;
    for (const auto& b : town.buildings) {
        jobs += b.jobs;
        industry += b.use == BuildingUse::Industry || b.kind == SurfaceMesh::Industry;
        agriculture += b.use == BuildingUse::Agriculture || b.kind == SurfaceMesh::Greenhouse;
        utilities += b.use == BuildingUse::Utilities || b.kind == SurfaceMesh::Utility;
        civic += b.use == BuildingUse::Civic;
    }
    if(includeRegion&&town.urban){const auto& region=*town.urban;industry+=region.industry;agriculture+=region.agriculture;
        utilities+=region.utilities;civic+=region.civic;jobs+=region.jobs;}
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
             : agriculture > industry        ? "AGRICULTURE / LIFE SUPPORT"
                     : "INDUSTRY / HABITAT";
    s.reserves[size_t(Good::Ice)] = int64_t((.02 + p.environment.waterActivity) * 12000000);
    s.reserves[size_t(Good::Ore)] = int64_t(p.environment.chemistry.metal * 20000000);
    s.reserves[size_t(Good::Carbon)] = int64_t(p.environment.chemistry.carbon * 20000000);
    s.reserves[size_t(Good::Silicate)] = int64_t(p.environment.chemistry.silicate * 20000000);
    budget(s);
    specialize(s, p, town, utilities, industry, agriculture, civic);
    if(includeRegion&&town.urban)s.role=std::string(urbanTierName(town.urban->growth.tier))+" / "+s.role;
    return s;
}

void buildLanes(EconomicNetwork& network, const std::atomic<bool>* cancel) {
    std::array<std::vector<size_t>, goodCount> producers;
    std::vector<size_t> exchanges;
    for (size_t a = 0; a < network.sites.size(); ++a) {
        const auto& site = network.sites[a];
        if (site.orbital) {
            exchanges.push_back(a);
            continue;
        }
        for (size_t g = 0; g < goodCount; ++g)
            if (std::any_of(site.recipes.begin(), site.recipes.end(), [&](unsigned r) {
                    return productionRecipes()[r].outputs[g] > 0;
                }))
                producers[g].push_back(a);
    }
    for (size_t b = 0; b < network.sites.size(); ++b) {
        if (cancelled(cancel))
            return;
        auto& site = network.sites[b];
        for (size_t g = 0; g < goodCount; ++g) {
            const bool input = std::any_of(site.recipes.begin(), site.recipes.end(), [&](unsigned r) {
                return productionRecipes()[r].inputs[g] > 0;
            });
            if (!site.orbital && !input && !householdRation(site, Good(g)))
                continue;
            std::array<double, 2> best{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            const auto consider = [&](std::span<const size_t> candidates, double handling) {
                for (const auto a : candidates) {
                    if (a == b)
                        continue;
                    const auto& source = network.sites[a];
                    const double distance = glm::length(site.systemMetres - source.systemMetres);
                    const double cost = std::log1p(distance / 3e8) + handling + (source.sealed ? .35 : 0.);
                    if (cost < best[0]) {
                        best[1] = best[0];
                        site.lanes[g][1] = site.lanes[g][0];
                        best[0] = cost;
                        site.lanes[g][0] = uint16_t(a + 1);
                    } else if (cost < best[1]) {
                        best[1] = cost;
                        site.lanes[g][1] = uint16_t(a + 1);
                    }
                }
            };
            consider(producers[g], 0);
            consider(exchanges, 2.2);
        }
    }
}
} // namespace

std::span<const size_t> economicLaneSources(const EconomicSite& site, size_t good, std::span<const size_t> ordinary,
                                           std::array<size_t, 10>& scratch) {
    size_t count = 0;
    for (const auto index : site.lanes[good])
        if (index)
            scratch[count++] = index - 1;
    for (const auto index : ordinary)
        if (count < scratch.size() && std::find(scratch.begin(), scratch.begin() + count, index) == scratch.begin() + count)
            scratch[count++] = index;
    return std::span(scratch).first(count);
}

double economicLaneWeight(const EconomicSite& site, size_t good, size_t source) {
    return site.lanes[good][0] == source + 1 ? 1.7 : site.lanes[good][1] == source + 1 ? 1.3 : 1.;
}

bool economicWeatherHold(const EconomicSite& site, uint64_t tick) {
    if (site.orbital || !site.climate.solid || site.climate.pressureBar <= 0)
        return false;
    const double altitude = glm::length(site.metres) - site.climate.radiusMetres;
    const auto weather = sampleWeather(site.climate, site.metres, altitude, std::min(1e12, tick * economyTickSeconds));
    return (weather.storm > .35 && weather.gustMetresPerSecond >= std::max(28., site.climate.windMetresPerSecond * 1.6)) ||
           (weather.type == WeatherType::Thunderstorm && weather.visibilityMetres < 9000) ||
           (weather.type == WeatherType::Dust && weather.visibilityMetres < 3500);
}

size_t EconomicNetwork::residentBytes() const {
    size_t n =
        sizeof(*this) + sites.capacity() * sizeof(EconomicSite) + vessels.capacity() * sizeof(ActivityId);
    for (const auto& s : sites)
        n += s.name.capacity() + s.role.capacity() + s.localWorks.capacity() + s.recipes.capacity() * sizeof(unsigned) +
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
    std::map<ActivityId,unsigned> retainedPopulation;
    std::map<ActivityId,Stock> retainedInitial;
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
            auto s=surfaceSite(p,town,world->climate,true);
            retainedPopulation.emplace(s.id,town.population);
            if(town.urban)retainedInitial.emplace(s.id,surfaceSite(p,town,world->climate,false).initial);
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
    net->legacyFingerprint=net->fingerprint;
    net->legacyEndowmentMoney=economicInitialTreasuryCents+economicInitialCarrierCents;
    for(const auto& s:net->sites){
        net->fingerprint=mix64(net->fingerprint^s.seed^s.population^s.id.slot);
        const auto found=retainedPopulation.find(s.id);const auto original=found==retainedPopulation.end()?s.population:found->second;
        net->legacyFingerprint=mix64(net->legacyFingerprint^s.seed^original^s.id.slot);
        net->legacyEndowmentMoney+=economicInitialFirmCents+int64_t(original)*(economicFirmPerResidentCents+economicHouseholdPerResidentCents);
        const auto initial=retainedInitial.find(s.id);const auto& stock=initial==retainedInitial.end()?s.initial:initial->second;
        for(size_t g=0;g<goodCount;++g)net->legacyInitialTotal[g]+=stock[g];
    }
    buildLanes(*net, cancel);
    return cancelled(cancel) ? nullptr : net;
}
} // namespace infinity
