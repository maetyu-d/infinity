#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinity {
class ExplorationValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    WeaponSystem& weapons_;
    ActivityState& activity_;
    SDL_Window* window_;
    std::shared_ptr<EconomicNetwork> network_;
    std::unique_ptr<Economy> economy_;
    EconomyPlayer player_;
    ExplorationLedger ledger_;
    SurveyController scanner_;
    MiningController miner_;
    SurveyObserver observer_;
    Address planet_, patch_, target_;
    SurveyContract job_;
    MarketView market_;
    std::shared_ptr<EconomySnapshot> snapshot_;
    unsigned stage_ = 0, age_ = 0, stable_ = 0;
    bool ready_ = false, complete_ = false, captured_ = false;
    uint64_t sequence_ = 0, descriptor_ = 0;
    std::string feedback_;
    void expect(bool b, const char* why) {
        if (!b)
            throw std::runtime_error(why);
    }
    std::string execute(EconomicCommand c) {
        c.sequence = ++sequence_;
        return ledger_.execute(u_, *economy_, player_, c);
    }
    void place() {
        age_ = stable_ = 0;
        ready_ = complete_ = captured_ = false;
        scanner_.reset();
        miner_.reset();
        observer_ = {};
        observer_.seconds = 100;
        double log = 0;
        if (stage_ == 1 || stage_ == 2) {
            target_ = job_.target;
            const auto p = u_.describe(target_);
            observer_.reference = target_;
            log = p.frame.logRadius;
            observer_.position = {0, 0, std::pow(10., log) * 3};
        } else if (stage_ >= 4) {
            const auto p = u_.describe(planet_);
            auto d = glm::normalize(glm::dvec3(.37, .74, 1));
            const auto sun = glm::normalize(u_.relative(planet_.parent(), planet_, 0).position);
            if (glm::dot(d, sun) < 0)
                d = -d;
            patch_ = planet_.child(Kind::Surface, encodeTile(tileAt(d, 7)));
            target_ = patch_;
            observer_.reference = planet_;
            observer_.position = d * (std::pow(10., p.frame.logRadius) + u_.terrainHeight(p, d) + 10);
            auto tangent = tangentFrame(d) * glm::dvec3(0, 0, -1);
            observer_.attitude = glm::quatLookAtRH(glm::normalize(tangent - d * .2), d);
            observer_.landed = true;
            log = 3;
        } else {
            target_ = planet_;
            observer_.reference = planet_;
            observer_.position = network_->sites[0].metres;
            const auto direction = glm::normalize(-observer_.position);
            auto up = glm::normalize(glm::cross(direction, glm::dvec3(1, .2, .1)));
            observer_.attitude = glm::quatLookAtRH(direction, up);
            log = u_.describe(planet_).frame.logRadius;
        }
        const auto q = observer_.attitude;
        const auto offset = observer_.position / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream text;
        text.precision(17);
        text << observer_.reference.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' '
             << q.z << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        expect(nav_.restore(text.str()), "Native exploration view invalid");
        nav_.selected.reset();
        pilot_.resetMotion();
    }
    void next(unsigned s) {
        stage_ = s;
        place();
    }

  public:
    ExplorationValidation(const Universe& u, Navigation& n, Pilot& p, ShipCrew& crew, WeaponSystem& weapons,
                          ActivityState& activity, SDL_Window* window)
        : u_(u), nav_(n), pilot_(p), crew_(crew), weapons_(weapons), activity_(activity), window_(window) {
        planet_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        descriptor_ = descriptorFingerprint(u_.describe(planet_));
        network_ = std::make_shared<EconomicNetwork>();
        network_->system = planet_.parent();
        network_->universeSeed = u_.seed();
        network_->fingerprint = 530053;
        const auto port = sampleActivity(u_.describe(planet_), 0, 100);
        EconomicSite site;
        site.id = port.id;
        site.name = port.name;
        site.role = "SURVEY COOPERATIVE";
        site.orbital = true;
        site.radius = port.radius;
        site.metres = port.position;
        site.capacityKg = 2000000;
        site.initial.fill(10000);
        network_->sites.push_back(site);
        economy_ = std::make_unique<Economy>(network_);
        ledger_.offer(u_, *economy_);
        job_ = ledger_.state().contracts.front();
        activity_.enabled = true;
        activity_.seconds = 100;
        activity_.selected = port.id;
        observer_.reference = planet_;
        observer_.position = site.metres;
        observer_.seconds = 100;
        EconomicCommand c;
        c.port = port.id;
        c.action = EconomicAction::SellWeapon;
        c.weaponMount = 0;
        c.serviceUnits = player_.weaponAt(0);
        c.sequence = ++sequence_;
        expect(economy_->execute(c, player_).starts_with("WEAPON SOLD"),
               "Native mining station could not clear fitted laser");
        c.sequence = 0;
        c.action = EconomicAction::FitMiningHead;
        c.limitCents = 100000000;
        c.observer = observer_;
        expect(execute(c).starts_with("MINING HEAD FITTED"), "Native mining head fitting failed");
        c.action = EconomicAction::AcceptSurvey;
        c.contract = job_.id;
        expect(execute(c).starts_with("SURVEY ACCEPTED"), "Native survey reward not funded");
        weapons_.setLoadout(player_.weaponLoadout());
        crew_.setWeaponLevels(ledger_.state().stationLoadout(player_));
        place();
    }
    void advance(Renderer& renderer) {
        renderer.freezeVisualTime(100);
        activity_.seconds = 100;
        weapons_.advance(1. / 60);
        crew_.advance(.1);
        if (ready_) {
            ++age_;
            if ((stage_ == 1 || stage_ == 4) && !complete_) {
                if (scanner_.update(u_, observer_, target_, ledger_.state().scannerLevel,
                                    crew_.efficiency(CrewStation::Navigation), .1, true)) {
                    EconomicCommand c;
                    c.action = EconomicAction::ScanSurvey;
                    c.surveyTarget = target_;
                    c.observer = observer_;
                    c.workSeconds = 8;
                    feedback_ = execute(c);
                    expect(feedback_.starts_with("DISCOVERY") || feedback_.starts_with("SURVEY UPDATED"),
                           "Native survey rejected");
                    complete_ = true;
                }
            }
            if (stage_ == 5 && !complete_) {
                const auto access = miningAccess(u_, observer_, target_, 0, renderer.weaponTargets());
                expect(access.allowed, "Native surface mining has no physical access");
                const bool done = miner_.update(access, 1, 1, .1, true);
                if (miner_.progress() > 0)
                    weapons_.utilityBeam(access.ray, access.endpoint, 0);
                if (done) {
                    EconomicCommand c;
                    c.action = EconomicAction::ExtractProspect;
                    c.surveyTarget = target_;
                    c.observer = observer_;
                    c.workSeconds = 6;
                    c.weaponMount = 0;
                    c.combatPresent = true;
                    feedback_ = execute(c);
                    expect(feedback_.starts_with("EXTRACTED") && player_.massKg() == 10,
                           "Native mining did not transfer real cargo");
                    complete_ = true;
                }
            }
        }
        snapshot_ = std::make_shared<EconomySnapshot>();
        snapshot_->network = network_;
        snapshot_->sites = economy_->sites();
        snapshot_->player = player_;
        snapshot_->exploration = ledger_.state();
        snapshot_->message = feedback_;
        const auto page =
            stage_ == 2 || stage_ == 6 || stage_ == 7 ? MarketPage::Journal : MarketPage::Survey;
        market_ =
            economicMarketView(snapshot_.get(), network_->sites[0].id, stage_ == 3 ? 3 : 0, 10, true, page);
        market_.open = stage_ == 0 || stage_ == 2 || stage_ == 3 || stage_ == 6 || stage_ == 7;
        renderer.setEconomy(snapshot_, &market_);
    }
    void annotate(FlightInstruments& t) {
        t.explorationActive = stage_ == 1 || stage_ == 4 || stage_ == 5;
        t.exploration = {stage_ == 5 ? "GEOLOGICAL EXTRACTION" : "EXPLORATION SCANNER",
                         stage_ == 5 ? miner_.status() : scanner_.status(),
                         "JOURNAL " + std::to_string(ledger_.state().journal.size()) + " / MINED " +
                             std::to_string(ledger_.state().minedKg) + " KG",
                         "F11 JOURNAL / F2 SURVEY EQUIPMENT"};
        t.systemTarget = "EXPLORATION / SCIENTIFIC STRUCTURES RETAINED";
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        if (finished())
            return;
        const auto generation = cache.stats();
        const bool settled = stats.visibleObjects && !generation.pending && !generation.queued &&
                             !stats.lodTransitions && !stats.uploadDeferred && !stats.weatherPending &&
                             !stats.surfacePending;
        if (!ready_) {
            stable_ = settled ? stable_ + 1 : 0;
            if (stable_ >= 10)
                ready_ = true;
            return;
        }
        if (!captured_ && age_ >= 30) {
            static const char* names[] = {"survey-equipment",       "survey-orbit-scan",
                                          "survey-world-journal",   "survey-contract-paid",
                                          "survey-surface-scan",    "survey-mining-beam",
                                          "survey-extracted-cargo", "survey-journal-portrait"};
            renderer.screenshot(
                (std::filesystem::path(directory) / (std::string(names[stage_]) + ".ppm")).string());
            std::cout << "EXPLORATION_CAPTURE " << names[stage_] << '\n';
            captured_ = true;
        }
        if (age_ < 40 || ((stage_ == 1 || stage_ == 4 || stage_ == 5) && !complete_))
            return;
        if (stage_ == 2) {
            SurveyObserver port;
            port.reference = planet_;
            port.position = network_->sites[0].metres;
            port.seconds = 100;
            EconomicCommand c;
            c.port = job_.issuer;
            c.contract = job_.id;
            c.action = EconomicAction::ClaimSurvey;
            c.observer = port;
            feedback_ = execute(c);
            expect(feedback_.starts_with("SURVEY CONTRACT COMPLETE"), "Native survey claim failed");
        }
        if (stage_ == 6) {
            const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(600 / density), int(1000 / density));
            SDL_SyncWindow(window_);
            int w, h;
            SDL_GetWindowSizeInPixels(window_, &w, &h);
            renderer.resize(w, h);
        }
        if (stage_ == 7) {
            expect(descriptorFingerprint(u_.describe(planet_)) == descriptor_,
                   "Exploration changed world generation");
            expect(player_.massKg() == 10 && ledger_.state().surveysDelivered == 1,
                   "Native exploration outcome lost cargo or reward");
            ExplorationLedger reloaded;
            expect(reloaded.restore(ledger_.serialize(), u_), "Native exploration ledger failed restart");
            stage_ = 8;
            std::cout << "PASS: native survey, crew-assisted reading, equipment fitting, funded contract, "
                         "physical extraction, persistent journal and portrait HUD\n";
            return;
        }
        next(stage_ + 1);
    }
    bool finished() const {
        return stage_ == 8;
    }
};
} // namespace infinity
