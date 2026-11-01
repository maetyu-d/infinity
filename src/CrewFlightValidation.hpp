#pragma once
#include "CrewValidation.hpp"
namespace infinite {
class CrewFlightValidation {
    const Universe& universe_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    SDL_Window* window_;
    unsigned stage_ = 0, age_ = 0;
    double elapsed_ = 0, spares_ = 0, medical_ = 0;
    CrewStation damaged_ = CrewStation::Bridge;
    std::string initialCamera_, feedback_;
    bool finished_ = false;
    void next(unsigned s) {
        stage_ = s;
        age_ = 0;
    }
    void command(CrewCommand c, size_t i, CrewStation room) {
        const auto camera = nav_.serialize(), ship = pilot_.serialize();
        feedback_ = crew_.command(c, i, room);
        if (nav_.serialize() != camera || pilot_.serialize() != ship)
            throw std::runtime_error("Crew flight command altered the physical ship");
    }

  public:
    CrewFlightValidation(const Universe& u, Navigation& n, Pilot& p, ShipCrew& c, SDL_Window* w)
        : universe_(u), nav_(n), pilot_(p), crew_(c), window_(w) {
        // Reuse the established orbital fixture's camera without its scripted orders.
        CrewValidation setup(u, n, p, c, w);
        initialCamera_ = nav_.serialize();
        feedback_ = "F9 FLIGHT CONSOLE / MANUAL HELM AND ROLL LIVE";
    }
    void advance(Renderer& renderer) {
        ++age_;
        renderer.freezeVisualTime(12);
        HelmInput helm;
        helm.throttle = .01;
        helm.roll = stage_ < 3 ? .08 : 0;
        pilot_.update(helm, .02, renderer.submittedGeometry(nav_.anchor));
        if (stage_ == 4 || stage_ == 5) {
            for (unsigned i = 0; i < 4; ++i) {
                crew_.advance(.25, 2, true);
                elapsed_ += .25;
            }
            if (int(elapsed_) % 5 == 0)
                command(CrewCommand::Emergency, 3, damaged_);
        }
    }
    CrewView view() const {
        auto v = crew_.view(stage_ < 2 ? 2 : 3, damaged_);
        v.flightOpen = true;
        v.message = feedback_;
        return v;
    }
    void observe(Renderer& renderer, const std::string& directory) {
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "CREW_FLIGHT_CAPTURE " << name << " speed=" << glm::length(pilot_.ship().velocity)
                      << " spares=" << crew_.spares() << '\n';
        };
        if (stage_ == 0 && age_ > 85) {
            if (nav_.serialize() == initialCamera_ || glm::length(pilot_.ship().velocity) <= 0)
                throw std::runtime_error("Flight stopped with the crew console open");
            capture("crew-flight-stations");
            command(CrewCommand::Rest, 2, CrewStation::Bunks);
            next(1);
        } else if (stage_ == 1 && age_ > 12) {
            capture("crew-flight-orders");
            command(CrewCommand::BestStations, 0, CrewStation::Bridge);
            pilot_.applyCombatDamage(30);
            crew_.observeHull(pilot_.ship().hull);
            for (size_t i = 0; i < crewRoomCount; ++i)
                if (crew_.rooms()[i].fire > 0)
                    damaged_ = CrewStation(i);
            spares_ = crew_.spares();
            medical_ = crew_.medical();
            next(2);
        } else if (stage_ == 2 && age_ > 12) {
            capture("crew-flight-emergency");
            command(CrewCommand::Emergency, 3, damaged_);
            next(3);
        } else if (stage_ == 3 && age_ > 12) {
            capture("crew-flight-response");
            next(4);
        } else if (stage_ == 4 && elapsed_ >= 100) {
            const auto& room = crew_.rooms()[size_t(damaged_)];
            if (room.fire > .01 || room.integrity < 99 || crew_.spares() >= spares_ ||
                crew_.medical() >= medical_)
                throw std::runtime_error("Flight damage control did not complete real work");
            ShipCrew restored(universe_.seed());
            if (!restored.restore(crew_.serialize()) || restored.serialize() != crew_.serialize())
                throw std::runtime_error("In-flight crew orders failed save round trip");
            const auto d = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(960 / d), int(1280 / d));
            next(5);
        } else if (stage_ == 5 && age_ > 35) {
            capture("crew-flight-portrait");
            std::cout
                << "PASS: native live helm and roll, crew orders, station matching, emergency response, "
                   "real repair and first aid, unchanged ship at command dispatch, persistence and portrait "
                   "console\n";
            next(6);
        } else if (stage_ == 6 && age_ > 12)
            finished_ = true;
        if (age_ > 1200)
            throw std::runtime_error("Crew flight validation stalled");
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
