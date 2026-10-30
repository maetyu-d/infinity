#include "infinite/Navigation.hpp"
#include "infinite/Piloting.hpp"
#include "infinite/Presentation.hpp"
#include "infinite/StellarEvolution.hpp"
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace infinite;
static unsigned checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::map<StellarKind, unsigned> counts;
        std::set<std::string> spectra, brown;
        for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
            Universe u(seed), replay(seed);
            for (unsigned galaxy = 0; galaxy < 4; galaxy++) {
                const auto g = u.describe(Address{}.child(Kind::Galaxy, galaxy));
                auto children = u.children(g);
                check(children.size() == 192 + stellarPopulationCount,
                      "New stars not discoverable as real galaxy children");
                for (unsigned i = 0; i < 192; i++)
                    check(children[i].id() == i, "Existing systems reordered");
                for (unsigned i = 0; i < stellarPopulationCount; i++) {
                    const auto n = u.describe(g.address.child(Kind::System, stellarIdFlag | i));
                    const auto& s = *n.stellar;
                    counts[s.kind]++;
                    check(n.address == children[192 + i] &&
                              n.seed == childSeed(g.seed, Kind::System, stellarIdFlag | i),
                          "Stellar identity replaced");
                    check(Address::parse(n.address.str()) == n.address,
                          "High-ID star address did not round trip");
                    check(s.ageGyr <= g.environment.ageGyr + 1e-9 && s.ageGyr > 0,
                          "Star older than parent galaxy");
                    if (n.massSolar > 8)
                        check(n.luminositySolar <= 60000 * n.massSolar * (1 + 1e-12),
                              "Massive star exceeds its electron-scattering luminosity envelope");
                    const double rotationSpeed = 6.283185307179586 * s.radiusMetres / s.rotationSeconds;
                    check(rotationSpeed * rotationSpeed * s.radiusMetres / (1.32712440018e20 * s.massSolar) <=
                              .250000000001,
                          "Stellar envelope rotates above the centrifugal breakup bound");
                    check(n.massSolar > 0 && n.stellarRadiusMetres > 0 && std::isfinite(n.luminositySolar),
                          "Invalid star dimensions");
                    check(
                        std::abs(n.luminositySolar / (std::pow(n.stellarRadiusMetres / solarRadiusMetres, 2) *
                                                      std::pow(n.temperatureK / 5772, 4)) -
                                 1) < 1e-12,
                        "Luminosity violates Stefan-Boltzmann relation");
                    const auto sample = u.starSample(g, n.address.id());
                    check(glm::length(sample.position - n.frame.position) < 1e-15 &&
                              sample.mass == n.massSolar &&
                              glm::length(sample.color - stellarRadiance(n)) == 0,
                          "Galaxy sample is a different star");
                    check(descriptorFingerprint(n) == descriptorFingerprint(replay.describe(n.address)),
                          "Order-dependent stellar process");
                    if (s.kind == StellarKind::MainSequence) {
                        check(s.ageGyr < s.mainSequenceGyr, "Dead main-sequence star");
                        spectra.insert(spectralClass(n.temperatureK));
                    }
                    if (s.kind == StellarKind::BrownDwarf) {
                        check(s.massSolar < .075 && s.temperatureK < 2500 && s.coolingAgeGyr == s.ageGyr,
                              "Hydrogen-fusing brown dwarf");
                        brown.insert(spectralClass(n.temperatureK, true));
                    }
                    if (s.kind == StellarKind::WhiteDwarf) {
                        check(s.massSolar < 1.44 && s.radiusMetres > 2e6 && s.radiusMetres < 2e7 &&
                                  s.ageGyr > s.mainSequenceGyr,
                              "Invalid white dwarf progenitor or radius");
                    }
                    if (s.kind == StellarKind::NeutronStar) {
                        check(s.radiusMetres >= 11000 && s.radiusMetres <= 14000 && s.initialMassSolar >= 8 &&
                                  s.ageGyr > s.mainSequenceGyr,
                              "Invalid neutron star remnant");
                    }
                    if (s.kind == StellarKind::RedGiant || s.kind == StellarKind::CarbonStar ||
                        s.kind == StellarKind::RedSupergiant || s.kind == StellarKind::BlueSupergiant ||
                        s.kind == StellarKind::WolfRayet)
                        check(s.ageGyr > s.mainSequenceGyr, "Evolved star has not left main sequence");
                    if (!s.maturePlanets) {
                        check(u.planetCount(n) == 0 && u.children(n).empty() &&
                                  std::isfinite(u.systemRadiusMetres(n)),
                              "Young/remnant host has generic mature planets");
                        continue;
                    }
                    // Representative planets exercise the inherited irradiation, survival bound,
                    // brown-dwarf mass budget, and unchanged full refinement hierarchy.
                    if (i % 20 == 15 || i % 20 == 17 || i % 20 == 18 || i == 4) {
                        double mass = 0;
                        for (unsigned j = 0; j < u.planetCount(n); j++) {
                            const auto p = u.describe(n.address.child(Kind::Planet, j));
                            check(p.orbit.centralMassSolar == n.massSolar, "Orbit ignores stellar mass");
                            check(p.orbit.semiMajorMetres * (1 - p.orbit.eccentricity) >
                                      n.stellarRadiusMetres * 4,
                                  "Planet intersects star");
                            if (s.kind == StellarKind::WhiteDwarf)
                                check(p.orbit.semiMajorMetres * (1 - p.orbit.eccentricity) >
                                          .8 * s.survivorInnerAu * 1.495978707e11,
                                      "Planet inside remnant survival zone");
                            check(p.environment.ageGyr <= n.environment.ageGyr &&
                                      p.environment.temperature > 0,
                                  "Planet lost inherited age/heat");
                            check(descriptorFingerprint(p) ==
                                      descriptorFingerprint(replay.describe(p.address)),
                                  "Stellar descendants not deterministic");
                            mass += p.massSolar;
                        }
                        if (s.kind == StellarKind::BrownDwarf)
                            check(mass < n.massSolar * .15, "Substellar planets exceed disc mass budget");
                        auto a = n.address.child(Kind::Planet, 0);
                        while (a.kind() != Kind::Lattice) {
                            auto c = u.children(u.describe(a), 1);
                            check(!c.empty(), "New stellar system lost atomic depth");
                            a = c.front();
                        }
                        check(seedOf(a, seed) == u.describe(a).seed,
                              "Atomic descendant changed hierarchical seed");
                    }
                }
            }
        }
        check(counts.size() == 10, "Missing stellar families");
        check(spectra.size() == 7, "Missing OBAFGKM spectral class");
        for (const auto& type : brown)
            std::cout << "BROWN_SPECTRAL " << type << "\n";
        check(brown.size() == 3, "Missing LTY brown-dwarf class");
        for (auto kind : {StellarKind::WhiteDwarf, StellarKind::NeutronStar, StellarKind::BrownDwarf}) {
            const unsigned index = kind == StellarKind::WhiteDwarf    ? 15
                                   : kind == StellarKind::NeutronStar ? 16
                                                                      : 17;
            for (unsigned seed = 0; seed < 50; ++seed) {
                const auto young = stellarEvolution(seed, index, 5, true);
                const auto old = stellarEvolution(seed, index, 10, true);
                check(old.coolingAgeGyr > young.coolingAgeGyr && old.temperatureK <= young.temperatureK,
                      "Cooling branch heats up with age");
            }
        }
        {
            Universe u;
            const auto galaxy = u.describe(Address{}.child(Kind::Galaxy, 0));
            const auto a = GeometryGenerator(u).generate(galaxy, 0);
            const auto b = GeometryGenerator(u).generate(galaxy, 2);
            std::map<uint64_t, Particle> added;
            for (const auto& particle : a->particles)
                if (particle.objectId != UINT64_MAX && isDiverseStar(particle.objectId))
                    check(added.emplace(particle.objectId, particle).second,
                          "Duplicate stellar LOD identity");
            check(added.size() == stellarPopulationCount, "Galaxy LOD omitted addressed stellar population");
            for (const auto& particle : b->particles)
                if (particle.objectId != UINT64_MAX && isDiverseStar(particle.objectId)) {
                    const auto& prior = added.at(particle.objectId);
                    check(particle.position == prior.position && particle.color == prior.color &&
                              particle.radius == prior.radius,
                          "Stellar LOD replaced or moved an emitter");
                }
        }
        // Old, gas-poor populations never acquire newborn massive stars.
        for (unsigned i = 0; i < 192; i++) {
            const auto s = stellarEvolution(11 + i, i, 12, false);
            check(s.kind != StellarKind::Protostar && s.kind != StellarKind::WolfRayet &&
                      s.kind != StellarKind::RedSupergiant && s.kind != StellarKind::BlueSupergiant,
                  "Massive young star in old gas-poor population");
        }
        Universe u;
        Navigation nav(u);
        RegionCache cache(u);
        FlightController flightController(u, nav, cache);
        Pilot pilot(u, nav, flightController);
        pilot.startInSystem();
        const auto anchor = nav.anchor;
        const auto eye = nav.cameraPosition();
        const auto flight = pilot.serialize();
        pilot.selectNextStar();
        check(nav.selected && isDiverseStar(nav.selected->id()), "No stellar destination control");
        check(nav.anchor == anchor && glm::length(nav.cameraPosition() - eye) == 0,
              "Selecting a star teleported the ship");
        for (unsigned i = 0; i < stellarPopulationCount; i++)
            pilot.selectNextStar();
        check((nav.selected->id() & ~stellarIdFlag) == 0, "Stellar catalogue does not wrap");
        const auto a = nav.selected.value();
        auto worker = std::async(std::launch::async, [&] { return descriptorFingerprint(u.describe(a)); });
        check(worker.get() == descriptorFingerprint(u.describe(a)), "Parallel replay differs");
        for (auto [kind, count] : counts)
            std::cout << stellarKindName(kind) << '=' << count << '\n';
        std::cout << "PASS: " << checks
                  << " stellar evolution, spectral, population, planetary inheritance and identity checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
