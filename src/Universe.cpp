#include "infinite/Universe.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <numbers>
#include <stdexcept>

namespace infinite {
constexpr double pi = std::numbers::pi;
struct Universe::DescriptorCache {
    std::mutex mutex;
    std::map<Address, Node> objects;
    std::deque<Address> insertionOrder;
    std::map<Address, std::shared_ptr<const std::vector<Orbit>>> populations;
};
Universe::Universe(uint64_t seed) : seed_(seed), descriptors_(std::make_shared<DescriptorCache>()) {}
const Material& material(Mineral m) {
    static const std::array<Material, 5> lib{
        {{Mineral::Quartz, "QUARTZ", "SiO2", "trigonal / alpha quartz", {.46f, .81f, .88f}, 2650, 4.913e-10},
         {Mineral::Iron, "IRON", "Fe", "body-centred cubic", {.74f, .66f, .55f}, 7874, 2.866e-10},
         {Mineral::Olivine, "OLIVINE", "Mg2SiO4", "orthorhombic", {.51f, .71f, .25f}, 3300, 4.754e-10},
         {Mineral::Ice, "ICE Ih", "H2O", "hexagonal oxygen network", {.55f, .76f, .95f}, 917, 4.518e-10},
         {Mineral::Graphite, "GRAPHITE", "C", "hexagonal / AB layers", {.29f, .32f, .36f}, 2260, 2.461e-10}}};
    return lib.at(static_cast<size_t>(m));
}
void Composition::normalize() {
    for (double value : {silicate, metal, carbon, ice})
        if (!std::isfinite(value) || value < 0)
            throw std::invalid_argument("Invalid chemistry reservoir");
    double s = silicate + metal + carbon + ice;
    if (!(s > 0) || !std::isfinite(s))
        throw std::invalid_argument("Empty chemistry");
    silicate /= s;
    metal /= s;
    carbon /= s;
    ice /= s;
}
glm::dvec3 keplerPosition(const Orbit& o, double seconds) {
    constexpr double mu = 1.32712440018e20;
    double mean = std::remainder(
               o.phase + seconds * std::sqrt(mu * o.centralMassSolar / std::pow(o.semiMajorMetres, 3)),
               2 * pi),
           e = mean;
    for (int i = 0; i < 10; ++i)
        e -= (e - o.eccentricity * std::sin(e) - mean) / (1 - o.eccentricity * std::cos(e));
    double x = o.semiMajorMetres * (std::cos(e) - o.eccentricity),
           z = o.semiMajorMetres * std::sqrt(1 - o.eccentricity * o.eccentricity) * std::sin(e);
    return {x, z * std::sin(o.inclination), z * std::cos(o.inclination)};
}
glm::vec3 stellarColor(double temperatureK) {
    return glm::mix(glm::vec3(1, .6, .24), glm::vec3(.68, .8, 1),
                    static_cast<float>(std::clamp((temperatureK - 3000) / 7000, 0.0, 1.0)));
}
std::string Node::label() const {
    if (address.kind() == Kind::Planet && satelliteOf != UINT64_MAX)
        return "Moon " + std::to_string(seed % 100000);
    if (address.kind() == Kind::Planet && expandedPopulation && frame.logRadius < 6.08)
        return "Dwarf " + std::to_string(seed % 100000);
    return std::string(kindName(address.kind())) + " " + std::to_string(seed % 100000);
}
bool isMoon(const Address& a) {
    return a.kind() == Kind::Planet && (a.id() & moonIdFlag) != 0;
}
uint64_t moonId(uint64_t primary, unsigned ordinal) {
    if (primary >= 65536 || ordinal >= 65536)
        throw std::invalid_argument("Moon identity out of range");
    return moonIdFlag | (primary << 16) | ordinal;
}
uint64_t moonPrimary(uint64_t id) {
    if (!(id & moonIdFlag) || (id & ~moonIdFlag) >> 32)
        throw std::invalid_argument("Invalid moon identity");
    return (id & ~moonIdFlag) >> 16;
}
unsigned Universe::legacyPlanetCount(const Node& system) const {
    if (system.address.kind() != Kind::System)
        throw std::invalid_argument("Expected a system");
    return unsigned(5 + system.seed % 5);
}
unsigned Universe::planetCount(const Node& system) const {
    return legacyPlanetCount(system) + 8 + unsigned(mix64(system.seed ^ 0x504f50554c415445ULL) % 5);
}
namespace {
double bodyMass(const Node& n) {
    return 4.188790204786391 * material(n.environment.mineral).density * std::pow(10., 3 * n.frame.logRadius);
}
double moonInner(const Node& n) {
    return std::pow(10., n.frame.logRadius) *
           std::max(3.2, 3.05 * std::cbrt(material(n.environment.mineral).density / 917.));
}
} // namespace
std::shared_ptr<const std::vector<Orbit>> Universe::planetOrbits(const Node& system) const {
    {
        std::lock_guard lock(descriptors_->mutex);
        if (auto it = descriptors_->populations.find(system.address); it != descriptors_->populations.end())
            return it->second;
    }
    auto orbits = std::make_shared<std::vector<Orbit>>();
    std::vector<Node> old;
    const unsigned legacy = legacyPlanetCount(system), total = planetCount(system);
    for (unsigned i = 0; i < legacy; i++) {
        old.push_back(makeChild(system, {Kind::Planet, i}));
        orbits->push_back(old.back().orbit);
    }
    auto hill = [&](const Node& n) {
        return n.orbit.semiMajorMetres * std::cbrt(bodyMass(n) / (3 * system.massSolar * 1.98847e30));
    };
    double outer =
        old.back().orbit.semiMajorMetres * (1 + old.back().orbit.eccentricity) + 6 * hill(old.back());
    unsigned outerSlot = 0;
    for (unsigned id = legacy; id < total; id++) {
        const unsigned slot = id - legacy;
        Random r{childSeed(system.seed, Kind::Planet, id)};
        double a = 0, e = .004 + r.unit(2) * .012;
        if (slot < 3) {
            const double lo = std::max(system.stellarRadiusMetres * (system.companions ? 32 : 8),
                                       .04 * std::sqrt(system.luminositySolar) * 1.495978707e11);
            const double hi = old.front().orbit.semiMajorMetres * (1 - old.front().orbit.eccentricity) -
                              5 * hill(old.front());
            if (hi > lo * 2)
                a = lo * std::pow(hi / lo, (slot + 1.) / 4.);
        } else if (slot - 3 < legacy - 1) {
            const auto& left = old[slot - 3];
            const auto& right = old[slot - 2];
            const double lo = left.orbit.semiMajorMetres * (1 + left.orbit.eccentricity) + 4.5 * hill(left);
            const double hi =
                right.orbit.semiMajorMetres * (1 - right.orbit.eccentricity) - 4.5 * hill(right);
            if (hi > lo * 1.06) {
                a = std::sqrt(lo * hi);
                e = std::min(e, (hi - lo) / (hi + lo) * .25);
            }
        }
        if (a == 0) {
            a = outer * 1.3 * std::pow(1.65, outerSlot++);
            e = .008 + r.unit(2) * .025;
        }
        orbits->push_back({a, e, r.unit(3) * 2 * pi, r.signedUnit(4) * .08, system.massSolar});
    }
    std::lock_guard lock(descriptors_->mutex);
    auto result = descriptors_->populations.emplace(system.address, orbits).first->second;
    if (descriptors_->populations.size() > 64)
        descriptors_->populations.erase(descriptors_->populations.begin());
    return result;
}
double Universe::systemRadiusMetres(const Node& system) const {
    double radius = std::pow(10., system.frame.logRadius);
    for (const auto& orbit : *planetOrbits(system))
        radius = std::max(radius, orbit.semiMajorMetres * (1 + orbit.eccentricity) * 1.4);
    return radius;
}
unsigned Universe::moonCount(const Node& planet) const {
    if (planet.address.kind() != Kind::Planet || isMoon(planet.address))
        return 0;
    const auto system = describe(planet.address.parent());
    const double hill = planet.orbit.semiMajorMetres * (1 - planet.orbit.eccentricity) *
                        std::cbrt(bodyMass(planet) / (3 * system.massSolar * 1.98847e30));
    const double ratio = .32 * hill / moonInner(planet);
    if (ratio < 1)
        return 0;
    const unsigned capacity = 1 + unsigned(std::floor(std::log(ratio) / std::log(1.8)));
    const unsigned desired = 2 + unsigned(mix64(planet.seed ^ 0x4d4f4f4e53595354ULL) % 5);
    return std::min(desired, capacity);
}
std::vector<Address> Universe::moons(const Node& planet) const {
    std::vector<Address> result;
    for (unsigned i = 0; i < moonCount(planet); i++)
        result.push_back(planet.address.parent().child(Kind::Planet, moonId(planet.address.id(), i)));
    return result;
}
double Universe::cosmicDensity(glm::dvec3 p, uint64_t seed) const {
    return cosmicStructure(p, seed).density;
}
namespace {
constexpr double pi2 = 2 * pi;
double powerIntegral(double lo, double hi, double alpha) {
    return (std::pow(hi, 1 - alpha) - std::pow(lo, 1 - alpha)) / (1 - alpha);
}
double sampleMass(double u, double upper) {
    // Continuous broken power-law IMF, conditioned on a surviving main-sequence
    // population. No massive newborn stars are placed in an ancient halo.
    upper = std::clamp(upper, .1, 12.0);
    double low = powerIntegral(.08, std::min(.5, upper), 1.3);
    double high = upper > .5 ? .5 * powerIntegral(.5, upper, 2.3) : 0;
    double choice = u * (low + high), lo = .08, alpha = 1.3;
    if (choice > low) {
        choice = (choice - low) / .5;
        lo = .5;
        alpha = 2.3;
    }
    return std::pow(std::pow(lo, 1 - alpha) + choice * (1 - alpha), 1 / (1 - alpha));
}
glm::dvec3 galacticPosition(const GalaxyProcess& g, const Random& r, Population population, double age) {
    double angle = r.unit(1) * pi2;
    if (population == Population::Bulge || population == Population::Halo) {
        // Truncated Hernquist spheroid: M(<r) proportional to r^2/(r+a)^2.
        double scale = population == Population::Halo ? .18 : .065;
        double u = std::sqrt(r.unit(0)) / (1 + scale);
        double radius = scale * u / (1 - u);
        double y = r.signedUnit(3), xz = std::sqrt(1 - y * y);
        double flatten = g.type == GalaxyType::Elliptical ? g.axisRatio : .7;
        return {radius * xz * std::cos(angle), radius * y * flatten,
                radius * xz * std::sin(angle) * (.8 + .2 * g.axisRatio)};
    }
    // Invert the truncated exponential surface-density disc CDF, r exp(-r/Rd).
    double norm = 1 - std::exp(-1 / g.discScale) * (1 + 1 / g.discScale);
    double lo = 0, hi = 1;
    for (int i = 0; i < 24; ++i) {
        double mid = (lo + hi) * .5, q = mid / g.discScale;
        if (1 - std::exp(-q) * (1 + q) < r.unit(0) * norm)
            lo = mid;
        else
            hi = mid;
    }
    double radius = (lo + hi) * .5;
    if (population == Population::Arm) {
        angle = (r.bits(2) % g.arms) * pi2 / g.arms + std::log(radius + .035) / std::tan(g.pitch);
        angle += r.signedUnit(5) * (.06 + .22 * std::min(age, 1.0));
        if (g.type == GalaxyType::BarredSpiral && radius < .25) {
            return {r.signedUnit(6) * .28, r.signedUnit(3) * g.thickness, r.signedUnit(7) * .055};
        }
    }
    if (population == Population::Association) {
        Random clump{featureSeed(r.key, Feature::GalaxyStructure, 80)};
        // Shared association centres, rather than independent position jitter.
        double a = clump.unit(0) * pi2, rad = .15 + .55 * clump.unit(1);
        return {rad * std::cos(a) + r.signedUnit(8) * .09, r.signedUnit(3) * .08,
                rad * std::sin(a) * .65 + r.signedUnit(9) * .09};
    }
    double vertical = std::atanh(std::clamp(r.signedUnit(3), -.995, .995)) * g.thickness;
    return {radius * std::cos(angle), vertical, radius * std::sin(angle)};
}
} // namespace
StarSample Universe::starSample(const Node& g, uint64_t id) const {
    Random r{childSeed(g.seed, Kind::System, id)};
    Random pop{featureSeed(r.key, Feature::StellarPopulation, 0)};
    const auto& model = g.galaxy;
    double choice = pop.unit(0);
    Population population = choice < .07                                             ? Population::Halo
                            : choice < .07 + model.bulgeFraction                     ? Population::Bulge
                            : model.type == GalaxyType::Irregular                    ? Population::Association
                            : choice < .07 + model.bulgeFraction + model.gasFraction ? Population::Arm
                                                                                     : Population::Disc;
    double age = (population == Population::Halo || population == Population::Bulge)
                     ? g.environment.ageGyr * (.72 + .28 * pop.unit(1))
                 : population == Population::Arm || population == Population::Association
                     ? .005 + .3 * pop.unit(1)
                     : .1 + (g.environment.ageGyr - .1) * pop.unit(1);
    age = std::min(age, g.environment.ageGyr);
    // Associations use a common parent-level key and one of seven centres.
    Random spatial = r;
    if (population == Population::Association)
        spatial.key = featureSeed(g.seed, Feature::GalaxyStructure, id % 7);
    auto p = galacticPosition(model, spatial, population, age);
    if (population == Population::Association)
        p += glm::dvec3(r.signedUnit(15), r.signedUnit(16), r.signedUnit(17)) * .06;
    double mass = sampleMass(pop.unit(2), std::pow(9.5 / std::max(.005, age), .4));
    double metallicity = .022 * std::exp(-glm::length(p) * 1.4) *
                         (population == Population::Halo ? .12 : .5 + .5 * (1 - age / 14));
    return {p, stellarColor(5772 * std::pow(mass, .48)), mass, age, metallicity, population};
}
glm::dvec3 Universe::gasSample(const Node& g, uint64_t id) const {
    Random r{featureSeed(g.seed, Feature::GalaxyStructure, 10000 + id)};
    Population population =
        g.galaxy.type == GalaxyType::Irregular ? Population::Association : Population::Arm;
    if (population == Population::Association) {
        auto centre = galacticPosition(
            g.galaxy, Random{featureSeed(g.seed, Feature::GalaxyStructure, id % 7)}, population, .01);
        return centre + glm::dvec3(r.signedUnit(3), r.signedUnit(4), r.signedUnit(5)) * .07;
    }
    return galacticPosition(g.galaxy, r, population, .01);
}
Node Universe::describe(const Address& a) const {
    auto nodes = lineage(a);
    return nodes.back();
}
std::vector<Node> Universe::lineage(const Address& a) const {
    a.validate();
    Node root;
    root.address.cell = a.cell;
    root.seed = seedOf(root.address, seed_);
    root.frame.logRadius = 26;
    root.environment.ageGyr = 13.4;
    std::vector<Node> nodes{root};
    nodes.reserve(a.path.size() + 1);
    for (auto s : a.path) {
        auto address = nodes.back().address.child(s.kind, s.id);
        {
            std::lock_guard lock(descriptors_->mutex);
            auto cached = descriptors_->objects.find(address);
            if (cached != descriptors_->objects.end()) {
                nodes.push_back(cached->second);
                continue;
            }
        }
        // Pure descriptor construction runs outside the lock. Duplicate worker
        // requests may compute twice, but always produce the same value.
        auto node = makeChild(nodes.back(), s);
        {
            std::lock_guard lock(descriptors_->mutex);
            auto [entry, inserted] = descriptors_->objects.emplace(address, node);
            if (inserted) {
                descriptors_->insertionOrder.push_back(address);
                if (descriptors_->objects.size() > 1024) {
                    descriptors_->objects.erase(descriptors_->insertionOrder.front());
                    descriptors_->insertionOrder.pop_front();
                }
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}
std::vector<LocalFrame> Universe::frames(const Address& a) const {
    std::vector<LocalFrame> f;
    for (auto& n : lineage(a))
        f.push_back(n.frame);
    return f;
}
Pose Universe::relative(const Address& a, const Address& anchor, double log) const {
    return relativePose(frames(a), a, frames(anchor), anchor, log);
}
glm::dvec3 Universe::pointRelative(const Address& a, glm::dvec3 p, const Address& anchor, double log) const {
    return relativePoint(frames(a), a, p, frames(anchor), anchor, log);
}
Node Universe::makeChild(const Node& p, Segment s) const {
    if (static_cast<int>(s.kind) != static_cast<int>(p.address.kind()) + 1)
        throw std::invalid_argument("Invalid hierarchy: child type must refine parent");
    Node n;
    n.address = p.address.child(s.kind, s.id);
    n.seed = childSeed(p.seed, s.kind, s.id);
    n.environment = p.environment;
    n.planetSeed = p.planetSeed;
    n.planetLogRadius = p.planetLogRadius;
    n.geographicDirection = p.geographicDirection;
    n.terrainOrientation = p.terrainOrientation;
    n.hostRockSeed = p.hostRockSeed;
    n.hostRockImpact = p.hostRockImpact;
    n.grainScaleToRock = p.grainScaleToRock;
    n.expandedPopulation = p.expandedPopulation;
    n.satelliteOf = p.satelliteOf;
    n.galaxy = p.galaxy;
    n.population = p.population;
    n.geology = p.geology;
    n.rock = p.rock;
    n.crystal = p.crystal;
    Random r{n.seed};
    auto& env = n.environment;
    switch (s.kind) {
    case Kind::Galaxy: {
        n.frame.logRadius = 20.3 + .6 * r.unit(8);
        glm::dvec3 q;
        for (uint64_t attempt = 0; attempt < 64; ++attempt) {
            q = {r.signedUnit(attempt * 4) * .9, r.signedUnit(attempt * 4 + 1) * .9,
                 r.signedUnit(attempt * 4 + 2) * .9};
            if (r.unit(attempt * 4 + 3) < cosmicDensity(q * 4.0, p.seed))
                break;
        }
        n.frame.position = q;
        env.ageGyr = 7 + 5 * r.unit(10);
        n.galaxy = galaxyProcess(n.seed, cosmicStructure(q * 4.0, p.seed), env.ageGyr);
        n.frame.orientation = glm::angleAxis(
            r.unit(25) * 2 * pi, glm::normalize(glm::dvec3(r.signedUnit(26), 1, r.signedUnit(27))));
        env.chemistry = {.60 + .1 * r.unit(9), .24, .12, .04};
        env.chemistry.normalize();
        break;
    }
    case Kind::System: {
        auto sample = starSample(p, s.id);
        n.frame.position = sample.position;
        n.massSolar = sample.mass;
        n.luminositySolar = std::pow(n.massSolar, 3.5);
        // Refractory material condenses outside the irradiated inner disc.
        // The catalogue and its enclosing reference radius use the same bound.
        double innerAu = std::max(.25, .04 * std::sqrt(n.luminositySolar));
        n.frame.logRadius =
            std::log10(1.6 * innerAu * std::pow(1.85, 4 + static_cast<int>(n.seed % 5)) * 1.495978707e11);
        n.temperatureK = 5772 * std::pow(n.massSolar, .48);
        n.stellarRadiusMetres = 6.957e8 * std::pow(n.massSolar, .8);
        n.companions = r.unit(22) < .24 ? 1 : 0;
        env.ageGyr = sample.ageGyr;
        n.population = sample.population;
        // These are condensed-solid reservoirs, not bulk stellar H/He fractions.
        // Enrichment and radial populations modify the inherited relative budget.
        env.chemistry.metal *= .6 + sample.metallicity * 25;
        env.chemistry.normalize();
        env.temperature = n.temperatureK;
        break;
    }
    case Kind::Planet: {
        if (isMoon(n.address)) {
            const auto primary = describe(p.address.child(Kind::Planet, moonPrimary(s.id)));
            const unsigned ordinal = unsigned(s.id & 65535);
            if (ordinal >= moonCount(primary))
                throw std::invalid_argument("Moon outside stable satellite catalogue");
            n.expandedPopulation = true;
            n.satelliteOf = primary.address.id();
            env = primary.environment;
            const double radius = std::pow(10., primary.frame.logRadius);
            n.orbit = {moonInner(primary) * std::pow(1.8, ordinal), .002 + r.unit(2) * .012,
                       r.unit(3) * 2 * pi, primary.orbit.inclination + r.signedUnit(4) * .16,
                       bodyMass(primary) / 1.98847e30};
            n.frame.position =
                primary.frame.position + keplerPosition(n.orbit, 0) / std::pow(10., p.frame.logRadius);
            n.frame.logRadius =
                std::log10(std::max(20000., radius * (.12 + .045 * r.unit(5)) * std::pow(.68, ordinal)));
            n.planetLogRadius = n.frame.logRadius;
            n.planetSeed = n.seed;
            env.temperature = std::max(2.725, primary.environment.temperature * (.97 + .06 * r.unit(7)));
            env.pressureGPa = std::min(.08, env.pressureGPa * .15);
            env.peakTemperature = std::max(env.temperature, 650 + 1000 * r.unit(8));
            env.coolingRate = std::clamp(.55 + .4 * r.unit(14), .1, 1.);
            env.strain = std::clamp(env.strain + .25 * r.unit(16), 0., 1.);
            env.impact = std::clamp(env.impact + .2 * r.unit(17), 0., 1.);
            env.mineral = selectMineral(n.seed, env);
            const double massLimitedRadius =
                radius * std::cbrt(.004 * material(primary.environment.mineral).density /
                                   material(env.mineral).density);
            n.frame.logRadius =
                std::log10(std::max(3000., std::min(std::pow(10., n.frame.logRadius), massLimitedRadius)));
            n.planetLogRadius = n.frame.logRadius;
            env.waterActivity *= std::clamp(std::pow(10., n.frame.logRadius - 6.5), .002, .15);
            n.geology = planetProcess(n.seed, n.frame.logRadius, env);
            n.massSolar = bodyMass(n) / 1.98847e30;
            break;
        }
        if (s.id >= planetCount(p))
            throw std::invalid_argument("Planet index is outside this system's catalogue");
        double innerAu = std::max(.25, .04 * std::sqrt(p.luminositySolar));
        double au = innerAu * (1 + .4 * r.unit(1)) * std::pow(1.85, static_cast<double>(s.id));
        n.orbit = {au * 1.495978707e11, .01 + .12 * r.unit(2), r.unit(3) * 2 * pi, r.signedUnit(4) * .06,
                   p.massSolar};
        n.frame.position = keplerPosition(n.orbit, 0) / std::pow(10.0, p.frame.logRadius);
        n.frame.logRadius = 6.45 + .6 * r.unit(5);
        if (s.id >= legacyPlanetCount(p)) {
            n.expandedPopulation = true;
            n.orbit = planetOrbits(p)->at(size_t(s.id));
            au = n.orbit.semiMajorMetres / 1.495978707e11;
            n.frame.position = keplerPosition(n.orbit, 0) / std::pow(10., p.frame.logRadius);
            n.frame.logRadius = 5.4 + 1.05 * r.unit(5);
        }
        n.planetLogRadius = n.frame.logRadius;
        n.planetSeed = n.seed;
        env.temperature = 278.0 * std::pow(p.luminositySolar, .25) / std::sqrt(au);
        if (n.expandedPopulation)
            env.temperature = std::max(2.725, env.temperature);
        env.pressureGPa = .015 + .35 * r.unit(7);
        env.peakTemperature = std::max(env.temperature, 850 + 1700 * std::exp(-env.ageGyr / 12) * r.unit(8));
        // Condensation beyond the irradiation-dependent snow line retains ice;
        // differentiation partitions Fe and silicates without adding a new element.
        env.chemistry.ice *= env.temperature < 180 ? 15 + 10 * r.unit(9) : .04;
        env.chemistry.metal *= .35 + 2.8 * r.unit(10);
        env.chemistry.carbon *= .4 + 2.0 * r.unit(11);
        env.chemistry.normalize();
        env.coolingRate = std::clamp(.12 + .76 * r.unit(14) + env.ageGyr * .01, 0.0, 1.0);
        env.waterActivity = std::clamp(env.chemistry.ice * 2 + .4 * r.unit(15), 0.0, 1.0);
        env.strain = .05 + .6 * r.unit(16);
        env.impact = .08 + .78 * r.unit(17) * std::exp(-env.ageGyr / 10);
        if (env.chemistry.ice > .25 && env.temperature < 250)
            env.pressureGPa = std::min(env.pressureGPa, .12);
        env.mineral = selectMineral(n.seed, env);
        n.geology = planetProcess(n.seed, n.frame.logRadius, env);
        if (n.expandedPopulation)
            n.massSolar = bodyMass(n) / 1.98847e30;
        break;
    }
    case Kind::Surface: {
        Tile t = decodeTile(s.id);
        n.geographicDirection = tileDirection(t);
        n.frame.logRadius = p.frame.logRadius + std::log10(2.0 / (1u << t.level));
        n.frame.position = n.geographicDirection *
                           (1 + terrainHeight(p, n.geographicDirection) / std::pow(10.0, p.frame.logRadius));
        n.terrainOrientation = tangentFrame(n.geographicDirection);
        n.frame.orientation = n.terrainOrientation;
        break;
    }
    case Kind::Mountain:
    case Kind::Rock: {
        double x = r.signedUnit(0) * .6, z = r.signedUnit(1) * .6;
        n.frame.position = terrainPoint(p, x, z);
        double ratio = std::pow(10.0, p.frame.logRadius - p.planetLogRadius);
        auto basis = p.terrainOrientation;
        n.geographicDirection =
            glm::normalize(p.geographicDirection + basis * glm::dvec3(x * ratio, 0, z * ratio));
        auto axis = glm::cross(p.geographicDirection, n.geographicDirection);
        auto transport = glm::normalize(
            glm::dquat(1 + glm::dot(p.geographicDirection, n.geographicDirection), axis.x, axis.y, axis.z));
        n.terrainOrientation = glm::normalize(transport * basis);
        n.frame.orientation = glm::inverse(basis) * n.terrainOrientation;
        n.frame.logRadius = s.kind == Kind::Mountain
                                ? p.frame.logRadius - 1.4
                                : std::min(-.4 + .7 * r.unit(7), p.frame.logRadius - 1.0);
        if (s.kind == Kind::Rock) {
            n.frame.position.y += std::pow(10.0, n.frame.logRadius - p.frame.logRadius) * .55;
            const auto site = geologySample(*n.geology, n.geographicDirection);
            env.strain = std::clamp(env.strain * .5 + site.convergence + site.shear * .4, 0.0, 1.0);
            env.peakTemperature =
                std::max(env.temperature, p.environment.peakTemperature * (.65 + .35 * site.volcanism));
            env.coolingRate =
                std::clamp(env.coolingRate + .3 * site.volcanism - .1 * env.pressureGPa, 0.0, 1.0);
            n.rock = rockProcess(n.seed, env, site);
            env.mineral = selectMineral(n.seed, env);
        }
        break;
    }
    case Kind::Grain: {
        double theta = r.unit(0) * 2 * pi, v = r.signedUnit(1);
        glm::dvec3 dir(std::sqrt(1 - v * v) * std::cos(theta), v, std::sqrt(1 - v * v) * std::sin(theta));
        // Locate a grain on the actual SDF rock boundary with a bracketed root.
        // Find the outermost actual solid crossing, including rays whose origin
        // lies in a cavity. A sign bracket is required before bisection.
        double hi = 1.6, lo = hi;
        bool bracketed = false;
        for (int step = 1; step <= 256; ++step) {
            lo = 1.6 * (1 - static_cast<double>(step) / 256);
            if (rockSdf(p, dir * lo) < 0) {
                bracketed = true;
                break;
            }
            hi = lo;
        }
        if (!bracketed)
            throw std::runtime_error("No solid grain host on ray");
        for (int i = 0; i < 48; ++i) {
            double mid = (lo + hi) * .5;
            if (rockSdf(p, dir * mid) < 0)
                lo = mid;
            else
                hi = mid;
        }
        n.frame.position = dir * ((lo + hi) * .5);
        n.frame.orientation = tangentFrame(dir) * glm::angleAxis(r.unit(4) * 2 * pi, glm::dvec3(0, 1, 0));
        // Longer diffusion/growth during slow cooling produces larger grains.
        // Metamorphic fabrics elongate habits and retain the host strain history.
        n.frame.logRadius = p.frame.logRadius - 1.9 - 1.6 * env.coolingRate - .25 * r.unit(2);
        env.mineral = selectMineral(n.seed, env);
        n.crystal = crystalProcess(n.seed, env);
        n.hostRockSeed = p.seed;
        n.hostRockImpact = p.history[1].intensity;
        n.grainScaleToRock = std::pow(10.0, n.frame.logRadius - p.frame.logRadius);
        break;
    }
    case Kind::Lattice: {
        bool inside = false;
        for (uint64_t attempt = 0; attempt < 128; ++attempt) {
            auto candidate = glm::dvec3(r.signedUnit(attempt * 3) * .3, -.2 - r.unit(attempt * 3 + 1) * .3,
                                        r.signedUnit(attempt * 3 + 2) * .3);
            if (grainSdf(p, candidate) < -.08) {
                n.frame.position = candidate;
                inside = true;
                break;
            }
        }
        if (!inside)
            throw std::runtime_error("No interior lattice specimen in grain");
        n.frame.logRadius = std::log10(material(env.mineral).cellMetres * 6);
        break;
    }
    default:
        break;
    }
    for (size_t i = 0; i < n.history.size(); ++i) {
        auto type = static_cast<EventType>(i == 0   ? 0
                                           : i == 1 ? 1
                                           : i == 2 ? 2
                                           : i == 3 ? 3
                                           : i == 4 ? 5
                                                    : 6);
        std::array<double, 6> intensity{1,
                                        env.impact,
                                        std::clamp(env.peakTemperature / 2600, 0.0, 1.0),
                                        env.coolingRate,
                                        env.waterActivity,
                                        1 - env.coolingRate};
        n.history[i] = {type, env.ageGyr * (1 - static_cast<double>(i) / 7), intensity[i], mix64(n.seed + i)};
    }
    if (s.kind == Kind::Lattice)
        n.history = p.history;
    return n;
}
std::vector<Address> Universe::children(const Node& n, size_t limit) const {
    if (n.address.kind() == Kind::System) {
        std::vector<Address> result;
        for (unsigned i = 0; i < planetCount(n) && result.size() < limit; i++)
            result.push_back(n.address.child(Kind::Planet, i));
        for (unsigned i = 0; i < planetCount(n) && result.size() < limit; i++)
            for (const auto& moon : moons(describe(n.address.child(Kind::Planet, i))))
                if (result.size() < limit)
                    result.push_back(moon);
        return result;
    }
    size_t count = 0;
    auto kind = n.address.kind();
    switch (kind) {
    case Kind::Cosmic:
        count = 128;
        break;
    case Kind::Galaxy:
        count = 192;
        break;
    case Kind::System:
        count = 5 + n.seed % 5;
        break;
    case Kind::Planet:
        count = 18;
        break;
    case Kind::Surface:
        count = 12;
        break;
    case Kind::Mountain:
        count = 24;
        break;
    case Kind::Rock:
        count = 36;
        break;
    case Kind::Grain:
        count = 1;
        break;
    default:
        break;
    }
    std::vector<Address> out;
    out.reserve(std::min(count, limit));
    Random r{n.seed};
    for (size_t i = 0; i < std::min(count, limit); ++i) {
        uint64_t id = i;
        if (kind == Kind::Planet) {
            id = encodeTile({static_cast<int>(i % 6), 8, static_cast<uint32_t>(r.bits(i * 2) % 256),
                             static_cast<uint32_t>(r.bits(i * 2 + 1) % 256)});
        }
        out.push_back(n.address.child(static_cast<Kind>(static_cast<int>(kind) + 1), id));
    }
    return out;
}
double Universe::terrainHeight(const Node& p, glm::dvec3 d) const {
    if (!p.geology)
        throw std::invalid_argument("Terrain has no planetary process descriptor");
    return geologySample(*p.geology, d).heightMetres;
}
glm::dvec3 Universe::terrainDirection(const Node& p, double x, double z) const {
    double ratio = std::pow(10.0, p.frame.logRadius - p.planetLogRadius);
    if (x == 0 && z == 0)
        return p.geographicDirection;
    return glm::normalize(p.geographicDirection + p.terrainOrientation * glm::dvec3(x * ratio, 0, z * ratio));
}
glm::dvec3 Universe::terrainNormal(const Node& n, glm::dvec3 d) const {
    double log = n.address.kind() == Kind::Planet ? n.frame.logRadius : n.planetLogRadius;
    auto q = tangentFrame(d);
    constexpr double step = 1e-5;
    auto point = [&](glm::dvec3 v) {
        v = glm::normalize(v);
        return v * (1 + terrainHeight(n, v) / std::pow(10.0, log));
    };
    auto east = q * glm::dvec3(step, 0, 0), north = q * glm::dvec3(0, 0, step);
    return glm::normalize(glm::cross(point(d + north) - point(d - north), point(d + east) - point(d - east)));
}
glm::vec3 Universe::terrainAlbedo(const Node& n, glm::dvec3 d) const {
    const auto field = geologySample(*n.geology, d);
    return terrainAlbedo(n, d, field);
}
glm::vec3 Universe::terrainAlbedo(const Node& n, glm::dvec3, const GeologySample& field) const {
    double log = n.address.kind() == Kind::Planet ? n.frame.logRadius : n.planetLogRadius;
    double h = field.heightMetres / std::pow(10.0, log);
    glm::vec3 color;
    if (n.geology->type == GeologyType::IceShell)
        color = glm::mix(glm::vec3(.21, .31, .38), glm::vec3(.75, .84, .88),
                         static_cast<float>(smoothstep(-.001, .002, h)));
    else if (n.geology->liquidWater > .1 && h < -.0008)
        color = {.025f, .18f, .28f};
    else
        color = glm::mix(glm::vec3(.18, .22, .15), glm::vec3(.58, .47, .31),
                         static_cast<float>(smoothstep(-.001, .003, h)));
    color = glm::mix(color, glm::vec3(.11, .10, .095),
                     static_cast<float>(std::clamp(field.volcanism * .7, 0.0, .8)));
    if (n.geology->liquidWater > .1 && h > .003)
        color = glm::mix(color, glm::vec3(.85, .88, .85), static_cast<float>(smoothstep(.003, .006, h)));
    return color;
}
glm::dvec3 Universe::terrainPoint(const Node& p, double x, double z) const {
    double ratio = std::pow(10.0, p.frame.logRadius - p.planetLogRadius);
    auto n = p.geographicDirection;
    auto basis = p.terrainOrientation;
    auto d = terrainDirection(p, x, z);
    double h = terrainHeight(p, d) / std::pow(10.0, p.frame.logRadius),
           h0 = terrainHeight(p, n) / std::pow(10.0, p.frame.logRadius);
    return glm::inverse(basis) * ((d - n) / ratio + d * h - n * h0);
}
double Universe::rockSdf(const Node& n, glm::dvec3 p) const {
    return rockDensity(n.seed, n.rock, p);
}
double Universe::grainSdf(const Node& n, glm::dvec3 p) const {
    double sdf = crystalDensity(n.environment.mineral, n.crystal, p);
    if (n.hostRockSeed) {
        auto hostPoint = n.frame.position + n.frame.orientation * (p * n.grainScaleToRock);
        sdf = std::max(sdf, rockDensity(n.hostRockSeed, n.rock, hostPoint) / n.grainScaleToRock);
    }
    return sdf;
}
uint64_t atomicSiteId(int x, int y, int z, unsigned motif) {
    if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX || z < INT16_MIN || z > INT16_MAX ||
        motif > UINT16_MAX)
        throw std::invalid_argument("Atomic site address out of range");
    return static_cast<uint64_t>(static_cast<uint16_t>(x)) |
           (static_cast<uint64_t>(static_cast<uint16_t>(y)) << 16) |
           (static_cast<uint64_t>(static_cast<uint16_t>(z)) << 32) | (static_cast<uint64_t>(motif) << 48);
}
uint64_t descriptorFingerprint(const Node& n) {
    uint64_t hash = generationRecipeVersion;
    auto add = [&](uint64_t value) { hash = mix64(hash ^ value); };
    auto real = [&](double value) { add(std::bit_cast<uint64_t>(value)); };
    auto vec = [&](glm::dvec3 v) {
        real(v.x);
        real(v.y);
        real(v.z);
    };
    for (auto c : n.address.cell)
        add(static_cast<uint64_t>(c));
    for (auto p : n.address.path) {
        add(static_cast<uint64_t>(p.kind));
        add(p.id);
    }
    add(n.seed);
    if (n.expandedPopulation) {
        add(populationRecipeVersion);
        add(n.satelliteOf);
    }
    real(n.frame.logRadius);
    vec(n.frame.position);
    real(n.frame.orientation.w);
    real(n.frame.orientation.x);
    real(n.frame.orientation.y);
    real(n.frame.orientation.z);
    const auto& e = n.environment;
    real(e.chemistry.silicate);
    real(e.chemistry.metal);
    real(e.chemistry.carbon);
    real(e.chemistry.ice);
    real(e.temperature);
    real(e.pressureGPa);
    real(e.ageGyr);
    real(e.peakTemperature);
    real(e.coolingRate);
    real(e.waterActivity);
    real(e.strain);
    real(e.impact);
    add(static_cast<uint64_t>(e.mineral));
    for (auto event : n.history) {
        add(static_cast<uint64_t>(event.type));
        real(event.ageGyr);
        real(event.intensity);
        add(event.seed);
    }
    real(n.massSolar);
    real(n.temperatureK);
    real(n.luminositySolar);
    real(n.stellarRadiusMetres);
    add(n.companions);
    real(n.orbit.semiMajorMetres);
    real(n.orbit.eccentricity);
    real(n.orbit.phase);
    real(n.orbit.inclination);
    real(n.orbit.centralMassSolar);
    vec(n.geographicDirection);
    real(n.terrainOrientation.w);
    real(n.terrainOrientation.x);
    real(n.terrainOrientation.y);
    real(n.terrainOrientation.z);
    real(n.planetLogRadius);
    add(n.planetSeed);
    add(n.hostRockSeed);
    real(n.hostRockImpact);
    real(n.grainScaleToRock);
    add(static_cast<uint64_t>(n.galaxy.type));
    for (double v : {n.galaxy.bulgeFraction, n.galaxy.gasFraction, n.galaxy.discScale, n.galaxy.thickness,
                     n.galaxy.pitch, n.galaxy.axisRatio, n.galaxy.merger})
        real(v);
    add(n.galaxy.arms);
    add(static_cast<uint64_t>(n.population));
    add(n.geology != nullptr);
    if (n.geology) {
        const auto& p = *n.geology;
        add(p.seed);
        add(static_cast<uint64_t>(p.type));
        for (double v : {p.logRadius, p.activity, p.liquidWater, p.ice, p.ageGyr})
            real(v);
        for (const auto& plate : p.plates) {
            vec(plate.centre);
            vec(plate.angularVelocity);
            real(plate.continental);
        }
        for (const auto& crater : p.impacts) {
            vec(crater.centre);
            real(crater.radius);
            real(crater.survival);
        }
        for (auto hotspot : p.hotspots)
            vec(hotspot);
    }
    add(static_cast<uint64_t>(n.rock.type));
    vec(n.rock.axes);
    real(n.rock.fabric.w);
    real(n.rock.fabric.x);
    real(n.rock.fabric.y);
    real(n.rock.fabric.z);
    for (double v : {n.rock.porosity, n.rock.bedding, n.rock.weathering, n.rock.strain, n.rock.impact,
                     n.rock.coolingRate})
        real(v);
    vec(n.crystal.axes);
    real(n.crystal.growth);
    real(n.crystal.boundaryAngle);
    real(n.crystal.dislocation);
    return hash;
}
} // namespace infinite
