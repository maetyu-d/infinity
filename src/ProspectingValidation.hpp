#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace infinity {
class ProspectingValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    Renderer& renderer_;
    WeaponSystem& weapons_;
    SDL_Window* window_;
    Node planet_, original_;
    ProspectSite site_, originalSite_;
    SurveyObserver observer_;
    std::shared_ptr<EconomicNetwork> network_;
    std::unique_ptr<Economy> economy_;
    EconomyPlayer player_;
    ExplorationLedger ledger_;
    SurveyController scanner_;
    MiningController miner_;
    MarketView market_;
    std::shared_ptr<EconomySnapshot> snapshot_;
    unsigned stage_ = 0, age_ = 0, stable_ = 0;
    bool captured_ = false, done_ = false;
    uint64_t sequence_ = 0;
    std::string feedback_;
    void expect(bool b, const char* why) {
        if (!b)
            throw std::runtime_error(why);
    }
    ProspectSite choose(const Node& p) {
        const auto sun = glm::normalize(u_.relative(p.address.parent(), p.address, 0).position);
        for (unsigned i = 0; i < 12; ++i) {
            auto d = glm::normalize(
                sun + tangentFrame(sun) * glm::dvec3(std::sin(i * 2.4) * .12, 0, std::cos(i * 2.4) * .12));
            auto sites = generateProspects(u_, p, tileAt(d, 7));
            if (!sites.empty())
                return sites.front();
        }
        throw std::runtime_error("Native geology has no above-water prospect");
    }
    std::string execute(EconomicCommand c) {
        c.sequence = ++sequence_;
        return ledger_.execute(u_, *economy_, player_, c);
    }
    void place() {
        scanner_.reset();
        miner_.reset();
        age_ = stable_ = 0;
        captured_ = done_ = false;
        const auto up = site_.direction;
        const auto side = tangentFrame(up) * glm::dvec3(0, 0, 1);
        const double height = stage_ == 0   ? 12000
                              : stage_ == 1 ? 36
                                            : 14,
                     back = stage_ == 0   ? 6000
                            : stage_ == 1 ? 100
                                          : 18;
        const auto eye = site_.metres + up * height + side * back;
        const auto q = glm::quatLookAtRH(glm::normalize(site_.outcropMetres + up * 6. - eye), up);
        const double log = stage_ == 0 ? 3 : 2;
        const auto offset = eye / std::pow(10., log) - q * glm::dvec3(0, 0, 1);
        std::ostringstream s;
        s.precision(17);
        s << planet_.address.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z
          << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
        expect(nav_.restore(s.str()), "Native prospect camera invalid");
        nav_.select(site_.surface);
        pilot_.resetMotion();
        FlightWaypoint point;
        point.reference = planet_.address;
        point.metres = site_.metres + site_.direction * 16.;
        point.standOff = 12;
        point.surfacePort = true;
        point.label = site_.name;
        pilot_.trackWaypoint(point);
        observer_ = {};
        observer_.reference = planet_.address;
        observer_.position = eye;
        observer_.attitude = q;
        observer_.seconds = 100;
    }

  public:
    ProspectingValidation(const Universe& u, Navigation& n, Pilot& p, Renderer& renderer,
                          WeaponSystem& weapons, SDL_Window* window)
        : u_(u), nav_(n), pilot_(p), renderer_(renderer), weapons_(weapons), window_(window) {
        planet_ = u_.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0));
        original_ = planet_;
        site_ = choose(planet_);
        originalSite_ = site_;
        network_ = std::make_shared<EconomicNetwork>();
        network_->system = planet_.address.parent();
        network_->universeSeed = u_.seed();
        network_->fingerprint = 560056;
        const auto port = sampleActivity(planet_, 0, 100);
        EconomicSite d;
        d.id = port.id;
        d.name = "GEOLOGICAL EQUIPMENT LAB";
        d.orbital = true;
        d.radius = port.radius;
        d.metres = port.position;
        d.capacityKg = 2000000;
        d.initial.fill(1000);
        network_->sites.push_back(d);
        economy_ = std::make_unique<Economy>(network_);
        EconomicCommand c;
        c.action = EconomicAction::FitMiningHead;
        c.port = d.id;
        c.weaponMount = 1;
        c.limitCents = 100000000;
        SurveyObserver at;
        at.reference = d.id.home;
        at.position = d.metres;
        c.observer = at;
        expect(execute(c).starts_with("MINING HEAD FITTED"), "Native prospecting equipment fit failed");
        weapons_.setLoadout({0, 0, 0});
        renderer_.setActivity(nullptr);
        place();
    }
    void advance() {
        ++age_;
        weapons_.advance(1. / 60);
        if (stage_ == 2 && !done_) {
            if (scanner_.update(u_, observer_, site_.surface, 1, 1, .1, true)) {
                EconomicCommand c;
                c.action = EconomicAction::ScanSurvey;
                c.surveyTarget = site_.surface;
                c.observer = observer_;
                c.workSeconds = 8;
                feedback_ = execute(c);
                expect(feedback_.starts_with("DISCOVERY"), "Native visible outcrop could not be surveyed");
                done_ = true;
            }
        } else if (stage_ == 3 && !done_) {
            const auto access = miningAccess(u_, observer_, site_.surface, 1);
            expect(access.allowed, "Generated site does not allow actual near-ground mining");
            const auto ready = miner_.update(access, 1, 1, .1, true);
            if (miner_.progress() > 0)
                weapons_.utilityBeam(access.ray, access.endpoint, 1);
            if (ready) {
                EconomicCommand c;
                c.action = EconomicAction::ExtractProspect;
                c.surveyTarget = site_.surface;
                c.observer = observer_;
                c.workSeconds = 6;
                c.weaponMount = 1;
                c.combatPresent = true;
                feedback_ = execute(c);
                expect(feedback_.starts_with("EXTRACTED") && player_.massKg() == 10,
                       "Visible outcrop did not supply real finite cargo");
                done_ = true;
            }
        } else
            done_ = true;
        snapshot_ = std::make_shared<EconomySnapshot>();
        snapshot_->network = network_;
        snapshot_->sites = economy_->sites();
        snapshot_->player = player_;
        snapshot_->exploration = ledger_.state();
        snapshot_->message = feedback_;
        market_ =
            economicMarketView(snapshot_.get(), network_->sites[0].id, 0, 10, false, MarketPage::Journal);
        market_.open = stage_ == 5;
        renderer_.setEconomy(snapshot_, &market_);
    }
    void annotate(FlightInstruments& t) {
        t.explorationActive = true;
        t.exploration = {"GEOLOGICAL PROSPECT / " + site_.name,
                         std::string(site_.easy ? "GENTLE APPROACH" : "ROUGH GROUND / HOVER") + " / SLOPE " +
                             std::to_string(int(site_.slopeDegrees)) + " DEG",
                         stage_ == 2   ? scanner_.status()
                         : stage_ == 3 ? miner_.status()
                                       : "VISIBLE CANONICAL OUTCROPS / REGIONAL RESOURCE FIELD",
                         "SHIFT-F12 NEXT PROSPECT / J APPROACH / F12 SURVEY / CTRL-SPACE EXTRACT"};
        t.destination = site_.name;
    }
    void observe(const FrameStats& stats, RegionCache& cache, const std::string& directory) {
        if (finished())
            return;
        const auto g = cache.stats();
        const bool settled = stats.terrainTiles > 50 && stats.prospectSites && !g.pending && !g.queued &&
                             !stats.lodTransitions && !stats.uploadDeferred && !stats.weatherPending;
        stable_ = settled ? stable_ + 1 : 0;
        if (age_ > 2000)
            throw std::runtime_error("Native prospect scene failed to settle / " + std::to_string(stage_));
        if (!captured_ && stable_ >= 8 && done_) {
            expect(descriptorFingerprint(site_.rocks[0]) ==
                       descriptorFingerprint(u_.describe(site_.rocks[0].address)),
                   "Prospecting changed source identity");
            if (stage_ == 1 || stage_ == 2 || stage_ == 3) {
                expect(stats.prospectRocks > 0, "No geological outcrops requested");
                expect(bool(renderer_.submittedGeometry(site_.rocks[0].address)),
                       "Actual outcrop geometry missing");
                const auto rendered = renderer_.submittedGeometry(site_.rocks[0].address);
                const auto generated = GeometryGenerator(u_).generate(
                    u_.describe(site_.rocks[0].address), rendered->detail, nullptr, rendered->viewKey);
                expect(geometryFingerprint(*rendered) == geometryFingerprint(*generated),
                       "Outcrop renderer substituted source geometry");
                std::cout << "OUTCROP_ID descriptor=" << rendered->descriptor
                          << " geometry=" << geometryFingerprint(*rendered) << '\n';
            }
            const auto root = std::filesystem::path(directory);
            std::filesystem::create_directories(root);
            if (stage_ == 1) {
                std::ofstream file(root / "prospect-view.txt");
                file << nav_.serialize();
            }
            renderer_.screenshot((root / ("prospect-" + std::to_string(stage_) + ".ppm")).string());
            std::cout << "PROSPECT_CAPTURE " << stage_ << " source=" << site_.rocks[0].address.str()
                      << " slope=" << site_.slopeDegrees << " grade=" << site_.concentration
                      << " rocks=" << stats.prospectRocks << " markers=" << stats.prospectMarkers << '\n';
            captured_ = true;
            age_ = 0;
        } else if (captured_ && age_ >= 8) {
            ++stage_;
            if (stage_ == 4) {
                for (const auto& address : u_.children(u_.describe(original_.address.parent()))) {
                    const auto p = u_.describe(address);
                    if (hasSolidSurface(p) && p.environment.temperature < 180 && p.geology->ice > .3) {
                        planet_ = p;
                        site_ = choose(p);
                        break;
                    }
                }
            }
            if (stage_ == 5) {
                planet_ = original_;
                site_ = originalSite_;
                const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
                SDL_SetWindowSize(window_, int(600 / density), int(1000 / density));
                SDL_SyncWindow(window_);
                int w, h;
                SDL_GetWindowSizeInPixels(window_, &w, &h);
                renderer_.resize(w, h);
            }
            if (finished()) {
                ExplorationLedger restored;
                expect(restored.restore(ledger_.serialize(), u_) &&
                           restored.state().prospects[0].extracted == ledger_.state().prospects[0].extracted,
                       "Prospect depletion lost on restart");
                std::cout << "PASS: native geological reconnaissance, physical canonical outcrops, "
                             "accessible survey and extraction, cold-world variation and portrait journal\n";
            } else
                place();
        }
    }
    bool finished() const {
        return stage_ == 6;
    }
};
} // namespace infinity
