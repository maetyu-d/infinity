#include "infinite/Piloting.hpp"
#include "infinite/Streaming.hpp"
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace infinite;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
static glm::dvec3 direction(unsigned i) {
    return glm::normalize(glm::dvec3(std::sin(i * 1.7), .35 + std::sin(i * .37), std::cos(i * 2.1)));
}
int main() {
    try {
        std::map<TerrainType, PlanetProcess> regimes;
        for (unsigned mode = 0; mode < 4; mode++) {
            Environment environment;
            environment.ageGyr = mode == 1 ? 1 : 4;
            environment.peakTemperature = mode == 1 ? 2200 : 1000;
            environment.waterActivity = mode == 2 ? .9 : 0;
            environment.temperature = mode == 3 ? 110 : 280;
            environment.chemistry = mode == 3 ? Composition{.2, .1, .05, .65} : Composition{};
            for (uint64_t seed = 1; seed < 50; seed++) {
                const auto planet = planetProcess(seed, 6.4, environment, true);
                if (planet->terrain.type != TerrainType::None) {
                    regimes.emplace(planet->terrain.type, *planet);
                    break;
                }
            }
        }
        check(regimes.size() == 4, "Environmental constraints did not produce four terrain regimes");
        for (const auto& [type, planet] : regimes) {
            auto base = planet;
            base.terrain = {};
            double maximumRelief = 0, maximumLocalRange = 0;
            for (unsigned i = 0; i < 160; i++) {
                const auto d = direction(i), tangent = tangentFrame(d) * glm::dvec3(1, 0, 0);
                const auto sample = geologySample(planet, d), legacy = geologySample(base, d);
                check(std::isfinite(sample.heightMetres), "Non-finite terrain height");
                check(std::abs(sample.heightMetres - legacy.heightMetres - sample.localReliefMetres) < 1e-8,
                      "Local terrain replaced the underlying planetary geology");
                const auto adjacent =
                    geologySample(planet, glm::normalize(d + glm::dvec3(1e-10, -1e-10, 1e-10)));
                check(std::abs(sample.heightMetres - adjacent.heightMetres) < .02,
                      "Spherical terrain contains a seam");
                maximumRelief = std::max(maximumRelief, std::abs(sample.localReliefMetres));
                const double angle = planet.terrain.wavelengthMetres / std::pow(10., planet.logRadius);
                const auto a = geologySample(planet, glm::normalize(d + tangent * angle * .35));
                const auto b = geologySample(planet, glm::normalize(d - tangent * angle * .35));
                maximumLocalRange =
                    std::max(maximumLocalRange, std::abs(a.localReliefMetres - b.localReliefMetres));
                check(geologySample(planet, d).heightMetres == sample.heightMetres,
                      "Terrain depends on query order");
            }
            check(maximumRelief > 20 && maximumLocalRange > 30, "Nearby terrain is still effectively flat");
            std::cout << terrainName(type) << " relief=" << maximumRelief
                      << " local_range=" << maximumLocalRange << '\n';
        }
        Universe universe;
        const auto system = universe.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9));
        std::optional<Node> rugged;
        unsigned smooth = 0, rough = 0;
        for (auto address : universe.children(system)) {
            const auto planet = universe.describe(address);
            if (isMoon(address) || address.id() < universe.legacyPlanetCount(system)) {
                check(planet.geology->terrain.type == TerrainType::None,
                      "Older planets or moons were resurfaced");
                continue;
            }
            if (planet.geology->terrain.type == TerrainType::None)
                ++smooth;
            else {
                ++rough;
                if (!rugged)
                    rugged = planet;
            }
            check(planet.seed == seedOf(address, universe.seed()), "Terrain changed planet identity or seed");
            check(glm::length(planet.frame.position - keplerPosition(planet.orbit, 0) /
                                                          std::pow(10., system.frame.logRadius)) < 1e-14,
                  "Terrain moved a planetary orbit");
        }
        check(rugged.has_value() && rough > 0, "No terrain worlds in the default system");
        const auto planet = *rugged;
        const auto descriptor = descriptorFingerprint(planet);
        auto fresh = Universe{}.describe(planet.address);
        check(descriptorFingerprint(fresh) == descriptor, "Terrain descriptor changed on regeneration");
        const Tile tile = tileAt(direction(17), 11);
        auto mesh = terrainGeometry(universe, planet, tile, 0);
        check(mesh->terrainNormals.size() == 81, "Rugged terrain lost its field-gradient lighting normals");
        const auto focus = direction(17);
        const auto frontier = terrainFrontier(focus * 1.0001, .0001, 900);
        unsigned finest = 0;
        const auto target = tileAt(focus, 24);
        for (auto leaf : frontier)
            if (leaf.face == target.face && (target.x >> (24 - leaf.level)) == leaf.x &&
                (target.y >> (24 - leaf.level)) == leaf.y)
                finest = std::max(finest, leaf.level);
        check(frontier.size() <= 192 && finest >= 9, "Terrain LOD spent its budget away from the ship");
        std::cout << "Near-camera terrain level=" << finest << " leaves=" << frontier.size() << '\n';
        for (unsigned i = 0; i < 81; i++) {
            const auto d = terrainVertexDirection(terrainVertexKey(tile, i % 9, i / 9));
            const auto expected =
                d * (1 + universe.terrainHeight(planet, d) / std::pow(10., planet.frame.logRadius));
            check(glm::dot(mesh->terrainNormals[i], glm::vec3(universe.terrainNormal(planet, d))) > .999999f,
                  "Terrain lighting normal diverged from the generated field");
            check(mesh->precisePositions[i] == expected,
                  "Streamed terrain differs from the collision height field");
        }
        // Captured from b92a8b7's terrainGeometry implementation, compiled
        // against the current immutable descriptor API. Includes every original
        // vertex colour, position and index, with edge stitching at three LODs.
        const uint64_t originalTiles[] = {
            8732566901351970831ULL,  11077755634067242990ULL, 4170208593386473552ULL,
            5063890994104898538ULL,  14719642070300385007ULL, 13760645048301932663ULL,
            13846345641173000075ULL, 10633665284142523898ULL, 7280726599465176103ULL,
            9205391699238090587ULL,  4184421966678382107ULL,  17685900974369419495ULL,
            6635290239928751265ULL,  9061997958241101634ULL,  2195908602913796480ULL,
            9014495794211952667ULL,  16780647719588031657ULL, 2833596960696537995ULL};
        unsigned tileIndex = 0;
        for (auto a : {Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0),
                       Address{}.child(Kind::Galaxy, 0).child(Kind::System, 1).child(Kind::Planet, 10)})
            for (unsigned level : {0u, 6u, 14u})
                for (unsigned stitch : {0u, 5u, 15u}) {
                    const auto t = tileAt(glm::normalize(glm::dvec3(.3, .7, -.2)), level);
                    check(geometryFingerprint(*terrainGeometry(universe, universe.describe(a), t, stitch)) ==
                              originalTiles[tileIndex++],
                          "Surface presentation changed a baseline terrain mesh");
                }
        // Lighting metadata is independent of the exact pre-existing mesh.
        for (unsigned id : {0u, unsigned(planet.address.id())}) {
            const auto body = universe.describe(system.address.child(Kind::Planet, id));
            const auto tileMesh = terrainGeometry(universe, body, tile, 0);
            check(tileMesh->terrainNormals.size() == 81 && tileMesh->terrainAlbedo.size() == 81,
                  "Legacy solid terrain lacks field normals or unlit albedo");
            const auto fingerprint = geometryFingerprint(*tileMesh);
            for (unsigned i = 0; i < 81; i++) {
                const auto d = terrainVertexDirection(terrainVertexKey(tile, i % 9, i / 9));
                const auto normal = universe.terrainNormal(body, d);
                const auto albedo = universe.terrainAlbedo(body, d);
                const auto original =
                    albedo *
                    float(.19 + .81 * std::max(0., glm::dot(normal, glm::normalize(glm::dvec3(-.5, .8, 1)))));
                check(tileMesh->solid.vertices[i].color == rgba(original) &&
                          tileMesh->terrainAlbedo[i] == rgba(albedo),
                      "Lighting changed Original mesh colours or baked light into albedo");
                check(glm::dot(tileMesh->terrainNormals[i], glm::vec3(normal)) > .999999f,
                      "Legacy or rugged normal diverged from collision field");
            }
            tileMesh->terrainNormals.clear();
            tileMesh->terrainAlbedo.clear();
            check(geometryFingerprint(*tileMesh) == fingerprint,
                  "Render-only metadata changed procedural geometry identity");
        }
        // The existing hierarchy must carry the same new terrain all the way
        // through mineral/lattice inspection and back to its parent world.
        auto address = planet.address;
        std::map<Address, std::pair<uint64_t, uint64_t>> identities;
        GeometryGenerator generator(universe);
        while (true) {
            const auto node = universe.describe(address);
            identities.emplace(address, std::pair{descriptorFingerprint(node),
                                                  geometryFingerprint(*generator.generate(node, 0))});
            if (address.kind() == Kind::Lattice)
                break;
            address = universe.children(node, 1).front();
        }
        Universe returnedUniverse;
        GeometryGenerator returnedGenerator(returnedUniverse);
        for (const auto& [a, hashes] : identities) {
            const auto node = returnedUniverse.describe(a);
            check(descriptorFingerprint(node) == hashes.first &&
                      geometryFingerprint(*returnedGenerator.generate(node, 0)) == hashes.second,
                  "Terrain-to-atom round trip changed identity or geometry");
        }
        auto gas = planet;
        auto envelope = std::make_shared<PlanetProcess>(*planet.geology);
        envelope->type = GeologyType::GasEnvelope;
        gas.geology = envelope;
        check(!hasSolidSurface(gas) && universe.children(gas).empty(),
              "Gaseous body exposed a terrain hierarchy");
        check(!terrainGeometry(universe, gas, tile, 0), "Gaseous planet generated a terrain patch");
        bool rejected = false;
        try {
            universe.terrainHeight(gas, direction(0));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "Gaseous planet exposed a landing height");
        auto sphere = GeometryGenerator(universe).generate(gas, 0);
        for (const auto& vertex : sphere->solid.vertices)
            check(std::abs(glm::length(glm::vec3(vertex.x, vertex.y, vertex.z)) - 1) < 1e-6,
                  "Terrain deformed the gaseous envelope");
        RegionCache cache(universe, 24 * 1024 * 1024);
        TerrainStreamer streamer(universe);
        streamer.update(gas, {0, 1.1, 0}, 900, 1. / 60, cache);
        check(!streamer.ready() && streamer.patches().empty(), "Gas planet started terrain streaming");
        Navigation nav(universe);
        FlightController flight(universe, nav, cache);
        Pilot pilot(universe, nav, flight);
        glm::dvec3 landing{};
        bool found = false;
        for (unsigned i = 0; i < 2000; i++) {
            auto d = direction(i);
            if (glm::dot(d, universe.terrainNormal(planet, d)) > .997) {
                landing = d;
                found = true;
                break;
            }
        }
        check(found, "Terrain has no naturally gentle landing sites");
        const auto normal = universe.terrainNormal(planet, landing);
        const auto q = glm::quatLookAtRH(glm::normalize(glm::cross(normal, glm::dvec3(1, 0, 0))), normal);
        const auto eye = landing * (1 + (universe.terrainHeight(planet, landing) + 32) /
                                            std::pow(10., planet.frame.logRadius));
        const auto offset = eye - q * glm::dvec3(0, 0, 1);
        std::ostringstream state;
        state.precision(17);
        state << planet.address.str() << ' ' << planet.frame.logRadius << ' ' << q.w << ' ' << q.x << ' '
              << q.y << ' ' << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        check(nav.restore(state.str()), "Terrain landing fixture failed to restore");
        nav.changeFrame(FlightController::surfaceAt(planet.address, landing));
        nav.setRenderScale(2);
        pilot.resetMotion();
        check(pilot.land(), "Landing refused the enhanced solid world");
        for (unsigned i = 0; i < 3000 && !pilot.ship().landed; i++) {
            cache.beginFrame();
            pilot.update({}, 1. / 60);
            cache.finishFrame();
        }
        check(pilot.ship().landed && pilot.ship().hull == 100,
              "Landing does not meet generated terrain safely");
        check(descriptorFingerprint(universe.describe(planet.address)) == descriptor,
              "Landing modified terrain");
        std::cout << "Terrain world " << planet.address.str() << " / " << planet.label() << " rough=" << rough
                  << " smooth=" << smooth
                  << "; deterministic mesh/physics, legacy worlds, gas exclusion, atoms and landing passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
