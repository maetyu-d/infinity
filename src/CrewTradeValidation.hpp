#pragma once
#include "infinite/Economy.hpp"
#include "infinite/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class CrewTradeValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    EconomyRuntime& runtime_;
    SDL_Window* window_;
    Address system_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    std::shared_ptr<const EconomySnapshot> state_;
    MarketView view_;
    std::optional<ActivityId> port_;
    CrewMember actor_;
    std::string identity_, camera_;
    unsigned stage_ = 0, age_ = 0;
    size_t selected_ = 0;
    uint64_t sequence_ = 0;
    int64_t initialCash_ = 0;
    bool finished_ = false;
    static std::string identity(const CrewMember& p) {
        std::ostringstream o;
        writeCrewPerson(o, p);
        return o.str();
    }
    void next(unsigned s) {
        stage_ = s;
        age_ = 0;
    }
    void place(const ActivityId& id, glm::dvec3 metres, double radius) {
        const auto up = glm::normalize(metres), eye = metres + up * (radius * 2 + 50);
        const auto forward = -up;
        const auto q =
            glm::quatLookAtRH(forward, std::abs(forward.y) < .98 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0));
        const auto offset = eye / 1000. - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << id.home.str() << " 3 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
             << ' ' << offset.y << ' ' << offset.z;
        if (!nav_.restore(text.str()))
            throw std::runtime_error("Crew trade camera invalid");
        pilot_.resetMotion();
        camera_ = nav_.serialize();
    }
    void submit(bool buy) {
        const auto index = *state_->siteIndex(*port_);
        const auto q = economicCrewQuote(*state_, index, actor_, buy, crew_.living());
        if (!q.available)
            throw std::runtime_error("Native crew quote unavailable / " + q.status);
        EconomicCommand c;
        c.sequence = sequence_ = state_->player.lastCommand + 1;
        c.port = *port_;
        c.action = buy ? EconomicAction::BuyCrew : EconomicAction::SellCrew;
        c.crewId = actor_.id;
        c.crewAboard = unsigned(crew_.living());
        if (!buy)
            c.person = actor_;
        c.limitCents = buy ? q.buyCents : q.sellCents;
        if (!runtime_.command(c))
            throw std::runtime_error("Native crew order queue full");
    }

  public:
    CrewTradeValidation(const Universe& u, Navigation& n, Pilot& p, ShipCrew& crew, EconomyRuntime& r,
                        SDL_Window* window)
        : u_(u), nav_(n), pilot_(p), crew_(crew), runtime_(r), window_(window) {
        const auto home = system_.child(Kind::Planet, 0);
        const auto port = sampleActivity(u.describe(home), 0, 0);
        place(port.id, port.position, port.radius);
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
        if (stage_ == 0) {
            for (size_t i = 0; i < state_->sites.size(); ++i)
                if (state_->network->sites[i].orbital) {
                    auto offers = economicCrewOffers(*state_, i);
                    if (!offers.empty() &&
                        economicCrewQuote(*state_, i, offers[0], true, crew_.living()).available) {
                        port_ = state_->network->sites[i].id;
                        actor_ = offers[0];
                        identity_ = identity(actor_);
                        initialCash_ = state_->player.credits;
                        place(*port_, state_->network->sites[i].metres, state_->network->sites[i].radius);
                        next(1);
                        break;
                    }
                }
            if (!port_)
                throw std::runtime_error("No stocked affordable orbital crew market");
        }
        if (stage_ == 3 && state_->player.lastCommand >= sequence_) {
            if (crew_.living() != 7 || !crewOwned(state_->player, actor_.id))
                throw std::runtime_error("Native crew did not board");
            next(4);
        }
        if (stage_ == 6 && state_->player.lastCommand >= sequence_) {
            const auto stock = economicCrewOffers(*state_, *state_->siteIndex(*port_));
            const auto found =
                std::find_if(stock.begin(), stock.end(), [&](const auto& m) { return m.id == actor_.id; });
            if (crew_.living() != 6 || found == stock.end() || identity(*found) != identity_)
                throw std::runtime_error("Native sale lost person identity or state");
            next(7);
        }
        if (stage_ == 9 && state_->player.lastCommand >= sequence_) {
            const auto person = std::find_if(crew_.members().begin(), crew_.members().end(),
                                             [&](const auto& m) { return m.id == actor_.id; });
            if (person == crew_.members().end() || identity(*person) != identity_ || crew_.living() != 7 ||
                state_->player.credits >= initialCash_ || nav_.serialize() != camera_)
                throw std::runtime_error("Native repurchase changed person, money or camera");
            ShipCrew copy(u_.seed());
            if (!copy.restore(crew_.serialize()) || copy.serialize() != crew_.serialize())
                throw std::runtime_error("Native traded crew did not persist");
            const auto d = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(960 / d), int(1280 / d));
            next(10);
        }
        if (port_) {
            MarketContext context;
            context.crew = crew_.members();
            const auto rows = economicCrewRows(*state_, *state_->siteIndex(*port_), crew_.members());
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].person.id == actor_.id)
                    selected_ = i;
            view_ = economicMarketView(
                state_.get(), port_, selected_, 1,
                economicTradeAccess(u_, nav_, pilot_, state_->network->sites[*state_->siteIndex(*port_)]),
                MarketPage::Crew, context);
            renderer.setEconomy(state_, &view_);
        }
    }
    void observe(Renderer& renderer, const std::string& directory) {
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "CREW_TRADE_CAPTURE " << name << " crew=" << crew_.living()
                      << " credits=" << state_->player.credits << '\n';
        };
        if (stage_ == 1 && age_ > 75) {
            capture("crew-trade-buy");
            next(2);
        } else if (stage_ == 2 && age_ > 12) {
            submit(true);
            next(3);
        } else if (stage_ == 4 && age_ > 12) {
            capture("crew-trade-aboard");
            next(5);
        } else if (stage_ == 5 && age_ > 12) {
            submit(false);
            next(6);
        } else if (stage_ == 7 && age_ > 12) {
            capture("crew-trade-sold");
            next(8);
        } else if (stage_ == 8 && age_ > 12) {
            submit(true);
            next(9);
        } else if (stage_ == 10 && age_ > 35) {
            capture("crew-trade-portrait");
            std::cout
                << "PASS: native named crew buy, paid sale, finite port stock, identical-person repurchase, "
                   "durable ownership, no resale money creation and fitted portrait crew market\n";
            next(11);
        } else if (stage_ == 11 && age_ > 12)
            finished_ = true;
        if (age_ > 1200)
            throw std::runtime_error("Native crew trade stage stalled");
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
