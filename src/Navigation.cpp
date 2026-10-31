#include "infinite/Navigation.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace infinite {
Navigation::Navigation(const Universe& u) : universe_(u) {
    reset();
}
Navigation::View Navigation::view() const {
    return {anchor, selected, logDistance, orientation, offset};
}
void Navigation::remember() {
    views_.insert_or_assign(anchor, view());
    // Breadcrumbs on the active ancestry are always retained.
    while (views_.size() > 128) {
        auto it = std::find_if(views_.begin(), views_.end(),
                               [&](const auto& v) { return !v.first.ancestorOf(anchor); });
        if (it == views_.end())
            break;
        views_.erase(it);
    }
}
void Navigation::reset() {
    views_.clear();
    speedMultiplier_ = 1;
    anchor = Address{}.child(Kind::Galaxy, 0);
    logDistance = targetLogDistance = universe_.describe(anchor).frame.logRadius + std::log10(3.1);
    orientation = targetOrientation_ = glm::angleAxis(-.40, glm::dvec3(1, 0, 0));
    offset = targetOffset_ = {};
    transitioning = false;
    motion_ = Motion::Focus;
    selectDefault();
}
void Navigation::selectDefault() {
    auto remembered = views_.find(anchor);
    if (remembered != views_.end())
        selected = remembered->second.selected;
    else {
        auto children = universe_.children(universe_.describe(anchor), 1);
        selected = children.empty() ? std::optional<Address>{} : children.front();
    }
    explicitSelection_ = false;
}
void Navigation::select(const Address& a) {
    if (a == anchor)
        throw std::invalid_argument("Selection must be another object");
    universe_.describe(a);
    selected = a;
    explicitSelection_ = true;
}
void Navigation::suggestSelection(const Address& address) {
    if (!explicitSelection_ && address != anchor)
        selected = address;
}
void Navigation::rebase(const Address& a) {
    // A pure reference-frame change: physical camera position and orientation
    // are unchanged until update() advances the requested motion.
    auto pose = universe_.relative(anchor, a, logDistance);
    offset = pose.position + pose.orientation * offset;
    orientation = glm::normalize(pose.orientation * orientation);
    anchor = a;
    selectDefault();
}
void Navigation::focus() {
    if (selected)
        focus(*selected);
}
void Navigation::focus(Address a) {
    if (a == anchor)
        return;
    const auto destination = universe_.describe(a);
    const auto previous = anchor;
    // Remember the actual selected branch, including nonzero child identifiers.
    if (anchor.ancestorOf(a) && a.path.size() > anchor.path.size())
        selected = anchor.child(a.path[anchor.path.size()].kind, a.path[anchor.path.size()].id);
    remember();
    rebase(a);
    if (auto saved = views_.find(a); saved != views_.end()) {
        targetLogDistance = saved->second.logDistance;
        targetOrientation_ = saved->second.orientation;
        targetOffset_ = saved->second.offset;
    } else {
        targetLogDistance = destination.frame.logRadius + std::log10(3.1);
        targetOrientation_ =
            anchor.kind() >= Kind::Surface ? glm::angleAxis(-.45, glm::dvec3(1, 0, 0)) : orientation;
        targetOffset_ = {};
    }
    if (a.ancestorOf(previous))
        selected = a.child(previous.path[a.path.size()].kind, previous.path[a.path.size()].id);
    motion_ = Motion::Focus;
    focusStartLog_ = logDistance;
    focusStartOffset_ = offset;
    focusStartOrientation_ = orientation;
    focusProgress_ = 0;
    transitioning = true;
}
void Navigation::parent() {
    if (!anchor.path.empty())
        focus(anchor.parent());
}
void Navigation::changeFrame(const Address& address) {
    if (address == anchor)
        return;
    universe_.describe(address);
    const auto previous = anchor;
    if (anchor.ancestorOf(address))
        selected = anchor.child(address.path[anchor.path.size()].kind, address.path[anchor.path.size()].id);
    remember();
    rebase(address);
    if (address.ancestorOf(previous))
        selected =
            address.child(previous.path[address.path.size()].kind, previous.path[address.path.size()].id);
    takeControl();
}
void Navigation::setRenderScale(double logMetres) {
    if (!std::isfinite(logMetres))
        return;
    logMetres = std::clamp(logMetres, -10.5, 26.8);
    const auto eye = cameraPosition() * std::pow(10.0, logDistance - logMetres);
    logDistance = logMetres;
    offset = eye - orientation * glm::dvec3(0, 0, 1);
    takeControl();
}
void Navigation::stopMotion() {
    takeControl();
}
void Navigation::zoom(double wheel) {
    if (!std::isfinite(wheel) || wheel == 0)
        return;
    auto node = universe_.describe(anchor);
    if (wheel > 0 && selected && selected->parent() == anchor &&
        (explicitSelection_ || logDistance < node.frame.logRadius + 1.1)) {
        auto destination = *selected;
        remember();
        rebase(destination);
    }
    if (!transitioning)
        targetLogDistance = logDistance;
    targetLogDistance = std::clamp(targetLogDistance - wheel * .23, -10.5, 26.8);
    targetOrientation_ = orientation;
    motion_ = Motion::Zoom;
    transitioning = true;
}
void Navigation::update(double dt) {
    if (!transitioning || !std::isfinite(dt) || dt <= 0)
        return;
    double t = 1 - std::exp(-std::min(dt, .1) * 5), oldLog = logDistance;
    logDistance = glm::mix(logDistance, targetLogDistance, t);
    if (motion_ == Motion::Zoom) {
        // Pan/pivot is a physical displacement. Changing render units must not
        // pull it towards a new origin or introduce a translation on rebasing.
        offset *= std::pow(10.0, oldLog - logDistance);
        if (std::abs(logDistance - targetLogDistance) < .0003) {
            offset *= std::pow(10.0, logDistance - targetLogDistance);
            logDistance = targetLogDistance;
            transitioning = false;
        }
        // Promote only when the parent fits in a well-conditioned local frame,
        // never while an explicit inward focus is crossing a large scale gap.
        while (!anchor.path.empty() && targetLogDistance > oldLog &&
               logDistance > universe_.describe(anchor.parent()).frame.logRadius - .4) {
            auto child = anchor;
            rebase(anchor.parent());
            selected = child;
        }
    } else {
        focusProgress_ = glm::mix(focusProgress_, 1.0, t);
        logDistance = glm::mix(focusStartLog_, targetLogDistance, focusProgress_);
        double pan = focusProgress_;
        if (targetLogDistance > focusStartLog_) {
            // First reveal the parent around the departing child. Recentring a
            // macro-scale translation while still at atomic render units would
            // send the camera far beyond the parent before returning.
            pan =
                smoothstep(std::max(focusStartLog_, targetLogDistance - 1.0), targetLogDistance, logDistance);
            offset = focusStartOffset_ * std::pow(10.0, focusStartLog_ - logDistance) * (1 - pan) +
                     targetOffset_ * std::pow(10.0, targetLogDistance - logDistance) * pan;
        } else
            offset = glm::mix(focusStartOffset_, targetOffset_, pan);
        orientation = glm::normalize(glm::slerp(focusStartOrientation_, targetOrientation_, pan));
        double angle = 1 - std::abs(glm::dot(orientation, targetOrientation_));
        if (std::abs(logDistance - targetLogDistance) < .0003 &&
            glm::length(offset - targetOffset_) < .0003 && angle < 1e-10) {
            logDistance = targetLogDistance;
            offset = targetOffset_;
            orientation = targetOrientation_;
            transitioning = false;
        }
    }
}
void Navigation::rotate(double dx, double dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0 && dy == 0))
        return;
    // Look from the eye, rather than orbiting the current object's origin.
    // Keep the existing pivot/offset representation for zoom and saved views.
    const auto eye = cameraPosition();
    orientation = glm::normalize(orientation * glm::angleAxis(-dx * .002, glm::dvec3(0, 1, 0)) *
                                 glm::angleAxis(-dy * .002, glm::dvec3(1, 0, 0)));
    offset = eye - orientation * glm::dvec3(0, 0, 1);
    takeControl();
}
void Navigation::takeControl() {
    transitioning = false;
    targetLogDistance = logDistance;
    targetOrientation_ = orientation;
    targetOffset_ = offset;
}
void Navigation::move(glm::dvec3 axes, double dt, bool fast, bool precise) {
    const auto length = glm::length(axes);
    if (!std::isfinite(length) || length == 0 || !std::isfinite(dt) || dt <= 0)
        return;
    // Render units track the current physical scale. Cap long pauses and
    // normalize diagonals so frame stalls and extra keys cannot cause jumps.
    const double speed = (fast ? 2.5 : .32) * speedMultiplier_ * (precise ? .125 : 1);
    offset += orientation * (axes / length) * std::min(dt, .1) * speed;
    takeControl();
}
void Navigation::adjustSpeed(double steps) {
    if (std::isfinite(steps))
        speedMultiplier_ =
            std::clamp(speedMultiplier_ * std::exp2(std::clamp(steps, -12., 12.)), 1. / 64, 256.);
}
std::string Navigation::serialize() const {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::setprecision(17);
    auto write = [&](const View& v) {
        s << v.address.str() << ' ' << v.logDistance << ' ' << v.orientation.w << ' ' << v.orientation.x
          << ' ' << v.orientation.y << ' ' << v.orientation.z << ' ' << v.offset.x << ' ' << v.offset.y << ' '
          << v.offset.z << ' ' << (v.selected ? v.selected->str() : "-") << '\n';
    };
    s << "INFINITE_NAV_2\n";
    write(view());
    s << views_.size() << '\n';
    for (const auto& [address, v] : views_)
        write(v);
    return s.str();
}
bool Navigation::restore(std::string_view text) {
    try {
        std::istringstream s{std::string(text)};
        s.imbue(std::locale::classic());
        auto read = [&](bool selection) {
            View v;
            std::string address, selectedAddress;
            s >> address >> v.logDistance >> v.orientation.w >> v.orientation.x >> v.orientation.y >>
                v.orientation.z >> v.offset.x >> v.offset.y >> v.offset.z;
            if (!s || !std::isfinite(v.logDistance) || v.logDistance < -10.5 || v.logDistance > 26.8 ||
                !std::isfinite(glm::length(v.orientation)) ||
                std::abs(glm::length(v.orientation) - 1) > .0001 || !std::isfinite(glm::length(v.offset)) ||
                glm::length(v.offset) > 1e12)
                throw std::invalid_argument("Invalid saved view");
            v.address = Address::parse(address);
            universe_.describe(v.address);
            if (selection) {
                s >> selectedAddress;
                if (!s)
                    throw std::invalid_argument("Missing selection");
                if (selectedAddress != "-") {
                    v.selected = Address::parse(selectedAddress);
                    if (v.selected == v.address)
                        throw std::invalid_argument("Invalid saved selection");
                    universe_.describe(*v.selected);
                }
            }
            return v;
        };
        std::string first;
        s >> first;
        bool modern = first == "INFINITE_NAV_2";
        if (!modern) {
            s.clear();
            s.seekg(0);
        }
        auto current = read(modern);
        std::map<Address, View> views;
        if (modern) {
            size_t count;
            s >> count;
            if (!s || count > 128)
                return false;
            for (size_t i = 0; i < count; ++i) {
                auto v = read(true);
                if (!views.emplace(v.address, v).second)
                    return false;
            }
        }
        s >> std::ws;
        if (!s.eof())
            return false;
        anchor = current.address;
        logDistance = targetLogDistance = current.logDistance;
        orientation = targetOrientation_ = current.orientation;
        offset = targetOffset_ = current.offset;
        views_ = std::move(views);
        selected = current.selected;
        if (!modern)
            selectDefault();
        explicitSelection_ = false;
        transitioning = false;
        motion_ = Motion::Focus;
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace infinite
