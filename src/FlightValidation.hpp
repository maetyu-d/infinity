#pragma once
#include "infinite/Flight.hpp"
#include "infinite/Renderer.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace infinite {
// Offline native-renderer fixture. Steering follows physical waypoints; the
// production controller alone decides all reference-frame and scale changes.
class FlightValidation {
  public:
    FlightValidation(const Universe& universe, Navigation& nav) : universe_(universe), nav_(nav) {
        if (!nav_.restore("0,0,0 26 1 0 0 0 .1 .2 -.2"))
            throw std::runtime_error("Unable to initialize native automatic-flight fixture");
    }
    void advance(FlightController& flight, Renderer& renderer) {
        // Keep the initial cosmic view until asynchronous root geometry has
        // actually rendered, just as later levels must be visible to pass.
        if (visible_.empty() && nav_.anchor.kind() == Kind::Cosmic)
            return;
        glm::dvec3 goal;
        if (returning_ && !entries_.empty()) {
            const auto& entry = entries_.back();
            const auto child = universe_.describe(entry.address);
            goal = universe_.pointRelative(entry.address.parent(), glm::normalize(child.frame.position) * 2.5,
                                           nav_.anchor, nav_.logDistance);
        } else
            goal = universe_.relative(flight.target().value_or(nav_.anchor), nav_.anchor, nav_.logDistance)
                       .position;
        const auto eye = nav_.cameraPosition();
        if (glm::length(goal - eye) > 1e-14) {
            const auto direction = glm::normalize(goal - eye);
            const auto up = std::abs(direction.y) > .95 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
            nav_.orientation = glm::quatLookAtRH(direction, up);
            nav_.offset = eye - nav_.orientation * glm::dvec3(0, 0, 1);
        }
        const auto old = nav_.anchor;
        flight.update({0, 0, -1}, 1. / 30, true, false, renderer.submittedGeometry(nav_.anchor));
        if (nav_.transitioning || !std::isfinite(glm::length(nav_.cameraPosition())))
            throw std::runtime_error("Automatic flight invoked focus or produced a non-finite camera");
        if (nav_.anchor != old) {
            std::cout << "AUTO_FRAME " << old.str() << " -> " << nav_.anchor.str()
                      << " log=" << nav_.logDistance << '\n';
            if (!returning_) {
                const auto node = universe_.describe(nav_.anchor);
                entries_.push_back({nav_.anchor, nav_.cameraPosition() *
                                                     std::pow(10., nav_.logDistance - node.frame.logRadius)});
                ++inward_;
            } else if (!entries_.empty() && nav_.anchor == entries_.back().address.parent()) {
                entries_.pop_back();
                ++outward_;
            }
        }
    }
    void observe(const FrameStats& stats, Renderer& renderer, const std::string& captures) {
        const auto node = universe_.describe(nav_.anchor);
        const auto geometry = renderer.submittedGeometry(nav_.anchor);
        const auto kind = nav_.anchor.kind();
        if (!returning_ && geometry && stats.visibleObjects &&
            nav_.logDistance < node.frame.logRadius + .65 && (kind != Kind::Lattice || stats.atoms > 0) &&
            !visible_.contains(kind)) {
            if (geometry->seed != node.seed || geometry->address != node.address ||
                geometry->descriptor != descriptorFingerprint(node))
                throw std::runtime_error("Automatic flight displayed a different procedural object");
            visible_.insert(kind);
            std::cout << "AUTO_VISIBLE " << kindName(kind) << " " << nav_.anchor.str()
                      << " log=" << nav_.logDistance << " draws=" << stats.drawCalls
                      << " atoms=" << stats.atoms << " mode=" << renderModeName(renderer.mode()) << '\n';
            if (!captures.empty())
                renderer.screenshot((std::filesystem::path(captures) /
                                     ("flight-" + std::to_string(static_cast<unsigned>(kind)) + ".ppm"))
                                        .string());
        }
        if (!returning_ && kind == Kind::Lattice && visible_.contains(kind) &&
            nav_.logDistance < node.frame.logRadius + .5)
            returning_ = true;
        if (returning_ && entries_.empty() && kind == Kind::Cosmic) {
            if (visible_.size() != 9 || inward_ != 8 || outward_ != 8)
                throw std::runtime_error("Native automatic flight skipped a scale or failed its return");
            finished_ = true;
            std::cout << "PASS: native automatic flight rendered all nine scales and returned; "
                         "8 inward / 8 outward reference changes, no focus or wheel commands.\n";
        }
    }
    bool finished() const {
        return finished_;
    }

  private:
    struct Entry {
        Address address;
        glm::dvec3 localPoint;
    };
    const Universe& universe_;
    Navigation& nav_;
    std::vector<Entry> entries_;
    std::set<Kind> visible_;
    unsigned inward_ = 0, outward_ = 0;
    bool returning_ = false, finished_ = false;
};
} // namespace infinite
