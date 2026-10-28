#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>

using namespace infinite;
namespace {
size_t checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void plan(const SurfaceSettlement& s, const std::filesystem::path& file) {
    std::ofstream out(file);
    const double r = s.radius;
    out << "<svg xmlns='http://www.w3.org/2000/svg' width='960' height='1000' viewBox='" << -r << ' '
        << -r - 80 << ' ' << 2 * r << ' ' << 2 * r + 80 << "'>";
    out << "<rect x='" << -r << "' y='" << -r - 80 << "' width='" << 2 * r << "' height='" << 2 * r + 80
        << "' fill='#101825'/>";
    out << "<text x='" << -r + 30 << "' y='" << -r - 25
        << "' fill='#d5e8ed' font-family='sans-serif' font-size='35'>" << cityPatternName(s.pattern) << " / "
        << s.buildings.size() << " buildings</text>";
    const auto local = [&](glm::dvec3 p) { return glm::inverse(s.orientation) * (p - s.landingPoint); };
    for (const auto& road : s.roads) {
        const auto a = local(road.a), b = local(road.b);
        out << "<path d='M " << a.x << ' ' << a.z << " L " << b.x << ' ' << b.z << "' stroke='"
            << (road.kind == StreetClass::Avenue ? "#e3b778" : "#697b8f") << "' stroke-width='" << road.width
            << "' fill='none'/>";
    }
    const char* colors[] = {"#7abdb6", "#a790d3", "#e1bb7f", "#c87a74", "#89bd73", "#adb9c6", "#538d65"};
    for (const auto& b : s.buildings) {
        const auto parcel =
            std::find_if(s.parcels.begin(), s.parcels.end(), [&](const auto& p) { return p.id == b.parcel; });
        if (parcel == s.parcels.end())
            continue;
        out << "<polygon points='";
        for (auto p : parcel->corners)
            out << p.x << ',' << p.y << ' ';
        out << "' fill='" << colors[unsigned(b.use)] << "' stroke='#dce6e9' stroke-width='1'/>";
    }
    out << "<rect x='-32' y='-32' width='64' height='64' fill='#72c4dc'/><text x='0' y='10' "
           "text-anchor='middle' fill='#101825' font-family='sans-serif' font-size='30'>H</text></svg>";
}
} // namespace
int main(int argc, char** argv) {
    try {
        Universe u;
        const auto address = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 137).child(Kind::Planet, 0);
        const auto planet = u.describe(address);
        const auto before = descriptorFingerprint(planet);
        const auto world = generateSurfaceLife(u, planet);
        check(world && !world->settlements.empty(), "City fixture has no existing city");
        check(world->residentBytes() < 4 * 1024 * 1024,
              "Urban descriptions exceed the unchanged cache budget");
        std::set<uint64_t> fingerprints;
        unsigned architecturalTypes = 0;
        for (unsigned pattern = 0; pattern < 4; pattern++) {
            auto city = world->settlements.front();
            const auto seed = city.seed;
            const auto port = city.landingPoint;
            const auto orientation = city.orientation;
            check(refineCity(u, planet, city, nullptr, CityPattern(pattern)),
                  "City pattern did not generate");
            check(city.seed == seed && city.landingPoint == port && city.orientation == orientation,
                  "City growth moved its port or changed its seed");
            check(city.buildings.size() > 30 && city.layoutRecipe == cityRecipeVersion,
                  "City is still a sparse prop grid");
            check(city.junctions.size() > 20 && city.streets.size() > 25,
                  "Missing explicit city street topology");
            check(fingerprints.insert(cityFingerprint(city)).second,
                  "Urban morphologies are indistinguishable");
            auto replay = world->settlements.front();
            check(refineCity(u, planet, replay, nullptr, CityPattern(pattern)) &&
                      cityFingerprint(replay) == cityFingerprint(city),
                  "City generation depends on mutable history or query order");
            std::vector<std::vector<unsigned>> adjacent(city.junctions.size());
            std::set<uint64_t> edgeIds;
            unsigned avenues = 0, locals = 0;
            for (unsigned edge = 0; edge < city.streets.size(); edge++) {
                const auto& e = city.streets[edge];
                check(e.a < adjacent.size() && e.b < adjacent.size() && e.a != e.b,
                      "Invalid street graph edge");
                check(edgeIds.insert(e.id).second, "Duplicate logical street identity");
                check(e.roadCount > 0 && e.roadBegin + e.roadCount <= city.roads.size(),
                      "Street lost its physical geometry");
                adjacent[e.a].push_back(e.b);
                adjacent[e.b].push_back(e.a);
                avenues += e.kind == StreetClass::Avenue;
                locals += e.kind == StreetClass::Local;
                for (unsigned i = e.roadBegin; i < e.roadBegin + e.roadCount; i++) {
                    const auto& road = city.roads[i];
                    check(road.street == edge, "Road refers to a different logical street");
                    const double grade =
                        std::abs(glm::length(road.a) - glm::length(road.b)) / glm::length(road.b - road.a);
                    check(grade <= .180001, "Road exceeds the construction grade limit");
                    for (const auto& offset : road.surface) {
                        const auto p = (road.a + road.b) * .5 + glm::dvec3(offset);
                        const double h =
                            glm::length(p) - world->radiusMetres - u.terrainHeight(planet, glm::normalize(p));
                        check(std::abs(h - .12) < .002,
                              "Pavement vertex does not meet the actual terrain field");
                    }
                }
            }
            check(avenues > 0 && locals > 0, "City has no street hierarchy");
            std::vector<bool> reachable(adjacent.size());
            std::queue<unsigned> queue;
            queue.push(0);
            reachable[0] = true;
            while (!queue.empty()) {
                const auto a = queue.front();
                queue.pop();
                for (auto b : adjacent[a])
                    if (!reachable[b]) {
                        reachable[b] = true;
                        queue.push(b);
                    }
            }
            check(std::all_of(reachable.begin(), reachable.end(), [](bool v) { return v; }),
                  "Inaccessible road component survived city growth");
            if (CityPattern(pattern) == CityPattern::Organic) {
                unsigned junctions = 0;
                for (size_t i = 0; i < adjacent.size(); i++)
                    junctions +=
                        adjacent[i].size() == 3 && glm::length(city.junctions[i].local) < city.radius * .6;
                check(junctions >= 8, "Organic growth did not produce interior T junctions");
            }
            std::set<BuildingUse> uses;
            std::set<SurfaceMesh> kinds;
            std::set<uint64_t> ids;
            unsigned population = 0;
            for (const auto& b : city.buildings) {
                check(b.frontage < city.streets.size(), "Building has no actual street frontage");
                check(reachable[city.streets[b.frontage].a], "Building lacks a connected route to its port");
                check(ids.insert(b.id).second, "City reused a building identity");
                check(b.pathCount >= 2 && b.pathBegin + b.pathCount <= city.pedestrianPoints.size(),
                      "Home-associated street path is missing");
                check(
                    std::any_of(city.parcels.begin(), city.parcels.end(),
                                [&](const auto& p) { return p.id == b.parcel && p.frontage == b.frontage; }),
                    "Building was placed independently of its parcel");
                uses.insert(b.use);
                kinds.insert(b.kind);
                population += b.inhabitants;
            }
            check(uses.size() >= 3 && kinds.size() >= 4,
                  "Missing functional districts or architectural massing variety");
            check(population == city.population, "City population is unrelated to its housing");
            architecturalTypes = std::max(architecturalTypes, unsigned(kinds.size()));
            SurfaceWorld one = *world;
            one.settlements = {city};
            check(!surfaceCollision(one, port + city.direction * 80., port + city.direction * 2., 2),
                  "City growth obstructed its existing landing approach");
            for (unsigned sample = 0; sample < 32; sample++) {
                const auto person = sampleResident(
                    one, city, unsigned(uint64_t(sample) * city.population / 32), 12 + sample * 7.);
                const double error = std::abs(glm::length(person.position) - one.radiusMetres -
                                              u.terrainHeight(planet, glm::normalize(person.position)));
                check(error < .7, "Urban pedestrian route detached from the real terrain");
                check(!surfaceCollision(one, person.position + glm::normalize(person.position) * .9,
                                        person.position + glm::normalize(person.position) * .9, .3),
                      "Pedestrian route crosses a building");
            }
            std::cout << cityPatternName(city.pattern) << " buildings=" << city.buildings.size()
                      << " streets=" << city.streets.size() << " parcels=" << city.parcels.size()
                      << " population=" << city.population << " routes=" << city.pedestrianPoints.size()
                      << '\n';
            if (argc == 3 && std::string(argv[1]) == "--svg") {
                std::filesystem::create_directories(argv[2]);
                plan(city,
                     std::filesystem::path(argv[2]) / (std::string(cityPatternName(city.pattern)) + ".svg"));
            }
        }
        check(descriptorFingerprint(u.describe(address)) == before,
              "Urban growth modified the underlying planet");
        std::cout << "PASS: " << checks
                  << " urban topology, frontage, hierarchy, mixed uses, deterministic morphology, ground "
                     "routes, clearance and preservation checks; architectural families="
                  << architecturalTypes << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
