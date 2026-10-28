#include "infinite/Piloting.hpp"
#include "infinite/SurfaceLife.hpp"
#include <bit>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace infinite;
namespace {
size_t checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
glm::dvec3 direction(unsigned i) {
    return glm::normalize(glm::dvec3(std::sin(i * 1.7), .35 + std::sin(i * .37), std::cos(i * 2.1)));
}
void place(Navigation& nav, const Node& planet, glm::dvec3 eyeMetres, glm::dquat q, double log = 2) {
    const auto offset = eyeMetres / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
    std::ostringstream state;
    state.precision(17);
    state << planet.address.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
          << offset.x << ' ' << offset.y << ' ' << offset.z;
    check(nav.restore(state.str()), "Surface flight fixture failed to restore");
}
uint64_t digest(const SurfaceWorld& world) {
    uint64_t h = world.seed;
    for (const auto& s : world.settlements) {
        h = mix64(h ^ s.seed ^ s.population ^ s.buildings.size());
        for (const auto& b : s.buildings) {
            h = mix64(h ^ b.id ^ uint64_t(b.kind) ^ b.inhabitants);
            for (double p :
                 {b.position.x, b.position.y, b.position.z, b.dimensions.x, b.dimensions.y, b.dimensions.z})
                h = mix64(h ^ std::bit_cast<uint64_t>(p));
        }
        for (const auto& r : s.roads)
            for (double p : {r.a.x, r.a.y, r.a.z, r.b.x, r.b.y, r.b.z})
                h = mix64(h ^ std::bit_cast<uint64_t>(p));
    }
    return h;
}
} // namespace
int main() {
    try {
        Universe u;
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 0).child(Kind::Planet, 0);
        const auto base = u.describe(home);
        GeometryGenerator generator(u);
        std::map<Address, std::pair<uint64_t, uint64_t>> preserved;
        auto address = home;
        while (true) {
            const auto node = u.describe(address);
            preserved.emplace(address, std::pair{descriptorFingerprint(node),
                                                 geometryFingerprint(*generator.generate(node, 0))});
            if (address.kind() == Kind::Lattice)
                break;
            address = u.children(node, 1).front();
        }
        std::set<WeatherType> weatherTypes;
        std::map<SettlementKind, std::shared_ptr<SurfaceWorld>> societies;
        std::optional<Node> inhabitedFixture;
        for (unsigned regime = 0; regime < 4; regime++) {
            auto planet = base;
            planet.frame.logRadius = 6.65;
            planet.environment.temperature = regime == 0 ? 285 : regime == 1 ? 230 : regime == 2 ? 130 : 110;
            planet.environment.waterActivity = regime == 0 ? 1 : regime == 1 ? .25 : .03;
            planet.environment.ageGyr = 4;
            planet.environment.mineral = Mineral::Quartz;
            planet.environment.chemistry = {.65, regime == 3 ? .1 : .3, .03, .02};
            for (unsigned attempt = 0; attempt < 24; attempt++) {
                planet.seed = mix64(base.seed ^ mix64(attempt + regime * 100));
                planet.geology = planetProcess(planet.seed, planet.frame.logRadius, planet.environment, true);
                const auto climate = planetClimate(planet);
                for (unsigned i = 0; i < 100; i++) {
                    const auto d = direction(i);
                    const double time = i * climate.daySeconds / 17;
                    const auto w = sampleWeather(climate, d, 0, time),
                               repeat = sampleWeather(climate, d, 0, time);
                    weatherTypes.insert(w.type);
                    check(w.cloud == repeat.cloud && w.wind == repeat.wind,
                          "Weather lost deterministic replay");
                    check(std::isfinite(w.temperatureK + w.pressureBar + w.cloud + w.precipitation +
                                        glm::length(w.wind)),
                          "Non-finite weather");
                    check(w.cloud >= 0 && w.cloud <= 1 && w.storm >= 0 && w.storm <= 1,
                          "Unbounded weather strength");
                    check(std::abs(glm::dot(w.wind, d)) < 1e-10, "Wind left the planetary tangent plane");
                    const auto neighbour =
                        sampleWeather(climate, glm::normalize(d + glm::dvec3(1e-9)), 0, time);
                    check(std::abs(neighbour.cloud - w.cloud) < 1e-5, "Weather field has a spatial seam");
                    const auto next = sampleWeather(climate, d, 0, time + .001);
                    check(std::abs(next.cloud - w.cloud) < .001, "Weather jumps between frames");
                }
                const auto world = generateSurfaceLife(u, planet);
                if (world->settlements.empty())
                    continue;
                if (!inhabitedFixture)
                    inhabitedFixture = planet;
                check(world->residentBytes() < 4 * 1024 * 1024,
                      "Single-world society exceeded the cache budget");
                check(digest(*world) == digest(*generateSurfaceLife(u, planet)),
                      "Surface generation depends on request order");
                for (const auto& s : world->settlements) {
                    societies.try_emplace(s.kind, world);
                    check(s.sealed != climate.temperate, "City protection violates inherited climate");
                    check(glm::dot(s.direction, u.terrainNormal(planet, s.direction)) > .994,
                          "Surface port is not landable");
                    check(std::abs(glm::length(s.landingPoint) - world->radiusMetres -
                                   u.terrainHeight(planet, s.direction)) < 1e-6,
                          "Port changed the terrain height");
                    check(planet.geology->liquidWater < .08 || u.terrainHeight(planet, s.direction) > 2,
                          "Settlement is underwater");
                    check(!surfaceCollision(*world, s.landingPoint + s.direction * 60.,
                                            s.landingPoint + s.direction * 2., 2),
                          "Landing pad is obstructed by a building");
                    unsigned population = 0;
                    std::set<uint64_t> buildings;
                    for (const auto& b : s.buildings) {
                        check(buildings.insert(b.id).second, "Duplicate parcel identity");
                        population += b.inhabitants;
                        const auto below = b.position - b.orientation * glm::dvec3(0, b.foundationMetres, 0);
                        check(glm::length(below) - world->radiusMetres <
                                  u.terrainHeight(planet, glm::normalize(below)) + 1e-6,
                              "Floating foundation");
                        const auto from = b.position + b.orientation * glm::dvec3(0, b.dimensions.y * .5,
                                                                                  -b.dimensions.z - 10);
                        const auto to = b.position + b.orientation * glm::dvec3(0, b.dimensions.y * .5,
                                                                                b.dimensions.z + 10);
                        check(surfaceCollision(*world, from, to, 2).has_value(),
                              "Swept ship tunneled through a building");
                    }
                    check(population == s.population && population > 0,
                          "Inhabitants do not belong to actual buildings");
                    for (const auto& road : s.roads)
                        check(std::abs(glm::length(road.a) - world->radiusMetres -
                                       u.terrainHeight(planet, glm::normalize(road.a)) - .22) < 1e-6,
                              "Road detached from terrain");
                    bool moving = false;
                    for (unsigned id = 0; id < std::min(24u, s.population); id++) {
                        const auto person = sampleResident(*world, s, id, 12),
                                   repeat = sampleResident(*world, s, id, 12);
                        const auto later = sampleResident(*world, s, id, 22);
                        check(person.id == repeat.id && person.position == repeat.position &&
                                  later.id == person.id,
                              "Resident identity depends on time/camera");
                        check(person.suited == s.sealed, "Unprotected inhabitant on a sealed colony");
                        check(buildings.contains(person.homeBuilding) &&
                                  person.homeBuilding == later.homeBuilding,
                              "Resident has no stable home building");
                        check(std::abs(glm::length(person.position) - world->radiusMetres -
                                       u.terrainHeight(planet, glm::normalize(person.position))) < .7,
                              "Resident left the ground");
                        moving |= glm::length(later.position - person.position) > 1;
                    }
                    check(moving, "Residents never move");
                }
                std::cout << "SOCIETY regime=" << regime << " temperate=" << climate.temperate
                          << " sites=" << world->settlements.size() << " bytes=" << world->residentBytes()
                          << '\n';
                break;
            }
        }
        check(societies.size() == 4, "Missing city, colony, mining or research settlement process");
        check(weatherTypes.contains(WeatherType::Rain) && weatherTypes.contains(WeatherType::Snow) &&
                  weatherTypes.contains(WeatherType::Cloud),
              "Climate does not produce distinct precipitation regimes");
        auto desert = planetClimate(base);
        desert.solid = true;
        desert.pressureBar = .8;
        desert.moisture = 0;
        desert.temperatureK = 300;
        desert.windMetresPerSecond = 28;
        desert.scaleHeightMetres = 8000;
        bool dust = false;
        for (unsigned i = 0; i < 500; i++)
            dust |= sampleWeather(desert, direction(i), 0, i * 300.).type == WeatherType::Dust;
        check(dust, "Dry atmospheric circulation never produces dust weather");
        const auto upper = sampleWeather(desert, {0, 1, 0}, 1e6, 12);
        auto scalding = desert;
        scalding.temperatureK = 600;
        scalding.pressureBar = .1;
        scalding.moisture = 1;
        for (unsigned i = 0; i < 100; i++)
            check(sampleWeather(scalding, direction(i), 0, 12).precipitation == 0,
                  "Liquid-water weather on a scalding low-pressure world");
        check(upper.precipitation == 0 && upper.wind == glm::dvec3(0), "Weather extends into orbital vacuum");
        auto gas = base;
        auto envelope = std::make_shared<PlanetProcess>(*gas.geology);
        envelope->type = GeologyType::GasEnvelope;
        gas.geology = envelope;
        check(generateSurfaceLife(u, gas)->settlements.empty(), "Gaseous body acquired surface residents");
        check(sampleWeather(planetClimate(gas), {0, 1, 0}, 0, 12).type == WeatherType::Vacuum,
              "Gaseous body exposed a landable weather layer");
        auto hot = base;
        hot.environment.temperature = 1400;
        check(generateSurfaceLife(u, hot)->settlements.empty(), "Molten world acquired a surface city");
        auto airless = base;
        airless.frame.logRadius = 4;
        const auto vacuum = sampleWeather(planetClimate(airless), {0, 1, 0}, 0, 12);
        check(vacuum.pressureBar == 0 && vacuum.wind == glm::dvec3(0) && vacuum.precipitation == 0,
              "Airless world has rain or wind");
        size_t meshBytes = 0;
        for (unsigned kind = 0; kind < unsigned(SurfaceMesh::Count); kind++)
            for (unsigned detail = 0; detail < 3; detail++) {
                const auto mesh = surfaceMesh(SurfaceMesh(kind), detail);
                meshBytes += mesh.bytes();
                check(!mesh.vertices.empty() && mesh.indexCount() % 3 == 0, "Missing surface template");
                for (size_t i = 0; i < mesh.indexCount(); i++)
                    check(mesh.index(i) < mesh.vertices.size(), "Bad surface mesh index");
            }
        check(meshBytes < 2 * 1024 * 1024, "Surface template memory grew beyond its bound");
        SurfaceLifeCache bounded(u, 1024);
        for (unsigned i = 0; i < 8; i++) {
            bounded.beginFrame();
            auto p = hot;
            p.address = home.parent().child(Kind::Planet, i);
            bounded.request(p);
            bounded.finishFrame();
            bounded.wait();
            check(bounded.stats().bytes <= 1024 && bounded.stats().pending == 0,
                  "Surface cache exceeded its budget");
        }
        check(bounded.stats().evicted > 0, "Surface cache never evicts");
        SurfaceLifeCache stale(u);
        stale.beginFrame();
        stale.request(base);
        for (unsigned i = 0; i < 12; i++) {
            stale.beginFrame();
            stale.finishFrame();
        }
        stale.wait();
        check(stale.stats().cancelled > 0 || stale.stats().completed > 0,
              "Surface task did not complete or cancel");
        std::atomic<bool> cancelled{true};
        check(inhabitedFixture.has_value(), "Missing inhabited cancellation fixture");
        const auto stopped = generateSurfaceLife(u, *inhabitedFixture, &cancelled);
        check(!stopped || stopped->settlements.empty(), "Cancelled surface generation built settlements");
        std::optional<Node> actual;
        std::shared_ptr<SurfaceWorld> actualWorld;
        for (unsigned star = 0; star < 12 && !actual; star++) {
            const auto system = u.describe(home.parent().parent().child(Kind::System, star));
            for (const auto& a : u.children(system)) {
                if (isMoon(a))
                    continue;
                const auto p = u.describe(a);
                const auto world = generateSurfaceLife(u, p);
                if (!world->settlements.empty()) {
                    actual = p;
                    actualWorld = world;
                    break;
                }
            }
        }
        check(actual.has_value(), "No real catalogue planet has inhabited land");
        const auto& settlement = actualWorld->settlements.front();
        ActivityState saved;
        saved.seconds = 120;
        saved.selected = ActivityId{actual->address, surfaceActivitySlot + settlement.slot};
        ActivityState restored;
        check(restored.restore(saved.serialize(), u) && restored.serialize() == saved.serialize(),
              "Surface destination lost on save/restart");
        Navigation nav(u);
        RegionCache cache(u);
        FlightController flight(u, nav, cache);
        Pilot pilot(u, nav, flight);
        const auto q = tangentFrame(settlement.direction);
        place(nav, *actual, settlement.landingPoint + settlement.direction * 500., q);
        nav.changeFrame(FlightController::surfaceAt(actual->address, settlement.direction));
        pilot.resetMotion();
        pilot.setSurfaceWorld(actualWorld);
        const FlightWaypoint approach{actual->address,
                                      settlement.landingPoint + settlement.direction * 60.,
                                      {},
                                      0,
                                      settlement.name,
                                      true};
        pilot.navigateWaypoint(approach);
        for (unsigned i = 0; i < 6000; i++) {
            cache.beginFrame();
            pilot.trackWaypoint(approach);
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        check(pilot.instruments().approachError < 8 && pilot.instruments().speed < 3,
              "Surface approach did not reach its real landing concourse");
        check(pilot.landAt(actual->address, settlement.direction), "Selected surface port refuses landing");
        for (unsigned i = 0; i < 5000 && !pilot.ship().landed; i++) {
            cache.beginFrame();
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        check(pilot.ship().landed && pilot.ship().hull == 100, "Surface port landing failed");
        pilot.launch();
        check(!pilot.ship().landed, "Cannot launch from a colony");
        const auto& building = settlement.buildings.front();
        const auto forward = building.orientation * glm::dvec3(0, 0, 1);
        const auto up = building.orientation * glm::dvec3(0, 1, 0);
        const auto from =
            building.position +
            building.orientation * glm::dvec3(0, building.dimensions.y * .5, -building.dimensions.z * .5 - 8);
        place(nav, *actual, from, glm::quatLookAtRH(forward, up));
        pilot.resetMotion();
        pilot.toggleAssist();
        pilot.setVelocity(forward * 80.);
        bool contact = false;
        for (unsigned i = 0; i < 40 && !contact; i++) {
            cache.beginFrame();
            pilot.update({}, 1. / 60);
            cache.finishFrame();
            contact = pilot.instruments().phase == "STRUCTURE CONTACT / BRAKED";
        }
        check(contact && pilot.instruments().speed < 1,
              "Pilot did not stop at the generated building collider");
        for (const auto& [a, hashes] : preserved) {
            const auto p = u.describe(a);
            check(descriptorFingerprint(p) == hashes.first &&
                      geometryFingerprint(*generator.generate(p, 0)) == hashes.second,
                  "Weather or habitation changed the existing universe-to-atom hierarchy");
        }
        std::cout << "ACTUAL_SURFACE " << actual->address.str() << " " << settlement.name
                  << " population=" << settlement.population << '\n';
        std::cout << "PASS: " << checks
                  << " climate, solid-only habitation, deterministic residents, foundations, collision, "
                     "async budgets, saved destination, landing and atomic-preservation checks / templates="
                  << meshBytes << " bytes\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
