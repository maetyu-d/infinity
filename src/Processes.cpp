#include "infinite/Universe.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace infinite {
namespace {
constexpr double pi = std::numbers::pi;
glm::dvec3 sphere(const Random& r, uint64_t i) {
    double z = r.signedUnit(i), a = r.unit(i + 1) * 2 * pi;
    return {std::sqrt(1 - z * z) * std::cos(a), z, std::sqrt(1 - z * z) * std::sin(a)};
}
WebSample webLevel(glm::dvec3 p, uint64_t seed) {
    // Adhesion-inspired cellular skeleton: equal arrival distances form sheets,
    // triple junctions form filaments, four-way junctions form cluster knots.
    // Bounded jitter and a two-cell halo retain the nearest four across cell edges.
    auto base = glm::floor(p);
    std::array<double, 4> nearest{100, 100, 100, 100};
    constexpr std::array order{0, -1, 1, -2, 2};
    for (int z : order)
        for (int y : order)
            for (int x : order) {
                auto c = base + glm::dvec3(x, y, z);
                // A conservative distance bound rejects distant cells before
                // hashing their sites. Search near cells first; compare squared
                // distances and take only the final four square roots.
                auto gap = glm::max(glm::dvec3(0), glm::max(c + .21 - p, p - c - .79));
                if (glm::dot(gap, gap) > nearest[3])
                    continue;
                uint64_t key = seed;
                for (int axis = 0; axis < 3; ++axis)
                    key = mix64(key ^ mix64(static_cast<uint64_t>(static_cast<int64_t>(c[axis]))));
                Random r{key};
                auto q = c + .5 + glm::dvec3(r.signedUnit(0), r.signedUnit(1), r.signedUnit(2)) * .28;
                double distance = glm::dot(q - p, q - p);
                for (size_t i = 0; i < nearest.size(); ++i)
                    if (distance < nearest[i])
                        std::swap(distance, nearest[i]);
            }
    for (auto& distance : nearest)
        distance = std::sqrt(distance);
    double a = nearest[1] - nearest[0], b = nearest[2] - nearest[0], c = nearest[3] - nearest[0];
    WebSample s;
    s.sheet = std::exp(-a * a / .0036);
    s.filament = std::exp(-(a * a + b * b) / .009);
    s.knot = std::exp(-(a * a + b * b + c * c) / .0225);
    s.voidDistance = nearest[0];
    s.density = std::clamp(.004 + .12 * s.sheet + .57 * s.filament + .8 * s.knot, 0.0, 1.0);
    return s;
}
} // namespace
WebSample cosmicStructure(glm::dvec3 p, uint64_t seed) {
    auto large = webLevel(p, featureSeed(seed, Feature::CosmicWeb, 0));
    auto small = webLevel(p * 2.7 + glm::dvec3(3.1, -1.7, 4.3), featureSeed(seed, Feature::CosmicWeb, 1));
    // Substructure grows primarily within the coarse collapsed skeleton.
    large.density = std::clamp(large.density * (.72 + .55 * small.density), 0.0, 1.0);
    return large;
}
GalaxyProcess galaxyProcess(uint64_t seed, const WebSample& web, double age) {
    Random r{featureSeed(seed, Feature::GalaxyStructure, 0)};
    GalaxyProcess m;
    m.merger = std::clamp(.15 * r.unit(0) + .85 * web.knot, 0.0, 1.0);
    double choice = r.unit(1);
    m.type = choice < .18 + .48 * m.merger ? GalaxyType::Elliptical
             : choice < .35                ? GalaxyType::Irregular
             : choice < .68                ? GalaxyType::BarredSpiral
                                           : GalaxyType::Spiral;
    m.gasFraction = m.type == GalaxyType::Elliptical ? .01 + .02 * r.unit(2) : .12 + .35 * r.unit(2);
    m.bulgeFraction = m.type == GalaxyType::Elliptical ? .92 : .08 + .25 * r.unit(3);
    if (m.type == GalaxyType::Irregular)
        m.bulgeFraction = .03;
    m.discScale = .15 + .1 * r.unit(4);
    m.thickness = .013 + .035 * r.unit(5) + .015 * m.merger;
    m.pitch = (.16 + .35 * r.unit(6));
    m.arms = m.type == GalaxyType::BarredSpiral ? 2 : 2 + static_cast<unsigned>(r.bits(7) % 3);
    m.axisRatio = .48 + .45 * r.unit(8);
    m.gasFraction *= std::clamp(1.3 - age / 25, .6, 1.3);
    return m;
}
std::shared_ptr<const PlanetProcess> planetProcess(uint64_t seed, double logRadius, const Environment& e) {
    auto p = std::make_shared<PlanetProcess>();
    p->seed = seed;
    p->logRadius = logRadius;
    p->ageGyr = e.ageGyr;
    // Size/age control remaining heat; hydration weakens a silicate lithosphere.
    p->activity = std::clamp(std::pow(10.0, logRadius - 6.8) * std::exp(-e.ageGyr / 7) *
                                 (.55 + e.peakTemperature / 2200),
                             .015, 1.0);
    p->ice = e.chemistry.ice;
    p->liquidWater =
        e.waterActivity * smoothstep(235, 275, e.temperature) * (1 - smoothstep(340, 410, e.temperature));
    p->type = e.chemistry.ice > .25 && e.temperature < 250 ? GeologyType::IceShell
              : p->activity * (.12 + p->liquidWater) > .27 ? GeologyType::MobileLid
                                                           : GeologyType::StagnantLid;
    Random r{featureSeed(seed, Feature::PlateMotion, 0)};
    for (size_t i = 0; i < p->plates.size(); ++i) {
        // Fibonacci centres avoid accidental tiny plates; keyed perturbations
        // and independent Euler poles supply coherent rigid plate motions.
        double y = 1 - 2 * (i + .5) / p->plates.size();
        double angle = i * pi * (3 - std::sqrt(5.0)) + .15 * r.signedUnit(i * 8);
        auto centre =
            glm::dvec3(std::sqrt(1 - y * y) * std::cos(angle), y, std::sqrt(1 - y * y) * std::sin(angle));
        p->plates[i] = {centre, sphere(r, i * 8 + 1) * (.3 + .7 * r.unit(i * 8 + 3)), r.unit(i * 8 + 4)};
    }
    Random impacts{featureSeed(seed, Feature::Impacts, 0)};
    for (size_t i = 0; i < p->impacts.size(); ++i) {
        double radius = .014 * std::pow(1 - .96 * impacts.unit(i * 4 + 2), -.7);
        double age = e.ageGyr * impacts.unit(i * 4 + 3);
        double resurfacing = p->activity * (p->type == GeologyType::MobileLid ? 2 : .35) + p->liquidWater;
        p->impacts[i] = {sphere(impacts, i * 4), radius, std::exp(-age * resurfacing)};
    }
    for (size_t i = 0; i < p->hotspots.size(); ++i)
        p->hotspots[i] = sphere(r, 1000 + i * 2);
    return p;
}
GeologySample geologySample(const PlanetProcess& p, glm::dvec3 d) {
    d = glm::normalize(d);
    GeologySample s;
    std::array<double, 12> weights;
    double sum = 0;
    for (size_t i = 0; i < p.plates.size(); ++i) {
        weights[i] = std::exp(28 * (glm::dot(d, p.plates[i].centre) - 1));
        sum += weights[i];
    }
    for (size_t i = 0; i < weights.size(); ++i) {
        weights[i] /= sum;
        s.continental += weights[i] * p.plates[i].continental;
    }
    // Pairwise smooth weights keep triple junctions continuous. Closure and
    // shear derive from v = omega x r, rather than a boundary-noise label.
    for (size_t i = 0; i < p.plates.size(); ++i)
        for (size_t j = i + 1; j < p.plates.size(); ++j) {
            double w = 4 * weights[i] * weights[j];
            if (w < 1e-12)
                continue;
            auto normal = p.plates[j].centre - p.plates[i].centre;
            normal -= d * glm::dot(normal, d);
            if (glm::dot(normal, normal) < 1e-24)
                continue;
            normal = glm::normalize(normal);
            auto velocity = glm::cross(p.plates[i].angularVelocity - p.plates[j].angularVelocity, d);
            double closure = glm::dot(velocity, normal);
            s.convergence += w * std::max(0.0, closure);
            s.divergence += w * std::max(0.0, -closure);
            s.shear += w * std::abs(glm::dot(velocity, glm::cross(d, normal)));
        }
    double mobile = p.type == GeologyType::MobileLid ? p.activity : p.activity * .12;
    double continents = (s.continental - .5) * .012;
    double ridges = .004 * s.convergence - .0017 * s.divergence;
    // Subduction trench and uplift are separated by crust buoyancy.
    ridges -= .002 * s.convergence * (1 - smoothstep(.32, .65, s.continental));
    double fine = fractalNoise(d * 180.0, featureSeed(p.seed, Feature::PlateMotion, 1), 4);
    for (auto hot : p.hotspots) {
        double distance = glm::length(d - hot);
        s.volcanism += p.activity * std::exp(-distance * distance / .0064);
    }
    double height = continents + mobile * ridges + .005 * s.volcanism;
    for (const auto& crater : p.impacts) {
        double q = glm::length(d - crater.centre) / crater.radius;
        if (q < 1.7) {
            double taper = 1 - smoothstep(1.35, 1.7, q);
            double bowl = -.03 * crater.radius * std::exp(-q * q * 2.5);
            double rim = .012 * crater.radius * std::exp(-std::pow((q - 1) * 6, 2));
            double ejecta = .002 * crater.radius * std::exp(-q) * smoothstep(.95, 1.2, q);
            double relief = (bowl + rim + ejecta) * crater.survival * taper;
            height += relief;
            s.cratering += std::abs(relief);
        }
    }
    // Analytic weathering/incision proxy, evaluated in planet coordinates.
    // It is deliberately not presented as a solved fluid/erosion simulation.
    double drainage = std::pow(1 - std::abs(fractalNoise(d * 42.0, p.seed, 3)), 8);
    s.erosion = p.liquidWater * (.0006 * drainage + .001 * mobile * s.convergence);
    s.sediment = p.liquidWater * (1 - smoothstep(-.002, .002, height));
    height -= s.erosion;
    height += s.sediment * .0005;
    height += fine * (.00025 + .0007 * mobile * s.convergence);
    if (p.type == GeologyType::IceShell) {
        // Extensional belts in a buoyant icy shell and subdued impact relief.
        height = height * .35 + .0015 * s.divergence - .001 * s.shear + fine * .00015;
    }
    s.heightMetres = std::pow(10.0, p.logRadius) * height;
    return s;
}
RockProcess rockProcess(uint64_t seed, const Environment& e, const GeologySample& site) {
    Random r{featureSeed(seed, Feature::RockFabric, 0)};
    RockProcess p;
    p.coolingRate = e.coolingRate;
    p.strain = e.strain;
    p.impact = e.impact;
    p.weathering = e.waterActivity * (1 - std::exp(-e.ageGyr / 2));
    if (e.chemistry.ice > .25 && e.temperature < 250)
        p.type = RockType::Glacial;
    else if (e.chemistry.metal > .48)
        p.type = RockType::Metallic;
    else if (e.impact > .72)
        p.type = RockType::Breccia;
    else if (e.strain > .45 && e.pressureGPa > .15 && e.peakTemperature > 600)
        p.type = RockType::Metamorphic;
    else if (e.waterActivity > .3 && site.sediment > .12 && e.peakTemperature < 1250)
        p.type = RockType::Sedimentary;
    else
        p.type = e.coolingRate > .55 ? RockType::Volcanic : RockType::Plutonic;
    p.axes = {.78 + .18 * r.unit(0), .7 + .2 * r.unit(1), .78 + .18 * r.unit(2)};
    if (p.type == RockType::Sedimentary)
        p.axes.y *= .6;
    if (p.type == RockType::Metamorphic)
        p.axes.y *= .75;
    p.bedding = .1 + .18 * r.unit(3);
    p.fabric = glm::angleAxis(.15 + e.strain * .5, sphere(r, 4));
    p.porosity = p.type == RockType::Volcanic      ? .06 + .22 * e.coolingRate
                 : p.type == RockType::Sedimentary ? .05 + .13 * (1 - e.pressureGPa)
                                                   : .01;
    return p;
}
Mineral selectMineral(uint64_t seed, const Environment& e) {
    const auto& c = e.chemistry;
    // Available reservoirs bound phase selection. The library covers five
    // illustrative solid endmembers, not a complete equilibrium phase diagram.
    std::array<double, 5> weight{
        c.silicate * (e.peakTemperature < 1450 ? .8 : .15) * (e.temperature < 846 ? 1 : 0),
        c.metal * (e.temperature < 1811 ? 1 : 0),
        c.silicate * (e.peakTemperature >= 1450 ? .85 : .2) * (e.temperature < 2163 ? 1 : 0),
        c.ice * (e.temperature < 250 && e.pressureGPa < .2 ? 4 : 0),
        c.carbon * (e.peakTemperature > 600 ? .8 : .3) * (e.temperature < 3900 ? 1 : 0)};
    double total = 0;
    for (double w : weight)
        total += w;
    if (!(total > 0))
        throw std::invalid_argument("No supported solid phase for this environment");
    double choice = Random{featureSeed(seed, Feature::CrystalGrowth, 0)}.unit(0) * total;
    for (size_t i = 0; i < weight.size(); ++i) {
        choice -= weight[i];
        if (choice < 0)
            return static_cast<Mineral>(i);
    }
    throw std::logic_error("Invalid mineral weights");
}
CrystalProcess crystalProcess(uint64_t seed, const Environment& e) {
    Random r{featureSeed(seed, Feature::CrystalGrowth, 1)};
    CrystalProcess c;
    c.growth = .2 + .8 * (1 - e.coolingRate);
    c.axes = {1 - .25 * e.strain, 1 + .2 * e.strain, .8 + .4 * c.growth};
    if (e.mineral == Mineral::Ice || e.mineral == Mineral::Graphite)
        c.axes.z = .45 + .15 * c.growth;
    c.axes *= .9 + .1 * r.unit(0);
    c.boundaryAngle = .08 * e.strain * (1 - .6 * (1 - e.coolingRate));
    c.dislocation = .12 * e.strain;
    return c;
}
RockField::RockField(uint64_t seed, const RockProcess& process)
    : seed_(seed), process_(process), inverse_(glm::inverse(process.fabric)) {
    Random r{featureSeed(seed, Feature::RockFabric, 1)};
    for (int i = 0; i < 9; ++i) {
        clasts_[i] = sphere(r, i * 4) * .46;
        clastRadii_[i] = .25 + .12 * r.unit(i * 4 + 2);
        pits_[i] = sphere(r, 100 + i * 4) * (.65 + .13 * r.unit(102 + i * 4));
        pitRadii_[i] = process.type == RockType::Volcanic ? .06 + process.porosity * .42 : .16;
    }
}
double rockDensity(uint64_t seed, const RockProcess& s, glm::dvec3 p) {
    Random r{featureSeed(seed, Feature::RockFabric, 1)};
    auto q = glm::inverse(s.fabric) * p;
    auto a = q / s.axes;
    double outer = glm::length(a) - .91;
    switch (s.type) {
    case RockType::Plutonic:
        // Joint-bounded intrusive block, exposed and rounded by weathering.
        outer = glm::mix(std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) - .72, outer,
                         .25 + .65 * s.weathering);
        break;
    case RockType::Volcanic:
        outer += .035 * std::sin(q.y * 16 + q.x * 3);
        break;
    case RockType::Sedimentary:
        outer = std::pow(std::pow(std::abs(a.x), 4) + std::pow(std::abs(a.y), 4) + std::pow(std::abs(a.z), 4),
                         .25) -
                .83;
        outer += .025 * std::sin(q.y * 2 * pi / s.bedding);
        break;
    case RockType::Metamorphic:
        outer += .06 * std::sin((q.y + .13 * std::sin(q.x * 5)) * 2 * pi / s.bedding);
        break;
    case RockType::Breccia:
        outer = glm::length(q) - .48;
        for (int i = 0; i < 9; ++i) {
            auto centre = sphere(r, i * 4) * .46;
            outer = std::min(outer, glm::length((q - centre) / glm::dvec3(1, .8, 1)) -
                                        (.25 + .12 * r.unit(i * 4 + 2)));
        }
        break;
    case RockType::Metallic:
        outer = std::max(outer, (std::abs(a.x) + std::abs(a.y) + std::abs(a.z)) * .57735 - .82);
        break;
    case RockType::Glacial:
        outer += .025 * std::sin(q.y * 21 + q.x * 2);
        break;
    }
    outer += fractalNoise(q * 9.0, seed, 3) * (s.type == RockType::Breccia ? .045 : .014);
    // Vesicles and regmaglypt/sublimation pits belong to appropriate processes.
    if (s.type == RockType::Volcanic || s.type == RockType::Metallic || s.type == RockType::Glacial)
        for (int i = 0; i < 9; ++i) {
            auto centre = sphere(r, 100 + i * 4) * (.65 + .13 * r.unit(102 + i * 4));
            double radius = s.type == RockType::Volcanic ? .06 + s.porosity * .42 : .16;
            outer = std::max(outer, radius - glm::length(q - centre));
        }
    if (glm::length(q) > .35 && (s.impact > .25 || s.strain > .3)) {
        double plane = q.y + (s.type == RockType::Metamorphic ? .12 * std::sin(q.x * 5) : .2 * q.z);
        double crack = std::abs(plane - .26) - .008 * (s.impact + s.strain);
        outer = std::max(outer, -crack);
    }
    return outer;
}
double RockField::operator()(glm::dvec3 p) const {
    const auto& s = process_;
    auto q = inverse_ * p;
    auto a = q / s.axes;
    double outer = glm::length(a) - .91;
    switch (s.type) {
    case RockType::Plutonic:
        // Joint-bounded intrusive block, exposed and rounded by weathering.
        outer = glm::mix(std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) - .72, outer,
                         .25 + .65 * s.weathering);
        break;
    case RockType::Volcanic:
        outer += .035 * std::sin(q.y * 16 + q.x * 3);
        break;
    case RockType::Sedimentary:
        outer = std::pow(std::pow(std::abs(a.x), 4) + std::pow(std::abs(a.y), 4) + std::pow(std::abs(a.z), 4),
                         .25) -
                .83;
        outer += .025 * std::sin(q.y * 2 * pi / s.bedding);
        break;
    case RockType::Metamorphic:
        outer += .06 * std::sin((q.y + .13 * std::sin(q.x * 5)) * 2 * pi / s.bedding);
        break;
    case RockType::Breccia:
        outer = glm::length(q) - .48;
        for (int i = 0; i < 9; ++i) {
            auto centre = clasts_[i];
            outer = std::min(outer, glm::length((q - centre) / glm::dvec3(1, .8, 1)) - clastRadii_[i]);
        }
        break;
    case RockType::Metallic:
        outer = std::max(outer, (std::abs(a.x) + std::abs(a.y) + std::abs(a.z)) * .57735 - .82);
        break;
    case RockType::Glacial:
        outer += .025 * std::sin(q.y * 21 + q.x * 2);
        break;
    }
    outer += fractalNoise(q * 9.0, seed_, 3) * (s.type == RockType::Breccia ? .045 : .014);
    // Vesicles and regmaglypt/sublimation pits belong to appropriate processes.
    if (s.type == RockType::Volcanic || s.type == RockType::Metallic || s.type == RockType::Glacial)
        for (int i = 0; i < 9; ++i) {
            auto centre = pits_[i];
            double radius = pitRadii_[i];
            outer = std::max(outer, radius - glm::length(q - centre));
        }
    if (glm::length(q) > .35 && (s.impact > .25 || s.strain > .3)) {
        double plane = q.y + (s.type == RockType::Metamorphic ? .12 * std::sin(q.x * 5) : .2 * q.z);
        double crack = std::abs(plane - .26) - .008 * (s.impact + s.strain);
        outer = std::max(outer, -crack);
    }
    return outer;
}
double crystalDensity(Mineral mineral, const CrystalProcess& c, glm::dvec3 p) {
    auto q = p / c.axes;
    double hexagon = -10;
    for (int i = 0; i < 6; ++i) {
        double a = i * pi / 3;
        hexagon = std::max(hexagon, q.x * std::cos(a) + q.y * std::sin(a) - .7);
    }
    switch (mineral) {
    case Mineral::Quartz:
        return std::max({hexagon, std::abs(q.z) - .95, hexagon + .65 * (std::abs(q.z) - .5)});
    case Mineral::Iron:
        return std::max({std::abs(q.x) - .72, std::abs(q.y) - .72, std::abs(q.z) - .72,
                         (std::abs(q.x) + std::abs(q.y) + std::abs(q.z)) / std::sqrt(3.0) - .95});
    case Mineral::Olivine:
        return std::max({std::abs(q.x) - .68, std::abs(q.y) - .85, std::abs(q.z) - .68,
                         (std::abs(q.x) + std::abs(q.z)) / std::sqrt(2.0) - .82});
    case Mineral::Ice:
    case Mineral::Graphite:
        return std::max(hexagon, std::abs(q.z) - .65);
    }
    throw std::invalid_argument("Unknown mineral habit");
}
glm::vec3 rockAlbedo(const RockProcess& r, Mineral mineral, glm::dvec3 p) {
    auto color = material(mineral).color;
    auto q = glm::inverse(r.fabric) * p;
    if (r.type == RockType::Sedimentary || r.type == RockType::Metamorphic) {
        double fold = r.type == RockType::Metamorphic ? .13 * std::sin(q.x * 5) : 0;
        float layer = static_cast<float>(.5 + .5 * std::sin((q.y + fold) * 2 * pi / r.bedding));
        return glm::mix(color * .46f, color, layer);
    }
    if (r.type == RockType::Volcanic)
        return color * .43f;
    return color * static_cast<float>(.75 + .25 * r.weathering);
}
std::string_view processName(GalaxyType t) {
    constexpr std::array names{"SPIRAL DISC", "BARRED SPIRAL", "ELLIPTICAL SPHEROID",
                               "IRREGULAR ASSOCIATIONS"};
    return names.at(static_cast<size_t>(t));
}
std::string_view processName(GeologyType t) {
    constexpr std::array names{"MOBILE LID / PLATE MOTION", "STAGNANT LID / HOTSPOTS",
                               "ICE SHELL / EXTENSION"};
    return names.at(static_cast<size_t>(t));
}
std::string_view processName(RockType t) {
    constexpr std::array names{
        "PLUTONIC / JOINTED", "VOLCANIC / VESICULAR", "SEDIMENTARY / BEDDED", "METAMORPHIC / FOLIATED",
        "IMPACT BRECCIA",     "METALLIC FRAGMENT",    "GLACIAL ICE"};
    return names.at(static_cast<size_t>(t));
}
} // namespace infinite
