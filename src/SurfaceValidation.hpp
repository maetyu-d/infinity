#pragma once
#include "infinite/Cockpit.hpp"
#include "infinite/Renderer.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace infinite {
class SurfaceValidation {
  public:
    SurfaceValidation(const Universe& universe, Navigation& nav, Pilot& pilot, ActivityState& activity)
        : universe_(universe), nav_(nav), pilot_(pilot), activity_(activity) {
        const auto galaxy = Address{}.child(Kind::Galaxy, 0);
        for (unsigned star = 0; star < 512 && (!city_ || !colony_); star++) {
            const auto system = universe_.describe(galaxy.child(Kind::System, star));
            for (unsigned id = 0; id < universe_.planetCount(system); id++) {
                const auto p = universe_.describe(system.address.child(Kind::Planet, id));
                const auto climate = planetClimate(p);
                if ((climate.temperate && city_) || (!climate.temperate && colony_) ||
                    climate.temperatureK < 85 || climate.temperatureK > 650 || climate.gravity > 32)
                    continue;
                const auto world = generateSurfaceLife(universe_, p);
                if (climate.temperate) {
                    const auto has = [&](CityPattern pattern) {
                        return std::any_of(
                            world->settlements.begin(), world->settlements.end(),
                            [&](const auto& city) { return city.layoutRecipe && city.pattern == pattern; });
                    };
                    if (!has(CityPattern::Planned) || !has(CityPattern::Organic) || !has(CityPattern::Radial))
                        continue;
                }
                const auto light =
                    glm::normalize(universe_.relative(system.address, p.address, p.frame.logRadius).position);
                for (const auto& s : world->settlements)
                    if (glm::dot(s.direction, light) > .2 &&
                        sampleWeather(climate, s.direction, 0, 12).storm < .55 &&
                        (climate.temperate || s.kind == SettlementKind::Colony)) {
                        if (climate.temperate) {
                            city_ = world;
                            citySlot_ = s.slot;
                        } else {
                            colony_ = world;
                            colonySlot_ = s.slot;
                        }
                        break;
                    }
            }
        }
        if (!city_ || !colony_)
            throw std::runtime_error("Native surface fixture cannot find real cities and sealed colonies");
        const auto cityPlanet = universe_.describe(city_->planet);
        const auto sun = glm::normalize(
            universe_.relative(city_->planet.parent(), city_->planet, cityPlanet.frame.logRadius).position);
        for (auto pattern : {CityPattern::Organic, CityPattern::Planned}) {
            const SurfaceSettlement* selected = nullptr;
            double light = -2;
            for (const auto& city : city_->settlements)
                if (city.pattern == pattern && glm::dot(city.direction, sun) > light) {
                    selected = &city;
                    light = glm::dot(city.direction, sun);
                }
            if (!selected)
                throw std::runtime_error("Missing actual city morphology");
            additionalSlots_.push_back(selected->slot);
        }
        for (unsigned i = 0; i < 4096 && !precipitation_; i++) {
            const double y = 1 - 2 * (i + .5) / 4096., a = i * 2.399963229728653;
            const auto d =
                glm::dvec3(std::sqrt(1 - y * y) * std::cos(a), y, std::sqrt(1 - y * y) * std::sin(a));
            const double h = universe_.terrainHeight(cityPlanet, d);
            if (glm::dot(d, sun) > .15 && sampleWeather(city_->climate, d, h + 15, 12).precipitation > .10)
                precipitation_ = d;
        }
        if (!precipitation_)
            throw std::runtime_error("Native weather fixture found no real precipitation front");
        std::cout << "SURFACE_CITY " << city_->planet.str() << " colony=" << colony_->planet.str() << '\n';
        place();
    }
    bool finished() const {
        return stage_ >= 7;
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        ++frames_;
        if (capturing_) {
            if (hold_-- > 0)
                return;
            capturing_ = false;
            {
                mode_ = 0;
                ++stage_;
                if (!finished()) {
                    renderer.setMode(RenderMode::Hyperion);
                    place();
                } else
                    std::cout << "PASS: native weather, city, residents and sealed colony / seven views in "
                                 "Hyperion rendering with identical planet geometry and camera\n";
            }
            return;
        }
        const auto generation = cache.stats();
        bool ready = stats.surfacePending == 0 && generation.pending == 0 && generation.queued == 0 &&
                     stats.lodTransitions == 0 && stats.uploadDeferred == 0 && stats.weatherPending == 0;
        ready &= stats.lifeWorlds > 0;
        if (stage_ == 0)
            ready &= stats.weatherCells > 0 && stats.surfaceSites > 0;
        else if (stage_ == 4)
            ready &= stats.weatherParticles > 0 && stats.terrainTiles > 50;
        else
            ready &= stats.surfaceBuildings > 0 && stats.surfaceTriangles > 0 && stats.terrainTiles > 50;
        if (stage_ == 2)
            ready &= stats.surfaceResidents > 0;
        stable_ = ready ? stable_ + 1 : 0;
        if (stable_ < 8)
            return;
        const auto contacts = renderer.celestialContacts();
        const auto rows = cockpitCelestialRows(contacts);
        if (renderer.pick(1170, 565) != nextLivingWorld(contacts, nav_.selected) ||
            renderer.pickActivity(1170, 565) || rows.empty() ||
            renderer.pick(1170, 217) != contacts[rows.front()].address)
            throw std::runtime_error("Life HUD panel or prioritized world rows selected the wrong address");
        const auto world = stage_ == 3 ? colony_ : city_;
        if (descriptorFingerprint(universe_.describe(world->planet)) != world->descriptor)
            throw std::runtime_error("Surface rendering altered a planetary descriptor");
        const auto geometry = renderer.submittedGeometry(world->planet);
        const auto hash =
            stage_ == 0 ? (geometry ? geometryFingerprint(*geometry) : 0) : renderer.terrainGeometryDigest();
        if (!hash)
            return;
        const auto renderedWorld = renderer.surfaceWorld(world->planet);
        if (!renderedWorld)
            return;
        uint64_t cityHash = 0;
        for (const auto& city : renderedWorld->settlements)
            cityHash = mix64(cityHash ^ cityFingerprint(city));
        if (mode_ == 0) {
            camera_ = nav_.serialize();
            geometry_ = hash;
            cityGeometry_ = cityHash;
        } else if (camera_ != nav_.serialize() || geometry_ != hash || cityGeometry_ != cityHash)
            throw std::runtime_error("Weather/society mode switch changed geometry or camera");
        static const char* names[] = {"weather-orbit", "city-approach",    "inhabitants",     "sealed-colony",
                                      "precipitation", "organic-overview", "planned-overview"};
        if (!directory.empty())
            renderer.screenshot(
                (std::filesystem::path(directory) / (std::to_string(stage_) + "-" + names[stage_] + "-" +
                                                     std::string(renderModeName(renderer.mode())) + ".ppm"))
                    .string());
        std::cout << "SURFACE_RENDER " << names[stage_] << " mode=" << renderModeName(renderer.mode())
                  << " sites=" << stats.surfaceSites << " buildings=" << stats.surfaceBuildings
                  << " residents=" << stats.surfaceResidents << " triangles=" << stats.surfaceTriangles
                  << " cloud_cells=" << stats.weatherCells << " cpu_bytes=" << stats.surfaceBytes
                  << " gpu_bytes=" << stats.surfaceGpuBytes << " precipitation=" << stats.weatherParticles
                  << " frame=" << frames_ << " shadow_bytes=" << stats.shadowBytes
                  << " shadow_active=" << stats.shadowActive << " shadow_updates=" << stats.shadowUpdates
                  << " life_worlds=" << stats.lifeWorlds << " life_scanning=" << stats.lifeScanning << '\n';
        if (stage_ == 1 && mode_ == 0)
            for (const auto& city : renderedWorld->settlements)
                std::cout << "CITY_LAYOUT " << city.name << " pattern=" << cityPatternName(city.pattern)
                          << " buildings=" << city.buildings.size() << " streets=" << city.streets.size()
                          << " parcels=" << city.parcels.size() << " population=" << city.population
                          << " recipe=" << city.layoutRecipe << '\n';
        stable_ = 0;
        hold_ = 6;
        capturing_ = true;
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    Pilot& pilot_;
    ActivityState& activity_;
    std::shared_ptr<SurfaceWorld> city_, colony_;
    uint32_t citySlot_ = 0, colonySlot_ = 0;
    unsigned stage_ = 0, mode_ = 0, stable_ = 0;
    int hold_ = 0;
    bool capturing_ = false;
    std::string camera_;
    uint64_t geometry_ = 0;
    uint64_t cityGeometry_ = 0;
    size_t frames_ = 0;
    std::vector<uint32_t> additionalSlots_;
    std::optional<glm::dvec3> precipitation_;
    void place() {
        const auto world = stage_ == 3 ? colony_ : city_;
        const auto slot = stage_ == 3 ? colonySlot_ : stage_ >= 5 ? additionalSlots_[stage_ - 5] : citySlot_;
        const auto& s = *std::find_if(world->settlements.begin(), world->settlements.end(),
                                      [&](const auto& s) { return s.slot == slot; });
        const auto p = universe_.describe(world->planet);
        auto up = s.direction;
        const auto east = s.orientation * glm::dvec3(1, 0, 0), north = s.orientation * glm::dvec3(0, 0, -1);
        glm::dvec3 eye, target;
        double log = 3;
        if (stage_ >= 5) {
            eye = s.landingPoint + north * (s.radius * .35) + up * (s.radius * .85);
            target = s.landingPoint + up * 10.;
            log = 3;
        } else if (stage_ == 4) {
            up = *precipitation_;
            eye = up * (world->radiusMetres + universe_.terrainHeight(p, up) + 15);
            target = eye + tangentFrame(up) * glm::dvec3(100, -8, 0);
            log = 3;
        } else if (stage_ == 0) {
            eye = s.direction * world->radiusMetres * 1.8;
            target = glm::dvec3(0);
            up = north;
            log = p.frame.logRadius;
        } else if (stage_ == 2) {
            SurfaceResident person;
            const unsigned people = std::min(96u, s.population);
            for (unsigned sample = 0; sample < people; sample++) {
                const unsigned id = unsigned(uint64_t(sample) * s.population / people);
                person = sampleResident(*world, s, id, 12);
                if (person.outside)
                    break;
            }
            target = person.position + up * 1.0;
            const auto forward = person.orientation * glm::dvec3(0, 0, -1);
            eye = person.position - forward * 13. + up * 3.;
            if (surfaceCollision(*world, eye, target, .1))
                eye = person.position + forward * 13. + up * 3.;
            if (surfaceCollision(*world, eye, target, .1))
                eye = person.position + up * 8. + east * 3.;
            log = 2;
            const auto d = glm::normalize(eye);
            const double ground = world->radiusMetres + universe_.terrainHeight(p, d);
            if (glm::length(eye) < ground + 2)
                eye = d * (ground + 2);
        } else {
            eye = s.landingPoint + north * (stage_ == 3 ? 350. : 820.) + up * (stage_ == 3 ? 105. : 220.);
            target = s.landingPoint + up * 20.;
            const auto d = glm::normalize(eye);
            const double ground = world->radiusMetres + universe_.terrainHeight(p, d);
            if (glm::length(eye) < ground + 60)
                eye = d * (ground + 60);
        }
        const auto q = glm::quatLookAtRH(glm::normalize(target - eye), up);
        const auto offset = eye / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream state;
        state.precision(17);
        state << p.address.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
              << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(state.str()))
            throw std::runtime_error("Native surface fixture placement failed");
        nav_.selected.reset();
        pilot_.resetMotion();
        activity_.selected = ActivityId{p.address, surfaceActivitySlot + s.slot};
        pilot_.setSurfaceWorld(world);
        std::cout << "SURFACE_FIXTURE " << stage_ << " " << p.address.str() << " " << s.name
                  << " population=" << s.population << '\n';
    }
};
} // namespace infinite
