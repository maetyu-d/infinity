#pragma once
#include "infinite/Piloting.hpp"
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>

namespace infinite {
class TerrainValidation {
  public:
    TerrainValidation(const Universe& universe, Navigation& nav, Pilot& pilot, bool quality = false,
                      bool surfaceFrame = false, std::string fixedView = {})
        : universe_(universe), nav_(nav), pilot_(pilot), quality_(quality), surfaceFrame_(surfaceFrame),
          fixedView_(std::move(fixedView)) {
        std::map<TerrainType, Node> examples;
        const auto galaxy = Address{}.child(Kind::Galaxy, 0);
        for (unsigned star = 0; star < 32 && examples.size() < 4; star++) {
            const auto system = universe_.describe(galaxy.child(Kind::System, star));
            for (unsigned id = universe_.legacyPlanetCount(system); id < universe_.planetCount(system);
                 id++) {
                auto planet = universe_.describe(system.address.child(Kind::Planet, id));
                const auto type = planet.geology->terrain.type;
                if (type != TerrainType::None)
                    examples.try_emplace(type, std::move(planet));
            }
        }
        if (examples.size() < 3)
            throw std::runtime_error("Terrain fixture did not find diverse real solid worlds");
        for (auto& [type, planet] : examples)
            planets_.push_back(std::move(planet));
        if (quality_) {
            planets_.resize(1);
            planets_.insert(planets_.begin(),
                            universe_.describe(galaxy.child(Kind::System, 9).child(Kind::Planet, 0)));
        }
        if (!fixedView_.empty()) {
            if (!nav_.restore(fixedView_))
                throw std::runtime_error("Invalid saved terrain test view");
            auto address = nav_.anchor;
            while (address.kind() > Kind::Planet)
                address = address.parent();
            if (address.kind() != Kind::Planet)
                throw std::runtime_error("Saved view has no solid planet");
            planets_ = {universe_.describe(address)};
            surfaceFrame_ = nav_.anchor != address;
        }
        place();
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        if (pendingCapture_) {
            if (hold_) {
                --hold_;
                return;
            }
            pendingCapture_ = false;
            if (++mode_ < 3)
                renderer.setMode(static_cast<RenderMode>(mode_));
            else {
                if (quality_ && ++altitude_ < 3) {
                } else {
                    ++world_;
                    altitude_ = 0;
                }
                mode_ = 0;
                if (world_ < planets_.size()) {
                    renderer.setMode(RenderMode::Original);
                    place();
                } else
                    std::cout << "PASS: native solid terrain worlds=" << planets_.size()
                              << " modes=3; height fields, positions and cameras retained\n";
            }
            return;
        }
        if (finished())
            return;
        const auto& planet = planets_[world_];
        if (descriptorFingerprint(universe_.describe(planet.address)) != descriptor_)
            throw std::runtime_error("Native terrain exploration changed its descriptor");
        auto generation = cache.stats();
        const bool ready = stats.terrainTiles > 50 && generation.pending == 0 && generation.queued == 0 &&
                           stats.lodTransitions == 0 && stats.uploadDeferred == 0 &&
                           stats.weatherPending == 0;
        stable_ = ready ? stable_ + 1 : 0;
        if (hold_) {
            --hold_;
            return;
        }
        if (stable_ < 8)
            return;
        if (mode_ == 0) {
            view_ = nav_.serialize();
            geometry_ = renderer.submittedGeometry(surfaceFrame_ ? nav_.anchor : planet.address);
            if (!geometry_)
                throw std::runtime_error("Terrain fixture lost its original addressed geometry");
            geometryHash_ = geometryFingerprint(*geometry_);
            terrainHash_ = renderer.terrainGeometryDigest();
        } else if (nav_.serialize() != view_ ||
                   geometryFingerprint(*renderer.submittedGeometry(
                       surfaceFrame_ ? nav_.anchor : planet.address)) != geometryHash_ ||
                   renderer.terrainGeometryDigest() != terrainHash_)
            throw std::runtime_error("Terrain rendering mode changed geometry or the camera");
        if (!directory.empty())
            renderer.screenshot((std::filesystem::path(directory) /
                                 (std::to_string(world_) + (quality_ ? "-" + std::to_string(altitude_) : "") +
                                  "-" + std::string(terrainName(planet.geology->terrain.type)) + "-" +
                                  std::string(renderModeName(renderer.mode())) + ".ppm"))
                                    .string());
        std::cout << "TERRAIN_RENDER " << planet.address.str()
                  << " type=" << terrainName(planet.geology->terrain.type)
                  << " mode=" << renderModeName(renderer.mode()) << " tiles=" << stats.terrainTiles
                  << " height_m="
                  << (!fixedView_.empty() ? pilot_.instruments().altitude
                      : quality_          ? heights_[altitude_]
                                          : 250.)
                  << " shadow=" << stats.shadowActive << " terrain=" << terrainHash_
                  << " geometry=" << geometryHash_ << " range_m=" << range_ << '\n';
        stable_ = 0;
        hold_ = 6;
        pendingCapture_ = true;
    }
    bool finished() const {
        return world_ >= planets_.size();
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    Pilot& pilot_;
    std::vector<Node> planets_;
    size_t world_ = 0;
    bool quality_ = false, surfaceFrame_ = false;
    unsigned altitude_ = 0;
    static constexpr double heights_[3] = {250, 2000, 11800};
    unsigned mode_ = 0, stable_ = 0, hold_ = 0;
    uint64_t descriptor_ = 0, geometryHash_ = 0, terrainHash_ = 0;
    double range_ = 0;
    bool pendingCapture_ = false;
    std::string view_, fixedView_;
    std::shared_ptr<const Geometry> geometry_;
    void place() {
        const auto& planet = planets_[world_];
        if (!fixedView_.empty()) {
            nav_.restore(fixedView_);
            nav_.selected.reset();
            pilot_.resetMotion();
            descriptor_ = descriptorFingerprint(planet);
            return;
        }
        const double radius = std::pow(10., planet.frame.logRadius),
                     wave = planet.geology->terrain.wavelengthMetres;
        const auto light = glm::normalize(
            universe_.relative(planet.address.parent(), planet.address, planet.frame.logRadius).position);
        glm::dvec3 chosen{0, 1, 0};
        range_ = 0;
        for (unsigned i = 0; i < 48; i++) {
            const auto d =
                glm::normalize(glm::dvec3(std::sin(i * 1.7), .35 + std::sin(i * .37), std::cos(i * 2.1)));
            if (glm::dot(d, light) < .35)
                continue;
            const auto east = tangentFrame(d) * glm::dvec3(1, 0, 0);
            double lo = 1e30, hi = -1e30;
            for (int j = -4; j <= 4; j++) {
                const auto q = glm::normalize(d + east * (j * wave / radius * .35));
                const double h = geologySample(*planet.geology, q).localReliefMetres;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
            if (hi - lo > range_ || i == 0) {
                range_ = hi - lo;
                chosen = d;
            }
        }
        if (range_ < 30 && !quality_)
            throw std::runtime_error("Native terrain world has no observable local relief");
        const auto tangent = tangentFrame(chosen) * glm::dvec3(1, 0, 0);
        const auto q = glm::quatLookAtRH(glm::normalize(tangent - chosen * .12), chosen);
        const auto eye =
            chosen *
            (1 +
             (universe_.terrainHeight(planet, chosen) + (quality_ ? heights_[altitude_] : 250.)) / radius);
        constexpr double renderLog = 4;
        const auto offset = eye * std::pow(10., planet.frame.logRadius - renderLog) - q * glm::dvec3(0, 0, 1);
        std::ostringstream state;
        state.precision(17);
        state << planet.address.str() << ' ' << renderLog << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' '
              << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(state.str()))
            throw std::runtime_error("Native terrain placement failed");
        if (surfaceFrame_)
            nav_.changeFrame(planet.address.child(Kind::Surface, encodeTile(tileAt(chosen, 7))));
        nav_.selected.reset();
        pilot_.resetMotion();
        descriptor_ = descriptorFingerprint(planet);
        std::cout << "TERRAIN_FIXTURE " << planet.address.str() << " label=" << planet.label()
                  << " seed=" << planet.seed << " relief_m=" << range_ << " frame=" << nav_.anchor.str()
                  << '\n';
    }
};
} // namespace infinite
