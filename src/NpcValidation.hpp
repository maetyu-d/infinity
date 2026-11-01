#pragma once
#include "infinite/NpcCombat.hpp"
#include "infinite/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class NpcValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ActivityState& activity_;
    WeaponSystem& weapons_;
    NpcCombat& combat_;
    SDL_Window* window_;
    Address home_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    ActivityId target_{home_, raiderActivitySlot};
    NpcPlayer player_;
    unsigned stage_ = 0, age_ = 0;
    bool simulate_ = false, fire_ = false, finished_ = false;
    double goal_ = 0, totalDamage_ = 0;
    glm::dvec3 initialEye_{};
    unsigned attackShots_ = 0;
    void next(unsigned s) {
        stage_ = s;
        age_ = 0;
    }
    void size(int w, int h) {
        const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
        SDL_SetWindowSize(window_, int(w / density), int(h / density));
    }
    std::vector<ActivityBody> bodies() const {
        return {sampleCombatActivity(u_, target_, activity_.seconds),
                sampleActivity(u_.describe(home_), 0, activity_.seconds),
                sampleActivity(u_.describe(home_), 1, activity_.seconds)};
    }
    ActivityBody targetBody() const {
        auto b = sampleCombatActivity(u_, target_, activity_.seconds);
        combat_.apply(b);
        weapons_.apply(b);
        return b;
    }
    glm::dquat look(glm::dvec3 direction, glm::dvec3 position) const {
        direction = glm::normalize(direction);
        auto up = glm::normalize(position);
        if (std::abs(glm::dot(direction, up)) > .98)
            up = std::abs(direction.y) < .98 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
        return glm::quatLookAtRH(direction, up);
    }
    void aim() {
        const auto b = targetBody();
        player_.forward = glm::normalize(b.position - player_.position);
        const auto q = look(player_.forward, player_.position);
        nav_.orientation = q;
        nav_.offset = player_.position / 1000. - q * glm::dvec3(0, 0, 1);
        if (glm::length(nav_.cameraPosition() - initialEye_) > 1e-8)
            throw std::runtime_error("Combat aiming moved the physical camera");
    }
    void place(glm::dvec3 position) {
        player_.reference = home_;
        player_.position = position;
        player_.size = 1;
        player_.hull = pilot_.ship().hull;
        auto target = targetBody();
        const auto q = look(target.position - position, position);
        const auto offset = position / 1000. - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << home_.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
             << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(text.str()))
            throw std::runtime_error("NPC fixture camera failed");
        pilot_.resetMotion();
        pilot_.setVelocity({});
        player_.forward = q * glm::dvec3(0, 0, -1);
        initialEye_ = nav_.cameraPosition();
        activity_.selected = target_;
    }
    bool shoot() {
        aim();
        auto b = targetBody();
        WeaponRay ray;
        ray.reference = home_;
        ray.origin = player_.position;
        ray.direction = player_.forward;
        ray.right = nav_.orientation * glm::dvec3(1, 0, 0);
        ray.seconds = activity_.seconds;
        auto candidates = bodies();
        for (auto& c : candidates) {
            combat_.apply(c);
            weapons_.apply(c);
        }
        return weapons_.fire(u_, ray, candidates);
    }
    void step() {
        const double dt = 1. / 60;
        activity_.seconds += dt;
        weapons_.advance(dt);
        if (fire_ && weapons_.cooldown() <= 1e-9 && targetBody().hull > 30)
            shoot();
        if (targetBody().hull <= 30)
            fire_ = false;
        player_.seconds = activity_.seconds;
        player_.hull = pilot_.ship().hull;
        const auto source = bodies();
        const auto report = combat_.update(u_, player_, source, weapons_, dt);
        pilot_.applyCombatDamage(report.playerDamage);
        totalDamage_ += report.playerDamage;
    }

  public:
    NpcValidation(const Universe& u, Navigation& n, Pilot& p, ActivityState& a, WeaponSystem& w, NpcCombat& c,
                  SDL_Window* window)
        : u_(u), nav_(n), pilot_(p), activity_(a), weapons_(w), combat_(c), window_(window) {
        weapons_.setLevel(1);
        combat_ = NpcCombat{};
        bool found = false;
        for (unsigned i = 0; i < 100; ++i) {
            activity_.seconds = 12 + i * 12;
            const auto b = sampleRaider(u_, home_, activity_.seconds);
            const auto port = sampleActivity(u_.describe(home_), 0, activity_.seconds);
            const auto position = b.position + glm::normalize(b.position - port.position) * 1600.;
            found = true;
            for (unsigned slot : {0u, 1u}) {
                const auto station = sampleActivity(u_.describe(home_), slot, activity_.seconds);
                if (glm::length(position - station.position) < station.radius + 6500)
                    found = false;
            }
            if (found) {
                place(position);
                break;
            }
        }
        if (!found)
            throw std::runtime_error("No unguarded canonical raider encounter");
        size(1440, 900);
    }
    void advance(Renderer& renderer) {
        ++age_;
        renderer.freezeVisualTime(12);
        if (simulate_)
            for (unsigned i = 0; i < 4; ++i)
                step();
        const auto it = combat_.agents().find(target_);
        if (stage_ == 1 && totalDamage_ > 0 && it != combat_.agents().end() && !combat_.shots().empty()) {
            attackShots_ = unsigned(it->second.shots);
            simulate_ = false;
            next(2);
        } else if (stage_ == 4 && activity_.seconds >= goal_) {
            if (it == combat_.agents().end() || it->second.mode != NpcMode::Evade)
                throw std::runtime_error("Native hostile failed evasion");
            simulate_ = false;
            next(5);
        } else if (stage_ == 7 && it != combat_.agents().end() && it->second.mode == NpcMode::Retreat) {
            const auto body = targetBody();
            if (glm::dot(body.velocity, body.position - player_.position) > 20) {
                simulate_ = false;
                next(8);
            }
        } else if (stage_ == 12 && it != combat_.agents().end() && it->second.mode == NpcMode::Attack &&
                   totalDamage_ > goal_ && !combat_.shots().empty()) {
            simulate_ = false;
            next(13);
        }
    }
    void observe(const FrameStats& stats, Renderer& renderer, const std::string& directory) {
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "NPC_CAPTURE " << name << " hull=" << pilot_.ship().hull
                      << " target=" << targetBody().hull << '\n';
        };
        if (stage_ == 0 && age_ > 100 && stats.activityVisible) {
            simulate_ = true;
            next(1);
        } else if (stage_ == 2 && age_ > 12) {
            capture("npc-attack");
            next(3);
        } else if (stage_ == 3 && age_ > 12) {
            if (!shoot())
                throw std::runtime_error("Native player shot failed");
            renderer.cockpitImpulse(CockpitImpulse::Recoil, .4);
            goal_ = activity_.seconds + .6;
            simulate_ = true;
            next(4);
        } else if (stage_ == 5 && age_ > 12) {
            capture("npc-evade");
            next(6);
        } else if (stage_ == 6 && age_ > 12) {
            fire_ = true;
            simulate_ = true;
            next(7);
        } else if (stage_ == 8 && age_ > 12) {
            capture("npc-retreat");
            NpcCombat copy;
            if (!copy.restore(combat_.serialize(u_.seed()), u_))
                throw std::runtime_error("Native retreat did not persist");
            std::cout << "NPC_PROOF attack_shots=" << attackShots_ << " player_damage=" << totalDamage_
                      << " retreat_hull=" << targetBody().hull << '\n';
            next(9);
        } else if (stage_ == 9 && age_ > 12) {
            target_ = {home_, 300};
            combat_ = NpcCombat{};
            const auto body = sampleCombatActivity(u_, target_, activity_.seconds);
            place(body.position + glm::normalize(body.position) * 900.);
            next(10);
        } else if (stage_ == 10 && age_ > 45 && stats.activityVisible) {
            capture("npc-patrol-friendly");
            next(11);
        } else if (stage_ == 11 && age_ > 12) {
            if (!shoot())
                throw std::runtime_error("Native patrol provocation failed");
            goal_ = totalDamage_;
            simulate_ = true;
            next(12);
        } else if (stage_ == 13 && age_ > 12) {
            capture("npc-patrol-response");
            next(14);
        } else if (stage_ == 14 && age_ > 12) {
            size(960, 1280);
            next(15);
        } else if (stage_ == 15 && age_ > 35) {
            capture("npc-patrol-portrait");
            WeaponSystem copy;
            if (!copy.restore(weapons_.serialize(), u_) || pilot_.ship().hull >= 100 ||
                targetBody().hull >= 100)
                throw std::runtime_error("Native combat damage or persistence failed");
            std::cout << "PASS: native NPC acquisition, incoming damage, evasion, low-hull retreat, patrol "
                         "retaliation, persistent IDs and portrait combat HUD\n";
            next(16);
        } else if (stage_ == 16 && age_ > 12)
            finished_ = true;
        if (age_ > 1200)
            throw std::runtime_error("NPC behaviour stage stalled / " + std::to_string(stage_));
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
