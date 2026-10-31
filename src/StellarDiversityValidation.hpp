#pragma once
#include "infinite/Renderer.hpp"
#include "infinite/StellarEvolution.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
namespace infinite {
class StellarDiversityValidation {
  public:
    StellarDiversityValidation(const Universe& u, Navigation& n, Pilot& p, ActivityState& a)
        : u_(u), n_(n), p_(p) {
        std::map<StellarKind, Node> kinds;
        std::map<std::string, Node> spectra;
        for (unsigned gal = 0; gal < 32 && (kinds.size() < 10 || spectra.size() < 7); gal++)
            for (unsigned i = 0; i < stellarPopulationCount; i++) {
                auto s =
                    u.describe(Address{}.child(Kind::Galaxy, gal).child(Kind::System, stellarIdFlag | i));
                kinds.try_emplace(s.stellar->kind, s);
                if (s.stellar->kind == StellarKind::MainSequence)
                    spectra.try_emplace(spectralClass(s.temperatureK), s);
            }
        if (kinds.size() != 10 || spectra.size() != 7)
            throw std::runtime_error("Stellar gallery missing real generated types");
        for (auto [kind, star] : kinds)
            scenes_.push_back({star, "star-" + std::to_string(unsigned(kind)), RenderMode::Hyperion});
        for (auto [type, star] : spectra)
            scenes_.push_back({star, "spectral-" + type, RenderMode::Hyperion});
        for (auto kind : {StellarKind::WhiteDwarf, StellarKind::NeutronStar})
            scenes_.push_back({kinds.at(kind), "star-" + std::to_string(unsigned(kind)) + "-rebased",
                               RenderMode::Hyperion, true});
        scenes_.push_back({kinds.at(StellarKind::RedSupergiant), "star-3-no-optics", RenderMode::Hyperion});
        a.enabled = false;
        place();
    }
    void advance(Renderer& r) {
        r.setMode(scenes_[stage_].mode);
        r.setStellarOptics(scenes_[stage_].name != "star-3-no-optics");
        r.setWeatherEffects(false);
        r.freezeVisualTime(12);
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& r, const std::string& dir) {
        ++frames_;
        if (hold_) {
            if (--hold_ == 0) {
                ++stage_;
                if (!finished())
                    place();
                else
                    std::cout << "PASS: addressed stellar families, spectral classes, compact radii, "
                                 "Hyperion rendering "
                                 "and physical camera invariance\n";
            }
            return;
        }
        const auto& scene = scenes_[stage_];
        const auto gs = cache.stats();
        auto g = r.submittedGeometry(scene.star.address);
        stable_ = g && !gs.pending && !gs.queued && !stats.lodTransitions && !stats.uploadDeferred
                      ? stable_ + 1
                      : 0;
        if (stable_ < 18)
            return;
        const auto hash = geometryFingerprint(*g),
                   desc = descriptorFingerprint(u_.describe(scene.star.address));
        if (desc != descriptorFingerprint(scene.star))
            throw std::runtime_error("Star changed on rendering-mode switch");
        const auto [it, inserted] = geometry_.emplace(scene.star.address, hash);
        if (!inserted && it->second != hash)
            throw std::runtime_error("Stellar geometry changed between modes or units");
        auto physical = n_.cameraPosition() * std::pow(10., n_.logDistance) / scene.star.stellarRadiusMetres;
        if (glm::length(physical - eye_) > 1e-9)
            throw std::runtime_error("Stellar camera changed physical location");
        const auto optical = extractStellarImage(u_, n_, {});
        if (std::abs(optical.angularRadius - std::asin(.25)) > 1e-9)
            throw std::runtime_error("Stellar physical angular size incorrect");
        r.screenshot((std::filesystem::path(dir) / (scene.name + ".ppm")).string());
        std::cout << "STELLAR_VARIETY scene=" << scene.name << " address=" << scene.star.address.str()
                  << " mode=" << renderModeName(scene.mode) << " geometry=" << hash << " descriptor=" << desc
                  << " mass_solar=" << scene.star.massSolar
                  << " radius_km=" << scene.star.stellarRadiusMetres / 1000
                  << " temperature_k=" << scene.star.temperatureK
                  << " luminosity_solar=" << scene.star.luminositySolar << " frame=" << frames_
                  << " camera=unchanged\n";
        stable_ = 0;
        hold_ = 6;
    }
    bool finished() const {
        return stage_ == scenes_.size();
    }

  private:
    struct Scene {
        Node star;
        std::string name;
        RenderMode mode;
        bool rebased = false;
    };
    const Universe& u_;
    Navigation& n_;
    Pilot& p_;
    std::vector<Scene> scenes_;
    std::map<Address, uint64_t> geometry_;
    unsigned stage_ = 0, stable_ = 0, hold_ = 0, frames_ = 0;
    glm::dvec3 eye_{};
    void place() {
        const auto& s = scenes_[stage_];
        const auto& star = s.star;
        eye_ = glm::normalize(glm::dvec3(.25, .1, 1)) * 4.;
        const auto q = glm::quatLookAtRH(-glm::normalize(eye_), glm::dvec3(0, 1, 0));
        const double log = std::log10(star.stellarRadiusMetres) + (s.rebased ? 2 : 1);
        auto offset = eye_ * star.stellarRadiusMetres / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << star.address.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z
             << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!n_.restore(text.str()))
            throw std::runtime_error("Stellar gallery invalid camera");
        n_.selected = star.address;
        p_.resetMotion();
        stable_ = 0;
    }
};
} // namespace infinite
