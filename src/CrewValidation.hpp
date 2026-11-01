#pragma once
#include "infinite/Cockpit.hpp"
#include "infinite/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class CrewValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    SDL_Window* window_;
    unsigned stage_ = 0, age_ = 0;
    double elapsed_ = 0, damagedIntegrity_ = 0, initialSpares_ = 0, initialMedical_ = 0;
    CrewStation room_ = CrewStation::Bridge;
    std::string camera_;
    bool finished_ = false;
    void next(unsigned s) {
        stage_ = s;
        age_ = 0;
        elapsed_ = 0;
    }

  public:
    CrewValidation(const Universe& u, Navigation& n, Pilot& p, ShipCrew& c, SDL_Window* w)
        : u_(u), nav_(n), pilot_(p), crew_(c), window_(w) {
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto port = sampleActivity(u.describe(home), 0, 12);
        auto up = glm::normalize(port.position);
        auto side = glm::normalize(glm::cross(up, glm::dvec3(0, 1, 0)));
        const auto eye = port.position + up * 2500. + side * 15000.;
        const auto q = glm::quatLookAtRH(glm::normalize(port.position - eye), up);
        const auto offset = eye / 1000. - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << home.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x << ' '
             << offset.y << ' ' << offset.z;
        if (!nav_.restore(text.str()))
            throw std::runtime_error("Crew fixture camera invalid");
        pilot_.resetMotion();
        crew_.observeHull(pilot_.ship().hull);
        crew_.toggleAutomatic();
        camera_ = nav_.serialize();
    }
    void advance(Renderer& r) {
        ++age_;
        r.freezeVisualTime(12);
        if (stage_ == 4 || stage_ == 6)
            for (unsigned i = 0; i < 4; ++i) {
                crew_.advance(.25, 2, true, false);
                elapsed_ += .25;
            }
    }
    CrewView view() const {
        auto v = crew_.view(3, room_);
        v.open = true;
        v.message = stage_ < 2   ? "SIX NAMED CREW / SKILLS FOLLOW STATION EXPERIENCE"
                    : stage_ < 4 ? "HULL HIT / FIRE / INJURIES / DISPATCH DAMAGE CONTROL"
                    : stage_ < 6 ? "CREW MOVING THROUGH CONNECTED COMPARTMENTS"
                                 : "FIRST AID AND REPAIRS CONSUME ONBOARD STORES";
        v.port = "PORT SERVICES OFFLINE / BERTH OR LAND FOR HIRING, STORES AND CLINIC";
        return v;
    }
    void observe(Renderer& r, const std::string& directory) {
        auto capture = [&](const char* name) {
            r.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "CREW_CAPTURE " << name << " spares=" << crew_.spares()
                      << " medical=" << crew_.medical() << '\n';
        };
        if (stage_ == 0 && age_ > 100) {
            capture("crew-stations");
            next(1);
        } else if (stage_ == 1 && age_ > 12) {
            pilot_.applyCombatDamage(30);
            crew_.observeHull(pilot_.ship().hull);
            for (size_t i = 0; i < crewRoomCount; ++i)
                if (crew_.rooms()[i].fire > 0)
                    room_ = CrewStation(i);
            damagedIntegrity_ = crew_.rooms()[size_t(room_)].integrity;
            initialSpares_ = crew_.spares();
            initialMedical_ = crew_.medical();
            next(2);
        } else if (stage_ == 2 && age_ > 12) {
            capture("crew-emergency");
            next(3);
        } else if (stage_ == 3 && age_ > 12) {
            crew_.order(3, CrewOrder::Firefight, room_);
            crew_.order(4, CrewOrder::Repair, room_);
            crew_.order(5, CrewOrder::Aid, room_);
            next(4);
        } else if (stage_ == 4 && elapsed_ >= 1) {
            next(5);
        } else if (stage_ == 5 && age_ > 12) {
            capture("crew-responders");
            next(6);
        } else if (stage_ == 6 && elapsed_ >= 90) {
            const auto& room = crew_.rooms()[size_t(room_)];
            if (room.fire > 1 || room.integrity <= damagedIntegrity_ || crew_.spares() >= initialSpares_ ||
                crew_.medical() >= initialMedical_)
                throw std::runtime_error("Crew failed real firefighting, repairs or first aid");
            ShipCrew copy(u_.seed());
            if (!copy.restore(crew_.serialize()) || copy.serialize() != crew_.serialize() ||
                nav_.serialize() != camera_)
                throw std::runtime_error("Crew save or camera invariance failed");
            next(7);
        } else if (stage_ == 7 && age_ > 12) {
            capture("crew-recovered");
            next(8);
        } else if (stage_ == 8 && age_ > 12) {
            const auto d = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(960 / d), int(1280 / d));
            next(9);
        } else if (stage_ == 9 && age_ > 35) {
            capture("crew-portrait");
            std::cout
                << "PASS: native crew stations, hull-hit fire and injuries, walking responders, finite "
                   "repair and medical stores, persistence, unchanged camera and fitted portrait panel\n";
            next(10);
        } else if (stage_ == 10 && age_ > 12)
            finished_ = true;
        if (age_ > 1200)
            throw std::runtime_error("Crew validation stalled");
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
