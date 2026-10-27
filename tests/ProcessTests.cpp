#include "infinite/Geometry.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numbers>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <set>
#include <stdexcept>

using namespace infinite;
static size_t checks = 0;
static void check(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
static glm::dvec3 direction(unsigned i, unsigned count) {
    double y = 1 - 2 * (i + .5) / count, angle = i * std::numbers::pi * (3 - std::sqrt(5.0));
    return {std::sqrt(1 - y * y) * std::cos(angle), y, std::sqrt(1 - y * y) * std::sin(angle)};
}
static void webTests() {
    double minimum = 1, maximum = 0, sheet = 0, filament = 0, knot = 0;
    unsigned voids = 0;
    for (unsigned i = 0; i < 1500; ++i) {
        Random r{937};
        auto p = glm::dvec3(r.signedUnit(i * 3), r.signedUnit(i * 3 + 1), r.signedUnit(i * 3 + 2)) * 4.0;
        auto s = cosmicStructure(p, 410);
        check(s.density >= 0 && s.density <= 1 && s.sheet >= 0 && s.knot <= 1, "invalid web density");
        check(cosmicStructure(p, 410).density == s.density, "web query order changed density");
        minimum = std::min(minimum, s.density);
        maximum = std::max(maximum, s.density);
        sheet = std::max(sheet, s.sheet);
        filament = std::max(filament, s.filament);
        knot = std::max(knot, s.knot);
        voids += s.density < .03;
    }
    check(maximum > .5 && minimum < .01 && voids > 500, "cosmic web lacks collapsed knots and empty voids");
    check(sheet > .95 && filament > .9 && knot > .8, "cosmic web lacks distinct collapse dimensions");
    for (int axis = 0; axis < 3; ++axis)
        for (int cell = -3; cell <= 3; ++cell) {
            glm::dvec3 p(.713, -.279, 1.816), delta(0);
            p[axis] = cell;
            delta[axis] = 1e-7;
            check(std::abs(cosmicStructure(p - delta, 410).density -
                           cosmicStructure(p + delta, 410).density) < 1e-4,
                  "cosmic field discontinuity at integer cell boundary");
        }
}
static void galaxyTests(const Universe& u) {
    std::map<GalaxyType, Node> types;
    for (uint64_t i = 0; i < 64; ++i) {
        auto n = u.describe(Address{}.child(Kind::Galaxy, i));
        types.try_emplace(n.galaxy.type, n);
    }
    check(types.size() == 4, "seed ensemble misses a galaxy process");
    std::map<GalaxyType, double> thickness;
    bool irradiatedInnerDisc = false;
    for (auto [type, g] : types) {
        double vertical = 0, radial = 0, age = 0;
        unsigned lowMass = 0, young = 0;
        for (unsigned i = 0; i < 4000; ++i) {
            auto s = u.starSample(g, i);
            check(s.ageGyr <= g.environment.ageGyr && s.ageGyr > 0, "star older than its parent population");
            check(s.mass >= .08 && s.mass <= 12 && 10 / std::pow(s.mass, 2.5) >= s.ageGyr,
                  "stellar mass incompatible with population lifetime");
            check(s.position == u.describe(g.address.child(Kind::System, i)).frame.position,
                  "galaxy process and addressed star disagree");
            if (s.mass > 2) {
                auto host = u.describe(g.address.child(Kind::System, i));
                irradiatedInnerDisc = irradiatedInnerDisc || host.luminositySolar > 40;
                auto planets = u.children(host);
                for (const auto& address : {planets.front(), planets.back()}) {
                    auto planet = u.describe(address);
                    check(planet.environment.temperature <= 1391 &&
                              planet.environment.peakTemperature >= planet.environment.temperature,
                          "luminous-star orbit violates solid condensation/thermal constraints");
                    check(glm::length(planet.frame.position) * std::pow(10., host.frame.logRadius) <
                              u.systemRadiusMetres(host),
                          "condensation-driven orbit left its physical system envelope");
                }
            }
            vertical += s.position.y * s.position.y;
            radial += s.position.x * s.position.x + s.position.z * s.position.z;
            age += s.ageGyr;
            lowMass += s.mass < .5;
            young += s.ageGyr < .5;
        }
        thickness[type] = vertical / radial;
        check(lowMass > 2400, "IMF no longer favours long-lived low mass stars");
        if (type == GalaxyType::Elliptical) {
            check(young < 150 && age / 4000 > 4 && g.galaxy.gasFraction < .04,
                  "quenched spheroid contains a young gas-rich disc population");
        }
        if (type == GalaxyType::Irregular)
            check(young > 2500, "irregular associations lost their young population");
        std::cout << processName(type) << " thickness=" << thickness[type] << " young=" << young << '\n';
    }
    check(thickness[GalaxyType::Elliptical] > thickness[GalaxyType::Spiral] * 2,
          "spheroid and spiral have indistinguishable spatial structure");
    check(irradiatedInnerDisc, "stellar ensemble did not exercise an irradiation-truncated inner disc");
    // Present the same descriptor under a different sample count: the prefix
    // must keep positions, ages, populations and masses without re-sampling.
    auto g = types.at(GalaxyType::Spiral);
    auto a = GeometryGenerator(u).generate(g, 0), b = GeometryGenerator(u).generate(g, 2);
    for (unsigned i = 0; i < 5000; ++i)
        check(a->particles[i].position == b->particles[i].position && a->particles[i].objectId == i,
              "galactic population changed with LOD sample count");
}
static void geologyTests() {
    Environment wet;
    wet.ageGyr = 3;
    wet.peakTemperature = 1800;
    wet.waterActivity = .9;
    auto mobile = planetProcess(42, 6.9, wet);
    auto dry = wet;
    dry.waterActivity = 0;
    auto stagnant = planetProcess(42, 6.9, dry);
    auto frozen = wet;
    frozen.temperature = 120;
    frozen.chemistry = {.2, .1, .05, .65};
    auto icy = planetProcess(42, 6.9, frozen);
    check(mobile->type == GeologyType::MobileLid && stagnant->type == GeologyType::StagnantLid &&
              icy->type == GeologyType::IceShell,
          "geology ignored heat, hydration or composition");
    double erosion = 0, difference = 0, extension = 0, convergence = 0, shear = 0;
    for (unsigned i = 0; i < 1500; ++i) {
        auto d = direction(i, 1500);
        auto a = geologySample(*mobile, d), b = geologySample(*stagnant, d);
        auto c = geologySample(*icy, d);
        check(std::isfinite(a.heightMetres) && std::abs(a.heightMetres) < 2e5, "invalid geological relief");
        check(b.erosion == 0 && c.erosion == 0, "liquid erosion on a dry or frozen body");
        auto nearby = geologySample(*mobile, glm::normalize(d + glm::dvec3(1e-8, -1e-8, 1e-8)));
        check(std::abs(a.heightMetres - nearby.heightMetres) < .2, "geology field has a seam");
        erosion += a.erosion;
        difference += std::abs(a.heightMetres - b.heightMetres);
        extension = std::max(extension, a.divergence);
        convergence = std::max(convergence, a.convergence);
        shear = std::max(shear, a.shear);
    }
    check(erosion > .02 && difference > 1e6, "water and plate regime do not affect terrain");
    check(extension > .1 && convergence > .1 && shear > .1,
          "plate velocities do not create all boundary processes");
    for (size_t i = 0; i < mobile->impacts.size(); ++i)
        check(mobile->impacts[i].survival <= stagnant->impacts[i].survival,
              "active resurfacing preserves more craters than stagnant crust");
    // Reverse all Euler poles: closure becomes divergence without re-rolling plates.
    auto reversed = *mobile;
    for (auto& plate : reversed.plates)
        plate.angularVelocity = -plate.angularVelocity;
    for (size_t i = 0; i < mobile->plates.size(); ++i)
        for (size_t j = i + 1; j < mobile->plates.size(); ++j) {
            auto d = glm::normalize(mobile->plates[i].centre - mobile->plates[j].centre);
            check(std::isfinite(geologySample(*mobile, d).heightMetres),
                  "degenerate projected boundary normal generated non-finite geology");
        }
    for (unsigned i = 0; i < 50; ++i) {
        auto d = direction(i, 50);
        auto a = geologySample(*mobile, d), b = geologySample(reversed, d);
        check(std::abs(a.convergence - b.divergence) < 1e-12 && std::abs(a.shear - b.shear) < 1e-12,
              "tectonic features are not driven by plate velocity");
    }
}
static void rockTests() {
    Environment base;
    base.impact = .1;
    base.strain = .1;
    base.coolingRate = .2;
    GeologySample site;
    site.sediment = .8;
    std::array<Environment, 7> environments;
    environments.fill(base);
    environments[1].coolingRate = .9;
    environments[2].waterActivity = .9;
    environments[3].strain = .8;
    environments[3].pressureGPa = .4;
    environments[4].impact = .9;
    environments[5].chemistry = {.2, .7, .08, .02};
    environments[6].temperature = 140;
    environments[6].chemistry = {.2, .1, .05, .65};
    std::set<uint64_t> signatures;
    for (size_t i = 0; i < environments.size(); ++i) {
        auto p = rockProcess(83, environments[i], site);
        check(static_cast<size_t>(p.type) == i, "rock origin ignored formation constraints");
        uint64_t signature = 0;
        check(rockDensity(83, p, {}) < 0, "rock has no connected interior host");
        for (unsigned j = 0; j < 400; ++j) {
            auto d = direction(j, 400);
            check(rockDensity(83, p, d * 1.3) > 0, "rock escapes its bounded meshing window");
            double lo = 0, hi = 1.3;
            for (int step = 0; step < 24; ++step) {
                double mid = (lo + hi) * .5;
                if (rockDensity(83, p, d * mid) < 0)
                    lo = mid;
                else
                    hi = mid;
            }
            signature = mix64(signature ^ static_cast<uint64_t>((lo + hi) * 1e8));
        }
        signatures.insert(signature);
    }
    check(signatures.size() == 7, "rock origins produce indistinguishable density fields");
    auto slow = crystalProcess(50, base);
    auto fastEnvironment = base;
    fastEnvironment.coolingRate = .95;
    auto fast = crystalProcess(50, fastEnvironment);
    check(slow.growth > fast.growth && slow.axes.z > fast.axes.z,
          "cooling no longer controls crystal growth");
    std::set<uint64_t> habits;
    for (auto mineral : {Mineral::Quartz, Mineral::Iron, Mineral::Olivine, Mineral::Ice, Mineral::Graphite}) {
        auto e = base;
        e.mineral = mineral;
        auto c = crystalProcess(83, e);
        uint64_t signature = 0;
        for (unsigned j = 0; j < 300; ++j) {
            double density = crystalDensity(mineral, c, direction(j, 300) * .7);
            signature = mix64(signature ^ static_cast<uint64_t>((density + 10) * 1e9));
        }
        habits.insert(signature);
    }
    // Ice and graphite share basal hexagonal symmetry but remain different
    // phases, compositions and atomic topology; three other habits differ.
    check(habits.size() >= 4, "material habits all use a generic random polyhedron");
}
static void latticeTests() {
    Environment ideal;
    ideal.temperature = 293;
    ideal.pressureGPa = 0;
    ideal.strain = 0;
    ideal.impact = 0;
    const std::array<std::map<uint8_t, unsigned>, 5> counts{std::map<uint8_t, unsigned>{{14, 3}, {8, 6}},
                                                            {{26, 2}},
                                                            {{12, 8}, {14, 4}, {8, 16}},
                                                            {{8, 4}},
                                                            {{6, 4}}};
    for (size_t m = 0; m < counts.size(); ++m) {
        auto mineral = static_cast<Mineral>(m);
        auto cell = crystalCell(mineral, ideal);
        std::map<uint8_t, unsigned> species;
        for (auto site : cell.sites) {
            ++species[site.element];
            check(glm::all(glm::greaterThanEqual(site.fractional, glm::dvec3(0))) &&
                      glm::all(glm::lessThan(site.fractional, glm::dvec3(1))),
                  "noncanonical fractional site");
        }
        check(species == counts[m], "crystallographic motif has the wrong stoichiometry");
        for (auto centre : cell.sites) {
            if (mineral == Mineral::Quartz && centre.element != 14)
                continue;
            if (mineral == Mineral::Olivine && centre.element != 14)
                continue;
            unsigned neighbours = 0;
            double cutoff = mineral == Mineral::Iron       ? 2.65e-10
                            : mineral == Mineral::Graphite ? 1.65e-10
                            : mineral == Mineral::Ice      ? 3.1e-10
                                                           : 1.9e-10;
            for (int z = -1; z <= 1; ++z)
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                        for (auto other : cell.sites) {
                            if ((mineral == Mineral::Quartz || mineral == Mineral::Olivine) &&
                                other.element != 8)
                                continue;
                            double distance =
                                glm::length(cell.basisMetres *
                                            (glm::dvec3(x, y, z) + other.fractional - centre.fractional));
                            neighbours += distance > .8e-10 && distance < cutoff;
                        }
            unsigned expected = mineral == Mineral::Iron ? 8 : mineral == Mineral::Graphite ? 3 : 4;
            if (neighbours != expected)
                std::cerr << "Coordination mineral=" << m << " element=" << unsigned(centre.element)
                          << " count=" << neighbours << '\n';
            check(neighbours == expected, "unit cell has the wrong local coordination topology");
        }
        auto hot = ideal;
        hot.temperature = mineral == Mineral::Iron ? 500 : 350;
        check(glm::determinant(crystalCell(mineral, hot).basisMetres) > glm::determinant(cell.basisMetres),
              "thermal expansion does not change cell volume");
        hot = ideal;
        hot.pressureGPa = .1;
        check(glm::determinant(crystalCell(mineral, hot).basisMetres) < glm::determinant(cell.basisMetres),
              "pressure does not compress cell volume");
    }
    auto hotIron = ideal;
    hotIron.temperature = 1300;
    auto fcc = crystalCell(Mineral::Iron, hotIron);
    check(fcc.phase == CrystalPhase::FccIron && fcc.sites.size() == 4, "iron phase ignored temperature");
    for (auto centre : fcc.sites) {
        unsigned neighbours = 0;
        for (int z = -1; z <= 1; ++z)
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                    for (auto site : fcc.sites) {
                        double distance = glm::length(
                            fcc.basisMetres * (glm::dvec3(x, y, z) + site.fractional - centre.fractional));
                        neighbours += distance > .8e-10 && distance < 2.85e-10;
                    }
        check(neighbours == 12, "FCC iron does not have twelve nearest neighbours");
    }
    auto damaged = ideal;
    damaged.impact = .9;
    check(vacancyProbability(damaged, Mineral::Iron) > vacancyProbability(ideal, Mineral::Iron),
          "collision history does not create retained defects");
    damaged.coolingRate = 1;
    auto annealed = damaged;
    annealed.coolingRate = 0;
    check(vacancyProbability(annealed, Mineral::Iron) < vacancyProbability(damaged, Mineral::Iron),
          "slow cooling does not anneal collision damage");
    for (unsigned i = 0; i < 1000; ++i) {
        auto pure = ideal;
        pure.chemistry = {1, 0, 0, 0};
        auto chosen = selectMineral(i, pure);
        check(chosen == Mineral::Quartz || chosen == Mineral::Olivine,
              "invented mineral outside chemical reservoir");
        pure.chemistry = {0, 0, 0, 1};
        pure.temperature = 120;
        check(selectMineral(i, pure) == Mineral::Ice, "pure cold ice lost phase identity");
    }
    for (auto chemistry : {Composition{0, 1, 0, 0}, Composition{0, 0, 0, 1}, Composition{1, 0, 0, 0},
                           Composition{0, 0, 1, 0}}) {
        auto liquid = ideal;
        liquid.chemistry = chemistry;
        liquid.temperature = 4500;
        bool rejected = false;
        try {
            (void)selectMineral(0, liquid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "phase selection produced an unsupported solid above its temperature window");
    }
}
static void inheritanceTests(const Universe& u) {
    std::vector<Address> specimens;
    for (uint64_t i = 0; i < 12; ++i) {
        auto a = Address{}.child(Kind::Galaxy, i).child(Kind::System, i * 11).child(Kind::Planet, i % 5);
        a = u.children(u.describe(a), 3).back().child(Kind::Mountain, i).child(Kind::Rock, i * 3);
        auto rock = u.describe(a);
        auto grain = u.describe(a.child(Kind::Grain, i));
        auto lattice = u.describe(grain.address.child(Kind::Lattice, 0));
        check(rock.geology->seed == grain.geology->seed && grain.geology->seed == lattice.geology->seed,
              "planetary provenance changed during material refinement");
        check(rock.environment.chemistry.silicate == grain.environment.chemistry.silicate &&
                  grain.environment.chemistry.ice == lattice.environment.chemistry.ice &&
                  grain.environment.mineral == lattice.environment.mineral,
              "refinement replaced inherited chemistry");
        check(rock.rock.type == grain.rock.type && lattice.crystal.axes == grain.crystal.axes &&
                  lattice.crystal.boundaryAngle == grain.crystal.boundaryAngle,
              "material fabric or crystal orientation history was not inherited");
        check(std::abs(u.rockSdf(rock, grain.frame.position)) < 1e-10 &&
                  u.grainSdf(grain, lattice.frame.position) < -.08,
              "material specimen left its host");
        specimens.push_back(lattice.address);
    }
    std::vector<uint64_t> serial(specimens.size()), parallel(specimens.size());
    for (size_t i = 0; i < specimens.size(); ++i)
        serial[i] = geometryFingerprint(*GeometryGenerator(u).generate(u.describe(specimens[i]), 0));
    oneapi::tbb::parallel_for(size_t(0), specimens.size(), [&](size_t j) {
        size_t i = specimens.size() - j - 1;
        Universe fresh(u.seed());
        parallel[i] =
            geometryFingerprint(*GeometryGenerator(fresh).generate(fresh.describe(specimens[i]), 0));
    });
    check(serial == parallel, "new process recipes depend on thread scheduling or generation order");
    // New descriptors must invalidate procedural identity proofs when changed.
    auto node = u.describe(specimens[0]);
    auto hash = descriptorFingerprint(node);
    node.rock.coolingRate += .01;
    check(descriptorFingerprint(node) != hash, "process parameters excluded from descriptor proof");
}
int main() {
    try {
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        Universe u;
        webTests();
        galaxyTests(u);
        geologyTests();
        rockTests();
        latticeTests();
        inheritanceTests(u);
        std::cout << "PASS: " << checks
                  << " process checks; web, populations, tectonics, lithologies, habits, "
                     "stoichiometry, coordination, phase, defects, inheritance, parallel regeneration.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
