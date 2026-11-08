#include "infinity/Docking.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
glm::dvec3 limited(glm::dvec3 v, double cap) {
    const double length = glm::length(v);
    return length > cap ? v * (cap / length) : v;
}
glm::dvec3 direction(glm::dvec3 v, glm::dvec3 fallback = {0, 0, -1}) {
    return glm::length(v) > 1e-12 ? glm::normalize(v) : fallback;
}
} // namespace
std::optional<DockingBerth> dockingBerth(const ActivityBody& b) {
    if (b.kind != ActivityKind::Harbour || b.disabled || !std::isfinite(b.hull) || b.hull <= 0 ||
        !std::isfinite(b.radius) || b.radius < 30 || !std::isfinite(glm::length(b.position)) ||
        !std::isfinite(glm::length(b.velocity)) || !std::isfinite(glm::length(b.orientation)) ||
        std::abs(glm::length(b.orientation) - 1) > 1e-6 || b.systems[size_t(ShipSystem::Hull)] <= 0)
        return {};
    return DockingBerth{b.id, b.position + b.orientation * glm::dvec3(0, .18, 1.45) * b.radius, b.velocity,
                        b.orientation, b.radius};
}
std::string_view dockingPhaseName(DockingPhase p) {
    switch (p) {
    case DockingPhase::Arrival:
        return "ARRIVAL / ACQUIRING GATE";
    case DockingPhase::Align:
        return "ALIGN / LEVEL WITH THE BERTH";
    case DockingPhase::Glide:
        return "GLIDE / FOLLOW THE LIGHTS";
    case DockingPhase::Capture:
        return "SOFT CAPTURE / HOLD STEADY";
    case DockingPhase::Docked:
        return "DOCKED / MAGNETIC CLAMPS SECURE";
    case DockingPhase::Departure:
        return "DEPARTURE / CLEARING THE LANE";
    default:
        return "BERTH / J DOCK";
    }
}
DockingReadout dockingReadout(const DockingBerth& b, glm::dvec3 eye, glm::dvec3 velocity,
                              glm::dquat attitude) {
    DockingReadout r;
    r.available = true;
    const auto inverse = glm::inverse(b.orientation);
    r.offset = inverse * (eye - b.metres);
    const auto relative = inverse * (velocity - b.velocity);
    r.range = glm::length(r.offset);
    r.closing = -relative.z;
    r.relativeSpeed = glm::length(relative);
    r.width = b.halfWidth();
    const auto forward = b.orientation * glm::dvec3(0, 0, -1), up = b.orientation * glm::dvec3(0, 1, 0);
    r.localForward = glm::inverse(attitude) * forward;
    r.localUp = glm::inverse(attitude) * up;
    r.heading = std::acos(std::clamp(-r.localForward.z, -1., 1.));
    r.roll = std::atan2(r.localUp.x, r.localUp.y);
    return r;
}
void DockingController::begin() {
    cancel();
    phase_ = DockingPhase::Arrival;
}
void DockingController::cancel() {
    phase_ = DockingPhase::None;
    dwell_ = 0;
    routeChecked_ = bypass_ = false;
}
void DockingController::beginFinal() {
    cancel();
    phase_ = DockingPhase::Capture;
}
void DockingController::depart() {
    phase_ = DockingPhase::Departure;
    dwell_ = 0;
}
bool DockingController::resumeClamp(const DockingReadout& r) {
    if (!(r.available && r.range >= 0 && r.range < .45 && r.relativeSpeed >= 0 && r.relativeSpeed < .18 &&
          r.heading >= 0 && r.heading < .035 && std::abs(r.roll) < .045))
        return false;
    phase_ = DockingPhase::Docked;
    dwell_ = 1.6;
    return true;
}
DockingGuidance DockingController::update(const DockingBerth& b, glm::dvec3 eye, glm::dvec3 velocity,
                                          glm::dquat attitude, double dt) {
    DockingGuidance g;
    g.point = b.metres;
    g.forward = b.orientation * glm::dvec3(0, 0, -1);
    g.up = b.orientation * glm::dvec3(0, 1, 0);
    g.velocity = b.velocity;
    if (phase_ == DockingPhase::None || !std::isfinite(dt) || dt <= 0 || dt > 1)
        return g;
    const auto r = dockingReadout(b, eye, velocity, attitude);
    glm::dvec3 goal(0, 0, b.laneLength());
    if (phase_ == DockingPhase::Arrival) {
        if (!routeChecked_) {
            // A rear-side arrival goes around the retained station, never through it.
            const glm::dvec3 centre(0, -.18 * b.radius, -1.45 * b.radius);
            const auto segment = goal - r.offset;
            const auto nearest = r.offset + segment * std::clamp(glm::dot(centre - r.offset, segment) /
                                                                     std::max(1., glm::dot(segment, segment)),
                                                                 0., 1.);
            bypass_ = glm::length(nearest - centre) < b.radius * 1.9;
            bypassPoint_ = centre + glm::dvec3(r.offset.x < 0 ? -4. : 4., 2., 2.) * b.radius;
            routeChecked_ = true;
        }
        if (bypass_) {
            goal = bypassPoint_;
            if (glm::length(goal - r.offset) < std::max(40., b.radius * .25)) {
                bypass_ = false;
                goal = {0, 0, b.laneLength()};
            }
        }
        const glm::dvec3 centre(0, -.18 * b.radius, -1.45 * b.radius);
        if (glm::length(r.offset - centre) < b.radius * 2.3)
            goal = centre + direction(r.offset - centre, {0, 0, 1}) * b.radius * 3.2;
        if (!bypass_ && glm::length(goal - r.offset) < 25 && r.relativeSpeed < 6)
            phase_ = DockingPhase::Align;
        else
            g.forward = direction(b.orientation * (goal - r.offset));
    }
    if (phase_ == DockingPhase::Align && glm::length(goal - r.offset) < 3 && r.relativeSpeed < .7 &&
        r.heading < .045 && std::abs(r.roll) < .06)
        phase_ = DockingPhase::Glide;
    if (phase_ == DockingPhase::Glide && r.range < 28)
        phase_ = DockingPhase::Capture;
    if (phase_ == DockingPhase::Capture) {
        const bool settled =
            r.range < .45 && r.relativeSpeed < .18 && r.heading < .035 && std::abs(r.roll) < .045;
        dwell_ = settled ? std::min(1.6, dwell_ + dt) : 0;
        if (dwell_ >= 1.6)
            phase_ = DockingPhase::Docked;
    }
    if (phase_ == DockingPhase::Docked) {
        g.clamp = true;
        return g;
    }
    if (phase_ == DockingPhase::Glide || phase_ == DockingPhase::Capture) {
        goal = {};
        const bool final = phase_ == DockingPhase::Capture;
        const auto lateral = limited(glm::dvec3(-r.offset.x, -r.offset.y, 0) * .55, final ? .8 : 5.);
        double closing = std::min(final ? 1.8 : 24., std::max(0., r.offset.z) * (final ? .45 : .20));
        if (r.heading > .12 || std::abs(r.roll) > .18 ||
            glm::length(glm::dvec2(r.offset)) > b.halfWidth() * .8)
            closing = 0;
        g.velocity +=
            b.orientation *
            (lateral + glm::dvec3(0, 0, r.offset.z < 0 ? std::min(1., -r.offset.z * .4) : -closing));
    } else if (phase_ == DockingPhase::Departure) {
        g.forward = b.orientation * glm::dvec3(0, 0, 1);
        if (r.offset.z > b.laneLength() + 30) {
            g.finished = true;
            cancel();
            return g;
        }
        goal.z += 100;
        const double alignment = glm::dot(attitude * glm::dvec3(0, 0, -1), g.forward);
        const auto lateral = limited(glm::dvec3(-r.offset.x, -r.offset.y, 0) * .6, 3.);
        g.velocity +=
            b.orientation *
            (lateral +
             glm::dvec3(0, 0, alignment > .985 ? std::min(32., 4. + std::max(0., r.offset.z) * .10) : 0.));
    } else {
        const auto delta = goal - r.offset;
        g.velocity += b.orientation * limited(delta * (phase_ == DockingPhase::Align ? .5 : .23),
                                              phase_ == DockingPhase::Align ? 6. : 150.);
    }
    g.point = b.metres + b.orientation * goal;
    return g;
}
Mesh dockingLane(const DockingBerth& b, double seconds, DockingPhase phase) {
    Mesh m;
    const bool departure = phase == DockingPhase::Departure;
    const glm::vec3 cyan(.24, 1.05, 1.25), violet(.74, .30, 1.1), gold(1.2, .65, .20);
    const auto color = phase == DockingPhase::Docked ? glm::vec3(.30, 1.2, .72) : departure ? gold : cyan;
    const auto line = [&](glm::dvec3 a, glm::dvec3 c, glm::vec3 ink) {
        const auto first = uint32_t(m.vertices.size());
        for (const auto p : {a, c})
            m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(ink)});
        m.indices.insert(m.indices.end(), {first, first + 1});
    };
    const double width = b.halfWidth(), length = b.laneLength();
    for (unsigned i = 0; i < 7; ++i) {
        const double z = length * i / 6., taper = .65 + .35 * i / 6.;
        const double x = width * taper, y = width * .52 * taper, corner = width * .25;
        for (int sx : {-1, 1})
            for (int sy : {-1, 1}) {
                const glm::dvec3 p(sx * x, sy * y, z);
                line(p, p + glm::dvec3(-sx * corner, 0, 0), color * float(.45 + .3 * i / 6.));
                line(p, p + glm::dvec3(0, -sy * corner * .5, 0), sy > 0 ? violet * .8f : color);
            }
    }
    // Traveling chevrons move toward the berth; no random pulses or temporal shimmer.
    const double travel = seconds * (departure ? -38 : 38);
    for (int i = 0; i < 12; ++i) {
        const double z = std::fmod(i * length / 12. - travel, length);
        const double pz = z < 0 ? z + length : z;
        for (int side : {-1, 1}) {
            const glm::dvec3 p(side * width, -width * .52, pz);
            line(p, p + glm::dvec3(-side * width * .16, 0, departure ? -12 : 12), color);
        }
    }
    for (int side : {-1, 1}) {
        line({side * width, -width * .52, 0}, {side * width, -width * .52, length}, color * .24f);
        line({-width * .5, side * width * .26, 0}, {width * .5, side * width * .26, 0}, color);
    }
    return m;
}
} // namespace infinity
