#include "infinite/Universe.hpp"
#include <algorithm>
#include <cmath>

namespace infinite {
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
} // namespace infinite
