#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinity {
class ExpeditionValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    Renderer& renderer_;
    SDL_Window* window_;
    std::shared_ptr<EconomicNetwork> network_;
    std::unique_ptr<Economy> economy_;
    EconomyPlayer player_;
    ExplorationLedger exploration_;
    ExpeditionLedger ledger_;
    SpecimenProbe probe_;
    SurveyController scanner_;
    SurveyObserver observer_;
    std::shared_ptr<EconomySnapshot> snapshot_;
    MarketView market_;
    Address planet_, target_;
    uint64_t sequence_ = 0;
    unsigned stage_ = 0, age_ = 0, stable_ = 0;
    bool captured_ = false, done_ = false;
    std::string feedback_, parkedNav_, parkedShip_;
    void expect(bool b, const char* why) {
        if (!b)
            throw std::runtime_error(why);
    }
    std::string issue(EconomicAction action) {
        EconomicCommand c;
        c.sequence = ++sequence_;
        c.action = action;
        c.contract = ledger_.state().jobs[0].id;
        c.port = network_->sites[0].id;
        c.observer = observer_;
        c.surveyTarget = target_;
        c.workSeconds = action == EconomicAction::RecoverCore ? 6 : 8;
        c.combatPresent = true;
        if (probe_.active())
            c.specimenLab = network_->sites[0].id;
        return ledger_.execute(u_, *economy_, exploration_, player_, c);
    }
    void place(const Address& a, glm::dvec3 metres, glm::dquat q, double log) {
        observer_.reference = a;
        observer_.position = metres;
        observer_.attitude = q;
        observer_.seconds = 100;
        const auto offset = metres / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << a.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << offset.x
          << ' ' << offset.y << ' ' << offset.z;
        expect(nav_.restore(s.str()), "Native specimen camera invalid");
        nav_.selected.reset();
        pilot_.resetMotion();
    }
    void next() {
        ++stage_;
        age_ = stable_ = 0;
        captured_ = done_ = false;
        scanner_.reset();
        const auto& j = ledger_.state().jobs[0];
        if (stage_ == 1) {
            feedback_ = issue(EconomicAction::DiagnoseExpedition);
            expect(feedback_.starts_with("EXPEDITION FUNDED"), "Native colony diagnosis failed");
            const auto p = u_.describe(planet_);
            const auto d = u_.describe(j.source).geographicDirection;
            const auto eye = d * (std::pow(10., p.frame.logRadius) + u_.terrainHeight(p, d) + 10);
            target_ = j.source.parent().parent();
            observer_.landed = true;
            place(planet_, eye,
                  glm::quatLookAtRH(glm::normalize(tangentFrame(d) * glm::dvec3(0, 0, -1) - d * .2), d), 3);
        } else if (stage_ == 2) {
            feedback_ = issue(EconomicAction::RecoverCore);
            expect(feedback_.starts_with("SEALED CORE"), "Native core recovery failed");
        } else if (stage_ == 3) {
            observer_.landed = true;
            observer_.size = 1;
            const auto d = glm::normalize(network_->sites[0].metres);
            place(planet_, network_->sites[0].metres,
                  glm::quatLookAtRH(-d, tangentFrame(d) * glm::dvec3(0, 0, 1)),
                  u_.describe(planet_).frame.logRadius);
            feedback_ = issue(EconomicAction::OpenSpecimen);
            expect(feedback_.starts_with("SPECIMEN LAB READY"), "Native specimen port link failed");
            parkedNav_ = nav_.serialize();
            parkedShip_ = pilot_.serialize();
            expect(probe_.enter(u_, nav_, pilot_, ledger_.state().jobs[0]), "Native probe entry failed");
            renderer_.setSpecimen(j.source);
            renderer_.setActivity(nullptr);
            target_ = j.source;
        } else if (stage_ == 4 || stage_ == 5) {
            target_ = stage_ == 4 ? j.grain() : j.lattice();
            expect(probe_.focus(u_, nav_, pilot_, target_), "Native probe magnification failed");
        } else if (stage_ == 6) {
            expect(probe_.leave(nav_, pilot_) && nav_.serialize() == parkedNav_ &&
                       pilot_.serialize() == parkedShip_,
                   "Native probe moved the parked ship");
            renderer_.setSpecimen({});
            observer_.reference = planet_;
            observer_.position = network_->sites[0].metres;
            observer_.size = 1;
            observer_.landed = true;
            feedback_ = issue(EconomicAction::RepairExpedition);
            expect(feedback_.starts_with("EXCHANGER FITTED"), "Native exchanger fitting failed");
        } else if (stage_ == 7) {
            const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_, int(600 / density), int(1000 / density));
            SDL_SyncWindow(window_);
            int w, h;
            SDL_GetWindowSizeInPixels(window_, &w, &h);
            renderer_.resize(w, h);
        }
    }

  public:
    ExpeditionValidation(const Universe& u, Navigation& n, Pilot& p, Renderer& r, SDL_Window* w)
        : u_(u), nav_(n), pilot_(p), renderer_(r), window_(w) {
        planet_ = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        network_ = std::make_shared<EconomicNetwork>();
        network_->system = planet_.parent();
        network_->universeSeed = u_.seed();
        network_->fingerprint = 550055;
        EconomicSite site;
        site.id = {planet_, 0};
        site.name = "MATERIAL EXPEDITION COLONY";
        site.role = "COLONY LAB";
        site.population = 1000;
        site.workers = 20;
        site.power = 20;
        site.radius = 100;
        site.capacityKg = 2000000;
        site.initial.fill(10000);
        const auto planet = u_.describe(planet_);
        auto d = glm::normalize(glm::dvec3(.37, .74, 1));
        site.metres = d * (std::pow(10., planet.frame.logRadius) + u_.terrainHeight(planet, d));
        network_->sites.push_back(site);
        economy_ = std::make_unique<Economy>(network_);
        ledger_.offer(u_, *economy_);
        target_ = planet_;
        observer_.landed = true;
        place(planet_, site.metres, glm::quatLookAtRH(-d, tangentFrame(d) * glm::dvec3(0, 0, 1)),
              planet.frame.logRadius);
        renderer_.setActivity(nullptr);
    }
    void advance() {
        ++age_;
        if (stage_ == 1 && !done_) {
            if (scanner_.update(u_, observer_, target_, 1, 1, .1, true)) {
                EconomicCommand c;
                c.sequence = ++sequence_;
                c.action = EconomicAction::ScanSurvey;
                c.surveyTarget = target_;
                c.observer = observer_;
                c.workSeconds = 8;
                feedback_ = exploration_.execute(u_, *economy_, player_, c);
                expect(feedback_.starts_with("DISCOVERY"), "Native core survey failed");
                done_ = true;
            }
        } else if (stage_ >= 3 && stage_ <= 5 && !done_) {
            observer_.reference = target_;
            observer_.position = {0, 0, 3 * std::pow(10., u_.describe(target_).frame.logRadius)};
            observer_.attitude = {1, 0, 0, 0};
            observer_.landed = false;
            observer_.size = 1e-12;
            if (scanner_.update(u_, observer_, target_, 1, 1, .1, true)) {
                feedback_ = issue(EconomicAction::AssaySpecimen);
                expect(stage_ == 5 ? feedback_.starts_with("ATOMIC ASSAY COMPLETE")
                                   : feedback_.starts_with("SOURCE ASSAY"),
                       "Native source assay failed");
                done_ = true;
            }
        } else
            done_ = true;
        snapshot_ = std::make_shared<EconomySnapshot>();
        snapshot_->network = network_;
        snapshot_->sites = economy_->sites();
        snapshot_->player = player_;
        snapshot_->exploration = exploration_.state();
        snapshot_->expeditions = ledger_.state();
        snapshot_->outcomes = economy_->outcomes();
        snapshot_->message = feedback_;
        snapshot_->portSignals = economicPortSignals(*snapshot_);
        market_ =
            economicMarketView(snapshot_.get(), network_->sites[0].id, 0, 10, true, MarketPage::Expeditions);
        market_.open = stage_ < 3 || stage_ >= 6;
        renderer_.setEconomy(snapshot_, &market_);
    }
    void annotate(FlightInstruments& t) {
        t.explorationActive = true;
        t.exploration = {probe_.active() ? "SPECIMEN PROBE / SHIP PARKED" : "COLONY MATERIAL EXPEDITION",
                         expeditionObjective(ledger_.state().jobs[0]), scanner_.status(),
                         "SHIFT-F11 EXPEDITIONS / F4 NEXT / F12 ASSAY"};
        if (probe_.active()) {
            t.phase = "SPECIMEN INSPECTION";
            t.destination = "PROBE / " + u_.describe(target_).label();
        }
    }
    void observe(const FrameStats& stats, RegionCache& cache, const std::string& dir) {
        const auto generation = cache.stats();
        const bool settled = stats.visibleObjects && !stats.lodTransitions && !stats.uploadDeferred &&
                             !generation.pending && !generation.queued;
        stable_ = settled ? stable_ + 1 : 0;
        if (age_ > 1600)
            throw std::runtime_error("Native expedition scene did not settle / stage " +
                                     std::to_string(stage_));
        if (!captured_ && stable_ >= 8 && done_) {
            if (probe_.active()) {
                expect(!stats.rootCells && !stats.terrainTiles && !stats.activityVisible,
                       "Specimen leaked universe backdrop");
                const auto submitted = renderer_.submittedGeometry(target_);
                expect(bool(submitted), "Canonical source geometry was not submitted");
                GeometryGenerator generator(u_);
                const auto original =
                    generator.generate(u_.describe(target_), submitted->detail, nullptr, submitted->viewKey);
                expect(geometryFingerprint(*submitted) == geometryFingerprint(*original),
                       "Lab presentation changed canonical source geometry");
                std::cout << "SPECIMEN_ID " << kindName(target_.kind()) << " address=" << target_.str()
                          << " descriptor=" << submitted->descriptor
                          << " geometry=" << geometryFingerprint(*submitted) << '\n';
            }
            renderer_.screenshot(
                (std::filesystem::path(dir) / ("expedition-" + std::to_string(stage_) + ".ppm")).string());
            captured_ = true;
            age_ = 0;
        } else if (captured_ && age_ >= 8) {
            if (stage_ == 7) {
                ExpeditionLedger loaded;
                expect(loaded.restore(ledger_.serialize(), u_), "Native expedition save failed");
                Economy restarted(network_);
                expect(restarted.restore(economy_->serialize()), "Native repaired colony failed restart");
                stage_ = 8;
                std::cout
                    << "PASS: native colony diagnosis, finite core recovery, source rock/mineral/atomic "
                       "assays, parked-ship round trip, funded repair and portrait expedition HUD\n";
            } else
                next();
        }
    }
    bool finished() const {
        return stage_ == 8;
    }
};
} // namespace infinity
