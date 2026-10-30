#pragma once
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
namespace infinite {
class PlanetDiversityValidation {
  public:
    PlanetDiversityValidation(const Universe& u, Navigation& n, Pilot& p, ActivityState& a)
        : u_(u), n_(n), p_(p), activity_(a) {
        std::map<PlanetType, Node> found;
        std::optional<Node> hot;
        for (unsigned star = 0; star < 512 && (found.size() < 9 || !hot); star++) {
            const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, star));
            for (unsigned i = 0; i < u.planetCount(system); i++) {
                const auto planet = u.describe(system.address.child(Kind::Planet, i));
                const auto t = planetTraits(planet);
                if (t.type == PlanetType::GasGiant && planet.environment.temperature > 900) {
                    if (!hot)
                        hot = planet;
                    continue;
                }
                if (!found.contains(t.type))
                    found.emplace(t.type, planet);
            }
        }
        if (found.size() != 9 || !hot)
            throw std::runtime_error("Planet tour needs all real solid and giant families");
        const PlanetType giants[] = {PlanetType::GasGiant, PlanetType::IceGiant, PlanetType::SubNeptune};
        const char* names[] = {"gas", "ice-giant", "sub-neptune"};
        for (unsigned i = 0; i < 3; i++)
            for (unsigned mode = 0; mode < 3; mode++)
                scenes_.push_back({found.at(giants[i]), std::string(names[i]) + "-" + std::to_string(mode),
                                   RenderMode(mode)});
        for (unsigned i = 0; i < 6; i++)
            scenes_.push_back({found.at(PlanetType(i)), "solid-" + std::to_string(i), RenderMode::Hyperion});
        scenes_.push_back({*hot, "hot-giant", RenderMode::Hyperion});
        scenes_.push_back({found.at(PlanetType::GasGiant), "rings-off", RenderMode::Hyperion, false});
        scenes_.push_back(
            {found.at(PlanetType::GasGiant), "rings-rebased", RenderMode::Hyperion, true, true});
        if (!planetTraits(found.at(PlanetType::GasGiant)).rings.present())
            throw std::runtime_error("Cold giant ring fixture absent");
        activity_.enabled = false;
        place();
    }
    void advance(Renderer& r) {
        const auto& s = scenes_.at(stage_);
        r.setMode(s.mode);
        r.setPlanetRings(s.rings);
        r.setStellarOptics(false);
        r.setPolarLights(false);
        r.freezeVisualTime(12);
        activity_.seconds = 12;
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        ++frames_;
        if (hold_) {
            if (--hold_ == 0) {
                ++stage_;
                if (!finished())
                    place();
                else
                    std::cout << "PASS: real planet families, ring composition, three rendering modes, "
                                 "unchanged geometry and physical camera\n";
            }
            return;
        }
        const auto& s = scenes_[stage_];
        auto geometry = renderer.submittedGeometry(s.planet.address);
        const auto gen = cache.stats();
        bool ready = geometry && !gen.pending && !gen.queued && !stats.lodTransitions &&
                     !stats.uploadDeferred && !stats.weatherPending;
        stable_ = ready ? stable_ + 1 : 0;
        if (stable_ < 18)
            return;
        const auto fingerprint = geometryFingerprint(*geometry),
                   descriptor = descriptorFingerprint(u_.describe(s.planet.address));
        if (descriptor != descriptorFingerprint(s.planet))
            throw std::runtime_error("Planet mode changed physical descriptor");
        auto [it, inserted] = geometry_.emplace(s.planet.address, fingerprint);
        if (!inserted && it->second != fingerprint)
            throw std::runtime_error("Planet geometry changed with rendering mode or units");
        const auto eye = n_.cameraPosition() * std::pow(10., n_.logDistance - s.planet.frame.logRadius);
        if (glm::length(eye - eye_) > 1e-10)
            throw std::runtime_error("Planet tour changed physical camera");
        if (!s.rings && stats.planetRings)
            throw std::runtime_error("Disabled rings still rendered");
        if (s.rings && planetTraits(s.planet).rings.present() && !stats.planetRings)
            throw std::runtime_error("Generated rings missing from scene");
        renderer.screenshot((std::filesystem::path(directory) / (s.name + ".ppm")).string());
        std::cout << "PLANET_VARIETY scene=" << s.name << " address=" << s.planet.address.str()
                  << " mode=" << renderModeName(s.mode) << " geometry=" << fingerprint
                  << " descriptor=" << descriptor
                  << " radius_km=" << std::pow(10., s.planet.frame.logRadius) / 1000
                  << " temperature_k=" << s.planet.environment.temperature << " rings=" << stats.planetRings
                  << " frame=" << frames_ << " camera=unchanged\n";
        hold_ = 6;
        stable_ = 0;
    }
    bool finished() const {
        return stage_ == scenes_.size();
    }

  private:
    struct Scene {
        Node planet;
        std::string name;
        RenderMode mode;
        bool rings = true, rebased = false;
    };
    const Universe& u_;
    Navigation& n_;
    Pilot& p_;
    ActivityState& activity_;
    std::vector<Scene> scenes_;
    std::map<Address, uint64_t> geometry_;
    unsigned stage_ = 0, stable_ = 0, hold_ = 0, frames_ = 0;
    glm::dvec3 eye_{};
    void place() {
        const auto& s = scenes_.at(stage_);
        const auto& planet = s.planet;
        const auto t = planetTraits(planet);
        const auto star = u_.describe(planet.address.parent());
        const auto sun =
            glm::normalize(u_.relative(star.address, planet.address, planet.frame.logRadius).position);
        const double sign = glm::dot(sun, t.axis) < 0 ? -1. : 1.;
        const auto direction = glm::normalize(sun + t.axis * (sign * .65));
        eye_ = direction * (t.rings.present() ? std::max(4.6, t.rings.outerRadius * 1.9) : 3.0);
        const auto up = std::abs(glm::dot(direction, t.axis)) > .95
                            ? glm::normalize(glm::cross(direction, glm::dvec3(1, .2, .1)))
                            : t.axis;
        const auto q = glm::quatLookAtRH(-direction, up);
        const double log = planet.frame.logRadius + (s.rebased ? 1 : 0);
        const auto offset = eye_ * std::pow(10., planet.frame.logRadius - log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << planet.address.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z
             << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!n_.restore(text.str()))
            throw std::runtime_error("Invalid planet diversity camera");
        n_.selected = planet.address;
        p_.resetMotion();
        stable_ = 0;
    }
};
} // namespace infinite
