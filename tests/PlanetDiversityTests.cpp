#include "infinite/Planetology.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace infinite;
static unsigned checks = 0;
static void check(bool v, const char* text) {
    ++checks;
    if (!v)
        throw std::runtime_error(text);
}
int main() {
    try {
        std::map<PlanetType, unsigned> counts;
        unsigned rings = 0, hot = 0;
        for (uint64_t seed : {0x494e46494e495445ULL, 17ULL, 937ULL}) {
            Universe u(seed), replay(seed);
            for (uint64_t star = 0; star < 32; star++) {
                const auto s = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
                const auto old = u.establishedPlanetCount(s);
                check(u.planetCount(s) == old + giantPopulationCount,
                      "Giant catalogue replaced established worlds");
                std::vector<Node> primaries;
                for (unsigned i = 0; i < u.planetCount(s); i++) {
                    auto p = u.describe(s.address.child(Kind::Planet, i));
                    auto t = planetTraits(p);
                    counts[t.type]++;
                    check(p.seed == childSeed(s.seed, Kind::Planet, i), "Changed hierarchical seed");
                    check(glm::length(t.axis) > .999 && glm::length(t.axis) < 1.001, "Invalid planet axis");
                    check(descriptorFingerprint(p) == descriptorFingerprint(replay.describe(p.address)),
                          "Order-dependent planet descriptor");
                    if (i < old) {
                        check(hasSolidSurface(p) && p.bulkDensity == 0,
                              "Established world became a gas envelope");
                        continue;
                    }
                    for (unsigned sample = 0; sample < 32; sample++) {
                        const double y = 1 - 2 * (sample + .5) / 32., angle = sample * 2.399963229728653;
                        const glm::dvec3 d(std::sqrt(1 - y * y) * std::cos(angle), y,
                                           std::sqrt(1 - y * y) * std::sin(angle));
                        const auto colour = giantAlbedo(t, d),
                                   near = giantAlbedo(t, glm::normalize(d + glm::dvec3(1e-9)));
                        check(glm::all(glm::greaterThanEqual(colour, glm::vec3(0))) &&
                                  glm::all(glm::lessThanEqual(colour, glm::vec3(1))) &&
                                  glm::length(colour - near) < 1e-4,
                              "Giant cloud field has a seam or invalid reflectance");
                    }
                    primaries.push_back(p);
                    check(!hasSolidSurface(p) && p.bulkDensity > 500 && p.bulkDensity < 2600,
                          "Giant has terrestrial geology or density");
                    check(u.children(p).empty(), "Giant exposed a landable surface address");
                    check(generateSurfaceLife(u, p)->settlements.empty(),
                          "Cities were placed on a gas giant");
                    const double radius = std::pow(10., p.frame.logRadius),
                                 mass = 4.188790204786391 * p.bulkDensity * std::pow(radius, 3);
                    check(std::abs(p.massSolar * 1.98847e30 / mass - 1) < 1e-12,
                          "Giant gravity uses condensed-solid density");
                    const auto debris = sampleActivity(p, 1000, 12);
                    const double energy =
                        glm::dot(debris.velocity, debris.velocity) * glm::length(debris.position);
                    check(std::abs(energy / (6.67430e-11 * mass) - 1) < 1e-10,
                          "Orbital activity does not use the giant's bulk mass");
                    const double expected =
                        std::max(2.725, 278. * std::pow(s.luminositySolar, .25) /
                                            std::sqrt(p.orbit.semiMajorMetres / 1.495978707e11));
                    check(p.environment.temperature >= expected &&
                              p.environment.temperature <=
                                  std::pow(std::pow(expected, 4) + std::pow(150., 4), .25) + 1e-9,
                          "Temperature is unrelated to host irradiation");
                    if (p.environment.temperature > 900)
                        hot++;
                    for (unsigned j = 0; j < u.planetCount(s); j++)
                        if (i != j) {
                            const auto other = u.describe(s.address.child(Kind::Planet, j));
                            const double m2 = 4.188790204786391 * planetDensity(other) *
                                              std::pow(10., 3 * other.frame.logRadius);
                            const double hill = std::cbrt((mass + m2) / (3 * s.massSolar * 1.98847e30)) *
                                                (p.orbit.semiMajorMetres + other.orbit.semiMajorMetres) * .5;
                            const auto& left =
                                p.orbit.semiMajorMetres < other.orbit.semiMajorMetres ? p : other;
                            const auto& right =
                                p.orbit.semiMajorMetres < other.orbit.semiMajorMetres ? other : p;
                            check(right.orbit.semiMajorMetres * (1 - right.orbit.eccentricity) -
                                          left.orbit.semiMajorMetres * (1 + left.orbit.eccentricity) >
                                      4.5 * hill,
                                  "Giant crosses an established orbital stability envelope");
                        }
                    if (t.rings.present()) {
                        rings++;
                        const auto& r = t.rings;
                        const double particleDensity = glm::mix(2900., 917., r.iceFraction),
                                     roche = 2.456 * std::cbrt(p.bulkDensity / particleDensity);
                        check(r.innerRadius > 1.1 && r.outerRadius < roche,
                              "Ring material escaped its Roche confinement");
                        check(p.environment.temperature < 210 || r.iceFraction == 0,
                              "Icy rings survived above their sublimation range");
                        check(ringOpticalDepth(r, r.innerRadius - .1) == 0 &&
                                  ringOpticalDepth(r, r.outerRadius + .1) == 0,
                              "Ring has a hard outer rectangle");
                        double minimum = 1, maximum = 0;
                        for (unsigned k = 0; k < 1000; k++) {
                            const double q =
                                ringOpticalDepth(r, glm::mix(r.innerRadius, r.outerRadius, (k + .5) / 1000.));
                            minimum = std::min(minimum, q);
                            maximum = std::max(maximum, q);
                            check(q >= 0 && q < r.opticalDepth, "Unbounded ring optical density");
                        }
                        check(maximum > minimum * 5 && maximum > .01, "Ring lacks bands and resonance gaps");
                    }
                }
            }
        }
        check(counts[PlanetType::GasGiant] > 50 && counts[PlanetType::IceGiant] > 50 &&
                  counts[PlanetType::SubNeptune] > 50,
              "Missing giant families");
        check(counts[PlanetType::IceWorld] > 10 && counts[PlanetType::Rocky] > 5 &&
                  counts[PlanetType::LavaWorld] > 0,
              "Missing solid-world variety");
        check(rings > 100 && hot > 10, "Rings or hot giants are missing");
        std::cout << "PASS: " << checks
                  << " planet diversity, irradiation, envelope mass, orbital stability, ring and "
                     "preservation checks / hot="
                  << hot << " rings=" << rings << '\n';
        for (auto [type, count] : counts)
            std::cout << planetTypeName(type) << '=' << count << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
