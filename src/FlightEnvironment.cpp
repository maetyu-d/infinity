#include "infinite/FlightEnvironment.hpp"
#include "infinite/Flight.hpp"
#include "infinite/Planetology.hpp"
#include "infinite/ShipDynamics.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace infinite {
struct FlightEnvironment::Impl {
    struct Body {
        Address address;
        double radius = 0, mu = 0;
        glm::dvec3 centre{};
        glm::dquat orientation{1, 0, 0, 0};
        PlanetClimate climate;
        bool gas = false;
        unsigned jets = 0;
    };
    std::map<Address, Body> bodies;
    std::vector<Address> systems;
    std::optional<Address> anchor;
    std::map<Address, const void*> catalogues;
    bool dirty = true;
};
FlightEnvironment::FlightEnvironment() : impl_(std::make_shared<Impl>()) {}
void FlightEnvironment::invalidate() {
    impl_->dirty = true;
}
bool FlightEnvironment::matches(const Address& a) const {
    return !impl_->dirty && impl_->anchor && *impl_->anchor == a;
}
size_t FlightEnvironment::sourceCount() const {
    return impl_->bodies.size();
}
void FlightEnvironment::refresh(const Universe& u, const Navigation& nav, FlightController& spatial) {
    auto& p = *impl_;
    auto add = [&](const Node& n) {
        if (n.address.kind() != Kind::System && n.address.kind() != Kind::Planet)
            return;
        if (p.bodies.contains(n.address))
            return;
        Impl::Body b;
        b.address = n.address;
        const bool star = n.address.kind() == Kind::System;
        b.radius = star ? n.stellarRadiusMetres : std::pow(10., n.frame.logRadius);
        b.mu = star ? 1.32712440018e20 * n.massSolar
                    : 6.67430e-11 * 4.188790204786391 * planetDensity(n) * std::pow(b.radius, 3);
        if (!star) {
            b.climate = planetClimate(n);
            b.gas = !hasSolidSurface(n);
            if (b.gas) {
                const auto traits = planetTraits(n);
                b.jets = traits.jetBands;
                b.climate.radiusMetres = b.radius;
                b.climate.axis = traits.axis;
                b.climate.pressureBar = .1;
                b.climate.temperatureK = n.environment.temperature;
                b.climate.gravity = b.mu / (b.radius * b.radius);
                b.climate.scaleHeightMetres = std::clamp(
                    3614. * b.climate.temperatureK / std::max(.1, b.climate.gravity), 1000., 250000.);
            }
        }
        p.bodies.emplace(n.address, std::move(b));
        p.dirty = true;
    };
    auto a = nav.anchor;
    while (a.kind() > Kind::System)
        a = a.parent();
    if (a.kind() == Kind::System) {
        if (std::find(p.systems.begin(), p.systems.end(), a) == p.systems.end())
            p.systems.push_back(a);
        add(u.describe(a));
        // The current ancestor is immediately available, even while the full
        // immutable catalogue is being prepared on an existing region worker.
        for (const auto& n : u.lineage(nav.anchor))
            add(n);
        if (nav.anchor.kind() >= Kind::Planet) {
            auto planet = nav.anchor;
            while (planet.kind() > Kind::Planet)
                planet = planet.parent();
            if (isMoon(planet))
                add(u.describe(a.child(Kind::Planet, moonPrimary(planet.id()))));
        }
        if (auto catalogue = spatial.catalogue(u.describe(a))) {
            if (p.catalogues[a] != catalogue.get()) {
                for (const auto& n : *catalogue)
                    add(n);
                p.catalogues[a] = catalogue.get();
            }
        }
    } else if (a.kind() == Kind::Galaxy) {
        // Include the nearest addressed star in an already prepared galaxy
        // catalogue, and an explicitly selected destination beyond that list.
        if (auto catalogue = spatial.catalogue(u.describe(a))) {
            const Node* nearest = nullptr;
            double best = 1e300;
            const auto galaxy = u.describe(a);
            const double scale = std::pow(10., galaxy.frame.logRadius - nav.logDistance);
            for (const auto& n : *catalogue) {
                const double d = glm::length(n.frame.position * scale - nav.cameraPosition());
                if (d < best) {
                    best = d;
                    nearest = &n;
                }
            }
            if (nearest) {
                add(*nearest);
                if (std::find(p.systems.begin(), p.systems.end(), nearest->address) == p.systems.end())
                    p.systems.push_back(nearest->address);
            }
        }
        if (nav.selected && nav.selected->kind() == Kind::System && nav.selected->parent() == a) {
            add(u.describe(*nav.selected));
            if (std::find(p.systems.begin(), p.systems.end(), *nav.selected) == p.systems.end())
                p.systems.push_back(*nav.selected);
        }
    }
    while (p.systems.size() > 3) {
        const auto oldest = p.systems.front();
        p.systems.erase(p.systems.begin());
        p.catalogues.erase(oldest);
        std::erase_if(p.bodies, [&](const auto& item) { return oldest.ancestorOf(item.first); });
        p.dirty = true;
    }
    if (p.dirty || !p.anchor || *p.anchor != nav.anchor) {
        for (auto& [address, b] : p.bodies) {
            const auto pose = u.relative(address, nav.anchor, 0);
            b.centre = pose.position;
            b.orientation = pose.orientation;
        }
        p.anchor = nav.anchor;
        p.dirty = false;
    }
}
ShipEnvironmentSample FlightEnvironment::sample(glm::dvec3 eye, double seconds) const {
    ShipEnvironmentSample result;
    for (const auto& [_, b] : impl_->bodies) {
        const auto delta = b.centre - eye;
        result.gravity += pointGravity(delta, b.mu, b.radius);
        const auto& c = b.climate;
        if (c.pressureBar <= 0)
            continue;
        const double distance = glm::length(delta), altitude = distance - b.radius;
        if (distance < 1e-20 || altitude > c.scaleHeightMetres * 24)
            continue;
        const auto direction = glm::inverse(b.orientation) * (-delta / distance);
        double pressure, temperature;
        glm::dvec3 wind;
        if (b.gas) {
            pressure = c.pressureBar *
                       std::exp(-std::max(-c.scaleHeightMetres * 5., altitude) / c.scaleHeightMetres);
            temperature = c.temperatureK;
            const auto east = glm::cross(c.axis, direction);
            wind = glm::length(east) > 1e-12
                       ? glm::normalize(east) *
                             (60. * std::sin(glm::dot(direction, c.axis) * std::max(4u, b.jets)))
                       : glm::dvec3(0);
        } else {
            const auto weather = sampleWeather(c, direction, std::max(0., altitude), seconds);
            pressure = weather.pressureBar;
            temperature = weather.temperatureK;
            wind = weather.wind;
        }
        const double density = pressure * 1e5 / ((b.gas ? 3614. : 287.) * std::max(30., temperature));
        if (density > result.airDensity) {
            result.airDensity = density;
            result.pressureBar = pressure;
            result.wind = b.orientation * wind;
        }
    }
    return result;
}
} // namespace infinite
