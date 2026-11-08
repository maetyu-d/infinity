#include "infinity/SurfaceProvince.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
bool hasSolidSurface(const Node& node) {
    return node.geology && node.geology->type != GeologyType::GasEnvelope;
}
std::string_view terrainName(TerrainType type) {
    switch (type) {
    case TerrainType::Highlands:
        return "Highlands";
    case TerrainType::Volcanic:
        return "Volcanic";
    case TerrainType::Dissected:
        return "Canyons";
    case TerrainType::IceRidges:
        return "Ice ridges";
    default:
        return "Plains";
    }
}
TerrainProcess terrainProcess(const PlanetProcess& planet, const Environment& environment) {
    TerrainProcess terrain;
    if (planet.type == GeologyType::GasEnvelope)
        return terrain;
    const Random random{featureSeed(planet.seed, Feature::LocalRelief, terrainRecipeVersion)};
    // A subset retains smoother landscapes. The catalogue's old planets use
    // the existing geological recipe; this additive layer is opt-in.
    if (random.unit(0) < .20)
        return terrain;
    terrain.seed = random.key;
    terrain.type = planet.type == GeologyType::IceShell                          ? TerrainType::IceRidges
                   : planet.liquidWater > .10                                    ? TerrainType::Dissected
                   : planet.activity > .10 && environment.peakTemperature > 1400 ? TerrainType::Volcanic
                                                                                 : TerrainType::Highlands;
    const double radius = std::pow(10., planet.logRadius);
    terrain.wavelengthMetres = std::clamp(radius * (.003 + .009 * random.unit(1)), 500., 12000.);
    terrain.reliefMetres = std::clamp(terrain.wavelengthMetres * (.16 + .12 * random.unit(2)), 40., 1800.);
    terrain.weathering = std::clamp(planet.liquidWater * (.4 + planet.ageGyr / 8.), 0., .85);
    if (terrain.type == TerrainType::IceRidges)
        terrain.reliefMetres *= .65;
    // Existing Euler motion provides the fabric direction; no independent
    // local terrain scene or sampling-window seed is introduced.
    terrain.fabric = tangentFrame(glm::normalize(planet.plates[0].angularVelocity));
    return terrain;
}
double localTerrainHeight(const PlanetProcess& planet, glm::dvec3 direction, const GeologySample& geology) {
    const auto& terrain = planet.terrain;
    if (planet.type == GeologyType::GasEnvelope || terrain.type == TerrainType::None)
        return 0;
    const double radius = std::pow(10., planet.logRadius);
    const auto d = glm::normalize(direction);
    const auto q = glm::inverse(terrain.fabric) * d * (radius / terrain.wavelengthMetres);
    const double regional = smoothstep(-.28, .28, fractalNoise(d * 11., terrain.seed, 2));
    // Domain warping bends ridges continuously in planet coordinates. The
    // same field is sampled by orbit meshes, streamed patches and collisions.
    const double bend = fractalNoise(q * .35, mix64(terrain.seed ^ 1), 2);
    const auto warped = q + glm::dvec3(bend * .8, bend * .3, -bend * .5);
    const double signal = fractalNoise(warped, terrain.seed, 4);
    const double ridge = std::pow(std::max(0., 1 - std::sqrt(signal * signal * 3.2 + .0025)), 3);
    const double detail = fractalNoise(warped * 3.2, mix64(terrain.seed ^ 2), 3);
    double height = 0;
    switch (terrain.type) {
    case TerrainType::Highlands:
        height = (ridge - .4 + detail * .30) * (.45 + .55 * std::clamp(geology.convergence, 0., 1.));
        break;
    case TerrainType::Volcanic:
        height = (ridge * ridge - .35 + detail * .22) * (.45 + std::min(1., geology.volcanism) * 1.4);
        break;
    case TerrainType::Dissected: {
        const double channel = std::pow(std::max(0., 1 - std::sqrt(signal * signal * 5 + .0025)), 9);
        height = detail * .35 - channel * (.65 + terrain.weathering * .35);
        break;
    }
    case TerrainType::IceRidges:
        height = (ridge - .4 + detail * .20) *
                 (.5 + .5 * std::clamp(geology.divergence + geology.shear * .5, 0., 1.));
        break;
    default:
        return 0;
    }
    return terrain.reliefMetres * regional * height * (1 - terrain.weathering * .35);
}
SurfaceProvince ProvinceSample::dominant() const {
    return SurfaceProvince(std::max_element(weights.begin(), weights.end()) - weights.begin());
}
std::string_view provinceName(SurfaceProvince type) {
    constexpr std::array names{"Weathered Bedrock", "Folded Uplands", "Volcanic Field",
                               "Impact Field", "Sediment Basin", "Frost Belt"};
    return names[size_t(type)];
}
ProvinceSample surfaceProvince(const PlanetProcess& planet, glm::dvec3 d, const GeologySample& g) {
    if (planet.type == GeologyType::GasEnvelope)
        return {};
    const double h = g.heightMetres / std::pow(10., planet.logRadius);
    const double elevation = smoothstep(.0009, .004, h);
    const double folding = smoothstep(.045, .52, g.convergence + g.shear * .65);
    const double lava = smoothstep(.021, .22, g.volcanism);
    const double ejecta = smoothstep(.000055, .00042, g.cratering);
    const double deposition = smoothstep(.018, .20, g.sediment) * (1 - smoothstep(-.001, .0015, h));
    double frost = 0;
    if (planet.type == GeologyType::IceShell || planet.ice > .12) {
        // Use the same spin axis as planetary weather.
        const auto axis = glm::normalize(planet.plates.front().angularVelocity);
        const double polarCold = smoothstep(.43, .94, std::abs(glm::dot(axis, d)));
        frost = planet.type == GeologyType::IceShell ? .56 + .63 * polarCold + .16 * elevation
                : smoothstep(.12, .46, planet.ice) * (.15 + .87 * polarCold + .28 * elevation);
    }
    const double bedrock = .36 + .25 * (1 - smoothstep(.015, .19, planet.liquidWater));
    return {{bedrock, .66 * elevation + .84 * folding, 1.55 * lava, 1.52 * ejecta,
             1.42 * deposition, frost}};
}
glm::vec3 provinceAlbedo(const ProvinceSample& sample, glm::vec3 substrate) {
    constexpr std::array palette{
        glm::vec3(.36f, .23f, .24f), glm::vec3(.58f, .40f, .29f), glm::vec3(.12f, .075f, .16f),
        glm::vec3(.62f, .49f, .42f), glm::vec3(.33f, .45f, .27f), glm::vec3(.49f, .69f, .79f)};
    glm::vec3 color(0);
    double total = 0;
    for (size_t i = 0; i < palette.size(); ++i) {
        const double weight = sample.weights[i] * sample.weights[i];
        color += palette[i] * float(weight);
        total += weight;
    }
    return total > 0 ? glm::mix(substrate, color / float(total), .72f) : substrate;
}
std::vector<Address> geologicalLandmarks(const Universe& u, const Node& planet, size_t limit) {
    if (limit == 0 || planet.address.kind() != Kind::Planet || !hasSolidSurface(planet))
        return {};
    struct Candidate {
        uint64_t tile = 0;
        double score = 0;
    };
    std::array<Candidate, 6> best{};
    const auto& process = *planet.geology;
    const double radius = std::pow(10., process.logRadius);
    auto consider = [&](glm::dvec3 d) {
        const auto tile = tileAt(d, 7);
        d = tileDirection(tile);
        const auto geology = geologySample(process, d);
        if (process.liquidWater > .1 && geology.heightMetres / radius < -.0008)
            return;
        const auto sample = surfaceProvince(process, d, geology);
        const auto index = size_t(sample.dominant());
        const double score = sample.weights[index];
        if (index != size_t(SurfaceProvince::Bedrock) && score > best[index].score &&
            glm::dot(d, u.terrainNormal(planet, d)) > .96)
            best[index] = {encodeTile(tile), score};
    };
    for (const auto& impact : process.impacts)
        if (impact.survival > .12) {
            consider(impact.centre);
            const auto rim = tangentFrame(impact.centre) * glm::dvec3(impact.radius, 0, 0);
            consider(glm::normalize(impact.centre + rim));
        }
    for (const auto& hotspot : process.hotspots)
        consider(hotspot);
    for (const auto& plate : process.plates) {
        consider(plate.centre);
        // The nearest plate supplies a repeatable candidate along a boundary.
        const Plate* neighbour = nullptr;
        double nearest = -1;
        for (const auto& other : process.plates) {
            const double proximity = glm::dot(plate.centre, other.centre);
            if (&plate != &other && proximity > nearest) {
                nearest = proximity;
                neighbour = &other;
            }
        }
        consider(glm::normalize(plate.centre + neighbour->centre));
    }
    for (unsigned i = 0; i < 16; ++i) {
        const double y = 1 - 2 * (i + .5) / 16;
        const double angle = i * std::numbers::pi * (3 - std::sqrt(5.));
        const double radial = std::sqrt(1 - y * y);
        consider({radial * std::cos(angle), y, radial * std::sin(angle)});
    }
    std::vector<Address> result;
    result.reserve(std::min(limit, best.size() - 1));
    for (const auto& candidate : best)
        if (candidate.score > 0 && result.size() < limit)
            result.push_back(planet.address.child(Kind::Surface, candidate.tile));
    return result;
}
} // namespace infinity
