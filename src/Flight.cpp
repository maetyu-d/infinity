#include "infinite/Flight.hpp"
#include "infinite/Streaming.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace infinite {
Address FlightController::surfaceAt(const Address& planet, glm::dvec3 p) {
    if (planet.kind() != Kind::Planet || !std::isfinite(glm::length(p)) || glm::length(p) == 0)
        throw std::invalid_argument("Surface lookup requires a planet and a finite nonzero direction");
    return planet.child(Kind::Surface, encodeTile(tileAt(p, 8)));
}
void FlightController::changeFrame(const Address& address) {
    nav_.changeFrame(address);
    ++frameChanges_;
}
void FlightController::update(glm::dvec3 axes, double dt, bool fast, bool precise,
                              const std::shared_ptr<const Geometry>& visibleGeometry) {
    if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(glm::length(axes)))
        return;
    dt = std::min(dt, .1);
    if (glm::length(axes) > 0)
        holdForFlight_ = false;
    if (nav_.transitioning) {
        if (glm::length(axes) == 0) {
            holdForFlight_ = true;
            return; // Explicit focus and wheel zoom retain their existing behaviour.
        }
        nav_.stopMotion();
    }
    if (holdForFlight_)
        return; // A manual parent/focus view remains usable until flight resumes.
    if (nav_.anchor.kind() == Kind::Cosmic) {
        const auto point = nav_.cameraPosition() * std::pow(10., nav_.logDistance - rootCellLogMetres);
        const auto [root, local] = canonicalRoot(nav_.anchor, point);
        if (root != nav_.anchor)
            changeFrame(root);
        for (const auto& address : rootNeighborhood(nav_.anchor, local, true))
            cache_.request(universe_.describe(address), 0, 8);
    }
    auto lineage = universe_.lineage(nav_.anchor);
    auto node = lineage.back();
    auto eye = nav_.cameraPosition();
    double radius = std::pow(10., node.frame.logRadius - nav_.logDistance);
    if (visibleGeometry && visibleGeometry->address == nav_.anchor && nav_.anchor.kind() == Kind::Galaxy)
        galaxySamples_ = visibleGeometry;
    if (galaxySamples_ && !galaxySamples_->address.ancestorOf(nav_.anchor))
        galaxySamples_.reset();
    // Early precision frames cover much more than the small physical body.
    // Crossing toward a nearer sibling must still work inside that region.
    if (lineage.size() > 1 && node.address.kind() != Kind::Surface && glm::length(eye) > radius * 4) {
        const auto& parent = lineage[lineage.size() - 2];
        const double parentRadius = std::pow(10., parent.frame.logRadius - nav_.logDistance);
        const auto rotation = glm::inverse(node.frame.orientation);
        double distance = glm::length(eye) * .6;
        std::optional<Address> sibling;
        auto considerSibling = [&](const Address& address, glm::dvec3 point) {
            if (address == node.address)
                return;
            const auto local = rotation * ((point - node.frame.position) * parentRadius);
            const auto separation = glm::length(local - eye);
            if (separation < distance) {
                distance = separation;
                sibling = address;
            }
        };
        if (auto catalog = cache_.catalog(parent))
            for (const auto& child : *catalog)
                considerSibling(child.address, child.frame.position);
        if (node.address.kind() == Kind::System && galaxySamples_ &&
            galaxySamples_->address == parent.address) {
            uint64_t closest = UINT64_MAX;
            for (const auto& particle : galaxySamples_->particles) {
                if (particle.objectId == UINT64_MAX || particle.objectId == node.address.id())
                    continue;
                const auto local = rotation * ((particle.position - node.frame.position) * parentRadius);
                const double separation = glm::length(local - eye);
                if (separation < distance) {
                    distance = separation;
                    closest = particle.objectId;
                }
            }
            if (closest != UINT64_MAX)
                sibling = parent.address.child(Kind::System, closest);
        }
        if (sibling) {
            const auto next = universe_.describe(*sibling);
            if (cache_.request(next, 0, 0)) {
                changeFrame(*sibling);
                node = next;
                eye = nav_.cameraPosition();
                radius = std::pow(10., node.frame.logRadius - nav_.logDistance);
            }
        }
    }
    // Wider exit regions prevent chatter. Terrain patches have tighter bounds
    // so walking sideways streams the next canonical tile instead of empty space.
    if (lineage.size() > 1) {
        const auto& parent = lineage[lineage.size() - 2];
        double parentRadius = std::pow(10., parent.frame.logRadius - nav_.logDistance);
        double exit = node.address.kind() == Kind::Surface || node.address.kind() == Kind::Mountain
                          ? radius * 3
                          : std::max(radius * 8, parentRadius * .16);
        if (glm::length(eye) > exit) {
            changeFrame(parent.address);
            node = parent;
            eye = nav_.cameraPosition();
            radius = parentRadius;
        }
    }
    std::optional<Node> nearest;
    double nearestDistance = std::numeric_limits<double>::infinity();
    auto consider = [&](const Node& child) {
        const auto delta = child.frame.position * radius - eye;
        const double distance = glm::length(delta);
        if (distance < nearestDistance) {
            nearest = child;
            nearestDistance = distance;
        }
    };
    // Catalogs are asynchronous and shared with rendering; never wait here.
    if (auto catalog = cache_.catalog(node))
        for (const auto& child : *catalog)
            consider(child);
    if (nav_.selected && nav_.selected->parent() == node.address)
        consider(universe_.describe(*nav_.selected));
    // Every displayed star carries its real system ID, including those beyond
    // the small locator catalog. Inspect existing particles without copying them.
    if (node.address.kind() == Kind::Galaxy && visibleGeometry && visibleGeometry->address == node.address) {
        uint64_t closest = UINT64_MAX;
        for (const auto& particle : visibleGeometry->particles) {
            if (particle.objectId == UINT64_MAX)
                continue;
            const double distance = glm::length(particle.position * radius - eye);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                closest = particle.objectId;
            }
        }
        if (closest != UINT64_MAX)
            nearest = universe_.describe(node.address.child(Kind::System, closest));
    }
    if (node.address.kind() == Kind::Planet && glm::length(eye) < radius * 1.6 && glm::length(eye) > 0)
        consider(universe_.describe(surfaceAt(node.address, eye)));
    // Re-centre a terrain sampling frame laterally without re-rolling geology.
    if (node.address.kind() == Kind::Surface && glm::length(glm::dvec2(eye.x, eye.z)) > radius * .9) {
        const auto planet = universe_.describe(node.address.parent());
        const auto point =
            universe_.pointRelative(node.address, eye / radius, planet.address, planet.frame.logRadius);
        const auto next = surfaceAt(planet.address, point);
        if (next != node.address) {
            const auto tile = universe_.describe(next);
            const auto pose = universe_.relative(next, nav_.anchor, nav_.logDistance);
            if (glm::length(pose.position - eye) < glm::length(eye) * .7 && cache_.request(tile, 0, 0)) {
                changeFrame(next);
                target_.reset();
                return; // Next frame obtains the new tile's own catalog.
            }
        }
    }
    target_ = nearest ? std::optional<Address>(nearest->address) : std::nullopt;
    double clearance = nearest ? nearestDistance : std::max(glm::length(eye), radius * .2);
    if (node.address.kind() == Kind::Planet && glm::length(eye) > 0) {
        const auto direction = glm::normalize(eye);
        const double surface =
            radius * (1 + universe_.terrainHeight(node, direction) / std::pow(10., node.frame.logRadius));
        // Slow for the actual geological surface, while retaining a floor set
        // by the nearby sampling tile, rather than descending into empty atoms.
        clearance = std::min(clearance, std::max(std::abs(glm::length(eye) - surface), clearance * .1));
    }
    clearanceMetres_ = std::max(1e-12, clearance * std::pow(10., nav_.logDistance));
    const double desiredLog =
        std::clamp(nav_.logDistance + std::log10(std::max(clearance, 1e-100)), -10.5, 26.8);
    const double oldLog = nav_.logDistance;
    const double newLog = oldLog + std::clamp(desiredLog - oldLog, -4 * dt, 4 * dt);
    nav_.setRenderScale(newLog);
    const double factor = std::pow(10., oldLog - newLog);
    clearance *= factor;
    nearestDistance *= factor;
    radius *= factor;
    if (nearest) {
        if (!nav_.selected || *nav_.selected != nearest->address)
            nav_.suggestSelection(nearest->address);
        const double childRadius = std::pow(10., nearest->frame.logRadius - newLog);
        const auto kind = nearest->address.kind();
        const double enter = kind == Kind::Surface || kind == Kind::Mountain
                                 ? childRadius * 2
                                 : std::max(childRadius * 4, radius * .08);
        if (nearestDistance < enter * 2) {
            auto ready = cache_.request(*nearest, 0, 0);
            cache_.catalog(*nearest); // Prefetch the next physical refinement.
            if (nearestDistance < enter && ready)
                changeFrame(nearest->address);
        }
    }
    // Substep by available clearance. Even maximum boost cannot tunnel past
    // an entire planet/mineral before the next proximity and frame query.
    const double speed = (fast ? 2.5 : .32) * nav_.speedMultiplier() * (precise ? .125 : 1);
    const double minimum = std::pow(10., -10.5 - nav_.logDistance);
    const double step = std::min(dt, .2 * std::max(clearance, minimum) / speed);
    nav_.move(axes, step, fast, precise);
}
} // namespace infinite
