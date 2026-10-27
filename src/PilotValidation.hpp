#pragma once
#include "infinite/Piloting.hpp"
#include "infinite/Renderer.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace infinite {
class PilotValidation {
  public:
    PilotValidation(const Universe& u, Navigation& n, Pilot& p) : u_(u), n_(n), p_(p) {
        auto system = u_.children(u_.describe(n_.anchor), 1).front();
        planet_ = u_.children(u_.describe(system), 1).front();
        for (const auto& a : u_.children(u_.describe(system))) {
            auto body = u_.describe(a);
            if (body.environment.temperature > 200 && body.environment.temperature < 600 &&
                body.environment.waterActivity > .15) {
                planet_ = a;
                break;
            }
        }
        for (uint64_t star = 0; star < 64; star++) {
            bool found = false;
            system = n_.anchor.child(Kind::System, star);
            for (const auto& a : u_.children(u_.describe(system))) {
                const auto body = u_.describe(a);
                if (body.environment.temperature > 220 && body.environment.temperature < 500 &&
                    body.environment.waterActivity > .15) {
                    planet_ = a;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        descriptor_ = descriptorFingerprint(u_.describe(planet_));
        std::cout << "PILOT_FIXTURE " << planet_.str()
                  << " temperature=" << u_.describe(planet_).environment.temperature << '\n';
        place(true);
    }
    void advance(Renderer& renderer) {
        ++age_;
        if (stage_ == 0)
            return;
        if (stage_ == 1)
            p_.update({}, 1. / 30, renderer.submittedGeometry(n_.anchor));
        if (stage_ == 2 || stage_ == 6)
            p_.update({}, 1. / 30, renderer.submittedGeometry(n_.anchor));
        if (stage_ == 3 && !n_.transitioning) {
            if (!p_.ship().shrinking)
                p_.toggleShrink();
            p_.update({}, 1. / 30, renderer.submittedGeometry(n_.anchor));
        }
    }
    void observe(const FrameStats& stats, Renderer& renderer, const std::string& directory) {
        if (descriptorFingerprint(u_.describe(planet_)) != descriptor_)
            throw std::runtime_error("Piloting altered planet identity");
        auto capture = [&](const char* name) {
            if (!directory.empty())
                renderer.screenshot(
                    (std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
        };
        if (stage_ == 0 && age_ >= 90 && stats.visibleObjects && stats.uploadDeferred == 0 &&
            stats.celestialContacts > 0 && stats.skyGalaxies > 0) {
            auto geometry = renderer.submittedGeometry(n_.anchor);
            if (!geometry)
                return;
            const auto hash = geometryFingerprint(*geometry);
            if (mode_ == 0) {
                camera_ = n_.serialize();
                geometry_ = hash;
            } else if (n_.serialize() != camera_ || hash != geometry_)
                throw std::runtime_error("Cockpit mode changed scene geometry or camera");
            if (!queued_) {
                capture(mode_ == 0 ? "orbit-original" : mode_ == 1 ? "orbit-hyperion" : "orbit-hybrid");
                queued_ = true;
                hold_ = 4;
                return;
            }
            if (--hold_ > 0)
                return;
            queued_ = false;
            if (++mode_ < 3) {
                renderer.setMode(static_cast<RenderMode>(mode_));
                age_ = 65;
                return;
            }
            std::cout << "PILOT_MODE_PROOF identical camera and planet geometry in all three cockpit modes / "
                         "sky stars="
                      << stats.skyStars << " background_galaxies=" << stats.skyGalaxies
                      << " atmosphere=" << stats.atmospheres << '\n';
            if (stats.skyGalaxies == 0)
                throw std::runtime_error("Extragalactic lights vanished from the outward-facing sky");
            renderer.setMode(RenderMode::Hyperion);
            const auto contacts = renderer.celestialContacts();
            auto target = std::find_if(contacts.begin(), contacts.end(), [&](const CelestialContact& c) {
                return c.address.kind() == Kind::Planet && c.address != planet_ && !c.occluded &&
                       c.distanceMetres > 10000 * std::pow(10., n_.logDistance);
            });
            if (target == contacts.end())
                throw std::runtime_error("Planetary cockpit lost the rest of its actual system");
            distantPlanet_ = target->address;
            const auto eye = n_.cameraPosition();
            const auto direction = n_.orientation * target->localDirection;
            const auto up = std::abs(direction.y) > .95 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
            n_.orientation = glm::quatLookAtRH(direction, up);
            n_.offset = eye - n_.orientation * glm::dvec3(0, 0, 1);
            n_.selected = distantPlanet_;
            p_.resetMotion();
            stage_ = 7;
            age_ = 0;
        } else if (stage_ == 7 && age_ == 30) {
            if (stats.celestialContacts < 6 || stats.celestialLights == 0 ||
                renderer.pick(720, 450) != distantPlanet_ || renderer.pick(1170, 217) != planet_.parent())
                throw std::runtime_error("Distant planet is not visible and selectable beyond the far plane");
            capture("system-contacts");
            std::cout << "PILOT_SYSTEM_CONTACTS count=" << stats.celestialContacts
                      << " lights=" << stats.celestialLights << " distant_planet_pick=PASS\n";
        } else if (stage_ == 7 && age_ == 40) {
            // Look toward the physical galaxy centre: the orbital comparison
            // deliberately faces a hemisphere where the disc is behind us.
            auto galaxy = planet_;
            while (galaxy.kind() > Kind::Galaxy)
                galaxy = galaxy.parent();
            const auto eye = n_.cameraPosition();
            const auto direction =
                glm::normalize(u_.relative(galaxy, n_.anchor, n_.logDistance).position - eye);
            const auto up = std::abs(direction.y) > .95 ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
            n_.orientation = glm::quatLookAtRH(direction, up);
            n_.offset = eye - n_.orientation * glm::dvec3(0, 0, 1);
            p_.resetMotion();
            stage_ = 5;
            age_ = 0;
        } else if (stage_ == 5 && age_ == 30) {
            if (stats.skyStars < 10)
                throw std::runtime_error("Addressed galactic stars disappeared from the cockpit sky");
            bool picked = false;
            if (auto sky = renderer.galaxySamples())
                for (const auto& star : sky->particles) {
                    if (star.objectId == UINT64_MAX)
                        continue;
                    const auto point = u_.pointRelative(
                        sky->address,
                        star.position *
                            std::pow(10., sky->logUnits - u_.describe(sky->address).frame.logRadius),
                        n_.anchor, n_.logDistance);
                    const auto local = glm::inverse(n_.orientation) * (point - n_.cameraPosition());
                    if (local.z >= 0)
                        continue;
                    const double x = 720 + local.x / -local.z * 450 / .7002075,
                                 y = 450 - local.y / -local.z * 450 / .7002075;
                    if (x > 150 && x < 1100 && y > 170 && y < 650 &&
                        renderer.pick(x, y) == sky->address.child(Kind::System, star.objectId)) {
                        picked = true;
                        break;
                    }
                }
            if (!picked)
                throw std::runtime_error("Addressed stellar background cannot be selected");
            capture("stellar-background");
            std::cout << "PILOT_STELLAR_SKY addressed stars=" << stats.skyStars << " sky_pick=PASS\n";
        } else if (stage_ == 5 && age_ == 40) {
            place(false);
            p_.land();
            stage_ = 1;
            age_ = 0;
        } else if (stage_ == 1) {
            if (age_ == 20)
                capture("landing-approach");
            if (p_.ship().landed) {
                if (std::abs(p_.instruments().altitude) > .15 || p_.ship().hull != 100)
                    throw std::runtime_error("Native touchdown was unsafe");
                const auto saved = p_.serialize();
                if (!p_.restore(saved) || !p_.ship().landed)
                    throw std::runtime_error("Native landed save failed");
                capture("landed");
                std::cout << "PILOT_TOUCHDOWN agl=" << p_.instruments().altitude << " hull=" << p_.ship().hull
                          << '\n';
                stage_ = 2;
                age_ = 0;
            }
        } else if (stage_ == 2) {
            if (age_ == 15) {
                const auto eye = n_.cameraPosition();
                p_.launch();
                if (n_.cameraPosition() != eye)
                    throw std::runtime_error("Native launch teleported");
            }
            if (age_ == 210) {
                if (p_.ship().landed || p_.instruments().altitude < 10)
                    throw std::runtime_error("Native launch did not clear terrain");
                capture("departure");
                std::cout << "PILOT_DEPARTURE agl=" << p_.instruments().altitude << '\n';
                auto a = FlightController::surfaceAt(planet_, direction_);
                while (a.kind() != Kind::Lattice)
                    a = u_.children(u_.describe(a), 1).front();
                nanoAddress_ = a;
                p_.toggleShrink();
                p_.adjustSize(-12);
                stage_ = 6;
                age_ = 0;
            }
        } else if (stage_ == 6) {
            if (!microCaptured_ && p_.instruments().hullMetres <= 1.2e-5) {
                capture("micro-shrink");
                microCaptured_ = true;
                std::cout << "PILOT_MICRO_SHIP hull_m=" << p_.instruments().hullMetres << '\n';
            }
            if (p_.ship().logSize <= -12) {
                n_.focus(nanoAddress_);
                p_.resetMotion();
                stage_ = 3;
                age_ = 0;
            }
        } else if (stage_ == 3 && age_ > 120 && !n_.transitioning && stats.atoms > 0) {
            if (p_.instruments().hullMetres > 1.21e-11)
                throw std::runtime_error("Atomic flight did not use the physically shrunken ship");
            capture("atomic-shrink");
            std::cout << "PASS: native cockpit mode identity, geological touchdown, persisted ship, thrust "
                         "launch and nano-size ship; atoms="
                      << stats.atoms << '\n';
            stage_ = 4;
            age_ = 0;
        } else if (stage_ == 4 && age_ > 10) {
            finished_ = true;
        }
        if (age_ > 4000)
            throw std::runtime_error("Native piloting phase timed out");
    }
    bool finished() const {
        return finished_;
    }

  private:
    const Universe& u_;
    Navigation& n_;
    Pilot& p_;
    Address planet_, nanoAddress_, distantPlanet_;
    bool microCaptured_ = false;
    glm::dvec3 direction_{};
    uint64_t descriptor_ = 0, geometry_ = 0;
    std::string camera_;
    int stage_ = 0, age_ = 0, mode_ = 0, hold_ = 0;
    bool finished_ = false, queued_ = false;
    void place(bool orbit) {
        auto p = u_.describe(planet_);
        double radius = std::pow(10., p.frame.logRadius);
        direction_ = glm::normalize(glm::dvec3(.3, 1, .4));
        for (int i = 0; i < 100; i++) {
            auto d = glm::normalize(glm::dvec3(std::sin(i * 1.7), 1, std::cos(i * 2.1)));
            if (glm::dot(d, u_.terrainNormal(p, d)) > .995) {
                direction_ = d;
                break;
            }
        }
        auto up = u_.terrainNormal(p, direction_);
        auto forward = glm::normalize(glm::cross(up, glm::dvec3(1, 0, 0)));
        auto q =
            glm::quatLookAtRH(orbit ? -direction_ : glm::normalize(forward - up * .06), orbit ? forward : up);
        const auto eye = direction_ * (orbit ? 2.2 : 1 + (u_.terrainHeight(p, direction_) + 42) / radius);
        const auto offset = eye - q * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << planet_.str() << ' ' << p.frame.logRadius << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z
          << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!n_.restore(s.str()))
            throw std::runtime_error("Native piloting placement failed");
        if (!orbit) {
            n_.changeFrame(FlightController::surfaceAt(planet_, direction_));
            n_.setRenderScale(2);
        }
        p_.resetMotion();
    }
};
} // namespace infinite
