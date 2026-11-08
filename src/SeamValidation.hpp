#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinity {
// Isolated native fixture: real geometry, scanner, ledger, mining beam and cockpit.
class SeamValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    Renderer& renderer_;
    WeaponSystem& weapons_;
    SDL_Window* window_;
    Node planet_;
    ProspectSite site_;
    Address source_, grain_;
    SurveyObserver observer_;
    std::shared_ptr<EconomicNetwork> network_;
    std::unique_ptr<Economy> economy_;
    EconomyPlayer player_;
    ExplorationLedger ledger_;
    SurveyController scanner_;
    MiningController miner_;
    MarketView market_;
    uint64_t sequence_ = 0, descriptor_ = 0;
    unsigned stage_ = 0, age_ = 0, stable_ = 0;
    uint64_t previousTriangles_ = 0;
    bool done_ = false, captured_ = false;
    std::string feedback_;
    void expect(bool b, const char* message) {
        if (!b)
            throw std::runtime_error(message);
    }
    std::string execute(EconomicAction action, const Address& target, double work = 8) {
        EconomicCommand c;
        c.sequence = ++sequence_;
        c.action = action;
        c.surveyTarget = target;
        c.observer = observer_;
        c.workSeconds = work;
        c.weaponMount = 1;
        c.combatPresent = true;
        return ledger_.execute(u_, *economy_, player_, c);
    }
    void setView() {
        observer_ = {};
        observer_.seconds = 100 + stage_ * 20;
        if (stage_ == 0 || stage_ >= 4) {
            observer_.reference = planet_.address;
            observer_.position = site_.metres + site_.direction * 16.;
            const auto to = u_.relative(source_, planet_.address, 0).position - observer_.position;
            observer_.attitude = glm::quatLookAtRH(glm::normalize(to), site_.direction);
        } else {
            observer_.reference = stage_ == 1 ? source_ : grain_;
            const auto n = u_.describe(observer_.reference);
            const double r = std::pow(10., n.frame.logRadius);
            observer_.position = {0, 0, r * 1.1};
            observer_.size = std::clamp(r * .001, 1e-12, .001);
        }
        auto place = [&] {
            const double log = observer_.reference.kind() >= Kind::Rock
                                   ? u_.describe(observer_.reference).frame.logRadius : 2;
            const auto offset = observer_.position / std::pow(10., log) -
                                observer_.attitude * glm::dvec3(0, 0, 1);
            std::ostringstream out;
            out.precision(17);
            out << observer_.reference.str() << ' ' << log << ' ' << observer_.attitude.w << ' '
                << observer_.attitude.x << ' ' << observer_.attitude.y << ' ' << observer_.attitude.z
                << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
            expect(nav_.restore(out.str()), "Native seam view invalid");
            pilot_.resetMotion();
            selectSeamSource(nav_, observer_.reference.kind() >= Kind::Rock ? observer_.reference : source_);
        };
        const auto desired = observer_;
        observer_.reference = planet_.address;
        observer_.position = site_.metres + site_.direction * 50.;
        place();
        if (observer_.size == 1)
            pilot_.restoreFullSize();
        else
            pilot_.adjustSize(std::log10(observer_.size) - pilot_.ship().targetLogSize);
        for (unsigned k = 0; k < 100; ++k)
            pilot_.update({}, .1);
        observer_ = desired;
        place();
        expect(std::abs(pilot_.sizeRatio() - observer_.size) < 1e-8,
               "Native seam physical ship size mismatch");
    }
  public:
    SeamValidation(const Universe& u, Navigation& nav, Pilot& pilot, Renderer& renderer,
                   WeaponSystem& weapons, SDL_Window* window)
        : u_(u), nav_(nav), pilot_(pilot), renderer_(renderer), weapons_(weapons), window_(window) {
        planet_ = u_.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0));
        auto sites = generateProspects(u_, planet_, tileAt(glm::normalize(glm::dvec3(.37, .74, 1)), 7));
        expect(!sites.empty(), "Native seam has no actual outcrop");
        site_ = sites.front();
        source_ = site_.rocks.front().address;
        grain_ = u_.children(u_.describe(source_), 1).front();
        descriptor_ = descriptorFingerprint(u_.describe(source_));
        network_ = std::make_shared<EconomicNetwork>();
        network_->system = planet_.address.parent();
        network_->universeSeed = u_.seed();
        network_->fingerprint = 570057;
        const auto body = sampleActivity(planet_, 0, 100);
        EconomicSite port;
        port.id = body.id;
        port.orbital = true;
        port.radius = body.radius;
        port.metres = body.position;
        port.capacityKg = 2000000;
        port.initial.fill(1000);
        network_->sites.push_back(port);
        economy_ = std::make_unique<Economy>(network_);
        EconomicCommand fit;
        fit.sequence = ++sequence_;
        fit.action = EconomicAction::FitMiningHead;
        fit.port = port.id;
        fit.weaponMount = 1;
        fit.limitCents = 100000000;
        SurveyObserver dock;
        dock.reference = port.id.home;
        dock.position = port.metres;
        fit.observer = dock;
        expect(ledger_.execute(u_, *economy_, player_, fit).starts_with("MINING HEAD FITTED"),
               "Native seam tool fit failed");
        renderer_.setActivity(nullptr);
        setView();
    }
    void advance() {
        ++age_;
        weapons_.advance(1. / 60);
        if (!done_) {
            if (stage_ <= 2 && scanner_.update(u_, observer_, observer_.reference.kind() >= Kind::Rock
                                                                    ? observer_.reference : site_.surface,
                                               1, 1, .1, true)) {
                feedback_ = execute(EconomicAction::ScanSurvey, stage_ == 0 ? site_.surface : observer_.reference);
                expect(feedback_.starts_with(stage_ == 0 ? "DISCOVERY" : "CLOSE SEAM SURVEY"),
                       "Native seam survey did not reach correct expedition stage");
                if (stage_ == 0) {
                    feedback_ = execute(EconomicAction::BeginSeam, source_);
                    expect(feedback_.starts_with("SEAM EXPEDITION"), "Native seam could not begin");
                }
                done_ = true;
            } else if (stage_ == 3) {
                feedback_ = execute(EconomicAction::MarkSeam, grain_);
                expect(feedback_.starts_with("MATERIAL MARKED"), "Native material mark failed");
                done_ = true;
            } else if (stage_ == 4) {
                const auto access = miningAccess(u_, observer_, source_, 1, {},
                                                markedSeam(ledger_.state(), source_));
                expect(access.allowed, "Native return could not reach marked seam");
                if (miner_.update(access, 1, 1, .1, true)) {
                    feedback_ = execute(EconomicAction::ExtractProspect, source_, 6);
                    expect(feedback_.starts_with("SEAM EXTRACTED") && player_.massKg() == 20,
                           "Native seam extraction did not produce selective cargo");
                    done_ = true;
                }
                if (miner_.progress() > 0)
                    weapons_.utilityBeam(access.ray, access.endpoint, 1);
            } else if (stage_ == 5)
                done_ = true;
        }
        auto s = std::make_shared<EconomySnapshot>();
        s->network = network_;
        s->sites = economy_->sites();
        s->player = player_;
        s->exploration = ledger_.state();
        market_ = economicMarketView(s.get(), network_->sites[0].id, 0, 10, stage_ == 5, MarketPage::Journal);
        market_.open = stage_ == 5;
        renderer_.setEconomy(s, &market_);
    }
    void annotate(FlightInstruments& t) {
        t.destination = stage_ > 0 && stage_ < 4 ? u_.describe(observer_.reference).label() : site_.name;
        t.range = 0;
        t.targetVisible = false;
        t.explorationActive = true;
        t.exploration = {"SHRINKING SEAM EXPEDITION", feedback_,
                         stage_ > 0 && stage_ < 4 ? "FRAGILE HULL / SLOW THROUGH FRACTURES" : "ENTRY BEACON / FINITE RESERVE",
                         "CTRL-F12 BEGIN OR MARK / SHIFT-J ENTRY / SHIFT-Z FULL SIZE"};
    }
    void observe(const FrameStats& stats, RegionCache& cache, const std::string& directory) {
        if (finished())
            return;
        const auto c = cache.stats();
        // Background requests can remain deferred by the cache budget. Require the visible
        // scene to stabilize and verify the actual inspected mesh before capturing it.
        const bool settled = stats.visibleObjects && !c.pending && !stats.lodTransitions &&
                             !stats.uploadDeferred && stats.triangles == previousTriangles_;
        previousTriangles_ = stats.triangles;
        stable_ = settled ? stable_ + 1 : 0;
        if (age_ > 1800)
            throw std::runtime_error("Native seam scene did not settle / " + std::to_string(stage_));
        if (!captured_ && stable_ >= 10 && done_) {
            expect(descriptorFingerprint(u_.describe(source_)) == descriptor_, "Native seam changed source");
            if (stage_ == 1 || stage_ == 2 || stage_ == 3) {
                const auto rendered = renderer_.submittedGeometry(observer_.reference);
                expect(bool(rendered), "Microscopic source not rendered");
                const auto generated = GeometryGenerator(u_).generate(u_.describe(observer_.reference),
                                             rendered->detail, nullptr, rendered->viewKey);
                expect(geometryFingerprint(*generated) == geometryFingerprint(*rendered),
                       "Inspected mesh changed canonical source geometry");
            }
            const auto root = std::filesystem::path(directory);
            std::filesystem::create_directories(root);
            renderer_.screenshot((root / ("seam-" + std::to_string(stage_) + ".ppm")).string());
            std::cout << "SEAM_CAPTURE " << stage_ << " source=" << source_.str()
                      << " hull=" << pilot_.instruments().hullMetres << " cargo=" << player_.massKg() << '\n';
            captured_ = true;
            age_ = 0;
        } else if (captured_ && age_ >= 8) {
            ++stage_;
            if (finished()) {
                ExplorationLedger restored;
                expect(restored.restore(ledger_.serialize(), u_) && restored.serialize() == ledger_.serialize(),
                       "Native seam save round trip failed");
                std::cout << "PASS: native shrinking seam expedition / 20 KG selective cargo / saved marks\n";
                return;
            }
            if (stage_ == 5) {
                const auto density = std::max(1., double(SDL_GetWindowPixelDensity(window_)));
                SDL_SetWindowSize(window_, int(600 / density), int(1000 / density));
                SDL_SyncWindow(window_);
                int w, h;
                SDL_GetWindowSizeInPixels(window_, &w, &h);
                renderer_.resize(w, h);
            }
            captured_ = done_ = false;
            age_ = stable_ = 0;
            scanner_.reset();
            setView();
        }
    }
    bool finished() const { return stage_ == 6; }
};
} // namespace infinity
