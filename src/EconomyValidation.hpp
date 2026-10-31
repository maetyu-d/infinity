#pragma once
#include "infinite/Economy.hpp"
#include "infinite/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class EconomyValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    EconomyRuntime& runtime_;
    ActivityState& activity_;
    SDL_Window* window_;
    Address system_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    std::optional<ActivityId> port_;
    MarketView view_;
    int stage_ = 0, age_ = 0;
    bool finished_ = false;
    int64_t beforeCredits_ = 0;
    std::string camera_;
    uint64_t geometry_ = 0;
    void place() {
        const auto planet = u_.describe(system_.child(Kind::Planet, 0));
        const auto harbour = sampleActivity(planet, 0, 0);
        const auto away = glm::normalize(harbour.position) * 1200. + glm::dvec3(100, 300, 200);
        auto eye = harbour.position + away;
        auto q = glm::quatLookAtRH(glm::normalize(-away), glm::normalize(harbour.position));
        const auto offset = eye / std::pow(10., planet.frame.logRadius) - q * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << planet.address.str() << ' ' << planet.frame.logRadius << ' ' << q.w << ' ' << q.x << ' ' << q.y
          << ' ' << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(s.str()))
            throw std::runtime_error("Economic validation camera failed");
        pilot_.resetMotion();
        port_ = ActivityId{planet.address, 0};
    }

  public:
    EconomyValidation(const Universe& u, Navigation& n, Pilot& p, EconomyRuntime& e, ActivityState& a,
                      SDL_Window* window)
        : u_(u), nav_(n), pilot_(p), runtime_(e), activity_(a), window_(window) {
        place();
        activity_.seconds = 0;
        runtime_.request(system_, 0);
    }
    void advance(Renderer& renderer) {
        age_++;
        auto s = runtime_.snapshot();
        if (!s || !s->network || s->network->system != system_)
            return;
        auto i = s->siteIndex(*port_);
        if (!i)
            throw std::runtime_error("Existing harbour has no economic account");
        view_ = economicMarketView(s.get(), port_, 1, 10,
                                   economicTradeAccess(u_, nav_, pilot_, s->network->sites[*i]));
        if (!view_.connected && !s->catchingUp)
            throw std::runtime_error("Economic harbour approach did not establish trade access");
        pilot_.setCargoMass(s->player.massKg());
        renderer.setEconomy(s, &view_);
    }
    void observe(const FrameStats& stats, Renderer& renderer, const std::string& directory) {
        auto s = runtime_.snapshot();
        if (!s || !s->network || s->network->system != system_ || s->catchingUp)
            return;
        auto i = s->siteIndex(*port_);
        if (!i)
            return;
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
        };
        auto next = [&](int stage) {
            stage_ = stage;
            age_ = 0;
        };
        if (stage_ == 0 && age_ > 100 && stats.visibleObjects && renderer.submittedGeometry(nav_.anchor)) {
            camera_ = nav_.serialize();
            geometry_ = geometryFingerprint(*renderer.submittedGeometry(nav_.anchor));
            beforeCredits_ = s->player.credits;
            capture("market-hyperion");
            next(1);
        } else if (stage_ == 1 && age_ > 8) {
            auto q = s->quote(*i, Good::Water, 10);
            runtime_.command({1, *port_, EconomicAction::Buy, Good::Water, 10, q.buyCents, 0});
            next(2);
        } else if (stage_ == 2 && s->player.lastCommand >= 1 && age_ > 8) {
            if (s->player.massKg() != 10 || s->player.credits >= beforeCredits_)
                throw std::runtime_error("Native market purchase failed");
            capture("market-cargo");
            next(40);
        } else if (stage_ == 40 && age_ > 8) {
            if (nav_.serialize() != camera_ ||
                geometryFingerprint(*renderer.submittedGeometry(nav_.anchor)) != geometry_)
                throw std::runtime_error("Economic trading changed universe/camera");
            auto q = s->quote(*i, Good::Water, 10);
            runtime_.command({2, *port_, EconomicAction::Sell, Good::Water, 10, q.sellCents, 0});
            next(5);
        } else if (stage_ == 5 && s->player.lastCommand >= 2 && age_ > 8) {
            if (s->player.massKg() != 0 || s->player.credits >= beforeCredits_)
                throw std::runtime_error("Native market resale failed or minted money");
            SDL_SetWindowSize(window_, 480, 640);
            renderer.setMode(RenderMode::Hyperion);
            next(6);
        } else if (stage_ == 6 && age_ > 30) {
            capture("market-portrait");
            next(60);
        } else if (stage_ == 60 && age_ > 8) {
            activity_.seconds = 300;
            runtime_.request(system_, 300);
            next(7);
        } else if (stage_ == 7 && s->tick >= 60 && age_ > 30) {
            bool freight = false;
            for (const auto& f : s->flights)
                freight |= f.kg > 0;
            bool receipt = false;
            for (const auto& e : s->events)
                receipt |= e.text.find("RECEIVED") != std::string::npos;
            if (!freight && !receipt)
                throw std::runtime_error("Actual economic network produced no cargo activity");
            capture("market-live-supply");
            std::cout << "ECONOMY_NATIVE sites=" << s->network->sites.size()
                      << " vessels=" << s->flights.size() << " tick=" << s->tick
                      << " player_credits_cents=" << s->player.credits << " cargo_kg=" << s->player.massKg()
                      << " worker_snapshot_bytes=" << runtime_.residentBytes() << '\n';
            std::cout << "PASS: native market purchase/resale, Hyperion scene identity, portrait "
                         "fit and real freight\n";
            next(8);
        } else if (stage_ == 8 && age_ > 10)
            finished_ = true;
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
