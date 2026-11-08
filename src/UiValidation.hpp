#pragma once
#include "WindowSizing.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include <filesystem>
#include <stdexcept>

namespace infinity {
class UiValidation {
  public:
    UiValidation(SDL_Window* window, Navigation& nav, Pilot& pilot)
        : window_(window), nav_(nav), pilot_(pilot) {
        SDL_Rect usable;
        const auto b = windowBorders(window);
        int x, y, w, h;
        if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable) ||
            !SDL_GetWindowPosition(window, &x, &y) || !SDL_GetWindowSize(window, &w, &h))
            throw std::runtime_error("Unable to inspect native window work area");
        if (x - b.left < usable.x || y - b.top < usable.y || x + w + b.right > usable.x + usable.w ||
            y + h + b.bottom > usable.y + usable.h)
            throw std::runtime_error("Startup window decorations exceed the display work area");
        std::cout << "UI_STARTUP window_and_decorations=inside_work_area\n";
        pilot_.startInSystem();
        camera_ = nav_.serialize();
        // Exact framebuffer sizes below are fixture requests, not interactive
        // minimum sizes. Production startup fitting above remains exercised.
        SDL_SetWindowMinimumSize(window_, 1, 1);
        ShipCrew shipCrew(584);
        crew_ = shipCrew.view(0, CrewStation::Bridge);
        crew_.members.resize(maxShipCrew);
        for (auto& member : crew_.members)
            member.name = "LIEUTENANT LONG SURNAME";
        crew_.tasks.resize(maxShipCrew);
        for (auto& task : crew_.tasks) {
            task.status = "REPAIRING";
            task.detail = "MAINTAINING LIFE SUPPORT";
        }
        crew_.summary = "CREW EMERGENCY / ENGINE DAMAGE / RELIEF HELM MOVING";
        crew_.alert = "ENGINE ROOM FIRE / REPAIRS IN PROGRESS";
        crew_.message = "DAMAGE CONTROL ORDER CONFIRMED";
        market_.connected = market_.ready = true;
        market_.page = MarketPage::Jobs;
        market_.site = "SURFACE EQUIPMENT PORT";
        market_.role = "COLONY JOB BOARD";
        market_.selected = 14;
        market_.summary = "CARGO FULL / CREW READY";
        market_.message = "TRANSACTION COMPLETE / EQUIPMENT READY";
        market_.plan = "DELIVER TO ORBITAL HARBOUR";
        market_.planTiming = "ETA 1:20 / DEADLINE 5:00";
        for (unsigned i = 0; i < 80; ++i)
            market_.detail.push_back("JOB ROW " + std::to_string(i) + " / FUNDED COLONY DELIVERY / LONG DESCRIPTION");
    }
    void advance(Renderer& renderer) {
        if (placed_ || finished())
            return;
        const auto& c = cases_[stage_];
        const double density = std::max(1.f, SDL_GetWindowPixelDensity(window_));
        SDL_SetWindowSize(window_, int(std::lround(c.w / density)), int(std::lround(c.h / density)));
        SDL_SyncWindow(window_);
        int w, h;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w != c.w || h != c.h)
            throw std::runtime_error("UI fixture did not receive its requested framebuffer size");
        renderer.resize(w, h);
        renderer.setCockpitValidation(true);
        renderer.setMode(c.mode);
        if (help_ != c.help) {
            renderer.toggleCockpitHelp();
            help_ = c.help;
        }
        placed_ = true;
        stable_ = 0;
    }
    void apply(FlightInstruments& t, Renderer& renderer) {
        t.contactsExpanded=true;
        const auto fixture = cases_[stage_].fixture;
        if (!fixture)
            return;
        // Presentation-only stress data; the navigation and ship simulation
        // continue to use their real state, checked in observe().
        t.weapon = "MINING PROBE LAUNCHER";
        t.explorationActive = true;
        t.exploration = {"MINING PROBE / NOSE", "SIX ROUNDS / CTRL-P LAUNCH", "SOLID TERRAIN / SAVED CAVERN",
                         "ALT-P CAMERA / CTRL-J ENTRANCE"};
        t.altitude = 35;
        t.gear = t.growthBlocked = true;
        t.descent = 100;
        t.stoppingDistance = 5000;
        t.crewFire = "GUNNERS HOLD FIRE / NEUTRAL SHIPS NEARBY";
        t.targetVisible = true;
        t.localTarget = {0, 0, -1};
        t.range = 4000;
        crew_.flightOpen = fixture == 2;
        crew_.open = fixture == 3;
        market_.open = fixture == 4;
        if (fixture == 2) {
            t.stationStatus = "CAPTAIN AT PORT GUN / CREW RELIEF PILOT ACTIVE";
            t.playerGun = 1;
            t.reliefHelm = "LIEUTENANT LONG SURNAME";
        }
        if (fixture == 5) {
            t.phase = "MINING PROBE CAMERA";
            t.exploration = {"BORING / CUTTER ACTIVE", "DEPTH 80M / TARGET 80M",
                             "CUTTER 100 RPM / LOAD 80% / TIP HEAT 50%", "ESC RETURNS TO SHIP / ALT-P CAMERA"};
        }
        renderer.setCrew(&crew_);
        renderer.setEconomy(nullptr, &market_);
    }
    std::string_view notice() const {
        return cases_[stage_].fixture
                   ? "TRANSACTION COMPLETE / MINING PROBE LAUNCHER FITTED / SIX ROUNDS AVAILABLE / NEW EQUIPMENT READY"
                   : "";
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        if (captured_) {
            if (hold_) {
                --hold_;
                return;
            }
            captured_ = false;
            placed_ = false;
            if (++stage_ == std::size(cases_))
                std::cout << "PASS: native UI fit / twelve layouts and popups / world and traffic hit testing / "
                             "camera unchanged\n";
            return;
        }
        const auto gen = cache.stats();
        stable_ = stats.visibleObjects && !gen.pending && !gen.queued && !stats.lodTransitions &&
                          !stats.uploadDeferred && !stats.weatherPending
                      ? stable_ + 1
                      : 0;
        if (stable_ < 10)
            return;
        const auto& c = cases_[stage_];
        const auto v = renderer.cockpitViewport();
        const CockpitLayout layout(v.aspect());
        if (nav_.serialize() != camera_ || v.x < 0 || v.y < 0 || v.x + v.width > c.w || v.y + v.height > c.h)
            throw std::runtime_error("UI resize changed the camera or clipped the HUD viewport");
        const auto worlds = renderer.celestialContacts();
        const auto ships = renderer.activityContacts();
        if (worlds.empty() || ships.empty())
            throw std::runtime_error("UI fixture has no real contacts to test");
        const auto worldPoint = v.toPixels({layout.celestial.x + 24, 217});
        const auto shipPoint = v.toPixels({layout.traffic.x + 24, 217});
        const auto world = renderer.pick(worldPoint.x, worldPoint.y);
        const auto ship = renderer.pickActivity(shipPoint.x, shipPoint.y);
        if (help_ || c.fixture >= 3) {
            if (world || ship)
                throw std::runtime_error("Controls guide allowed world selection through its panels");
        } else if (world != worlds[cockpitCelestialRows(worlds).front()].address ||
                   (c.fixture == 2 ? bool(ship) : ship != ships[cockpitActivityRows(ships).front()].body.id))
            throw std::runtime_error("Resized/Retina contact rows select the wrong world or craft");
        const auto blocked = v.toPixels({40, 80});
        if (renderer.pick(blocked.x, blocked.y) || renderer.pickActivity(blocked.x, blocked.y))
            throw std::runtime_error("Clicking an instrument selected an object behind the HUD");
        renderer.screenshot((std::filesystem::path(directory) / (std::string(c.name) + ".ppm")).string());
        std::cout << "UI_FIT case=" << c.name << " pixels=" << c.w << ',' << c.h << " viewport=" << v.x << ','
                  << v.y << ',' << v.width << ',' << v.height << " projection=" << v.projectionScale
                  << " hit_testing=PASS camera=unchanged text_overlap=0\n";
        captured_ = true;
        hold_ = 6;
    }
    bool finished() const {
        return stage_ == std::size(cases_);
    }

  private:
    struct Case {
        const char* name;
        int w, h;
        RenderMode mode;
        bool help;
        unsigned fixture;
    };
    static constexpr Case cases_[] = {{"small", 800, 500, RenderMode::Hyperion, false, 0},
                                      {"square", 600, 600, RenderMode::Hyperion, false, 0},
                                      {"portrait", 600, 900, RenderMode::Hyperion, false, 0},
                                      {"retina", 1920, 1200, RenderMode::Hyperion, false, 0},
                                      {"wide", 1440, 900, RenderMode::Hyperion, false, 0},
                                      {"controls", 800, 500, RenderMode::Hyperion, true, 0},
                                      {"crowded", 800, 500, RenderMode::Hyperion, false, 1},
                                      {"station", 600, 600, RenderMode::Hyperion, false, 2},
                                      {"crew", 600, 900, RenderMode::Hyperion, false, 3},
                                      {"market", 800, 500, RenderMode::Hyperion, false, 4},
                                      {"probe", 800, 500, RenderMode::Hyperion, false, 5},
                                      {"probe_portrait", 600, 900, RenderMode::Hyperion, false, 5}};
    SDL_Window* window_;
    Navigation& nav_;
    Pilot& pilot_;
    std::string camera_;
    CrewView crew_;
    MarketView market_;
    size_t stage_ = 0;
    unsigned stable_ = 0, hold_ = 0;
    bool placed_ = false, captured_ = false, help_ = false;
};
} // namespace infinity
