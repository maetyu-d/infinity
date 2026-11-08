#include "infinity/Exploration.hpp"
#include "infinity/MajorLandforms.hpp"
#include "infinity/Streaming.hpp"
#include "infinity/SurfaceHazards.hpp"
#include "infinity/SurfaceLife.hpp"
#include "infinity/SurfaceRecon.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace infinity;
static void check(bool b, const char* msg) {
    if (!b)
        throw std::runtime_error(msg);
}
static glm::dvec3 at(const MajorLandform& f, double x, double z) {
    return glm::normalize(f.centre + f.along * (x * f.lengthMetres / f.radiusMetres) +
                          f.across * (z * f.widthMetres / f.radiusMetres));
}
int main() {
    try {
        Universe u;
        std::array<unsigned, 5> counts{};
        std::array<std::optional<Node>, 5> fixtures;
        unsigned total = 0, solid = 0, featured = 0;
        // Real catalogue addresses, never hand-injected demonstration worlds.
        const auto galaxy = Address{}.child(Kind::Galaxy, 0);
        for (unsigned system = 0; system < 128; ++system) {
            const auto s = u.describe(galaxy.child(Kind::System, system));
            for (unsigned id = 0; id < u.planetCount(s); ++id) {
                const auto p = u.describe(s.address.child(Kind::Planet, id));
                ++total;
                if (!hasSolidSurface(p))
                    continue;
                ++solid;
                if (!p.geology->landform)
                    continue;
                ++featured;
                const auto& f = *p.geology->landform;
                ++counts[size_t(f.type)];
                if (!fixtures[size_t(f.type)] ||
                    glm::dot(f.centre,
                             glm::normalize(u.pointRelative(s.address, glm::dvec3(0), p.address, 0))) > .35)
                    fixtures[size_t(f.type)] = p;
                check(p.expandedPopulation, "Major landform altered a legacy planet");
                check(generateSurfaceLife(u, p)->settlements.empty(),
                      "Major relief displaced an established settlement");
                check(f.widthMetres <= f.radiusMetres * .1 && f.lengthMetres <= f.radiusMetres * .14,
                      "Oversized landform");
                if (f.type == MajorLandformType::IceSheet)
                    check(p.environment.temperature < 245 && p.geology->ice > .25, "Hot or ice-free glacier");
                if (f.type == MajorLandformType::Supercaldera || f.type == MajorLandformType::ShieldVolcano)
                    check(p.geology->activity > .045 && p.environment.chemistry.silicate > .25,
                          "Volcano ignored crust and heat");
            }
        }
        check(featured > 10 && featured < solid * .15, "Major landform rarity outside intended range");
        for (size_t type = 0; type < counts.size(); ++type) {
            check(counts[type] > 0, "Natural catalogue missing a major landform type");
            const auto& p = *fixtures[type];
            const auto& f = *p.geology->landform;
            const auto heightAt = [&](double x, double z) {
                return sampleMajorLandform(f, at(f, x, z)).reliefMetres;
            };
            switch (f.type) {
            case MajorLandformType::Supercaldera:
                check(heightAt(0, 0) < heightAt(0, 1), "Caldera has no collapse floor below its rim");
                break;
            case MajorLandformType::ShieldVolcano:
                check(heightAt(0, .25) > heightAt(0, 1.5), "Shield has no broad raised edifice");
                break;
            case MajorLandformType::IceSheet:
                check(heightAt(0, 0) > heightAt(0, 1.4), "Glacier has no thick accumulation zone");
                break;
            case MajorLandformType::Rift:
                check(heightAt(0, 0) < heightAt(0, 1.2), "Rift has no sunken fault floor");
                break;
            case MajorLandformType::ImpactBasin:
                check(heightAt(0, 0) < heightAt(0, .82), "Impact basin has no raised ring");
                break;
            }
            const auto hash = descriptorFingerprint(p);
            const auto a = majorLandformAddress(p);
            const auto children = u.children(p);
            check(std::find(children.begin(), children.end(), a) != children.end(),
                  "Landform is not discoverable");
            check(u.describe(a).label().starts_with(majorLandformName(f.type)), "Landmark lost its identity");
            check(surveyReading(u, a, 2, 100).formation.find(majorLandformName(f.type)) != std::string::npos,
                  "Survey omitted major landform");
            auto fresh = std::async(std::launch::async, [&] {
                Universe v;
                return descriptorFingerprint(v.describe(p.address));
            });
            check(fresh.get() == hash, "Generation order/thread changed landform");
            auto base = *p.geology;
            base.landform.reset();
            for (int i = -50; i <= 50; ++i) {
                const auto d = at(f, i * .04, .23);
                const auto g = geologySample(*p.geology, d);
                const auto old = geologySample(base, d);
                const auto m = sampleMajorLandform(f, d);
                check(std::isfinite(g.heightMetres) && g.heightMetres == u.terrainHeight(p, d),
                      "Render/physics height disagreement");
                check(std::abs(g.heightMetres - old.heightMetres - m.reliefMetres) < 1e-8,
                      "Major relief differs between samples");
                check(glm::length(u.terrainNormal(p, d)) > .9999, "Invalid surface normal");
                auto bare = p;
                bare.environment.chemistry = {1, 0, 0, 0};
                const auto exposure = resourceExposure(bare, d, g);
                check(exposure.fractions[0] == 0 && exposure.fractions[1] == 0 && exposure.fractions[3] == 0,
                      "Landmark invented minerals");
            }
            check(geologySample(*p.geology, -f.centre).heightMetres ==
                      geologySample(base, -f.centre).heightMetres,
                  "Relief leaked across planet");
            // Shared cube-face boundaries and both poles evaluate one continuous field.
            for (auto d : {glm::normalize(glm::dvec3(1, 1, 0)), glm::dvec3(0, 1, 0), glm::dvec3(0, -1, 0)}) {
                const auto tile = tileAt(d, 16);
                const auto c = tileDirection(tile);
                check(std::isfinite(u.terrainHeight(p, c)), "Polar/cube seam height invalid");
            }
            const auto edge = at(f, 2.3, 0);
            const auto near = at(f, 2.3 - 1e-7, 0);
            check(std::abs(sampleMajorLandform(f, edge).reliefMetres -
                           sampleMajorLandform(f, near).reliefMetres) < .01,
                  "Visible edge at landform boundary");
            const auto tile = tileAt(f.centre, 14);
            const auto mesh = terrainGeometry(u, p, tile, 0);
            check(mesh && mesh->solid.vertices.size() >= 81, "Missing streamed landform mesh");
            for (unsigned i = 0; i < 81; ++i) {
                const auto d = terrainVertexDirection(terrainVertexKey(tile, i % 9, i / 9));
                const auto expected = d * (1 + u.terrainHeight(p, d) / f.radiusMetres);
                check(glm::length(glm::dvec3(mesh->solid.vertices[i].x, mesh->solid.vertices[i].y,
                                             mesh->solid.vertices[i].z) -
                                  expected) < 2e-7,
                      "Stream mesh changed terrain field");
            }
            WeatherSample clear;
            clear.temperatureK = 220;
            clear.visibilityMetres = 120000;
            if (f.type == MajorLandformType::Supercaldera || f.type == MajorLandformType::ShieldVolcano) {
                const auto warning = surfaceConditions(p, f.centre, 0, clear);
                check(warning.kind == SurfaceHazard::Geothermal && warning.outdoorHold,
                      "Vent hazard missing");
                check(surfaceExposure(p, clear, 0, f.centre).heat > 0 &&
                          surfaceExposure(p, clear, 0, -f.centre).heat == 0,
                      "Geothermal heat is not local");
            }
            if (f.type == MajorLandformType::IceSheet) {
                std::vector<std::pair<Address, uint64_t>> lineage;
                auto specimen = a;
                while (true) {
                    const auto n = u.describe(specimen);
                    lineage.emplace_back(specimen, descriptorFingerprint(n));
                    if (specimen.kind() == Kind::Lattice)
                        break;
                    const auto next = u.children(n, 1);
                    check(!next.empty(), "Glacier-to-atom hierarchy missing");
                    specimen = next.front();
                }
                Universe returned;
                for (const auto& [address, digest] : lineage)
                    check(descriptorFingerprint(returned.describe(address)) == digest,
                          "Landform-to-atom round trip changed identity");
                const auto map = sampleSurfaceMap(u, p, tileAt(f.centre, 14));
                ReconInput input;
                input.open = true;
                input.observer.reference = p.address;
                input.observer.position = f.centre * (f.radiusMetres + u.terrainHeight(p, f.centre) + 500);
                ReconOrders orders;
                const auto recon = describeSurfaceRecon(u, map, input, orders, 0);
                check(std::any_of(recon.points.begin(), recon.points.end(),
                                  [&](const auto& point) { return point.address == a; }),
                      "Surface recon omitted canonical geological destination");
                check(sampleMajorLandform(f, f.centre).ice > .9, "Ice accumulation dome missing");
                const auto margin = at(f, 0, .98);
                const auto warning = surfaceConditions(p, margin, 0, clear);
                check(warning.kind == SurfaceHazard::Crevasses && warning.outdoorHold,
                      "Crevasse hazard missing");
            }
            std::cout << majorLandformName(f.type) << " count=" << counts[type]
                      << " fixture=" << p.address.str() << " width_km=" << f.widthMetres / 1000
                      << " relief_m=" << f.reliefMetres << " descriptor=" << hash << '\n';
        }
        Environment e;
        for (uint64_t seed = 0; seed < 100; ++seed) {
            check(!planetProcess(seed, 6.4, e, false)->landform, "Legacy recipe received a landform");
            check(!planetProcess(seed, 6.4, e, true, true)->landform, "Gas envelope received terrain");
        }
        // Sample cost and storage are bounded; no geometry held by the descriptor.
        const auto& p = *fixtures[0];
        auto base = *p.geology;
        base.landform.reset();
        volatile double sink = 0;
        auto measure = [&](const PlanetProcess& process) {
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < 10000; ++i)
                sink = geologySample(process, at(*p.geology->landform, (int(i % 101) - 50) * .05, .2))
                           .heightMetres;
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        };
        std::cout << "CATALOGUE " << featured << '/' << solid << " solid worlds (" << total
                  << " total), descriptor=" << sizeof(MajorLandform)
                  << " bytes; terrain sampling 10k baseline_ms=" << measure(base)
                  << " major_ms=" << measure(*p.geology) << " sink=" << sink << '\n';
        std::cout << "PASS rare physics-constrained landforms, deterministic regeneration, settlements, "
                     "discovery, surveys, materials, hazards and shared mesh/collision field\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
