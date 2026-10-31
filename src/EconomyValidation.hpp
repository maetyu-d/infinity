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
        } else if (stage_ == 86 && age_ > 10)
            finished_ = true;
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
