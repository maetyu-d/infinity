#pragma once
#include "infinite/Renderer.hpp"
#include "infinite/StationControl.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class StationSwapValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    WeaponSystem& weapons_;
    StationControl& control_;
    ActivityState& activity_;
    SDL_Window* window_;
    ActivityBody target_;
    CrewView view_;
    unsigned stage_ = 0, age_ = 0;
    bool finished_ = false;
    void next(unsigned s) {
        stage_ = s;
        age_ = 0;
    }
    void expect(bool yes, const char* message) {
        if (!yes)
            throw std::runtime_error(message);
    }

  public:
    StationSwapValidation(const Universe& u, Navigation& nav, Pilot& pilot, ShipCrew& crew,
                          WeaponSystem& weapons, StationControl& control, ActivityState& activity,
                          SDL_Window* window)
        : u_(u), nav_(nav), pilot_(pilot), crew_(crew), weapons_(weapons), control_(control),
          activity_(activity), window_(window) {
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        activity_.seconds = 12;
        target_ = sampleActivity(u.describe(home), 300, activity_.seconds);
        activity_.selected = target_.id;
        const auto q = glm::quatLookAtRH(glm::dvec3(0, 0, -1), glm::dvec3(0, 1, 0));
        const auto eye = target_.position + glm::dvec3(0, 0, 650);
        const auto offset = eye / 1000. - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << home.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x << ' '
             << offset.y << ' ' << offset.z;
        expect(nav_.restore(text.str()), "Invalid station swap camera");
        pilot_.resetMotion();
        crew_.toggleAutomatic();
        crew_.reconcile({2, 0, 0});
        crew_.setWeaponLevels({1, 1, 2});
        weapons_.setLoadout({1, 1, 2});
        crew_.manWeapon(6, 1);
        crew_.manWeapon(7, 2);
        for (unsigned i = 0; i < 100; ++i)
            crew_.advance(.25);
        expect(crew_.gunnerReadiness(1) > 0, "Fixture gunner did not arrive");
    }
    void advance(Renderer& renderer) {
        ++age_;
        if (stage_ == 1 && age_ == 1) {
            const auto ship = pilot_.serialize(), camera = nav_.serialize();
            expect(control_.request(1, crew_, pilot_, nav_), "Ready gun rejected captain swap");
            expect(pilot_.serialize() == ship && nav_.serialize() == camera,
                   "Handover changed physical ship");
        }
        if (stage_ == 6 && age_ == 1) {
            crew_.order(6, CrewOrder::Rest, CrewStation::Bunks);
            control_.synchronize(crew_, pilot_, nav_);
            expect(!control_.requested() && crew_.members()[6].order == CrewOrder::Rest,
                   "Unavailable pilot did not return captain to helm");
        }
        crew_.advance(.1, pilot_.instruments().properG, false, weapons_.firingMask() != 0,
                      weapons_.firingMask());
        control_.synchronize(crew_, pilot_, nav_);
        if (control_.active())
            control_.fly(pilot_, crew_, 1. / 60, renderer.submittedGeometry(nav_.anchor));
        weapons_.advance(1. / 60);
        control_.prepareWeapons(weapons_, crew_);
        if (stage_ == 1 && control_.active())
            next(2);
        view_ = crew_.view(6, CrewStation::Weapons);
        view_.flightOpen = stage_ > 0;
        view_.message = control_.status();
        renderer.setCrew(&view_);
    }
    void observe(Renderer& renderer, const std::string& directory) {
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "STATION_SWAP_CAPTURE " << name << " shots=" << weapons_.shotsFired()
                      << " helm_quality=" << crew_.reliefHelmReadiness() << '\n';
        };
        if (stage_ == 0 && age_ > 45) {
            capture("swap-helm");
            next(1);
        } else if (stage_ == 1 && age_ == 3) {
            expect(!control_.active() && crew_.members()[6].travel > 0, "Relief skipped corridor walk");
            capture("swap-relief-walking");
        } else if (stage_ == 2 && age_ > 15) {
            capture("swap-gun-ready");
            next(3);
        } else if (stage_ == 3 && age_ > 12) {
            // Aim from the actual port muzzle through the existing craft.
            const auto centre = u_.pointRelative(
                target_.id.home,
                target_.position / std::pow(10., u_.describe(target_.id.home).frame.logRadius), nav_.anchor,
                0);
            const auto muzzle = nav_.cameraPosition() * std::pow(10., nav_.logDistance) +
                                nav_.orientation * glm::dvec3(-4, 0, 0);
            const auto local = glm::inverse(nav_.orientation) * glm::normalize(centre - muzzle);
            control_.aim(
                0, 0,
                {std::atan2(local.x, -local.z) / .0025, -std::asin(std::clamp(local.y, -1., 1.)) / .0025},
                1. / 60);
            const auto ray = control_.ray(nav_, pilot_, activity_.seconds);
            expect(weapons_.fireMount(control_.mount(), u_, ray, std::span(&target_, 1)),
                   "Player gun did not fire");
            auto hit = target_;
            weapons_.apply(hit);
            expect(hit.hull == 90 && weapons_.mount(0).shotsFired == 0 && weapons_.mount(2).shotsFired == 0,
                   "Station trigger fired unrelated mounts or lost hit damage");
            capture("swap-gun-fire");
            next(4);
        } else if (stage_ == 4 && age_ == 1) {
            const auto ship = pilot_.serialize(), camera = nav_.serialize();
            control_.aim(0, 0, {220, -90}, 1. / 60);
            expect(pilot_.serialize() == ship && nav_.serialize() == camera, "Aiming rotated physical hull");
        } else if (stage_ == 4 && age_ > 18) {
            capture("swap-gun-aim");
            const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(960 / density), int(1280 / density));
            next(5);
        } else if (stage_ == 5 && age_ > 35) {
            capture("swap-portrait");
            next(6);
        } else if (stage_ == 6 && age_ > 15) {
            capture("swap-pilot-unavailable");
            const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(1440 / density), int(900 / density));
            crew_.manWeapon(6, 1);
            next(7);
        } else if (stage_ == 7 && age_ > 35 && crew_.gunnerReadiness(1) > 0) {
            capture("swap-returned-helm");
            ShipCrew loaded(u_.seed());
            loaded.setWeaponLevels({1, 1, 2});
            expect(loaded.restore(crew_.serialize()) && !loaded.reliefHelmId(),
                   "Reload retained remote authority");
            std::cout << "PASS: native captain/gunner handover, corridor walking, crew helm, independent "
                         "aim, selected-gun hit, pilot loss, return and portrait HUD\n";
            next(8);
        } else if (stage_ == 8 && age_ > 12)
            finished_ = true;
        expect(age_ < 1200, "Native station handover stalled");
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
