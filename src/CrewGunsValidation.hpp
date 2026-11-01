#pragma once
#include "infinite/Economy.hpp"
#include "infinite/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class CrewGunsValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    WeaponSystem& weapons_;
    EconomyRuntime& runtime_;
    ActivityState& activity_;
    SDL_Window* window_;
    Address system_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    std::shared_ptr<const EconomySnapshot> state_;
    std::optional<ActivityId> port_;
    MarketView market_;
    CrewView crewView_;
    ActivityBody target_;
    unsigned stage_ = 0, age_ = 0;
    uint64_t sequence_ = 0;
    bool finished_ = false;
    std::string feedback_;
    void next(unsigned s) {
        stage_ = s;
        age_ = 0;
    }
    void place(const Address& home, glm::dvec3 eye, glm::dvec3 direction) {
        auto up = glm::normalize(eye);
        direction = glm::normalize(direction);
        if (std::abs(glm::dot(up, direction)) > .98)
            up = std::abs(direction.y) < .98 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
        const auto q = glm::quatLookAtRH(direction, up);
        const auto offset = eye / 1000. - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << home.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x << ' '
             << offset.y << ' ' << offset.z;
        if (!nav_.restore(text.str()))
            throw std::runtime_error("Gun station camera invalid");
        pilot_.resetMotion();
    }
    void orderGun(size_t person, unsigned mount) {
        const auto camera = nav_.serialize(), ship = pilot_.serialize();
        feedback_ = crew_.manWeapon(person, mount);
        if (nav_.serialize() != camera || pilot_.serialize() != ship)
            throw std::runtime_error("Manning a gun changed the physical ship");
    }
    void submit(bool weapon, unsigned mount = 0) {
        const auto i = *state_->siteIndex(*port_);
        EconomicCommand c;
        c.sequence = sequence_ = state_->player.lastCommand + 1;
        c.port = *port_;
        if (weapon) {
            auto q = economicWeaponQuote(*state_, i, 1, false, mount);
            if (!q.available)
                throw std::runtime_error("Native extra gun unavailable");
            c.action = EconomicAction::FitWeapon;
            c.weaponMount = mount;
            c.serviceUnits = 1;
            c.limitCents = q.totalCents();
        } else {
            const auto offers = economicCrewOffers(*state_, i);
            if (offers.empty())
                throw std::runtime_error("No funded gunner offer");
            auto q = economicCrewQuote(*state_, i, offers[0], true, crew_.living());
            if (!q.available)
                throw std::runtime_error("Native gunner unaffordable");
            c.action = EconomicAction::BuyCrew;
            c.crewId = offers[0].id;
            c.crewAboard = unsigned(crew_.living());
            c.limitCents = q.buyCents;
        }
        if (!runtime_.command(c))
            throw std::runtime_error("Gun station order queue full");
    }
    WeaponRay ray() const {
        WeaponRay r;
        r.reference = nav_.anchor;
        r.origin = nav_.cameraPosition() * std::pow(10., nav_.logDistance);
        r.direction = nav_.orientation * glm::dvec3(0, 0, -1);
        r.right = nav_.orientation * glm::dvec3(1, 0, 0);
        r.scale = pilot_.sizeRatio();
        r.seconds = activity_.seconds;
        return r;
    }
    unsigned fire() {
        return weapons_.fireBattery(u_, ray(), std::span(&target_, 1));
    }

  public:
    CrewGunsValidation(const Universe& u, Navigation& n, Pilot& p, ShipCrew& c, WeaponSystem& w,
                       EconomyRuntime& e, ActivityState& a, SDL_Window* window)
        : u_(u), nav_(n), pilot_(p), crew_(c), weapons_(w), runtime_(e), activity_(a), window_(window) {
        const auto home = system_.child(Kind::Planet, 0);
        auto port = sampleActivity(u_.describe(home), 0, 0);
        auto up = glm::normalize(port.position);
        place(home, port.position + up * (port.radius * 2 + 50), -up);
        if (crew_.automatic())
            crew_.toggleAutomatic();
        runtime_.request(system_, 0);
    }
    void advance(Renderer& renderer) {
        ++age_;
        renderer.freezeVisualTime(12);
        state_ = runtime_.snapshot();
        if (!state_ || !state_->network || state_->network->system != system_ || state_->catchingUp)
            return;
        crew_.reconcile(state_->player.crew);
        crew_.reconcileMarket(state_->player.crewRevision, state_->player.crewTitles,
                              state_->player.crewBoarding);
        crew_.setWeaponLevels(state_->player.weaponLoadout());
        weapons_.setLoadout(state_->player.weaponLoadout());
        if (stage_ >= 11) {
            crew_.advance(.25, 0, false, weapons_.firingMask() != 0, weapons_.firingMask());
            weapons_.advance(.05);
        }
        weapons_.setCrewReadiness(crew_.weaponReadiness());
        for (unsigned i = 1; i < 3; ++i)
            weapons_.setMountReadiness(i, crew_.gunnerReadiness(i));
        if (stage_ == 0) {
            for (size_t i = 0; i < state_->sites.size(); ++i)
                if (state_->network->sites[i].orbital) {
                    auto q = economicWeaponQuote(*state_, i, 1, false, 1);
                    auto offers = economicCrewOffers(*state_, i);
                    bool parts = q.available;
                    for (size_t good = 0; good < goodCount; ++good)
                        if (q.materials[good] &&
                            state_->quote(i, Good(good), 2 * q.materials[good]).available <
                                2 * q.materials[good])
                            parts = false;
                    if (!parts || offers.size() < 2)
                        continue;
                    const auto a = economicCrewQuote(*state_, i, offers[0], true, 6),
                               b = economicCrewQuote(*state_, i, offers[1], true, 7);
                    if (q.totalCents() * 2.5 + a.buyCents + b.buyCents * 1.2 > state_->player.credits)
                        continue;
                    port_ = state_->network->sites[i].id;
                    const auto& d = state_->network->sites[i];
                    auto up = glm::normalize(d.metres);
                    place(d.id.home, d.metres + up * (d.radius * 2 + 50), -up);
                    next(1);
                    break;
                }
            if (!port_)
                throw std::runtime_error("No funded port for two guns and two crew");
        }
        if (stage_ == 3 && state_->player.lastCommand >= sequence_) {
            if (state_->player.weaponAt(1) != 1)
                throw std::runtime_error("Port gun fit failed");
            next(4);
        }
        if (stage_ == 5 && state_->player.lastCommand >= sequence_) {
            if (state_->player.weaponAt(2) != 1)
                throw std::runtime_error("Starboard gun fit failed");
            next(6);
        }
        if (stage_ == 7 && state_->player.lastCommand >= sequence_) {
            if (crew_.living() != 7)
                throw std::runtime_error("First gunner did not board");
            next(8);
        }
        if (stage_ == 9 && state_->player.lastCommand >= sequence_) {
            if (crew_.living() != 8)
                throw std::runtime_error("Second gunner did not board");
            next(10);
        }
        if (stage_ == 12 && crew_.gunnerReadiness(1) > 0 && crew_.gunnerReadiness(2) > 0)
            next(13);
        if (stage_ == 18 && crew_.gunnerReadiness(1) > 0) {
            if (fire()) {
                auto hit = target_;
                weapons_.apply(hit);
                if (hit.disabled) {
                    if (weapons_.damage().size() != 1)
                        throw std::runtime_error("Battery duplicated damage records");
                    next(19);
                }
            }
        }
        market_.open = stage_ < 6;
        if (market_.open) {
            MarketContext context;
            context.weaponMount = stage_ >= 4 ? 2 : 1;
            context.crew = crew_.members();
            const auto i = *state_->siteIndex(*port_);
            market_ = economicMarketView(state_.get(), port_, 0, 1,
                                         economicTradeAccess(u_, nav_, pilot_, state_->network->sites[i]),
                                         MarketPage::Weapons, context);
        }
        renderer.setEconomy(state_, &market_);
        crewView_ = crew_.view(stage_ < 11 ? 6 : 7, CrewStation::Weapons);
        crewView_.flightOpen = !market_.open;
        crewView_.message = feedback_;
        renderer.setCrew(&crewView_);
    }
    void observe(Renderer& renderer, const std::string& directory) {
        if (!state_ || !port_)
            return;
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "CREW_GUNS_CAPTURE " << name << " credits=" << state_->player.credits
                      << " crew=" << crew_.living() << " shots=" << weapons_.shotsFired() << '\n';
        };
        if (stage_ == 1 && age_ > 65) {
            capture("gun-station-shop");
            next(2);
        } else if (stage_ == 2 && age_ > 12) {
            submit(true, 1);
            next(3);
        } else if (stage_ == 4 && age_ > 12) {
            submit(true, 2);
            next(5);
        } else if (stage_ == 6 && age_ > 12) {
            submit(false);
            next(7);
        } else if (stage_ == 8 && age_ > 12) {
            submit(false);
            next(9);
        } else if (stage_ == 10 && age_ > 12) {
            if (crew_.gunnerReadiness(1) > 0 || crew_.gunnerReadiness(2) > 0)
                throw std::runtime_error("Unassigned gun crew became active");
            capture("gun-stations-unmanned");
            orderGun(6, 1);
            orderGun(7, 2);
            next(11);
        } else if (stage_ == 11 && age_ > 1) {
            capture("gun-stations-moving");
            next(12);
        } else if (stage_ == 13 && age_ > 12) {
            capture("gun-stations-manned");
            target_ = sampleActivity(u_.describe(port_->home), 300, activity_.seconds);
            const auto eye = target_.position + glm::dvec3(0, 0, 650);
            place(port_->home, eye, target_.position - eye);
            activity_.selected = target_.id;
            next(14);
        } else if (stage_ == 14 && age_ > 45) {
            capture("gun-battery-ready");
            next(15);
        } else if (stage_ == 15 && age_ > 12) {
            const auto mask = fire();
            auto hit = target_;
            weapons_.apply(hit);
            if (mask != 7 || std::abs(hit.hull - 70) > 1e-6)
                throw std::runtime_error("Three crewed guns failed shared linked damage");
            capture("gun-battery-fire");
            next(16);
        } else if (stage_ == 16 && age_ > 12) {
            crew_.order(6, CrewOrder::Rest, CrewStation::Bunks);
            weapons_.setMountReadiness(1, crew_.gunnerReadiness(1));
            if (fire() != 5)
                throw std::runtime_error("Relieved gunner's weapon still fired");
            feedback_ = "PORT GUNNER RESTING / NOSE AND STARBOARD STILL OPERATE";
            crewView_ = crew_.view(6, CrewStation::Weapons);
            crewView_.flightOpen = true;
            crewView_.message = feedback_;
            capture("gun-battery-relief");
            next(17);
        } else if (stage_ == 17 && age_ > 12) {
            orderGun(6, 1);
            next(18);
        } else if (stage_ == 19 && age_ > 12) {
            capture("gun-battery-disabled");
            ShipCrew c(u_.seed());
            c.setWeaponLevels(state_->player.weaponLoadout());
            WeaponSystem w;
            w.setLoadout(state_->player.weaponLoadout());
            if (!c.restore(crew_.serialize()) || c.serialize() != crew_.serialize() ||
                !w.restore(weapons_.serialize(), u_) || w.serialize() != weapons_.serialize())
                throw std::runtime_error("Crews and battery failed persistence");
            const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(960 / density), int(1280 / density));
            next(20);
        } else if (stage_ == 20 && age_ > 35) {
            capture("gun-stations-portrait");
            std::cout
                << "PASS: native paid extra mounts and gunner purchases, walking crew, linked three-gun "
                   "damage, relief stops its gun, shared disable record, persistence and portrait stations\n";
            next(21);
        } else if (stage_ == 21 && age_ > 12)
            finished_ = true;
        if (age_ > 1500)
            throw std::runtime_error("Crewed gun validation stalled");
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
