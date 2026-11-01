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
    WeaponSystem& weapons_;
    WeaponRay weaponRay_;
    ActivityBody weaponTarget_;
    bool weaponFlight_ = false;
    std::string weaponCamera_;
    Address system_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    std::optional<ActivityId> port_;
    MarketView view_;
    int stage_ = 0, age_ = 0;
    MarketPage page_ = MarketPage::Goods;
    size_t supply_ = 0;
    uint64_t sequence_ = 3;
    bool finished_ = false, requireLink_ = true;
    std::optional<HaulContract> haul_;
    int64_t haulCredits_ = 0;
    int64_t beforeCredits_ = 0;
    std::string camera_;
    uint64_t geometry_ = 0;
    size_t serviceRow_ = 0;
    int64_t serviceCredits_ = 0;
    uint64_t repairUnits_ = 0;
    std::unique_ptr<Economy> feedbackLedger_;
    std::shared_ptr<EconomySnapshot> feedback_;
    EconomyPlayer recoveryPlayer_;
    uint64_t recoveryId_ = 0;
    size_t feedbackSite_ = 0;
    std::string feedbackCamera_;
    uint64_t feedbackWorld_ = 0, feedbackGeometry_ = 0;
    void refreshFeedback(const std::shared_ptr<const EconomySnapshot>& live) {
        feedback_ = std::make_shared<EconomySnapshot>(*live);
        feedback_->sites = feedbackLedger_->sites();
        feedback_->tick = feedbackLedger_->tick();
        feedback_->contracts = feedbackLedger_->contracts();
        feedback_->hauls = feedbackLedger_->hauls();
        feedback_->recoveries = feedbackLedger_->recoveries();
        feedback_->outcomes = feedbackLedger_->outcomes();
        feedback_->player = recoveryPlayer_;
        feedback_->message = feedback_->outcomes.empty()
                                 ? "FUNDED RECOVERY WORK AT EXISTING SURFACE SITES"
                                 : "DELIVERY SAVED / CREW WORK AND OBSERVATIONS PERSIST";
        feedback_->jobs = economicJobs(*feedback_);
        feedback_->catchingUp = false;
        feedback_->routes = economicTradeRoutes(*feedback_);
        feedback_->portSignals = economicPortSignals(*feedback_);
    }
    void placeFeedback(const std::shared_ptr<const EconomySnapshot>& live, Renderer& renderer) {
        feedbackLedger_ = std::make_unique<Economy>(live->network);
        auto initial = std::make_shared<EconomySnapshot>(*live);
        initial->sites = feedbackLedger_->sites();
        initial->tick = 0;
        auto signals = economicPortSignals(*initial);
        double best = -1;
        for (size_t i = 0; i < live->network->sites.size(); ++i) {
            const auto& site = live->network->sites[i];
            if (site.orbital || site.population < 1000 || signals[i].power < .25f)
                continue;
            const auto sun = glm::normalize(u_.relative(system_, site.id.home, 0).position);
            const auto up = glm::normalize(site.metres);
            const double night = -glm::dot(up, sun);
            if (night < .1)
                continue;
            const double score = night + std::log1p(double(site.population)) * .1;
            if (score > best) {
                best = score;
                feedbackSite_ = i;
            }
        }
        if (best < 0)
            throw std::runtime_error("No real night-side settlement for economic light comparison");
        const auto& site = live->network->sites[feedbackSite_];
        port_ = site.id;
        const auto home = u_.describe(site.id.home);
        const auto up = glm::normalize(site.metres);
        auto north = glm::cross(up, glm::dvec3(1, 0, 0));
        if (glm::length(north) < .1)
            north = glm::cross(up, glm::dvec3(0, 1, 0));
        north = glm::normalize(north);
        auto eye = site.metres + north * 240. + up * 100.;
        const auto d = glm::normalize(eye);
        const auto ground = std::pow(10., home.frame.logRadius) + u_.terrainHeight(home, d);
        if (glm::length(eye) < ground + 60)
            eye = d * (ground + 60);
        const auto q = glm::quatLookAtRH(glm::normalize(site.metres + up * 20. - eye), up);
        const auto offset = eye / 1000. - q * glm::dvec3(0, 0, 1);
        std::ostringstream camera;
        camera.precision(17);
        camera << site.id.home.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
               << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(camera.str()))
            throw std::runtime_error("Economic city camera failed");
        nav_.changeFrame(FlightController::surfaceAt(site.id.home, up));
        nav_.setRenderScale(2);
        pilot_.resetMotion();
        std::istringstream old(pilot_.serialize());
        std::string clean, word;
        for (unsigned field = 0; old >> word; ++field)
            clean += (field == 23 || field == 24 ? "-" : field == 21 ? "0" : word) + std::string(" ");
        if (!pilot_.restore(clean))
            throw std::runtime_error("Economic city pilot reset failed");
        activity_.selected = site.id;
        const double density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
        SDL_SetWindowSize(window_, int(1440 / density), int(900 / density));
        renderer.freezeVisualTime(12);
        refreshFeedback(live);
        std::cout << "ECONOMIC_CITY " << site.id.str() << " name=" << site.name
                  << " light=" << feedback_->portSignals[feedbackSite_].lighting << '\n';
    }
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

    void placePort(const EconomicSite& site) {
        const auto home = u_.describe(site.id.home);
        const auto radial = glm::normalize(site.metres);
        auto tangent = glm::cross(radial, glm::dvec3(0, 1, 0));
        if (glm::length(tangent) < .01)
            tangent = glm::cross(radial, glm::dvec3(1, 0, 0));
        tangent = glm::normalize(tangent);
        const auto eye = site.metres + radial * (site.orbital ? site.radius * 2 + 100 : 8.);
        const auto q = glm::quatLookAtRH(site.orbital ? -radial : tangent, site.orbital ? tangent : radial);
        const auto offset = eye / std::pow(10., home.frame.logRadius) - q * glm::dvec3(0, 0, 1);
        std::ostringstream view;
        view.precision(17);
        view << home.address.str() << ' ' << home.frame.logRadius << ' ' << q.w << ' ' << q.x << ' ' << q.y
             << ' ' << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(view.str()))
            throw std::runtime_error("Native job placement failed");
        if (!site.orbital) {
            nav_.changeFrame(FlightController::surfaceAt(site.id.home, radial));
            nav_.setRenderScale(2);
        }
        pilot_.resetMotion();
        activity_.selected.reset();
        port_ = site.id;
        requireLink_ = false;
        if (!site.orbital && !pilot_.landAt(site.id.home, radial))
            throw std::runtime_error("Native job surface port has no safe landing approach");
    }

  public:
    EconomyValidation(const Universe& u, Navigation& n, Pilot& p, EconomyRuntime& e, ActivityState& a,
                      SDL_Window* window, WeaponSystem& weapons)
        : u_(u), nav_(n), pilot_(p), runtime_(e), activity_(a), window_(window), weapons_(weapons) {
        place();
        activity_.seconds = 0;
        runtime_.request(system_, 0);
    }
    void advance(Renderer& renderer) {
        age_++;
        if (feedback_) {
            if (stage_ >= 100 && !weaponFlight_) {
                view_ = economicMarketView(feedback_.get(), port_, 0, 10, false, page_);
                renderer.setEconomy(feedback_, &view_);
            } else
                renderer.setEconomy(feedback_, nullptr);
            return;
        }
        auto s = runtime_.snapshot();
        if (!s || !s->network || s->network->system != system_)
            return;
        auto i = s->siteIndex(*port_);
        if (!i)
            throw std::runtime_error("Existing harbour has no economic account");
        MarketContext context;
        context.hull = pilot_.ship().hull;
        context.cruiseSpeed = pilot_.ship().cruiseSpeed;
        const auto home = u_.describe(nav_.anchor);
        context.systemPosition = u_.pointRelative(
            nav_.anchor, nav_.cameraPosition() * std::pow(10., nav_.logDistance - home.frame.logRadius),
            system_, 0);
        view_ = economicMarketView(s.get(), port_,
                                   page_ == MarketPage::Services                                ? serviceRow_
                                   : page_ == MarketPage::Industry || page_ == MarketPage::Jobs ? 0
                                                                                                : 1,
                                   10, economicTradeAccess(u_, nav_, pilot_, s->network->sites[*i]), page_,
                                   context);
        if (!requireLink_) {
            pilot_.update({}, 1. / 30, renderer.submittedGeometry(nav_.anchor));
            if (age_ % 180 == 0)
                std::cout << "HAUL_APPROACH stage=" << stage_ << " agl=" << pilot_.instruments().altitude
                          << " speed=" << pilot_.instruments().speed << " landed=" << pilot_.ship().landed
                          << " phase=" << pilot_.instruments().phase << '\n';
            if (age_ > 1200)
                throw std::runtime_error("Native job approach stalled / " + pilot_.instruments().phase +
                                         " / AGL " + std::to_string(pilot_.instruments().altitude));
        }
        if (requireLink_ && !view_.connected && !s->catchingUp)
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
            std::cout << "ECONOMY_CAPTURE " << name << " page=" << unsigned(page_) << " tick=" << s->tick
                      << '\n';
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
            page_ = MarketPage::Routes;
            next(50);
        } else if (stage_ == 50 && age_ > 12) {
            capture("market-routes");
            next(59);
        } else if (stage_ == 59 && age_ > 8) {
            page_ = MarketPage::Industry;
            EconomicCommand fund{3, *port_, EconomicAction::Invest};
            fund.limitCents = facilityRefit(FacilityModule::Storage, 1).labourCents;
            runtime_.command(fund);
            next(51);
        } else if (stage_ == 51 && s->player.lastCommand >= 3 && age_ > 12) {
            if (!s->sites[*i].project)
                throw std::runtime_error("Native refit funding failed");
            capture("market-industry");
            next(52);
        } else if (stage_ == 52 && age_ > 3) {
            const auto parts = facilityRefit(FacilityModule::Storage, 1).materials;
            while (supply_ < goodCount && !parts[supply_])
                supply_++;
            if (supply_ == goodCount) {
                EconomicCommand deliver{++sequence_, *port_, EconomicAction::Invest};
                deliver.limitCents = facilityRefit(FacilityModule::Storage, 1).labourCents;
                runtime_.command(deliver);
                next(54);
            } else {
                const auto q = s->quote(*i, Good(supply_), parts[supply_]);
                runtime_.command(
                    {++sequence_, *port_, EconomicAction::Buy, Good(supply_), parts[supply_], q.buyCents});
                next(53);
            }
        } else if (stage_ == 53 && s->player.lastCommand >= sequence_ && age_ > 3) {
            if (s->player.cargo[supply_] != facilityRefit(FacilityModule::Storage, 1).materials[supply_])
                throw std::runtime_error("Native refit supply purchase failed");
            supply_++;
            next(52);
        } else if (stage_ == 54 && s->player.lastCommand >= sequence_ && age_ > 3) {
            if (s->player.massKg() || !s->sites[*i].project)
                throw std::runtime_error("Native refit material delivery failed");
            activity_.seconds = 60;
            runtime_.request(system_, 60);
            next(55);
        } else if (stage_ == 55 && s->tick >= 12 && age_ > 12) {
            if (s->sites[*i].project || s->sites[*i].modules[0] != 1 || nav_.serialize() != camera_ ||
                geometryFingerprint(*renderer.submittedGeometry(nav_.anchor)) != geometry_)
                throw std::runtime_error("Native commissioning changed world geometry or failed");
            capture("market-refit-complete");
            next(58);
        } else if (stage_ == 58 && age_ > 8) {
            SDL_SetWindowSize(window_, 480, 640);
            next(56);
        } else if (stage_ == 56 && age_ > 30) {
            capture("market-industry-portrait");
            next(61);
        } else if (stage_ == 61 && age_ > 8) {
            page_ = MarketPage::Routes;
            next(57);
        } else if (stage_ == 57 && age_ > 12) {
            capture("market-routes-portrait");
            next(62);
        } else if (stage_ == 62 && age_ > 8) {
            page_ = MarketPage::Goods;
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
                         "fit, funded refits, delivered capital, commissioning, trade intelligence and real "
                         "freight\n";
            next(70);
        } else if (stage_ == 70 && age_ > 10) {
            auto fits = [&](const auto& h) {
                return h.phase == HaulPhase::Open && h.kg <= s->player.capacityKg &&
                       h.kg * goodInfo(h.good).litresPerKg <= s->player.capacityLitres;
            };
            auto available = std::find_if(s->hauls.begin(), s->hauls.end(), [&](const auto& h) {
                return fits(h) && s->network->sites[h.source].orbital;
            });
            if (available == s->hauls.end())
                available = std::find_if(s->hauls.begin(), s->hauls.end(), fits);
            if (available == s->hauls.end())
                throw std::runtime_error("Native job board has no funded haul");
            haul_ = *available;
            page_ = MarketPage::Jobs;
            const double density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(1440 / density), int(900 / density));
            next(71);
        } else if (stage_ == 71 && age_ > 30) {
            capture("jobs-board");
            next(72);
        } else if (stage_ == 72 && age_ > 10) {
            runtime_.command({++sequence_, s->network->sites[haul_->source].id, EconomicAction::AcceptHaul,
                              haul_->good, haul_->kg, 0, haul_->id});
            next(73);
        } else if (stage_ == 73 && s->player.lastCommand >= sequence_ && age_ > 8) {
            if (!s->player.haul || s->player.haul->onboard)
                throw std::runtime_error("Native haul acceptance failed");
            placePort(s->network->sites[haul_->source]);
            next(74);
        } else if (stage_ == 74 && age_ > 15 &&
                   economicTradeAccess(u_, nav_, pilot_, s->network->sites[haul_->source])) {
            requireLink_ = true;
            haulCredits_ = s->player.credits;
            runtime_.command(
                {++sequence_, *port_, EconomicAction::PickupHaul, haul_->good, haul_->kg, 0, haul_->id});
            next(75);
        } else if (stage_ == 75 && s->player.lastCommand >= sequence_ && age_ > 12) {
            if (!s->player.haul || !s->player.haul->onboard || s->player.sealedKg() != haul_->kg ||
                s->player.credits != haulCredits_)
                throw std::runtime_error("Native sealed pickup failed");
            capture("jobs-sealed-cargo");
            next(76);
        } else if (stage_ == 76 && age_ > 10) {
            placePort(s->network->sites[haul_->destination]);
            next(77);
        } else if (stage_ == 77 && age_ > 15 &&
                   economicTradeAccess(u_, nav_, pilot_, s->network->sites[haul_->destination])) {
            requireLink_ = true;
            runtime_.command(
                {++sequence_, *port_, EconomicAction::DeliverHaul, haul_->good, haul_->kg, 0, haul_->id});
            next(78);
        } else if (stage_ == 78 && s->player.lastCommand >= sequence_ && age_ > 12) {
            if (s->player.haul || s->player.sealedKg() || s->player.deliveries != 1 ||
                s->player.credits != haulCredits_ + haul_->fee)
                throw std::runtime_error("Native funded haul delivery failed");
            capture("jobs-paid");
            std::cout << "PASS: native job board, reserved acceptance, physical pickup, sealed cargo, landed "
                         "delivery and paid carrier history\n";
            next(79);
        } else if (stage_ == 79 && age_ > 10) {
            SDL_SetWindowSize(window_, 480, 640);
            next(80);
        } else if (stage_ == 80 && age_ > 30) {
            capture("jobs-portrait");
            next(81);
        } else if (stage_ == 81 && age_ > 10) {
            auto provider = s->sites.size();
            for (size_t index = 0; index < s->sites.size(); ++index)
                if (s->network->sites[index].orbital &&
                    economicServiceQuote(*s, index, ShipService::Repair, 10).available &&
                    economicServiceQuote(*s, index, ShipService::CargoHold, 1).available) {
                    provider = index;
                    break;
                }
            if (provider == s->sites.size())
                throw std::runtime_error("Native universe has no stocked service port");
            placePort(s->network->sites[provider]);
            std::istringstream before(pilot_.serialize());
            std::string damaged, word;
            for (unsigned field = 0; before >> word; ++field)
                damaged += (field == 17 ? "83" : word) + " ";
            if (!pilot_.restore(damaged))
                throw std::runtime_error("Native damaged ship fixture failed");
            page_ = MarketPage::Services;
            const double density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(1440 / density), int(900 / density));
            next(82);
        } else if (stage_ == 82 && age_ > 30 &&
                   economicTradeAccess(u_, nav_, pilot_, s->network->sites[*i])) {
            requireLink_ = true;
            capture("services-before");
            auto q = economicServiceQuote(*s, *i, ShipService::Repair, 10);
            serviceCredits_ = s->player.credits - q.totalCents();
            repairUnits_ = s->player.repairPoints + 10;
            EconomicCommand c;
            c.sequence = ++sequence_;
            c.port = *port_;
            c.action = EconomicAction::RepairHull;
            c.serviceUnits = 10;
            c.limitCents = q.totalCents();
            runtime_.command(c);
            next(83);
        } else if (stage_ == 83 && s->player.lastCommand >= sequence_ && age_ > 12) {
            if (s->player.credits != serviceCredits_ || s->player.repairPoints != repairUnits_ ||
                pilot_.ship().hull != 93 || pilot_.ship().appliedRepairPoints != repairUnits_)
                throw std::runtime_error("Native paid repair did not reconcile with the physical ship");
            capture("services-repaired");
            serviceRow_ = 1;
            auto q = economicServiceQuote(*s, *i, ShipService::CargoHold, 1);
            serviceCredits_ -= q.totalCents();
            EconomicCommand c;
            c.sequence = ++sequence_;
            c.port = *port_;
            c.action = EconomicAction::UpgradeCargo;
            c.serviceUnits = 1;
            c.limitCents = q.totalCents();
            runtime_.command(c);
            next(84);
        } else if (stage_ == 84 && s->player.lastCommand >= sequence_ && age_ > 12) {
            if (s->player.cargoLevel != 1 || s->player.capacityKg != 6000 ||
                s->player.capacityLitres != 16000 || s->player.credits != serviceCredits_)
                throw std::runtime_error("Native cargo refit failed its funded capacity update");
            capture("services-upgraded");
            next(87);
        } else if (stage_ == 87 && age_ > 10) {
            // Let the GPU finish the wide capture before changing its backbuffer.
            SDL_SetWindowSize(window_, 480, 640);
            next(85);
        } else if (stage_ == 85 && age_ > 30) {
            capture("services-portrait");
            std::cout << "PASS: native funded repair, physical hull receipt, cargo upgrade and service "
                         "portrait fit\n";
            next(86);
        } else if (stage_ == 86 && age_ > 10) {
            placeFeedback(s, renderer);
            next(90);
        } else if (stage_ >= 90 && stage_ <= 92 && age_ > 180 && stats.surfaceBuildings > 0 &&
                   stats.surfacePending == 0 && stats.lodTransitions == 0 && stats.uploadDeferred == 0) {
            const auto world = renderer.surfaceWorld(port_->home);
            if (!world)
                return;
            if (stage_ == 90) {
                feedbackCamera_ = nav_.serialize();
                feedbackWorld_ = world->descriptor;
            } else if (nav_.serialize() != feedbackCamera_ || world->descriptor != feedbackWorld_)
                throw std::runtime_error("Economic optics changed city identity or camera");
            if (auto g = renderer.submittedGeometry(nav_.anchor)) {
                const auto hash = geometryFingerprint(*g);
                if (feedbackGeometry_ && hash != feedbackGeometry_)
                    throw std::runtime_error("Economic optics changed submitted world geometry");
                feedbackGeometry_ = hash;
            }
            const auto name = stage_ == 90   ? "economic-city-powered"
                              : stage_ == 91 ? "economic-city-offline"
                                             : "economic-city-restored";
            capture(name);
            std::cout << "ECONOMIC_LIGHT " << name
                      << " factor=" << feedback_->portSignals[feedbackSite_].lighting
                      << " world=" << world->descriptor << " geometry=" << feedbackGeometry_ << '\n';
            if (stage_ == 90) {
                next(98);
            } else if (stage_ == 91) {
                next(99);
            } else
                next(93);
        } else if (stage_ == 93 && age_ > 12) {
            auto freight = std::find_if(feedback_->flights.begin(), feedback_->flights.end(),
                                        [](const auto& f) { return f.kg > 0; });
            if (freight == feedback_->flights.end())
                throw std::runtime_error("No real loaded freight for HUD capture");
            activity_.selected = freight->id;
            next(94);
        } else if (stage_ == 94 && age_ > 20) {
            capture("economic-freight-hud");
            next(95);
        } else if (stage_ == 95 && age_ > 12) {
            SDL_SetWindowSize(window_, 480, 640);
            next(96);
        } else if (stage_ == 96 && age_ > 40) {
            capture("economic-freight-portrait");
            std::cout << "PASS: native port HUD, real-ledger power outage/recovery, preserved city/camera "
                         "and freight manifest\n";
            next(97);
        } else if (stage_ == 98 && age_ > 12) {
            feedbackLedger_->outage(*port_, 12);
            refreshFeedback(s);
            next(91);
        } else if (stage_ == 99 && age_ > 12) {
            feedbackLedger_->advanceTo(12, 64);
            refreshFeedback(s);
            next(92);
        } else if (stage_ == 97 && age_ > 12) {
            auto site = std::find_if(s->network->sites.begin(), s->network->sites.end(), [](const auto& d) {
                return !d.orbital && d.population && d.workers >= 2;
            });
            if (site == s->network->sites.end())
                throw std::runtime_error("No recovery settlement");
            port_ = site->id;
            feedbackLedger_->outage(*port_, 1000);
            feedbackLedger_->advanceTo(24);
            auto j = std::find_if(feedbackLedger_->recoveries().begin(), feedbackLedger_->recoveries().end(),
                                  [&](const auto& j) {
                                      return j.kind == RecoveryKind::Maintenance &&
                                             s->network->sites[j.site].id == *port_;
                                  });
            if (j == feedbackLedger_->recoveries().end())
                throw std::runtime_error("Real damaged plant did not post recovery");
            recoveryId_ = j->id;
            refreshFeedback(s);
            page_ = MarketPage::Jobs;
            next(100);
        } else if (stage_ == 100 && age_ > 20) {
            capture("recovery-jobs-portrait");
            next(101);
        } else if (stage_ == 101 && age_ > 12) {
            const auto j =
                *std::find_if(feedbackLedger_->recoveries().begin(), feedbackLedger_->recoveries().end(),
                              [&](const auto& j) { return j.id == recoveryId_; });
            EconomicCommand c;
            c.sequence = 1;
            c.port = *port_;
            c.action = EconomicAction::AcceptRecovery;
            c.contract = j.id;
            if (!feedbackLedger_->execute(c, recoveryPlayer_).starts_with("RECOVERY ACCEPTED"))
                throw std::runtime_error("Native recovery acceptance failed");
            for (size_t g = 0; g < goodCount; ++g)
                if (j.required[g]) {
                    std::optional<size_t> supplier;
                    int64_t cheapest = INT64_MAX;
                    for (size_t k = 0; k < s->network->sites.size(); ++k)
                        if (k != j.site) {
                            auto q = feedbackLedger_->quote(k, Good(g), j.required[g]);
                            if (q.available >= j.required[g] && q.buyCents < cheapest) {
                                supplier = k;
                                cheapest = q.buyCents;
                            }
                        }
                    if (!supplier)
                        throw std::runtime_error("No actual supplier for native kit");
                    c.sequence++;
                    c.port = s->network->sites[*supplier].id;
                    c.action = EconomicAction::Buy;
                    c.good = Good(g);
                    c.kg = j.required[g];
                    c.limitCents = cheapest;
                    if (!feedbackLedger_->execute(c, recoveryPlayer_).starts_with("BOUGHT"))
                        throw std::runtime_error("Native kit procurement failed");
                }
            c.sequence++;
            c.port = *port_;
            c.action = EconomicAction::DeliverRecovery;
            if (!feedbackLedger_->execute(c, recoveryPlayer_).starts_with("RECOVERY DELIVERED"))
                throw std::runtime_error("Native kit handover failed");
            refreshFeedback(s);
            page_ = MarketPage::Outcomes;
            next(102);
        } else if (stage_ == 102 && age_ > 20) {
            capture("recovery-working-portrait");
            next(103);
        } else if (stage_ == 103 && age_ > 12) {
            feedbackLedger_->advanceTo(88, 64);
            refreshFeedback(s);
            const auto& o = feedback_->outcomes.back();
            if (o.working || !o.outageCleared || o.crewPaid == 0)
                throw std::runtime_error("Native recovery crew did not restore existing plant");
            std::cout << "RECOVERY_OUTCOME payment=" << o.payment << " crew=" << o.crewPaid
                      << " power=" << o.before.power << ">" << o.after.power
                      << " output=" << o.after.outputKg - o.before.outputKg << '\n';
            SDL_SetWindowSize(window_, 720, 450);
            next(104);
        } else if (stage_ == 104 && age_ > 30) {
            capture("recovery-outcome");
            next(105);
        } else if (stage_ == 105 && age_ > 12) {
            SDL_SetWindowSize(window_, 480, 640);
            next(106);
        } else if (stage_ == 106 && age_ > 30) {
            capture("recovery-outcome-portrait");
            std::cout << "PASS: native funded recovery job, real supplier procurement, paid kit, crew work "
                         "and persistent outcomes\n";
            next(107);
        } else if (stage_ == 107 && age_ > 12) {
            page_ = MarketPage::Weapons;
            weapons_.setLevel(recoveryPlayer_.weaponLevel);
            next(108);
        } else if (stage_ == 108 && age_ > 20) {
            capture("weapons-shop-portrait");
            next(121);
        } else if (stage_ == 121 && age_ > 12) {
            auto q = economicWeaponQuote(*feedback_, *feedback_->siteIndex(*port_), 2);
            if (!q.available)
                throw std::runtime_error("Native armourer has no upgrade: " + q.status);
            EconomicCommand c;
            c.sequence = recoveryPlayer_.lastCommand + 1;
            c.port = *port_;
            c.action = EconomicAction::FitWeapon;
            c.serviceUnits = 2;
            c.limitCents = q.totalCents();
            if (!feedbackLedger_->execute(c, recoveryPlayer_).starts_with("WEAPON FITTED"))
                throw std::runtime_error("Weapon upgrade failed");
            refreshFeedback(s);
            weapons_.setLevel(2);
            next(109);
        } else if (stage_ == 109 && age_ > 20) {
            capture("weapons-upgraded-portrait");
            next(110);
        } else if (stage_ == 110 && age_ > 12) {
            auto q = economicWeaponQuote(*feedback_, *feedback_->siteIndex(*port_), 2, true);
            EconomicCommand c;
            c.sequence = recoveryPlayer_.lastCommand + 1;
            c.port = *port_;
            c.action = EconomicAction::SellWeapon;
            c.serviceUnits = 2;
            c.limitCents = q.sellCents;
            if (!feedbackLedger_->execute(c, recoveryPlayer_).starts_with("WEAPON SOLD") ||
                recoveryPlayer_.weaponLevel)
                throw std::runtime_error("Native weapon buyback failed");
            refreshFeedback(s);
            q = economicWeaponQuote(*feedback_, *feedback_->siteIndex(*port_), 3);
            c.sequence++;
            c.action = EconomicAction::FitWeapon;
            c.serviceUnits = 3;
            c.limitCents = q.totalCents();
            if (!feedbackLedger_->execute(c, recoveryPlayer_).starts_with("WEAPON FITTED"))
                throw std::runtime_error("Native weapon repurchase failed");
            refreshFeedback(s);
            weapons_.setLevel(3);
            next(111);
        } else if (stage_ == 111 && age_ > 20) {
            capture("weapons-rearmed-portrait");
            next(112);
        } else if (stage_ == 112 && age_ > 12) {
            weaponTarget_ = sampleActivity(u_.describe(port_->home), 300, activity_.seconds);
            weaponRay_.reference = port_->home;
            weaponRay_.origin = weaponTarget_.position + glm::dvec3(0, 0, 650);
            weaponRay_.direction = {0, 0, -1};
            weaponRay_.right = {1, 0, 0};
            weaponRay_.seconds = activity_.seconds;
            const auto q = glm::quatLookAtRH(weaponRay_.direction, glm::dvec3(0, 1, 0));
            std::ostringstream camera;
            camera.precision(17);
            const auto offset = weaponRay_.origin / 1000. - q * glm::dvec3(0, 0, 1);
            camera << port_->home.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
                   << offset.x << ' ' << offset.y << ' ' << offset.z;
            if (!nav_.restore(camera.str()))
                throw std::runtime_error("Weapon test camera failed");
            pilot_.resetMotion();
            pilot_.setVelocity({});
            weaponCamera_ = nav_.serialize();
            activity_.selected = weaponTarget_.id;
            weaponFlight_ = true;
            SDL_SetWindowSize(window_, 720, 450);
            next(113);
        } else if (stage_ == 113 && age_ > 60 && stats.activityVisible) {
            capture("weapons-idle");
            next(120);
        } else if (stage_ == 120 && age_ > 12) {
            if (!weapons_.fire(u_, weaponRay_, std::span(&weaponTarget_, 1)))
                throw std::runtime_error("Starter combat shot did not fire");
            renderer.cockpitImpulse(CockpitImpulse::Recoil, .6);
            next(114);
        } else if (stage_ == 114 && age_ > 12) {
            capture("weapons-fire");
            next(115);
        } else if (stage_ == 115 && age_ > 12) {
            for (unsigned shot = 0; shot < 4; ++shot) {
                weapons_.advance(.3);
                if (!weapons_.fire(u_, weaponRay_, std::span(&weaponTarget_, 1)))
                    throw std::runtime_error("Native sustained fire failed");
            }
            auto target = weaponTarget_;
            weapons_.apply(target);
            if (!target.disabled || target.hull != 0 || nav_.serialize() != weaponCamera_)
                throw std::runtime_error("Shots failed to disable target or moved the ship");
            WeaponSystem restored;
            if (!restored.restore(weapons_.serialize(), u_) || restored.damage().size() != 1 ||
                restored.damage()[0].hull != 0)
                throw std::runtime_error("Native combat damage failed save round trip");
            next(116);
        } else if (stage_ == 116 && age_ > 20) {
            capture("weapons-disabled");
            next(117);
        } else if (stage_ == 117 && age_ > 12) {
            SDL_SetWindowSize(window_, 480, 640);
            next(118);
        } else if (stage_ == 118 && age_ > 30) {
            capture("weapons-disabled-portrait");
            std::cout << "PASS: native weapon purchase, upgrade, buyback, repurchase, visible beam, recoil, "
                         "damage, disabled target and combat persistence\n";
            next(119);
        } else if (stage_ == 119 && age_ > 12)
            finished_ = true;
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
