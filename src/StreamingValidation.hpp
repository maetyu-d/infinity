#pragma once
#include "infinite/Flight.hpp"
#include "infinite/Renderer.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace infinite {
class StreamingValidation {
  public:
    StreamingValidation(const Universe& u, Navigation& nav, RegionCache& cache)
        : universe_(u), nav_(nav), cache_(cache) {
        if (!nav_.restore("0,0,0 26 1 0 0 0 .97 .97 -.03"))
            throw std::runtime_error("Unable to initialize streaming fixture");
        look({1, 0, 0});
    }
    void advance(FlightController& flight, Renderer& renderer) {
        if (stage_ == 1) {
            flight.update({0, 0, -1}, 1. / 60, true);
            if (nav_.anchor.cell[0] == 1) {
                nav_.selected.reset();
                stage_ = 2;
                stable_ = 0;
                std::cout << "STREAM_CROSS cell=1 physical=" << physicalEye().x << '\n';
            }
        } else if (stage_ >= 5) {
            const double ratio = stage_ == 6  ? .9998 + .0004 * std::min(1., travel_ / 48.)
                                 : stage_ > 6 ? 1.0002
                                              : .9998;
            const auto direction = glm::normalize(glm::dvec3(ratio, 1, .2));
            const auto point = direction * (1.00003 + universe_.terrainHeight(*planet_, direction) /
                                                          std::pow(10., planet_->frame.logRadius));
            const auto eye = universe_.pointRelative(planet_->address, point, nav_.anchor, nav_.logDistance);
            const auto tangent = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), direction));
            const auto heading = glm::normalize(tangent * .94 - direction * .35);
            const auto pose = universe_.relative(planet_->address, nav_.anchor, nav_.logDistance);
            nav_.orientation = glm::normalize(pose.orientation * glm::quatLookAtRH(heading, direction));
            nav_.offset = eye - nav_.orientation * glm::dvec3(0, 0, 1);
            flight.update({}, 1. / 60);
            if (stage_ == 6 && ++travel_ > 48) {
                stage_ = 7;
                stable_ = 0;
                std::cout << "STREAM_TERRAIN face-crossing complete\n";
            }
        }
        (void)renderer;
    }
    void observe(const FrameStats& stats, Renderer& renderer, const std::string& captures) {
        const auto generation = cache_.stats();
        bool settled = generation.pending == 0 && generation.queued == 0 && stats.lodTransitions == 0 &&
                       stats.uploadDeferred == 0 && stats.rootCells == 27;
        if (stage_ >= 5)
            settled &= stats.terrainTiles > 0;
        stable_ = settled ? stable_ + 1 : 0;
        if (hold_) {
            --hold_;
            return;
        }
        auto capture = [&](std::string name) {
            if (!captures.empty())
                renderer.screenshot((std::filesystem::path(captures) / name).string());
            hold_ = 4;
        };
        if (stage_ == 0 && stable_ >= 8) {
            stage_ = 1;
            stable_ = 0;
        } else if (stage_ == 2 && stable_ >= 8) {
            capture("root-a.ppm");
            worldEye_ = physicalEye();
            stage_ = 3;
        } else if (stage_ == 3 && stable_ >= 8) {
            nav_.changeFrame(Address{});
            nav_.selected.reset();
            if (glm::length(physicalEye() - worldEye_) > 1e-12)
                throw std::runtime_error("Root reference handoff teleported the native camera");
            stage_ = 4;
            stable_ = 0;
        } else if (stage_ == 4 && stable_ >= 8) {
            capture("root-b.ppm");
            stage_ = -1;
        } else if (stage_ == -1) {
            const auto galaxy = Address{}.child(Kind::Galaxy, 0);
            const auto system = universe_.children(universe_.describe(galaxy), 1).front();
            planet_ = universe_.describe(universe_.children(universe_.describe(system), 1).front());
            std::ostringstream state;
            state << planet_->address.str() << " 5 1 0 0 0 0 0 0";
            if (!nav_.restore(state.str()))
                throw std::runtime_error("Terrain fixture restore failed");
            stage_ = 5;
            stable_ = 0;
        } else if (stage_ == 5 && stable_ >= 8) {
            capture("terrain-before.ppm");
            stage_ = 6;
            stable_ = 0;
        } else if (stage_ == 7 && stable_ >= 8) {
            capture("terrain-" + std::string(renderModeName(renderer.mode())) + ".ppm");
            std::cout << "STREAM_RENDER " << renderModeName(renderer.mode())
                      << " tiles=" << stats.terrainTiles << " cells=" << stats.rootCells
                      << " cache=" << generation.bytes << '\n';
            if (modes_++ == 2) {
                stage_ = 9;
            } else {
                stage_ = 8;
            }
            stable_ = 0;
        } else if (stage_ == 8) {
            renderer.setMode(nextRenderMode(renderer.mode()));
            stage_ = 7;
            stable_ = 0;
        } else if (stage_ == 9) {
            finished_ = true;
            std::cout << "PASS: native cell crossing, invariant camera rebase, continuous terrain face "
                         "traversal and three rendering modes.\n";
        }
    }
    bool finished() const {
        return finished_;
    }

  private:
    const Universe& universe_;
    Navigation& nav_;
    RegionCache& cache_;
    std::optional<Node> planet_;
    int stage_ = 0, stable_ = 0, hold_ = 0, travel_ = 0, modes_ = 0;
    bool finished_ = false;
    glm::dvec3 worldEye_{};
    glm::dvec3 physicalEye() const {
        const auto node = universe_.describe(nav_.anchor);
        return universe_.pointRelative(
            nav_.anchor, nav_.cameraPosition() * std::pow(10., nav_.logDistance - node.frame.logRadius),
            Address{}, 26);
    }
    void look(glm::dvec3 direction) {
        const auto eye = nav_.cameraPosition();
        nav_.orientation = glm::quatLookAtRH(direction, glm::dvec3(0, 1, 0));
        nav_.offset = eye - nav_.orientation * glm::dvec3(0, 0, 1);
    }
};
} // namespace infinite
