#include "CelestialValidation.hpp"
#include "CloudValidation.hpp"
#include "CockpitMotionValidation.hpp"
#include "CombatOperationsValidation.hpp"
#include "CrewFlightValidation.hpp"
#include "CrewGunsValidation.hpp"
#include "CrewTradeValidation.hpp"
#include "CrewValidation.hpp"
#include "EconomyValidation.hpp"
#include "FlightValidation.hpp"
#include "LandingLightValidation.hpp"
#include "NpcValidation.hpp"
#include "PilotValidation.hpp"
#include "PlanetDiversityValidation.hpp"
#include "PlanetValidation.hpp"
#include "PointLightValidation.hpp"
#include "PolarValidation.hpp"
#include "StationSwapValidation.hpp"
#include "StellarDiversityValidation.hpp"
#include "StreamingValidation.hpp"
#include "SurfaceValidation.hpp"
#include "TerrainValidation.hpp"
#include "UiValidation.hpp"
#include "WeatherValidation.hpp"
#include "WindowSizing.hpp"
#include "infinite/Cockpit.hpp"
#include "infinite/Continuity.hpp"
#include "infinite/Crew.hpp"
#include "infinite/Economy.hpp"
#include "infinite/FireControl.hpp"
#include "infinite/Flight.hpp"
#include "infinite/NpcCombat.hpp"
#include "infinite/Piloting.hpp"
#include "infinite/Recovery.hpp"
#include "infinite/Renderer.hpp"
#include "infinite/StationControl.hpp"
#include "infinite/Weapons.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <oneapi/tbb/global_control.h>
#include <sstream>
#include <thread>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

using namespace infinite;
struct Options {
    bool tour = false, smoke = false, fresh = false, benchmark = false;
    int frames = 0, width = 1440, height = 900;
    uint64_t seed = 0x494e46494e495445ULL;
    std::string captures, profile, data, terrainView;
    RenderMode renderMode = RenderMode::Hyperion;
    double visualTime = -1;
    bool presentationTest = false;
    bool flightTest = false, planetTest = false, uiFitTest = false, polarTest = false, cloudTest = false,
         celestialTest = false, celestialSunOnly = false, landingLightTest = false, weatherTest = false,
         diversityTest = false, stellarDiversityTest = false;
    bool streamingTest = false;
    bool terrainQuality = false, terrainSurfaceFrame = false, sunShadows = true;
    bool economyTest = false, cockpitMotionTest = false, npcTest = false, crewTest = false,
         crewTradeTest = false, crewFlightTest = false, crewGunsTest = false, stationSwapTest = false,
         starStabilityTest = false, combatOperationsTest = false;
    bool pilotTest = false, terrainTest = false, surfaceTest = false;
};
static Options options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() {
            if (i + 1 >= argc)
                throw std::invalid_argument("Missing value for " + arg);
            return std::string(argv[++i]);
        };
        if (arg == "--tour")
            o.tour = true;
        else if (arg == "--no-sun-shadows")
            o.sunShadows = false;
        else if (arg == "--smoke-test")
            o.smoke = o.tour = o.fresh = true;
        else if (arg == "--benchmark-tour")
            o.benchmark = o.smoke = o.tour = o.fresh = true;
        else if (arg == "--star-stability-test") {
            o.smoke = o.fresh = o.starStabilityTest = o.benchmark = true;
            o.visualTime = 12;
        } else if (arg == "--combat-operations-test") {
            o.smoke = o.fresh = o.combatOperationsTest = o.benchmark = true;
            o.visualTime = 60;
        } else if (arg == "--station-swap-test") {
            o.smoke = o.fresh = o.stationSwapTest = o.benchmark = true;
            o.visualTime = 12;
        } else if (arg == "--crew-guns-test") {
            o.smoke = o.fresh = o.crewGunsTest = o.benchmark = true;
            o.tour = false;
        } else if (arg == "--crew-flight-test") {
            o.smoke = o.fresh = o.crewFlightTest = o.benchmark = true;
            o.tour = false;
        } else if (arg == "--crew-trade-test") {
            o.smoke = o.fresh = o.crewTradeTest = o.benchmark = true;
            o.tour = false;
        } else if (arg == "--crew-test") {
            o.smoke = o.fresh = o.crewTest = o.benchmark = true;
            o.tour = false;
        } else if (arg == "--npc-combat-test") {
            o.smoke = o.fresh = o.npcTest = o.benchmark = true;
            o.tour = false;
        } else if (arg == "--economy-test") {
            o.economyTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--cockpit-motion-test") {
            o.cockpitMotionTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--stellar-diversity-test") {
            o.stellarDiversityTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--planet-diversity-test") {
            o.diversityTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--weather-visibility-test") {
            o.weatherTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--landing-light-test") {
            o.landingLightTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--celestial-graphics-test" || arg == "--stellar-sun-test") {
            o.celestialSunOnly = arg == "--stellar-sun-test";
            o.celestialTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--cloud-volume-test") {
            o.cloudTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--polar-optics-test") {
            o.polarTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--ui-fit-test") {
            o.uiFitTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--planet-stability-test") {
            o.planetTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--flight-test") {
            o.flightTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--streaming-test") {
            o.streamingTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--pilot-test") {
            o.pilotTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--terrain-saved-view-test") {
            o.terrainView = value();
            o.terrainTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--terrain-surface-frame-test") {
            o.terrainSurfaceFrame = o.terrainQuality = o.terrainTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--terrain-quality-test") {
            o.terrainQuality = o.terrainTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--terrain-test") {
            o.terrainTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--surface-test") {
            o.surfaceTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--fresh")
            o.fresh = true;
        else if (arg == "--render-mode") {
            o.renderMode = parseRenderMode(value());
        } else if (arg == "--visual-time")
            o.visualTime = std::stod(value());
        else if (arg == "--presentation-test") {
            o.presentationTest = o.benchmark = o.smoke = o.tour = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--frames")
            o.frames = std::stoi(value());
        else if (arg == "--width")
            o.width = std::stoi(value());
        else if (arg == "--height")
            o.height = std::stoi(value());
        else if (arg == "--seed")
            o.seed = std::stoull(value(), nullptr, 0);
        else if (arg == "--capture-dir")
            o.captures = value();
        else if (arg == "--profile")
            o.profile = value();
        else if (arg == "--data")
            o.data = value();
        else if (arg == "--help") {
            std::cout
                << "Infinite pilot: W/S throttle, A/D strafe, Q/E lift, U/O or Alt+A/D roll, B brake, X "
                   "assist, "
                   "J navigate, L land, Enter launch, G gear, N long cruise, Z shrink, comma/period "
                   "size, Y planets, Shift-Y moons, V explorer. "
                   "Space fire laser, F9 crew console, F10 swap with gunner / return helm, "
                   "C mouse helm, arrows pitch/yaw or gun aim, right-drag look, "
                   "-/+ speed, Shift boost, Ctrl fine, scroll scale, click select, F focus, "
                   "Backspace parent, F1 controls, F7 cockpit motion, Tab overlay, R reset, M marker, P "
                   "screenshot, T tour, Esc "
                   "release mouse / quit.\nCLI: --fresh --tour --smoke-test --frames N --width N "
                   "--height N --seed N "
                   "--benchmark-tour --flight-test --streaming-test --pilot-test --planet-stability-test "
                   "--economy-test --station-swap-test --cockpit-motion-test --ui-fit-test "
                   "--polar-optics-test "
                   "--cloud-volume-test "
                   "--celestial-graphics-test "
                   "--stellar-diversity-test --stellar-sun-test --landing-light-test "
                   "--weather-visibility-test "
                   "--planet-diversity-test "
                   "--terrain-test "
                   "--surface-test --terrain-quality-test --terrain-surface-frame-test "
                   "--terrain-saved-view-test FILE --no-sun-shadows "
                   "--capture-dir DIR "
                   "--profile CSV "
                   "--data DIR\n";
            std::cout
                << "Hyperion rendering. --render-mode hyperion --presentation-test --visual-time SECONDS\n";
            std::cout
                << "F2 market, F5 goods/routes/industry/jobs/services/outcomes/weapons; arrows select, Enter "
                   "buy/fund/deliver, "
                   "Backspace "
                   "sell/collect; F3 supply "
                   "contract.\nLocal activity: K cycles traffic, I hails, J approaches/matches course, F6 "
                   "hides/shows. Shift-K cycles surface ports; J approaches, L lands.\n";
            std::exit(0);
        } else
            throw std::invalid_argument("Unknown option " + arg);
    }
    if (o.presentationTest || o.terrainTest || o.surfaceTest || o.planetTest) {
        o.visualTime = 12;
    }
    const unsigned validationModes =
        unsigned(o.economyTest) + unsigned(o.flightTest) + unsigned(o.streamingTest) + unsigned(o.pilotTest) +
        unsigned(o.terrainTest) + unsigned(o.surfaceTest) + unsigned(o.planetTest) + unsigned(o.uiFitTest) +
        unsigned(o.polarTest) + unsigned(o.cloudTest) + unsigned(o.celestialTest) +
        unsigned(o.landingLightTest) + unsigned(o.weatherTest) + unsigned(o.diversityTest) +
        unsigned(o.stellarDiversityTest) + unsigned(o.npcTest) + unsigned(o.crewTest) +
        unsigned(o.crewTradeTest) + unsigned(o.crewFlightTest) + unsigned(o.crewGunsTest) +
        unsigned(o.stationSwapTest) + unsigned(o.starStabilityTest) + unsigned(o.combatOperationsTest);
    if (validationModes > 1 || (validationModes && o.tour))
        throw std::invalid_argument("Choose one automated validation mode");
    if (o.width < 320 || o.height < 240 || o.width > 7680 || o.height > 4320 || o.frames < 0)
        throw std::invalid_argument("Invalid window size or frame count");
    return o;
}
int main(int argc, char** argv) {
    SDL_Window* window = nullptr;
    try {
        auto opt = options(argc, argv);
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism, 4);
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
            throw std::runtime_error(SDL_GetError());
        window = SDL_CreateWindow("INFINITE / A deterministic universe", opt.width, opt.height,
                                  (opt.smoke ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0) |
                                      ((!opt.smoke || opt.uiFitTest) ? SDL_WINDOW_RESIZABLE : 0));
        if (!window)
            throw std::runtime_error(SDL_GetError());
        if (opt.smoke && !opt.uiFitTest) {
            double density = std::max(1.0, static_cast<double>(SDL_GetWindowPixelDensity(window)));
            SDL_SetWindowSize(window, static_cast<int>(std::lround(opt.width / density)),
                              static_cast<int>(std::lround(opt.height / density)));
            SDL_SyncWindow(window);
        } else {
            SDL_SyncWindow(window);
            fitWindowToDisplay(window, true);
        }
        int pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        std::cout << "BACKBUFFER " << pixelWidth << 'x' << pixelHeight << " (requested " << opt.width << 'x'
                  << opt.height << ")\n";
        Universe universe(opt.seed);
        Navigation nav(universe);
        if (opt.presentationTest)
            nav.parent();
        RegionCache cache(universe);
        FlightController flight(universe, nav, cache);
        Pilot pilot(universe, nav, flight);
        bool pilotEnabled = (!opt.smoke && !opt.tour) || opt.economyTest || opt.pilotTest ||
                            opt.terrainTest || opt.surfaceTest || opt.planetTest || opt.uiFitTest ||
                            opt.polarTest || opt.cloudTest || opt.celestialTest || opt.landingLightTest ||
                            opt.weatherTest || opt.diversityTest || opt.stellarDiversityTest ||
                            opt.cockpitMotionTest || opt.npcTest || opt.crewTest || opt.crewTradeTest ||
                            opt.crewFlightTest || opt.crewGunsTest || opt.stationSwapTest ||
                            opt.starStabilityTest || opt.combatOperationsTest;
        std::unique_ptr<FlightValidation> flightProof;
        if (opt.flightTest)
            flightProof = std::make_unique<FlightValidation>(universe, nav);
        std::unique_ptr<StreamingValidation> streamingProof;
        if (opt.streamingTest)
            streamingProof = std::make_unique<StreamingValidation>(universe, nav, cache);
        auto executable = std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0]));
        auto defaultData = executable.parent_path() / "user-data";
        if (executable.parent_path().filename() == "MacOS" &&
            executable.parent_path().parent_path().filename() == "Contents")
            defaultData = executable.parent_path().parent_path().parent_path().parent_path() / "user-data";
        std::filesystem::path data = opt.data.empty() ? defaultData : std::filesystem::path(opt.data);
        std::filesystem::create_directories(data);
        Persistence persistence((data / "universe.sqlite").string(), universe.seed());
        if (!opt.fresh)
            if (auto state = persistence.loadState())
                nav.restore(*state);
        if (!opt.captures.empty())
            std::filesystem::create_directories(opt.captures);
        {
            Renderer renderer(window, universe, opt.benchmark);
            renderer.setSunShadows(opt.sunShadows);
            renderer.setMode(opt.renderMode);
            if (!opt.smoke) {
                std::ifstream motionSettings(data / "cockpit-motion.txt");
                int enabled = 1;
                if (motionSettings >> enabled && (enabled == 0 || enabled == 1))
                    renderer.setCockpitMotionEnabled(enabled != 0);
            }
            std::unique_ptr<TerrainValidation> terrainProof;
            std::string terrainView;
            if (!opt.terrainView.empty()) {
                std::ifstream input(opt.terrainView);
                std::ostringstream contents;
                contents << input.rdbuf();
                terrainView = contents.str();
                if (terrainView.empty())
                    throw std::runtime_error("Cannot read terrain view fixture");
            }
            if (opt.terrainTest)
                terrainProof = std::make_unique<TerrainValidation>(universe, nav, pilot, opt.terrainQuality,
                                                                   opt.terrainSurfaceFrame, terrainView);
            std::unique_ptr<PilotValidation> pilotProof;
            if (opt.pilotTest)
                pilotProof = std::make_unique<PilotValidation>(universe, nav, pilot);
            std::unique_ptr<CockpitMotionValidation> motionProof;
            if (opt.cockpitMotionTest)
                motionProof = std::make_unique<CockpitMotionValidation>(universe, nav, pilot);
            std::unique_ptr<PlanetValidation> planetProof;
            if (opt.planetTest)
                planetProof = std::make_unique<PlanetValidation>(universe, nav, pilot);
            std::unique_ptr<UiValidation> uiProof;
            if (opt.uiFitTest)
                uiProof = std::make_unique<UiValidation>(window, nav, pilot);
            std::unique_ptr<PolarValidation> polarProof;
            if (opt.polarTest)
                polarProof = std::make_unique<PolarValidation>(universe, nav, pilot);
            std::unique_ptr<CloudValidation> cloudProof;
            if (opt.cloudTest)
                cloudProof = std::make_unique<CloudValidation>(universe, nav, pilot);
            std::unique_ptr<CelestialValidation> celestialProof;
            if (opt.celestialTest)
                celestialProof =
                    std::make_unique<CelestialValidation>(universe, nav, pilot, opt.celestialSunOnly);
            std::unique_ptr<LandingLightValidation> landingLightProof;
            if (opt.landingLightTest)
                landingLightProof = std::make_unique<LandingLightValidation>(universe, nav, pilot);
            bool shipRestored = false, legacyShip = false;
            if (!opt.fresh && !opt.smoke) {
                std::ifstream saved(data / ("ship-" + std::to_string(universe.seed()) + ".txt"));
                std::string state;
                if (std::getline(saved, state)) {
                    legacyShip = state.rfind("INFINITE_SHIP 3 ", 0) != 0 &&
                                 state.rfind("INFINITE_SHIP 4 ", 0) != 0 &&
                                 state.rfind("INFINITE_SHIP 5 ", 0) != 0;
                    shipRestored = pilot.restore(state);
                }
            }
            if (pilotEnabled && !opt.smoke && (!shipRestored || legacyShip) &&
                nav.anchor.kind() < Kind::System)
                pilot.startInSystem();
            else if (!shipRestored && !opt.smoke && nav.anchor.kind() >= Kind::Rock &&
                     !pilot.ship().shrinking) {
                pilot.fitToView();
            }
            renderer.freezeVisualTime(opt.visualTime);
            ActivityState activity;
            activity.seconds = pilot.ship().flightSeconds;
            if (!opt.fresh && !opt.smoke) {
                std::ifstream file(data / ("activity-" + std::to_string(universe.seed()) + ".txt"));
                std::string text;
                if (std::getline(file, text))
                    activity.restore(text, universe);
            }
            if (opt.smoke)
                activity.seconds = opt.visualTime >= 0 ? opt.visualTime : 12;
            WeaponSystem weapons;
            ShipCrew crew(universe.seed());
            StationControl stationControl;
            FireControl fireControl;
            ShipSystem targetSystem = ShipSystem::Hull;
            RecoveryController recovery;
            if (!opt.fresh && !opt.smoke) {
                std::ifstream file(data / ("fire-control-" + std::to_string(universe.seed()) + ".txt"));
                std::string state;
                if (std::getline(file, state))
                    fireControl.restore(state);
            }
            if (!opt.smoke) {
                std::ifstream file(data / ("crew-" + std::to_string(universe.seed()) + ".txt"));
                if (file) {
                    std::string text;
                    if (!std::getline(file, text) || !crew.restore(text))
                        throw std::runtime_error("Invalid crew save / original file retained");
                }
            }
            crew.observeHull(pilot.ship().hull);
            if (!opt.smoke) {
                std::ifstream file(data / ("combat-" + std::to_string(universe.seed()) + ".txt"));
                std::string text;
                if (std::getline(file, text))
                    weapons.restore(text, universe);
            }
            NpcCombat npcCombat;
            if (!opt.smoke) {
                std::ifstream file(data / ("npc-combat-" + std::to_string(universe.seed()) + ".txt"));
                std::string text;
                if (std::getline(file, text))
                    npcCombat.restore(text, universe);
            }
            renderer.setNpcCombat(&npcCombat);
            renderer.setWeapons(&weapons);
            renderer.setActivity(&activity);
            std::unique_ptr<EconomyRuntime> economy;
            if (!opt.smoke || opt.economyTest || opt.crewTradeTest || opt.crewGunsTest)
                economy = std::make_unique<EconomyRuntime>(universe, data);
            if (!opt.smoke)
                weapons.setLoadout({0, 0, 0}); // wait for the authoritative saved mounts
            std::shared_ptr<const EconomySnapshot> economicSnapshot;
            MarketView marketView;
            bool marketOpen = false;
            bool crewOpen = false;
            bool crewFlightOpen = false;
            size_t crewSelected = 0;
            size_t crewOffer = 0;
            CrewStation crewStation = CrewStation::Bridge;
            CrewView crewView;
            std::string crewFeedback;
            auto crewCommand = [&](CrewCommand command) {
                crewFeedback = crew.command(command, crewSelected, crewStation);
            };
            uint64_t pendingCrewSequence = 0;
            std::optional<ActivityId> marketPort;
            size_t marketGood = 1;
            MarketPage marketPage = MarketPage::Goods;
            unsigned marketWeaponMount = 0;
            std::optional<ActivityId> tradeBuyer;
            Good tradeGood = Good::Water;
            int64_t tradeKg = 0;
            std::string tradeHint;
            EconomicAction pendingEconomicAction = EconomicAction::Buy;
            Good pendingEconomicGood = Good::Water;
            auto marketRows = [&]() -> size_t {
                return marketPage == MarketPage::Goods      ? goodCount
                       : marketPage == MarketPage::Industry ? facilityModuleCount
                       : marketPage == MarketPage::Services ? 3
                       : marketPage == MarketPage::Weapons  ? maxWeaponLevel
                       : marketPage == MarketPage::Crew
                           ? std::max<size_t>(
                                 1, economicSnapshot && marketPort && economicSnapshot->siteIndex(*marketPort)
                                        ? economicCrewOffers(*economicSnapshot,
                                                             *economicSnapshot->siteIndex(*marketPort))
                                                  .size() +
                                              crew.members().size()
                                        : 0)
                       : marketPage == MarketPage::Outcomes
                           ? std::max<size_t>(1, economicSnapshot ? economicSnapshot->outcomes.size() : 0)
                       : marketPage == MarketPage::Jobs
                           ? std::max<size_t>(1, economicSnapshot ? economicSnapshot->jobs.size() : 0)
                           : std::max<size_t>(1, economicSnapshot ? economicSnapshot->routes.size() : 0);
            };
            int64_t marketQuantity = 10;
            uint64_t economicSequence = 0, pendingEconomicSequence = 0;
            uint64_t pendingSecuritySequence = 0;
            uint64_t pendingRecoverySequence = 0;
            std::string recoveryMessage;
            double lastSecurityReport = -1;
            int64_t pendingCargoKg = 0;
            std::string marketFeedback;
            double marketFeedbackSeconds = 0;
            auto marketSay = [&](std::string message) {
                marketFeedback = std::move(message);
                marketFeedbackSeconds = 4;
            };
            auto connectedMarket = [&] {
                if (!pilotEnabled || !economicSnapshot || economicSnapshot->catchingUp || !marketPort)
                    return false;
                auto i = economicSnapshot->siteIndex(*marketPort);
                return i && economicTradeAccess(universe, nav, pilot, economicSnapshot->network->sites[*i]);
            };
            auto marketContext = [&] {
                MarketContext context;
                context.weaponMount = marketWeaponMount;
                context.hull = pilot.ship().hull;
                context.cruiseSpeed = pilot.ship().cruiseSpeed;
                context.crew = crew.members();
                if (economicSnapshot && economicSnapshot->network &&
                    economicSnapshot->network->system.ancestorOf(nav.anchor)) {
                    const auto home = universe.describe(nav.anchor);
                    context.systemPosition = universe.pointRelative(
                        nav.anchor,
                        nav.cameraPosition() * std::pow(10., nav.logDistance - home.frame.logRadius),
                        economicSnapshot->network->system, 0);
                }
                return context;
            };
            auto selectMarket = [&] {
                if (!economicSnapshot || !economicSnapshot->network)
                    return;
                if (activity.selected && economicSnapshot->siteIndex(*activity.selected))
                    marketPort = activity.selected;
                else {
                    double best = 1e300;
                    for (const auto& site : economicSnapshot->network->sites) {
                        const auto home = universe.describe(site.id.home);
                        auto point = universe.pointRelative(site.id.home,
                                                            site.metres / std::pow(10., home.frame.logRadius),
                                                            nav.anchor, nav.logDistance);
                        double distance = glm::length(point - nav.cameraPosition());
                        if (distance < best) {
                            best = distance;
                            marketPort = site.id;
                        }
                    }
                }
                if (marketPort)
                    activity.selected = marketPort;
            };
            auto crewPort = [&]() -> std::optional<size_t> {
                if (!pilotEnabled || !economicSnapshot || !economicSnapshot->network ||
                    economicSnapshot->catchingUp)
                    return {};
                for (size_t i = 0; i < economicSnapshot->network->sites.size(); ++i)
                    if (economicTradeAccess(universe, nav, pilot, economicSnapshot->network->sites[i]))
                        return i;
                return {};
            };
            auto submitCrew = [&](ShipService service) {
                const auto port = crewPort();
                if (!port || !economy) {
                    crewFeedback = "BERTH OR LAND AT A PORT / FULL SIZE / STOP SHIP";
                    return;
                }
                if (pendingCrewSequence > economicSnapshot->player.lastCommand ||
                    pendingEconomicSequence > economicSnapshot->player.lastCommand) {
                    crewFeedback = "WAIT FOR PORT ORDER";
                    return;
                }
                if (service == ShipService::CrewHire &&
                    crew.living() + economicSnapshot->player.rescued.size() >= maxShipCrew) {
                    crewFeedback = "ALL EIGHT BERTHS FILLED";
                    return;
                }
                if (service == ShipService::CrewSupplies && crew.spares() >= 100 && crew.medical() >= 50) {
                    crewFeedback = "CREW STORES FULL";
                    return;
                }
                auto quote = economicServiceQuote(*economicSnapshot, *port, service, 1);
                if (!quote.available) {
                    crewFeedback = quote.status;
                    return;
                }
                EconomicCommand c;
                c.port = economicSnapshot->network->sites[*port].id;
                c.action = service == ShipService::CrewHire       ? EconomicAction::HireCrew
                           : service == ShipService::CrewSupplies ? EconomicAction::SupplyCrew
                                                                  : EconomicAction::TreatCrew;
                c.serviceUnits = 1;
                c.limitCents = quote.totalCents();
                c.sequence = economicSequence =
                    std::max(economicSequence, economicSnapshot->player.lastCommand) + 1;
                if (economy->command(c)) {
                    pendingCrewSequence = c.sequence;
                    crewFeedback = "CREW SERVICE ORDER SENT";
                } else
                    crewFeedback = "PORT QUEUE BUSY / TRY AGAIN";
            };
            auto tradeCrew = [&](EconomicAction action, const CrewMember& person,
                                 std::optional<size_t> port) {
                if (!port || !economy || !economicSnapshot || economicSnapshot->catchingUp ||
                    !economicTradeAccess(universe, nav, pilot, economicSnapshot->network->sites[*port])) {
                    crewFeedback = "BERTH OR LAND AT THIS PORT / FULL SIZE AND STOPPED";
                    marketSay(crewFeedback);
                    return;
                }
                if (pendingCrewSequence > economicSnapshot->player.lastCommand ||
                    pendingEconomicSequence > economicSnapshot->player.lastCommand) {
                    crewFeedback = "WAIT FOR PORT ORDER";
                    marketSay(crewFeedback);
                    return;
                }
                const bool buy = action == EconomicAction::BuyCrew;
                const auto quote = economicCrewQuote(*economicSnapshot, *port, person, buy,
                                                     crew.living() + economicSnapshot->player.rescued.size());
                if (action != EconomicAction::ReleaseCrew && !quote.available) {
                    crewFeedback = quote.status;
                    marketSay(crewFeedback);
                    return;
                }
                EconomicCommand c;
                c.port = economicSnapshot->network->sites[*port].id;
                c.action = action;
                c.crewId = person.id;
                c.crewAboard = unsigned(crew.living() + economicSnapshot->player.rescued.size());
                if (!buy)
                    c.person = person;
                else {
                    const auto lost = std::find_if(crew.members().begin(), crew.members().end(),
                                                   [](const auto& m) { return m.health <= 0; });
                    if (lost != crew.members().end())
                        c.person = *lost;
                }
                c.limitCents = buy ? quote.buyCents : quote.sellCents;
                c.sequence = economicSequence =
                    std::max(economicSequence, economicSnapshot->player.lastCommand) + 1;
                if (economy->command(c)) {
                    pendingCrewSequence = c.sequence;
                    crewFeedback = "CREW TRANSFER PENDING";
                    marketSay(crewFeedback);
                }
            };
            auto tradeMarketCrew = [&](EconomicAction action) {
                if (!economicSnapshot || !marketPort)
                    return;
                const auto port = economicSnapshot->siteIndex(*marketPort);
                if (!port)
                    return;
                const auto rows = economicCrewRows(*economicSnapshot, *port, crew.members());
                if (marketGood >= rows.size())
                    return;
                const auto& row = rows[marketGood];
                if ((action == EconomicAction::BuyCrew) != row.buy) {
                    marketSay(row.buy ? "ENTER BUYS A PORT OFFER" : "BACKSPACE SELLS ABOARD CREW");
                    return;
                }
                tradeCrew(action, row.person, port);
            };
            auto submitMarket = [&](EconomicAction action, const EconomySnapshot::Job* job = nullptr) {
                if (!economy || !economicSnapshot || !marketPort || economicSnapshot->catchingUp)
                    return;
                auto i = economicSnapshot->siteIndex(*marketPort);
                if (!i)
                    return;
                if (pendingEconomicSequence > economicSnapshot->player.lastCommand ||
                    pendingCrewSequence > economicSnapshot->player.lastCommand)
                    return;
                EconomicCommand command;
                command.port = *marketPort;
                command.action = action;
                command.good = marketPage == MarketPage::Goods ? Good(marketGood) : Good::Water;
                command.module = FacilityModule(std::min(marketGood, facilityModuleCount - 1));
                command.kg = marketQuantity;
                if (job) {
                    action = action == EconomicAction::CancelHaul ? action : job->action;
                    command.action = action;
                    command.port = job->target;
                    command.contract = job->id;
                    command.good = job->good;
                    command.kg = job->kg;
                    if (job->combat) {
                        action =
                            action == EconomicAction::CancelHaul ? EconomicAction::CancelCombat : job->action;
                        command.action = action;
                        command.port = job->issuer;
                        if (action == EconomicAction::ReportCombat) {
                            marketSay(job->route + " / F4 CONTACT / KEEP PROTECTING");
                            return;
                        }
                        auto contract = std::find_if(economicSnapshot->combatContracts.begin(),
                                                     economicSnapshot->combatContracts.end(),
                                                     [&](const auto& c) { return c.id == job->id; });
                        if (contract == economicSnapshot->combatContracts.end())
                            return;
                        auto body = sampleCombatActivity(universe, contract->target, activity.seconds);
                        weapons.apply(body);
                        command.combatTarget = contract->target;
                        command.targetHull = body.hull;
                        command.impactSeconds = activity.seconds;
                    }
                    if (action == EconomicAction::CancelHaul && economicSnapshot->player.haul)
                        command.port = economicSnapshot->player.haul->source;
                    i = economicSnapshot->siteIndex(command.port);
                    if (!i) {
                        marketSay("RETURN TO THE JOB'S SYSTEM FIRST / F4");
                        return;
                    }
                }
                bool needsLink = action != EconomicAction::CollectDividends &&
                                 action != EconomicAction::AcceptHaul &&
                                 action != EconomicAction::AcceptRecovery;
                if (action == EconomicAction::AcceptCombat || action == EconomicAction::CancelCombat)
                    needsLink = false;
                if (action == EconomicAction::CancelHaul)
                    needsLink = economicSnapshot->player.haul && economicSnapshot->player.haul->onboard;
                if (action == EconomicAction::Contract) {
                    auto it = std::find_if(
                        economicSnapshot->contracts.begin(), economicSnapshot->contracts.end(),
                        [&](const auto& c) {
                            return c.site == *i && (!command.contract || c.id == command.contract) &&
                                   (c.phase == ContractPhase::Open || c.phase == ContractPhase::Accepted);
                        });
                    if (it == economicSnapshot->contracts.end())
                        return;
                    command.contract = it->id;
                    command.good = it->good;
                    needsLink = it->phase != ContractPhase::Open;
                }
                if (needsLink &&
                    !economicTradeAccess(universe, nav, pilot, economicSnapshot->network->sites[*i])) {
                    marketSay(job ? ("JOB PORT " + economicSnapshot->network->sites[*i].name +
                                     (action == EconomicAction::CancelHaul ? " / SHIFT-F4 RETURN SOURCE"
                                                                           : " / F4 TARGET / J APPROACH"))
                                  : "APPROACH PORT / STOP BELOW 3 M/S / LAND AT SURFACE PORTS");
                    return;
                }
                auto q = economicSnapshot->quote(*i, command.good, marketQuantity);
                command.limitCents = action == EconomicAction::Buy ? q.buyCents : q.sellCents;
                if (action == EconomicAction::Invest)
                    command.limitCents =
                        facilityRefit(command.module,
                                      economicSnapshot->sites[*i].modules[size_t(command.module)] + 1)
                            .labourCents;
                if (action == EconomicAction::FitWeapon || action == EconomicAction::SellWeapon) {
                    command.weaponMount = marketWeaponMount;
                    command.serviceUnits = action == EconomicAction::SellWeapon
                                               ? economicSnapshot->player.weaponAt(marketWeaponMount)
                                               : unsigned(marketGood + 1);
                    auto q = economicWeaponQuote(*economicSnapshot, *i, command.serviceUnits,
                                                 action == EconomicAction::SellWeapon, marketWeaponMount);
                    if (!q.available) {
                        marketSay(q.status);
                        return;
                    }
                    command.limitCents = action == EconomicAction::SellWeapon ? q.sellCents : q.totalCents();
                }
                if (action == EconomicAction::RepairHull || action == EconomicAction::UpgradeCargo) {
                    command.serviceUnits =
                        action == EconomicAction::RepairHull
                            ? unsigned(std::ceil(std::clamp(100. - pilot.ship().hull, 0., 10.)))
                            : economicSnapshot->player.cargoLevel + 1;
                    auto serviceQuote = economicServiceQuote(
                        *economicSnapshot, *i,
                        action == EconomicAction::RepairHull ? ShipService::Repair : ShipService::CargoHold,
                        command.serviceUnits);
                    if (!serviceQuote.available) {
                        marketSay(serviceQuote.status);
                        return;
                    }
                    command.limitCents = serviceQuote.totalCents();
                }
                economicSequence = std::max(economicSequence, economicSnapshot->player.lastCommand);
                command.sequence = ++economicSequence;
                if (economy->command(command)) {
                    pendingEconomicSequence = command.sequence;
                    pendingEconomicAction = action;
                    pendingEconomicGood = command.good;
                    pendingCargoKg = economicPendingPayload(command, economicSnapshot->sites[*i]);
                    pilot.setCargoMass(std::min<int64_t>(maxShipPayloadKg,
                                                         economicSnapshot->player.massKg() + pendingCargoKg));
                    marketSay("ORDER SUBMITTED / AWAITING LEDGER");
                }
            };
            std::unique_ptr<EconomyValidation> economyProof;
            if (opt.economyTest)
                economyProof = std::make_unique<EconomyValidation>(universe, nav, pilot, *economy, activity,
                                                                   window, weapons);
            std::unique_ptr<NpcValidation> npcProof;
            if (opt.npcTest)
                npcProof = std::make_unique<NpcValidation>(universe, nav, pilot, activity, weapons, npcCombat,
                                                           window);
            std::unique_ptr<CrewValidation> crewProof;
            if (opt.crewTest)
                crewProof = std::make_unique<CrewValidation>(universe, nav, pilot, crew, window);
            std::unique_ptr<CrewTradeValidation> crewTradeProof;
            if (opt.crewTradeTest)
                crewTradeProof =
                    std::make_unique<CrewTradeValidation>(universe, nav, pilot, crew, *economy, window);
            std::unique_ptr<CrewFlightValidation> crewFlightProof;
            if (opt.crewFlightTest)
                crewFlightProof = std::make_unique<CrewFlightValidation>(universe, nav, pilot, crew, window);
            std::unique_ptr<CrewGunsValidation> crewGunsProof;
            if (opt.crewGunsTest)
                crewGunsProof = std::make_unique<CrewGunsValidation>(universe, nav, pilot, crew, weapons,
                                                                     *economy, activity, window);
            std::unique_ptr<CombatOperationsValidation> combatOperationsProof;
            if (opt.combatOperationsTest)
                combatOperationsProof = std::make_unique<CombatOperationsValidation>(
                    universe, nav, pilot, crew, weapons, fireControl, activity);
            std::unique_ptr<StationSwapValidation> stationSwapProof;
            if (opt.stationSwapTest)
                stationSwapProof = std::make_unique<StationSwapValidation>(
                    universe, nav, pilot, crew, weapons, stationControl, activity, window);
            std::unique_ptr<PointLightValidation> pointLightProof;
            if (opt.starStabilityTest)
                pointLightProof = std::make_unique<PointLightValidation>(universe, nav, pilot, activity);
            std::unique_ptr<SurfaceValidation> surfaceProof;
            if (opt.surfaceTest)
                surfaceProof = std::make_unique<SurfaceValidation>(universe, nav, pilot, activity);
            std::unique_ptr<WeatherValidation> weatherProof;
            if (opt.weatherTest)
                weatherProof = std::make_unique<WeatherValidation>(universe, nav, pilot, activity);
            std::unique_ptr<StellarDiversityValidation> stellarDiversityProof;
            if (opt.stellarDiversityTest)
                stellarDiversityProof =
                    std::make_unique<StellarDiversityValidation>(universe, nav, pilot, activity);
            std::unique_ptr<PlanetDiversityValidation> diversityProof;
            if (opt.diversityTest)
                diversityProof = std::make_unique<PlanetDiversityValidation>(universe, nav, pilot, activity);
            auto damagedBody = [&](ActivityBody body) {
                npcCombat.apply(body);
                weapons.apply(body);
                return body;
            };
            auto activityBody = [&]() -> std::optional<ActivityBody> {
                if (!activity.selected)
                    return {};
                if (economicSnapshot && economicSnapshot->network) {
                    auto flight =
                        std::find_if(economicSnapshot->flights.begin(), economicSnapshot->flights.end(),
                                     [&](const auto& f) { return f.id == *activity.selected; });
                    if (flight != economicSnapshot->flights.end())
                        return damagedBody(sampleEconomicFreight(universe, *economicSnapshot->network,
                                                                 *flight, activity.seconds));
                    if (auto i = economicSnapshot->siteIndex(*activity.selected)) {
                        const auto& site = economicSnapshot->network->sites[*i];
                        ActivityBody body;
                        body.id = site.id;
                        body.kind = site.orbital ? ActivityKind::Harbour : ActivityKind::SurfacePort;
                        body.position = site.metres;
                        body.radius = site.radius;
                        body.name = site.name;
                        body.status = site.role + " / F2 MARKET";
                        annotateEconomicPort(body, *economicSnapshot);
                        return body;
                    }
                }
                if (activity.selected->slot >= surfaceActivitySlot) {
                    const auto world = renderer.surfaceWorld(activity.selected->home);
                    if (!world) {
                        for (const auto& c : renderer.activityContacts())
                            if (c.body.id == *activity.selected)
                                return c.body;
                        return {};
                    }
                    for (const auto& s : world->settlements)
                        if (surfaceActivitySlot + s.slot == activity.selected->slot)
                            return surfaceContact(*world, s, activity.seconds);
                    return {};
                }
                return damagedBody(sampleCombatActivity(universe, *activity.selected, activity.seconds));
            };
            auto waypointFor = [](const ActivityBody& body) {
                const bool surface = body.kind == ActivityKind::SurfacePort;
                return FlightWaypoint{body.id.home,
                                      body.position +
                                          (surface ? glm::normalize(body.position) * 60. : glm::dvec3(0)),
                                      body.velocity,
                                      surface                              ? 0.
                                      : body.kind == ActivityKind::Harbour ? body.radius * 4
                                      : (body.disabled || body.systems[size_t(ShipSystem::Engines)] <= 0)
                                          ? body.radius + 55
                                          : std::max(750., body.radius * 5),
                                      body.name,
                                      surface};
            };
            auto chooseActivity = [&](const ActivityId& id) {
                activity.selected = id;
                pilot.brake();
                pilot.clearWaypoint();
                if (auto body = activityBody())
                    pilot.trackWaypoint(waypointFor(*body));
            };

            auto navigateSelected = [&]() {
                if (auto body = activityBody())
                    pilot.navigateWaypoint(waypointFor(*body));
                else if (nav.selected)
                    pilot.navigate(*nav.selected);
            };
            std::unique_ptr<RoundTripVerifier> proof;
            if (opt.smoke && !opt.benchmark)
                proof = std::make_unique<RoundTripVerifier>(universe);
            std::cout << "Infinite / " << renderer.backend() << " / seed " << universe.seed() << '\n'
                      << std::flush;
            std::ofstream profile;
            if (!opt.profile.empty()) {
                profile.open(opt.profile);
                profile
                    << "frame,kind,view_log_m,wall_ms,cpu_ms,gpu_ms,draws,visible,atoms,cpu_cache_bytes,"
                       "gpu_bytes,pending,lod_transitions,sections,submit_ms,vertices,triangles,"
                       "particles,upload_bytes,transitioning,queued,cancelled,evicted,atom_triangles,"
                       "upload_deferred,render_mode,effect_bytes,terrain_tiles,root_cells,activity_"
                       "bodies,activity_visible,activity_triangles,activity_bytes,material_bytes,"
                       "surface_sites,surface_buildings,surface_residents,surface_triangles,surface_bytes,"
                       "surface_gpu_bytes,surface_pending,weather_cells,weather_particles,shadow_bytes,"
                       "shadow_active,shadow_updates,weather_pending,weather_cache_bytes,weather_gpu_bytes,"
                       "weather_volumes\n";
            }
            bool running = true, overlay = true, looking = false;
            bool mouseFlight = false, rightLook = false;
            int frame = 0, tourStage = 0, stable = 0, captureHold = 0;
            bool captureQueued = false, returning = false, tourPassed = false;
            uint64_t compareHash = 0;
            std::string compareCamera;
            GenerationStats compareGeneration{};
            size_t tourVisited = 0;
            std::vector<std::pair<Address, uint64_t>> visited;
            std::vector<double> frameTimes;
            double fps = 60, noticeTime = 6;
            std::string notice = "C mouse helm / U-O or Alt+A-D roll / J approach / L land / B brake";
            auto captureLook = [&] {
                const bool enabled = mouseFlight || rightLook;
                if (enabled == looking)
                    return;
                if (SDL_SetWindowRelativeMouseMode(window, enabled))
                    looking = enabled;
                else {
                    mouseFlight = rightLook = looking = false;
                    notice = std::string("Mouse capture unavailable: ") + SDL_GetError();
                    noticeTime = 6;
                }
            };
            auto releaseLook = [&] {
                mouseFlight = rightLook = false;
                captureLook();
            };
            auto previous = std::chrono::steady_clock::now();
            auto start = previous;
            auto targetMarketRoute = [&](bool destination) {
                if (!economicSnapshot || marketGood >= economicSnapshot->routes.size())
                    return;
                const auto& route = economicSnapshot->routes[marketGood];
                tradeBuyer = economicSnapshot->network->sites[route.destination].id;
                tradeGood = route.good;
                tradeKg = route.kg;
                tradeHint = "F4 BUYER " + economicSnapshot->network->sites[route.destination].name + " / " +
                            std::to_string(route.kg) + " KG " + std::string(goodInfo(route.good).name);
                marketPage = MarketPage::Goods;
                marketGood = size_t(route.good);
                marketQuantity = route.kg;
                marketOpen = false;
                chooseActivity(
                    economicSnapshot->network->sites[destination ? route.destination : route.source].id);
                notice = destination ? "TRADE BUYER TARGETED / J APPROACH / F2 MARKET"
                                     : "SUPPLIER TARGETED / J APPROACH / F2 BUY / F4 BUYER";
                noticeTime = 5;
            };
            auto targetTradeBuyer = [&]() {
                if (!tradeBuyer || !economicSnapshot || !economicSnapshot->siteIndex(*tradeBuyer))
                    return false;
                chooseActivity(*tradeBuyer);
                marketOpen = false;
                marketGood = size_t(tradeGood);
                marketQuantity =
                    std::max<int64_t>(1, std::min(tradeKg, economicSnapshot->player.cargo[marketGood]));
                notice = "TRADE BUYER TARGETED / J APPROACH / F2 SELL";
                noticeTime = 5;
                return true;
            };
            auto targetActiveHaul = [&](bool returnSource = false) {
                if (!economicSnapshot || !economicSnapshot->player.haul)
                    return false;
                const auto& h = *economicSnapshot->player.haul;
                marketOpen = false;
                if (!economicSnapshot->network || h.system != economicSnapshot->network->system) {
                    activity.selected.reset();
                    pilot.brake();
                    pilot.clearWaypoint();
                    nav.select(h.system);
                    notice = "ACTIVE HAUL'S SYSTEM TARGETED / J APPROACH / N CRUISE";
                } else {
                    chooseActivity(returnSource || !h.onboard ? h.source : h.destination);
                    notice = returnSource ? "HAUL RETURN SOURCE / J APPROACH / F2 JOBS"
                             : h.onboard  ? "HAUL DELIVERY / J APPROACH / F2 JOBS"
                                          : "HAUL PICKUP / J APPROACH / F2 JOBS";
                }
                noticeTime = 6;
                return true;
            };
            auto targetSecuritySystem = [&]() {
                if (!economicSnapshot || !economicSnapshot->player.combatJob || !economicSnapshot->network ||
                    economicSnapshot->player.combatJob->system == economicSnapshot->network->system)
                    return false;
                activity.selected.reset();
                pilot.brake();
                pilot.clearWaypoint();
                nav.select(economicSnapshot->player.combatJob->system);
                marketOpen = false;
                notice = "SECURITY CONTRACT SYSTEM TARGETED / J APPROACH / N CRUISE";
                noticeTime = 6;
                return true;
            };
            auto selectedJob = [&]() -> const EconomySnapshot::Job* {
                return economicSnapshot && marketGood < economicSnapshot->jobs.size()
                           ? &economicSnapshot->jobs[marketGood]
                           : nullptr;
            };
            auto targetJob = [&](bool returnSource) {
                if (targetSecuritySystem())
                    return;
                if (economicSnapshot && economicSnapshot->player.haul && economicSnapshot->network &&
                    economicSnapshot->player.haul->system != economicSnapshot->network->system) {
                    targetActiveHaul(returnSource);
                    return;
                }
                const auto* job = selectedJob();
                if (!job)
                    return;
                if (returnSource && job->haul && economicSnapshot->player.haul &&
                    economicSnapshot->player.haul->id == job->id) {
                    targetActiveHaul(true);
                    return;
                }
                chooseActivity(job->target);
                marketOpen = false;
                notice = "JOB PORT TARGETED / J APPROACH / F2 JOBS";
                noticeTime = 6;
            };
            uint64_t pendingWreck = 0;
            while (running) {
                auto now = std::chrono::steady_clock::now();
                double wall = std::chrono::duration<double>(now - previous).count();
                previous = now;
                double dt = std::clamp(wall, .0001, .05);
                fps = glm::mix(fps, 1 / std::max(wall, .0001), .035);
                double mouseX = 0, mouseY = 0;
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    // Offline/native fixtures must not consume incidental mouse
                    // or typing events from the desktop running the tests.
                    if (opt.smoke && event.type != SDL_EVENT_QUIT &&
                        event.type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                        continue;
                    if (event.type == SDL_EVENT_QUIT)
                        running = false;
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                        releaseLook();
                    if (!opt.smoke && (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                                       event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                                       event.type == SDL_EVENT_WINDOW_RESTORED ||
                                       event.type == SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED ||
                                       event.type == SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED))
                        fitWindowToDisplay(window, false);
                    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                        int w, h;
                        SDL_GetWindowSizeInPixels(window, &w, &h);
                        renderer.resize(w, h);
                    }
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                        if (crewFlightOpen && !crewOpen && !marketOpen && pilotEnabled &&
                            event.button.button == SDL_BUTTON_LEFT) {
                            int w, h, pw, ph;
                            SDL_GetWindowSize(window, &w, &h);
                            SDL_GetWindowSizeInPixels(window, &pw, &ph);
                            const auto viewport = renderer.cockpitViewport();
                            const auto point =
                                viewport.toCanvas({event.button.x * pw / w, event.button.y * ph / h});
                            const CrewFlightLayout layout(viewport.aspect());
                            if (layout.panel.contains(point)) {
                                for (size_t i = 0; i < crew.members().size(); ++i)
                                    if (layout.roster[i].contains(point))
                                        crewSelected = i;
                                for (size_t i = 0; i < crewRoomCount; ++i)
                                    if (layout.rooms[i].contains(point))
                                        crewStation = CrewStation(i);
                                for (size_t i = 0; i < layout.actions.size(); ++i)
                                    if (layout.actions[i].contains(point))
                                        crewCommand(CrewCommand(i));
                                for (unsigned i = 0; i < maxWeaponMounts; ++i)
                                    if (layout.guns[i].contains(point))
                                        crewFeedback = crew.manWeapon(crewSelected, i);
                                if (layout.automatic.contains(point))
                                    crewCommand(CrewCommand::Automatic);
                                continue;
                            }
                        }
                        if (crewOpen) {
                            if (event.button.button == SDL_BUTTON_LEFT) {
                                int w, h, pw, ph;
                                SDL_GetWindowSize(window, &w, &h);
                                SDL_GetWindowSizeInPixels(window, &pw, &ph);
                                const auto viewport = renderer.cockpitViewport();
                                const auto point =
                                    viewport.toCanvas({event.button.x * pw / w, event.button.y * ph / h});
                                const CrewPanelLayout layout(viewport.aspect());
                                for (size_t i = 0; i < crew.members().size(); ++i)
                                    if (layout.roster[i].contains(point))
                                        crewSelected = i;
                                for (size_t i = 0; i < crewRoomCount; ++i)
                                    if (layout.rooms[i].contains(point)) {
                                        crewStation = CrewStation(i);
                                        crewFeedback = crew.assign(crewSelected, crewStation);
                                    }
                            }
                            continue;
                        }
                        if (event.button.button == SDL_BUTTON_RIGHT) {
                            rightLook = true;
                            captureLook();
                        } else if (event.button.button == SDL_BUTTON_LEFT) {
                            int w, h, pw, ph;
                            SDL_GetWindowSize(window, &w, &h);
                            SDL_GetWindowSizeInPixels(window, &pw, &ph);
                            const double x = looking ? pw * .5 : event.button.x * pw / w;
                            const double y = looking ? ph * .5 : event.button.y * ph / h;
                            if (pilotEnabled && renderer.pickActivity(x, y)) {
                                chooseActivity(*renderer.pickActivity(x, y));
                                notice = hailActivity(*activityBody());
                                noticeTime = 6;
                            } else if (auto a = renderer.pick(x, y)) {
                                activity.selected.reset();
                                pilot.clearWaypoint();
                                if (pilotEnabled && a->kind() == Kind::Planet)
                                    pilot.selectPlanet(*a);
                                else
                                    nav.select(*a);
                                notice = "Selected " + universe.describe(*a).label() +
                                         " / J navigate / F explorer focus";
                                noticeTime = 4;
                            }
                        }
                    }
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
                        rightLook = false;
                        captureLook();
                    }
                    if (event.type == SDL_EVENT_MOUSE_MOTION && looking && !crewOpen &&
                        (event.motion.xrel != 0 || event.motion.yrel != 0)) {
                        if (pilotEnabled) {
                            mouseX += event.motion.xrel;
                            mouseY += event.motion.yrel;
                        } else
                            nav.rotate(event.motion.xrel, event.motion.yrel);
                        opt.tour = false;
                    }
                    if (crewOpen && event.type == SDL_EVENT_MOUSE_WHEEL)
                        continue;
                    if (crewFlightOpen && !marketOpen && pilotEnabled &&
                        event.type == SDL_EVENT_MOUSE_WHEEL) {
                        float x, y;
                        int w, h, pw, ph;
                        SDL_GetMouseState(&x, &y);
                        SDL_GetWindowSize(window, &w, &h);
                        SDL_GetWindowSizeInPixels(window, &pw, &ph);
                        const auto viewport = renderer.cockpitViewport();
                        if (CrewFlightLayout(viewport.aspect())
                                .panel.contains(viewport.toCanvas({x * pw / w, y * ph / h}))) {
                            if (!crew.members().empty() && event.wheel.y != 0)
                                crewSelected =
                                    (crewSelected + crew.members().size() + (event.wheel.y > 0 ? -1 : 1)) %
                                    crew.members().size();
                            continue;
                        }
                    }
                    if (marketOpen && event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                        marketGood =
                            (marketGood + marketRows() + (event.wheel.y > 0 ? -1 : 1)) % marketRows();
                        continue;
                    }
                    if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                        if (stationControl.requested())
                            stationControl.returnToHelm(crew);
                        nav.zoom(event.wheel.y);
                        if (pilotEnabled) {
                            pilotEnabled = false;
                            pilot.resetMotion();
                        }
                        opt.tour = false;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        if (pilotEnabled && !marketOpen && !crewOpen && !crewFlightOpen &&
                            event.key.scancode == SDL_SCANCODE_9) {
                            constexpr std::array systems{ShipSystem::Hull, ShipSystem::Engines,
                                                         ShipSystem::Thrusters, ShipSystem::NoseGun,
                                                         ShipSystem::LifeSupport};
                            auto it = std::find(systems.begin(), systems.end(), targetSystem);
                            targetSystem = systems[(size_t(it - systems.begin()) + 1) % systems.size()];
                            notice = "TARGET COMPONENT / " + std::string(shipSystemName(targetSystem));
                            noticeTime = 5;
                            continue;
                        }
                        if (pilotEnabled && !marketOpen && !crewOpen &&
                            (!crewFlightOpen || (event.key.mod & SDL_KMOD_CTRL)) &&
                            event.key.scancode >= SDL_SCANCODE_6 && event.key.scancode <= SDL_SCANCODE_8) {
                            fireControl.order(FireOrder(event.key.scancode - SDL_SCANCODE_6));
                            notice = "CREW GUNNERS / " + std::string(fireOrderName(fireControl.order()));
                            noticeTime = 6;
                            continue;
                        }
                        const bool numberOrder = !crewFlightOpen && event.key.scancode >= SDL_SCANCODE_1 &&
                                                 event.key.scancode <= SDL_SCANCODE_5;
                        const bool functionOrder = (event.key.mod & SDL_KMOD_CTRL) &&
                                                   event.key.scancode >= SDL_SCANCODE_F1 &&
                                                   event.key.scancode <= SDL_SCANCODE_F5;
                        if (stationControl.active() && (numberOrder || functionOrder)) {
                            stationControl.command(HelmOrder(
                                event.key.scancode - (numberOrder ? SDL_SCANCODE_1 : SDL_SCANCODE_F1)));
                            notice = "CREW ORDER / " + std::string(helmOrderName(stationControl.order()));
                            noticeTime = 5;
                            continue;
                        }
                        if (event.key.scancode == SDL_SCANCODE_F10 && pilotEnabled && !crewOpen &&
                            !marketOpen) {
                            if (stationControl.requested())
                                stationControl.returnToHelm(crew);
                            else {
                                unsigned gun = noWeaponMount;
                                if (crewFlightOpen && crewSelected < crew.members().size() &&
                                    crew.members()[crewSelected].duty == CrewStation::Weapons)
                                    gun = crew.members()[crewSelected].weaponMount;
                                else
                                    for (unsigned i : {1u, 2u, 0u})
                                        if (crew.gunnerReadiness(i) > 0) {
                                            gun = i;
                                            break;
                                        }
                                stationControl.request(gun, crew, pilot, nav);
                            }
                            notice = stationControl.status();
                            noticeTime = 7;
                            mouseX = mouseY = 0;
                            continue;
                        }
                        if (event.key.scancode == SDL_SCANCODE_F9 && pilotEnabled) {
                            crewFlightOpen = crewOpen || marketOpen || !crewFlightOpen;
                            crewOpen = marketOpen = false;
                            crewFeedback.clear();
                            overlay = true;
                            continue;
                        }
                        if (crewFlightOpen && !crewOpen && !marketOpen && pilotEnabled) {
                            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                                crewFlightOpen = false;
                                continue;
                            }
                            if (event.key.scancode >= SDL_SCANCODE_1 &&
                                event.key.scancode <= SDL_SCANCODE_8) {
                                const auto index = size_t(event.key.scancode - SDL_SCANCODE_1);
                                if ((event.key.mod & SDL_KMOD_CTRL) && index < maxWeaponMounts) {
                                    crewFeedback = crew.manWeapon(crewSelected, unsigned(index));
                                } else if (event.key.mod & SDL_KMOD_ALT) {
                                    if (index < crewRoomCount)
                                        crewStation = CrewStation(index);
                                } else if (index < crew.members().size())
                                    crewSelected = index;
                                continue;
                            }
                            if (event.key.mod & SDL_KMOD_CTRL) {
                                std::optional<CrewCommand> command;
                                switch (event.key.scancode) {
                                case SDL_SCANCODE_RETURN:
                                    command = CrewCommand::Assign;
                                    break;
                                case SDL_SCANCODE_R:
                                    command = CrewCommand::Repair;
                                    break;
                                case SDL_SCANCODE_F:
                                    command = CrewCommand::Firefight;
                                    break;
                                case SDL_SCANCODE_M:
                                    command = CrewCommand::Aid;
                                    break;
                                case SDL_SCANCODE_T:
                                    command = CrewCommand::Rest;
                                    break;
                                case SDL_SCANCODE_G:
                                    command = CrewCommand::Return;
                                    break;
                                case SDL_SCANCODE_J:
                                    command = CrewCommand::BestStations;
                                    break;
                                case SDL_SCANCODE_H:
                                    command = CrewCommand::Emergency;
                                    break;
                                default:
                                    break;
                                }
                                if (command) {
                                    crewCommand(*command);
                                    continue;
                                }
                            }
                        }
                        if (crewOpen) {
                            const auto count = crew.members().size();
                            if ((event.key.mod & SDL_KMOD_CTRL) && event.key.scancode >= SDL_SCANCODE_1 &&
                                event.key.scancode <= SDL_SCANCODE_3) {
                                crewFeedback = crew.manWeapon(crewSelected,
                                                              unsigned(event.key.scancode - SDL_SCANCODE_1));
                                continue;
                            }
                            switch (event.key.scancode) {
                            case SDL_SCANCODE_ESCAPE:
                            case SDL_SCANCODE_F8:
                                crewOpen = false;
                                break;
                            case SDL_SCANCODE_UP:
                                if (count)
                                    crewSelected = (crewSelected + count - 1) % count;
                                break;
                            case SDL_SCANCODE_DOWN:
                                if (count)
                                    crewSelected = (crewSelected + 1) % count;
                                break;
                            case SDL_SCANCODE_LEFT:
                                crewStation =
                                    CrewStation((size_t(crewStation) + crewRoomCount - 1) % crewRoomCount);
                                break;
                            case SDL_SCANCODE_RIGHT:
                                crewStation = CrewStation((size_t(crewStation) + 1) % crewRoomCount);
                                break;
                            case SDL_SCANCODE_RETURN:
                                crewFeedback = crew.assign(crewSelected, crewStation);
                                break;
                            case SDL_SCANCODE_R:
                                crewFeedback = crew.order(crewSelected, CrewOrder::Repair, crewStation);
                                break;
                            case SDL_SCANCODE_F:
                                crewFeedback = crew.order(crewSelected, CrewOrder::Firefight, crewStation);
                                break;
                            case SDL_SCANCODE_M:
                                crewFeedback = crew.order(crewSelected, CrewOrder::Aid, crewStation);
                                break;
                            case SDL_SCANCODE_T:
                                crewFeedback = crew.order(crewSelected, CrewOrder::Rest, CrewStation::Bunks);
                                break;
                            case SDL_SCANCODE_V:
                                crew.vent(crewStation);
                                break;
                            case SDL_SCANCODE_D:
                                crew.recall();
                                break;
                            case SDL_SCANCODE_A:
                                crew.toggleAutomatic();
                                break;
                            case SDL_SCANCODE_H:
                                if (auto port = crewPort()) {
                                    auto offers = economicCrewOffers(*economicSnapshot, *port);
                                    if (!offers.empty())
                                        tradeCrew(EconomicAction::BuyCrew, offers[crewOffer % offers.size()],
                                                  port);
                                    else
                                        crewFeedback = "NO CREW LEFT FOR SALE AT THIS PORT";
                                } else
                                    crewFeedback = "BERTH OR LAND AT A PORT TO BUY CREW";
                                break;
                            case SDL_SCANCODE_LEFTBRACKET:
                                if (auto port = crewPort()) {
                                    auto list = economicCrewOffers(*economicSnapshot, *port);
                                    if (!list.empty())
                                        crewOffer = (crewOffer + list.size() - 1) % list.size();
                                }
                                break;
                            case SDL_SCANCODE_RIGHTBRACKET:
                                ++crewOffer;
                                break;
                            case SDL_SCANCODE_F2:
                                crewOpen = false;
                                marketOpen = true;
                                selectMarket();
                                marketPage = MarketPage::Crew;
                                marketGood = 0;
                                break;
                            case SDL_SCANCODE_S:
                                submitCrew(ShipService::CrewSupplies);
                                break;
                            case SDL_SCANCODE_L:
                                submitCrew(ShipService::CrewCare);
                                break;
                            case SDL_SCANCODE_BACKSPACE:
                                if (crewSelected < crew.members().size())
                                    tradeCrew(event.key.mod & SDL_KMOD_SHIFT ? EconomicAction::ReleaseCrew
                                                                             : EconomicAction::SellCrew,
                                              crew.members()[crewSelected], crewPort());
                                break;
                            default:
                                break;
                            }
                            continue;
                        }
                        if (marketOpen) {
                            switch (event.key.scancode) {
                            case SDL_SCANCODE_F8:
                                marketOpen = false;
                                crewOpen = true;
                                crewFlightOpen = false;
                                releaseLook();
                                break;
                            case SDL_SCANCODE_ESCAPE:
                            case SDL_SCANCODE_F2:
                                marketOpen = false;
                                break;
                            case SDL_SCANCODE_UP:
                                marketGood = (marketGood + marketRows() - 1) % marketRows();
                                break;
                            case SDL_SCANCODE_DOWN:
                                marketGood = (marketGood + 1) % marketRows();
                                break;
                            case SDL_SCANCODE_LEFT:
                                if (marketPage == MarketPage::Weapons)
                                    marketWeaponMount =
                                        (marketWeaponMount + maxWeaponMounts - 1) % maxWeaponMounts;
                                else
                                    marketQuantity = std::max<int64_t>(1, marketQuantity / 10);
                                break;
                            case SDL_SCANCODE_RIGHT:
                                if (marketPage == MarketPage::Weapons)
                                    marketWeaponMount = (marketWeaponMount + 1) % maxWeaponMounts;
                                else
                                    marketQuantity = std::min<int64_t>(maxShipPayloadKg, marketQuantity * 10);
                                break;
                            case SDL_SCANCODE_F5:
                                marketPage = MarketPage((unsigned(marketPage) + 1) % 8);
                                marketGood = 0;
                                break;
                            case SDL_SCANCODE_RETURN:
                                if (marketPage == MarketPage::Crew) {
                                    tradeMarketCrew(EconomicAction::BuyCrew);
                                    break;
                                }
                                if (marketPage == MarketPage::Jobs) {
                                    if (auto job = selectedJob()) {
                                        if (job->working && !job->combat) {
                                            auto recovery =
                                                std::find_if(economicSnapshot->recoveries.begin(),
                                                             economicSnapshot->recoveries.end(),
                                                             [&](const auto& j) { return j.id == job->id; });
                                            if (recovery != economicSnapshot->recoveries.end())
                                                for (size_t k = 0; k < economicSnapshot->outcomes.size(); ++k)
                                                    if (economicSnapshot
                                                            ->outcomes[economicSnapshot->outcomes.size() - 1 -
                                                                       k]
                                                            .id == recovery->outcome) {
                                                        marketPage = MarketPage::Outcomes;
                                                        marketGood = k;
                                                        break;
                                                    }
                                        } else
                                            submitMarket(job->action, job);
                                    }
                                } else if (marketPage == MarketPage::Weapons)
                                    submitMarket(EconomicAction::FitWeapon);
                                else if (marketPage == MarketPage::Services)
                                    submitMarket(marketGood == 0   ? EconomicAction::RepairHull
                                                 : marketGood == 1 ? EconomicAction::UpgradeCargo
                                                                   : EconomicAction::DisembarkRescued);
                                else if (marketPage == MarketPage::Routes)
                                    targetMarketRoute(false);
                                else if (marketPage != MarketPage::Outcomes)
                                    submitMarket(marketPage == MarketPage::Industry ? EconomicAction::Invest
                                                                                    : EconomicAction::Buy);
                                break;
                            case SDL_SCANCODE_BACKSPACE:
                                if (marketPage == MarketPage::Crew) {
                                    tradeMarketCrew(event.key.mod & SDL_KMOD_SHIFT
                                                        ? EconomicAction::ReleaseCrew
                                                        : EconomicAction::SellCrew);
                                    break;
                                }
                                if (marketPage == MarketPage::Weapons)
                                    submitMarket(EconomicAction::SellWeapon);
                                else if (marketPage == MarketPage::Jobs) {
                                    if (auto job = selectedJob();
                                        job && job->haul && job->action != EconomicAction::AcceptHaul)
                                        submitMarket(EconomicAction::CancelHaul, job);
                                } else if (marketPage != MarketPage::Routes &&
                                           marketPage != MarketPage::Services &&
                                           marketPage != MarketPage::Outcomes)
                                    submitMarket(marketPage == MarketPage::Industry
                                                     ? EconomicAction::CollectDividends
                                                     : EconomicAction::Sell);
                                break;
                            case SDL_SCANCODE_DELETE:
                                if (marketPage == MarketPage::Industry)
                                    submitMarket(EconomicAction::CancelProject);
                                else if (marketPage != MarketPage::Services &&
                                         marketPage != MarketPage::Jobs &&
                                         marketPage != MarketPage::Weapons &&
                                         marketPage != MarketPage::Outcomes) {
                                    tradeBuyer.reset();
                                    tradeHint.clear();
                                    marketSay("TRADE BOOKMARK CLEARED / F4 SUPPLY CONTRACT");
                                }
                                break;
                            case SDL_SCANCODE_F3:
                                if (marketPage == MarketPage::Goods)
                                    submitMarket(EconomicAction::Contract);
                                break;
                            case SDL_SCANCODE_F4:
                                if (marketPage == MarketPage::Outcomes) {
                                    if (economicSnapshot && !economicSnapshot->outcomes.empty()) {
                                        const auto k =
                                            std::min(marketGood, economicSnapshot->outcomes.size() - 1);
                                        const auto& o =
                                            economicSnapshot
                                                ->outcomes[economicSnapshot->outcomes.size() - 1 - k];
                                        chooseActivity(economicSnapshot->network->sites[o.site].id);
                                        marketOpen = false;
                                    }
                                    break;
                                }
                                if (marketPage == MarketPage::Services || marketPage == MarketPage::Weapons) {
                                    if (marketPort) {
                                        chooseActivity(*marketPort);
                                        marketOpen = false;
                                        notice = "SERVICE PORT TARGETED / J APPROACH / F2 SERVICES";
                                        noticeTime = 6;
                                    }
                                    break;
                                }
                                if (marketPage == MarketPage::Jobs) {
                                    targetJob(event.key.mod & SDL_KMOD_SHIFT);
                                    break;
                                }
                                if (marketPage == MarketPage::Routes && !(event.key.mod & SDL_KMOD_SHIFT)) {
                                    targetMarketRoute(true);
                                    break;
                                }
                                if (!(event.key.mod & SDL_KMOD_SHIFT) && targetActiveHaul())
                                    break;
                                if (!(event.key.mod & SDL_KMOD_SHIFT) && targetSecuritySystem())
                                    break;
                                if (!(event.key.mod & SDL_KMOD_SHIFT) && targetTradeBuyer())
                                    break;
                                if (economicSnapshot)
                                    if (auto id = economicSnapshot->contractDestination()) {
                                        marketOpen = false;
                                        chooseActivity(*id);
                                        notice = "SUPPLY CONTRACT DESTINATION / J APPROACH / F2 MARKET";
                                        noticeTime = 5;
                                    }
                                break;
                            default:
                                break;
                            }
                            continue;
                        }
                        // These commands require the captain's helm. B always
                        // takes it back immediately, even during a handover.
                        if (stationControl.requested()) {
                            switch (event.key.scancode) {
                            case SDL_SCANCODE_B:
                            case SDL_SCANCODE_X:
                            case SDL_SCANCODE_N:
                            case SDL_SCANCODE_Z:
                            case SDL_SCANCODE_COMMA:
                            case SDL_SCANCODE_PERIOD:
                            case SDL_SCANCODE_V:
                            case SDL_SCANCODE_BACKSPACE:
                            case SDL_SCANCODE_R:
                            case SDL_SCANCODE_T:
                            case SDL_SCANCODE_F:
                            case SDL_SCANCODE_F2:
                            case SDL_SCANCODE_F8:
                            case SDL_SCANCODE_L:
                            case SDL_SCANCODE_RETURN:
                                stationControl.returnToHelm(crew);
                                break;
                            case SDL_SCANCODE_ESCAPE:
                                stationControl.returnToHelm(crew);
                                notice = stationControl.status();
                                noticeTime = 5;
                                continue;
                            default:
                                break;
                            }
                        }
                        switch (event.key.scancode) {
                        case SDL_SCANCODE_F8:
                            if (pilotEnabled) {
                                crewOpen = true;
                                crewFlightOpen = false;
                                marketOpen = false;
                                releaseLook();
                            }
                            break;
                        case SDL_SCANCODE_ESCAPE:
                            if (looking) {
                                releaseLook();
                                notice = "Mouse released / C to resume flight, Escape to save and quit";
                                noticeTime = 4;
                            } else
                                running = false;
                            break;
                        case SDL_SCANCODE_C:
                            mouseFlight = !mouseFlight;
                            captureLook();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_EQUALS:
                        case SDL_SCANCODE_KP_PLUS:
                        case SDL_SCANCODE_RIGHTBRACKET:
                            if (pilotEnabled)
                                pilot.adjustDrive(1);
                            else
                                nav.adjustSpeed(1);
                            break;
                        case SDL_SCANCODE_MINUS:
                        case SDL_SCANCODE_KP_MINUS:
                        case SDL_SCANCODE_LEFTBRACKET:
                            if (pilotEnabled)
                                pilot.adjustDrive(-1);
                            else
                                nav.adjustSpeed(-1);
                            break;
                        case SDL_SCANCODE_F:
                            activity.selected.reset();
                            pilotEnabled = false;
                            pilot.resetMotion();
                            nav.focus();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_F4:
                            if (!(event.key.mod & SDL_KMOD_SHIFT) && targetActiveHaul())
                                break;
                            if (!(event.key.mod & SDL_KMOD_SHIFT) && targetTradeBuyer())
                                break;
                            if (economicSnapshot)
                                if (auto id = economicSnapshot->contractDestination()) {
                                    chooseActivity(*id);
                                    notice = "SUPPLY CONTRACT DESTINATION / J APPROACH / F2 MARKET";
                                    noticeTime = 5;
                                }
                            break;
                        case SDL_SCANCODE_F2:
                            if (pilotEnabled && economy) {
                                selectMarket();
                                marketOpen = true;
                                pilot.brake();
                                releaseLook();
                            }
                            break;
                        case SDL_SCANCODE_F1:
                            if (pilotEnabled)
                                renderer.toggleCockpitHelp();
                            break;
                        case SDL_SCANCODE_F7:
                            renderer.setCockpitMotionEnabled(!renderer.cockpitMotionEnabled());
                            notice = renderer.cockpitMotionEnabled() ? "Cockpit motion ON / F7 toggles"
                                                                     : "Cockpit motion OFF / F7 toggles";
                            noticeTime = 4;
                            break;
                        case SDL_SCANCODE_BACKSPACE:
                            activity.selected.reset();
                            pilotEnabled = false;
                            pilot.resetMotion();
                            nav.parent();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_V:
                            pilotEnabled = !pilotEnabled;
                            pilot.resetMotion();
                            if (pilotEnabled)
                                pilot.brake();
                            if (pilotEnabled && nav.anchor.kind() >= Kind::Rock && !pilot.ship().shrinking)
                                pilot.fitToView();
                            opt.tour = false;
                            notice = pilotEnabled
                                         ? "Pilot helm / assist ON / W-S throttle / B recover"
                                         : "Explorer camera / existing WASD controls / V returns to ship";
                            noticeTime = 5;
                            break;
                        case SDL_SCANCODE_B:
                            if (pilotEnabled)
                                pilot.brake();
                            break;
                        case SDL_SCANCODE_X:
                            if (pilotEnabled) {
                                pilot.toggleAssist();
                                notice = pilot.ship().assist
                                             ? "Flight assist ON / release helm to arrest rotation"
                                             : "INERTIAL / spin persists / B recovers / X restores assist";
                                noticeTime = 6;
                            }
                            break;
                        case SDL_SCANCODE_N:
                            if (pilotEnabled) {
                                if (event.key.mod & SDL_KMOD_SHIFT)
                                    pilot.selectNextStar();
                                else
                                    pilot.toggleCruise();
                            }
                            break;
                        case SDL_SCANCODE_G:
                            if (pilotEnabled)
                                pilot.toggleGear();
                            break;
                        case SDL_SCANCODE_Z:
                            if (pilotEnabled) {
                                if (event.key.mod & SDL_KMOD_SHIFT)
                                    pilot.restoreFullSize();
                                else
                                    pilot.toggleShrink();
                            }
                            break;
                        case SDL_SCANCODE_COMMA:
                            if (pilotEnabled)
                                pilot.adjustSize(-1);
                            break;
                        case SDL_SCANCODE_PERIOD:
                            if (pilotEnabled)
                                pilot.adjustSize(1);
                            break;
                        case SDL_SCANCODE_Y:
                            if (pilotEnabled) {
                                if (event.key.mod & SDL_KMOD_CTRL) {
                                    if (auto world =
                                            nextLivingWorld(renderer.celestialContacts(), nav.selected)) {
                                        activity.selected.reset();
                                        pilot.selectPlanet(*world);
                                        notice = "LIFE DETECTED / " + universe.describe(*world).label() +
                                                 " / J APPROACH";
                                    } else {
                                        const auto contacts = renderer.celestialContacts();
                                        const bool scanning =
                                            std::any_of(contacts.begin(), contacts.end(), [](const auto& c) {
                                                return c.life == LifeDetection::Scanning ||
                                                       (c.address.kind() == Kind::Planet &&
                                                        c.life == LifeDetection::Unknown);
                                            });
                                        notice = scanning ? "SURFACE LIFE SCAN IN PROGRESS"
                                                          : "NO SURFACE LIFE DETECTED IN THIS SYSTEM";
                                    }
                                    noticeTime = 6;
                                } else {
                                    activity.selected.reset();
                                    if (event.key.mod & SDL_KMOD_SHIFT)
                                        pilot.selectNextMoon();
                                    else
                                        pilot.selectNextPlanet();
                                }
                            }
                            break;
                        case SDL_SCANCODE_K:
                            if (pilotEnabled) {
                                std::vector<ActivityContact> contacts;
                                for (const auto& c : renderer.activityContacts())
                                    if (!(event.key.mod & SDL_KMOD_SHIFT) ||
                                        c.body.kind == ActivityKind::SurfacePort)
                                        contacts.push_back(c);
                                if (!contacts.empty()) {
                                    auto it =
                                        std::find_if(contacts.begin(), contacts.end(), [&](const auto& c) {
                                            return activity.selected && c.body.id == *activity.selected;
                                        });
                                    const size_t next =
                                        it == contacts.end()
                                            ? 0
                                            : (size_t(it - contacts.begin()) + 1) % contacts.size();
                                    chooseActivity(contacts[next].body.id);
                                    notice = hailActivity(*activityBody());
                                    noticeTime = 6;
                                } else if (event.key.mod & SDL_KMOD_SHIFT) {
                                    notice = "No surface ports detected / approach a suitable solid world / "
                                             "F6 shows activity";
                                    noticeTime = 6;
                                }
                            }
                            break;
                        case SDL_SCANCODE_I:
                            if (pilotEnabled)
                                if (auto body = activityBody()) {
                                    notice = hailActivity(*body);
                                    noticeTime = 8;
                                }
                            break;
                        case SDL_SCANCODE_H: {
                            const ActivityContact* nearest = nullptr;
                            for (const auto& contact : renderer.activityContacts())
                                if (contact.body.hostile && !contact.body.disabled && !contact.occluded &&
                                    (!nearest || contact.distance < nearest->distance))
                                    nearest = &contact;
                            if (nearest) {
                                chooseActivity(nearest->body.id);
                                notice = nearest->body.status;
                                noticeTime = 4;
                            } else {
                                notice = "NO HOSTILE CONTACT IN SENSOR RANGE";
                                noticeTime = 3;
                            }
                            break;
                        }
                        case SDL_SCANCODE_F6:
                            activity.enabled = !activity.enabled;
                            if (!activity.enabled) {
                                activity.selected.reset();
                                pilot.clearWaypoint();
                            }
                            notice = activity.enabled ? "LOCAL ACTIVITY ON / K CONTACTS / I HAIL"
                                                      : "LOCAL ACTIVITY HIDDEN / F6 RETURNS";
                            noticeTime = 5;
                            break;
                        case SDL_SCANCODE_J:
                            if (pilotEnabled)
                                navigateSelected();
                            break;
                        case SDL_SCANCODE_L:
                            if (pilotEnabled) {
                                const auto body = activityBody();
                                const bool surface = body && body->kind == ActivityKind::SurfacePort;
                                const bool landing =
                                    surface ? pilot.landAt(body->id.home, glm::normalize(body->position))
                                            : pilot.land();
                                if (landing)
                                    activity.selected.reset();
                                notice =
                                    landing ? "Landing site acquired on existing terrain / assisted approach"
                                    : surface
                                        ? "J approaches the surface port / L lands when nearby"
                                        : "Approach a planet in ship mode before selecting a landing site";
                                noticeTime = 5;
                            }
                            break;
                        case SDL_SCANCODE_RETURN:
                            if (pilotEnabled) {
                                if (pilot.ship().landed)
                                    pilot.launch();
                                else
                                    navigateSelected();
                            }
                            break;
                        case SDL_SCANCODE_TAB:
                            overlay = !overlay;
                            if (!overlay)
                                crewFlightOpen = false;
                            break;
                        case SDL_SCANCODE_R:
                            activity.selected.reset();
                            if (pilotEnabled)
                                pilot.startInSystem();
                            else {
                                nav.reset();
                                pilot.resetMotion();
                            }
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_M:
                            persistence.mark(nav.anchor,
                                             "Discovered " + universe.describe(nav.anchor).label());
                            notice = "Discovery saved / " + nav.anchor.str();
                            noticeTime = 4;
                            break;
                        case SDL_SCANCODE_P: {
                            auto file =
                                (data / ("capture-" + std::to_string(SDL_GetTicks()) + ".ppm")).string();
                            renderer.screenshot(file);
                            notice = "Screenshot saved / " + file;
                            noticeTime = 4;
                            break;
                        }
                        case SDL_SCANCODE_T:
                            pilotEnabled = false;
                            pilot.resetMotion();
                            releaseLook();
                            nav.reset();
                            opt.tour = true;
                            tourStage = 0;
                            stable = 0;
                            returning = false;
                            visited.clear();
                            notice =
                                "Guided journey / all positions and materials come from this universe seed";
                            noticeTime = 5;
                            break;
                        default:
                            break;
                        }
                    }
                }
                if (!running)
                    break;
                if (!opt.smoke)
                    activity.advance(dt);
                if (economy) {
                    auto system = nav.anchor;
                    while (system.kind() > Kind::System)
                        system = system.parent();
                    if (system.kind() == Kind::System)
                        economy->request(system, activity.seconds);
                    economicSnapshot = economy->snapshot();
                    if (economicSnapshot) {
                        if (!opt.smoke) {
                            crew.reconcile(economicSnapshot->player.crew);
                            crew.reconcileMarket(economicSnapshot->player.crewRevision,
                                                 economicSnapshot->player.crewTitles,
                                                 economicSnapshot->player.crewBoarding);
                        }
                        if (pendingCrewSequence &&
                            economicSnapshot->player.lastCommand >= pendingCrewSequence) {
                            crewFeedback = economicSnapshot->message;
                            pendingCrewSequence = 0;
                        }
                        if (!opt.economyTest) {
                            weapons.setLoadout(economicSnapshot->player.weaponLoadout());
                            crew.setWeaponLevels(economicSnapshot->player.weaponLoadout());
                        }
                        if (pendingWreck && economicSnapshot->player.lastCommand >= pendingWreck)
                            pendingWreck = 0;
                        if (!pendingWreck && !economicSnapshot->catchingUp && !opt.smoke)
                            for (const auto& f : economicSnapshot->flights) {
                                auto d = std::find_if(
                                    weapons.damage().begin(), weapons.damage().end(), [&](const auto& d) {
                                        return d.id == f.id &&
                                               (d.hull <= 0 || d.systems[size_t(ShipSystem::Engines)] <= 0);
                                    });
                                if (f.disabledAt < 0 && d != weapons.damage().end()) {
                                    EconomicCommand c;
                                    c.port = f.id;
                                    c.action = EconomicAction::DisableVessel;
                                    c.impactSeconds = d->hitSeconds;
                                    c.sequence = economicSequence =
                                        std::max(economicSequence, economicSnapshot->player.lastCommand) + 1;
                                    if (economy->command(c))
                                        pendingWreck = c.sequence;
                                    break;
                                }
                            }
                        pilot.applyPortRepairs(economicSnapshot->player.repairPoints);
                        if (pendingEconomicSequence &&
                            economicSnapshot->player.lastCommand >= pendingEconomicSequence) {
                            pendingCargoKg = 0;
                            if (!economicSnapshot->outcomes.empty() &&
                                economicSnapshot->outcomes.back().command == pendingEconomicSequence) {
                                marketPage = MarketPage::Outcomes;
                                marketGood = 0;
                                marketOpen = true;
                            }
                            pendingEconomicSequence = 0;
                            marketSay(economicSnapshot->message);
                            if (pendingEconomicAction == EconomicAction::Sell &&
                                pendingEconomicGood == tradeGood && tradeBuyer &&
                                economicSnapshot->message.starts_with("SOLD") &&
                                economicSnapshot->player.cargo[size_t(tradeGood)] == 0) {
                                tradeBuyer.reset();
                                tradeHint.clear();
                            }
                        }
                        pilot.setCargoMass(std::min<int64_t>(
                            maxShipPayloadKg, economicSnapshot->player.massKg() + pendingCargoKg +
                                                  economicSnapshot->player.rescued.size() * 80));
                        if (pendingRecoverySequence &&
                            economicSnapshot->player.lastCommand >= pendingRecoverySequence) {
                            recoveryMessage = economicSnapshot->message;
                            notice = recoveryMessage;
                            noticeTime = 7;
                            pendingRecoverySequence = 0;
                        }
                    }
                    if (tradeBuyer && economicSnapshot && economicSnapshot->network &&
                        !economicSnapshot->siteIndex(*tradeBuyer)) {
                        tradeBuyer.reset();
                        tradeHint.clear();
                    }
                    if (marketOpen) {
                        if (!marketPort || (economicSnapshot && !economicSnapshot->siteIndex(*marketPort)))
                            selectMarket();
                        marketGood = std::min(marketGood, marketRows() - 1);
                        marketView =
                            economicMarketView(economicSnapshot.get(), marketPort, marketGood, marketQuantity,
                                               connectedMarket(), marketPage, marketContext());
                        if (marketPage == MarketPage::Crew)
                            marketGood = marketView.selected;
                        if (marketPage == MarketPage::Goods && tradeBuyer)
                            marketView.summary = tradeHint;
                        marketFeedbackSeconds = std::max(0., marketFeedbackSeconds - dt);
                        if (marketFeedbackSeconds > 0)
                            marketView.message = marketFeedback;
                    }
                    if (!marketOpen) {
                        marketView.open = false;
                        if (economicSnapshot) {
                            marketView.credits = economicSnapshot->player.credits;
                            marketView.cargoKg = economicSnapshot->player.massKg();
                            marketView.cargoLitres = economicSnapshot->player.volumeLitres();
                            marketView.activeJob =
                                !economicSnapshot->player.rescued.empty()
                                    ? "RESCUED " + std::to_string(economicSnapshot->player.rescued.size()) +
                                          " / F2 SERVICES"
                                : economicSnapshot->player.haul      ? "F4 HAUL"
                                : economicSnapshot->player.combatJob ? "F4 SECURITY TARGET"
                                                                     : "";
                        }
                    }
                    renderer.setEconomy(economicSnapshot, &marketView);
                }
                if (pilotEnabled && activity.selected)
                    if (auto body = activityBody())
                        pilot.trackWaypoint(waypointFor(*body));
                auto surfaceFrame = nav.anchor;
                while (surfaceFrame.kind() > Kind::Planet)
                    surfaceFrame = surfaceFrame.parent();
                pilot.setSurfaceWorld(
                    surfaceFrame.kind() == Kind::Planet ? renderer.surfaceWorld(surfaceFrame) : nullptr);
                pilot.setSystemAuthority(crew.systemHealth(ShipSystem::Engines) / 100.,
                                         crew.systemHealth(ShipSystem::Thrusters) / 100.);
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const bool stationWasRequested = stationControl.requested();
                const bool stationWasActive = stationControl.active();
                if (stationControl.requested() && (crewOpen || marketOpen || !pilotEnabled || opt.tour))
                    stationControl.returnToHelm(crew);
                stationControl.synchronize(crew, pilot, nav);
                if ((stationWasRequested && !stationControl.requested()) ||
                    (!stationWasActive && stationControl.active())) {
                    notice = stationControl.status();
                    noticeTime = 7;
                    mouseX = mouseY = 0;
                }
                flight.observeGalaxy(renderer.galaxySamples());
                if (!opt.smoke && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
                    const glm::dvec3 axes{static_cast<double>(keys[SDL_SCANCODE_D]) - keys[SDL_SCANCODE_A],
                                          static_cast<double>(keys[SDL_SCANCODE_E]) - keys[SDL_SCANCODE_Q],
                                          static_cast<double>(keys[SDL_SCANCODE_S]) - keys[SDL_SCANCODE_W]};
                    if (glm::length(axes) > 0)
                        opt.tour = false;
                    if (!opt.tour && pilotEnabled) {
                        HelmInput helm;
                        helm.throttle = -axes.z;
                        helm.strafe = axes.x;
                        helm.lift = axes.y;
                        helm.pitch = double(keys[SDL_SCANCODE_UP]) - keys[SDL_SCANCODE_DOWN];
                        helm.yaw = double(keys[SDL_SCANCODE_RIGHT]) - keys[SDL_SCANCODE_LEFT];
                        helm.roll = double(keys[SDL_SCANCODE_O]) - keys[SDL_SCANCODE_U];
                        helm.boost = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                        helm.precision = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
                        helm.brake = keys[SDL_SCANCODE_B];
                        if (marketOpen || crewOpen)
                            helm = {};
                        if (stationControl.active()) {
                            stationControl.aim(helm.pitch, helm.yaw, {mouseX, mouseY}, dt);
                            stationControl.fly(pilot, crew, universe, nav, activityBody(), dt,
                                               renderer.submittedGeometry(nav.anchor));
                        } else {
                            helm = resolveHelmInput(
                                helm, marketOpen || crewOpen ? glm::dvec2(0) : glm::dvec2(mouseX, mouseY), dt,
                                keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]);
                            pilot.update(helm, dt, renderer.submittedGeometry(nav.anchor));
                        }
                    } else if (!opt.tour)
                        flight.update(axes, dt, keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT],
                                      keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL],
                                      renderer.submittedGeometry(nav.anchor));
                }
                if (!opt.smoke && !opt.tour && pilotEnabled && pilot.ship().cruise &&
                    !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
                    pilot.update({}, dt, renderer.submittedGeometry(nav.anchor));
                if (!opt.smoke) {
                    weapons.advance(dt);
                    stationControl.prepareWeapons(weapons, crew);
                    const bool inhibit = !pilotEnabled || marketOpen || crewOpen ||
                                         pilot.instruments().fieldActive || pilot.ship().landed ||
                                         pilot.ship().hull <= 0 ||
                                         std::abs(pilot.ship().logSize - pilot.ship().targetLogSize) > .01;
                    if (!inhibit && !opt.tour && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
                        keys[SDL_SCANCODE_SPACE]) {
                        auto ray = stationControl.ray(nav, pilot, activity.seconds);
                        ray.targetSystem = targetSystem;
                        const auto geometry = renderer.submittedGeometry(nav.anchor);
                        const auto fired = stationControl.active()
                                               ? (weapons.fireMount(stationControl.mount(), universe, ray,
                                                                    renderer.weaponTargets(), geometry.get())
                                                      ? 1u << stationControl.mount()
                                                      : 0u)
                                               : weapons.fireBattery(universe, ray, renderer.weaponTargets(),
                                                                     geometry.get());
                        if (fired)
                            renderer.cockpitImpulse(
                                CockpitImpulse::Recoil,
                                .35 + weapons.level() * .12 +
                                    .08 * (unsigned(bool(fired & 2)) + unsigned(bool(fired & 4))));
                    }
                    if (!opt.tour) {
                        auto ray = stationControl.ray(nav, pilot, activity.seconds);
                        ray.targetSystem = targetSystem;
                        const auto fired = fireControl.update(
                            universe, ray, nav.orientation, activity.selected,
                            stationControl.active() ? stationControl.mount() : 0, crew, weapons,
                            renderer.weaponTargets(), dt,
                            inhibit || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS),
                            renderer.submittedGeometry(nav.anchor).get());
                        if (fired)
                            renderer.cockpitImpulse(CockpitImpulse::Recoil, .18);
                    }
                }
                nav.update((opt.benchmark ? 1.0 / 60 : dt) * (opt.tour ? 3 : 1));
                if (!opt.smoke) {
                    crew.observeHull(pilot.ship().hull);
                    NpcPlayer player;
                    player.reference = nav.anchor;
                    player.position = nav.cameraPosition() * std::pow(10., nav.logDistance);
                    player.velocity = universe.relative(pilot.ship().frame, nav.anchor, 0).orientation *
                                      pilot.ship().velocity;
                    player.forward = nav.orientation * glm::dvec3(0, 0, -1);
                    player.orientation = nav.orientation;
                    player.size = pilot.sizeRatio();
                    player.hull = pilot.ship().hull;
                    player.seconds = activity.seconds;
                    player.landed = pilot.ship().landed;
                    player.field = pilot.instruments().fieldActive || opt.tour || !pilotEnabled ||
                                   !activity.enabled ||
                                   !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
                    std::optional<ActivityId> escort;
                    if (economicSnapshot)
                        for (const auto& job : economicSnapshot->combatContracts)
                            if (job.phase == CombatJobPhase::Accepted && job.kind == CombatJobKind::Escort)
                                escort = job.target;
                    npcCombat.setEscort(escort);
                    const auto combat = npcCombat.update(universe, player, renderer.combatBodies(), weapons,
                                                         dt, renderer.submittedGeometry(nav.anchor).get());
                    for (const auto& body : combat.civilianHits)
                        weapons.receiveNpcDamage(body, 4., activity.seconds);
                    for (const auto& [system, damage] : combat.playerHits) {
                        pilot.applyCombatDamage(damage);
                        crew.observeCombatHit(pilot.ship().hull, system, damage);
                    }
                    if (economicSnapshot && economy && !economicSnapshot->catchingUp && !opt.smoke &&
                        economicSnapshot->player.combatJob && economicSnapshot->network &&
                        economicSnapshot->player.combatJob->system == economicSnapshot->network->system) {
                        if (economicSnapshot->player.lastCommand >= pendingSecuritySequence)
                            pendingSecuritySequence = 0;
                        if (!pendingSecuritySequence && !pendingEconomicSequence && !pendingCrewSequence &&
                            !pendingWreck && activity.seconds - lastSecurityReport >= 1.) {
                            const auto id = economicSnapshot->player.combatJob->id;
                            auto it = std::find_if(economicSnapshot->combatContracts.begin(),
                                                   economicSnapshot->combatContracts.end(),
                                                   [&](const auto& c) { return c.id == id; });
                            if (it != economicSnapshot->combatContracts.end() &&
                                it->phase == CombatJobPhase::Accepted) {
                                auto body =
                                    damagedBody(sampleCombatActivity(universe, it->target, activity.seconds));
                                const auto point = universe.pointRelative(
                                    body.id.home,
                                    body.position /
                                        std::pow(10., universe.describe(body.id.home).frame.logRadius),
                                    nav.anchor, nav.logDistance);
                                EconomicCommand c;
                                c.action = EconomicAction::ReportCombat;
                                c.port = economicSnapshot->network->sites[it->site].id;
                                c.contract = it->id;
                                c.combatTarget = it->target;
                                c.targetHull = body.hull;
                                c.targetDistance = glm::length(point - nav.cameraPosition()) *
                                                   std::pow(10., nav.logDistance);
                                c.impactSeconds = activity.seconds;
                                c.serviceUnits = unsigned(std::ceil(pilot.ship().hull));
                                c.combatPresent = !player.field && !player.landed &&
                                                  pilot.sizeRatio() >= .99 && !pilot.ship().cruise;
                                if (it->kind == CombatJobKind::Bounty) {
                                    const auto receipt = std::find_if(
                                        weapons.damage().begin(), weapons.damage().end(), [&](const auto& d) {
                                            return d.id == it->target && d.hull <= 0 &&
                                                   d.hitSeconds >= it->acceptedSeconds;
                                        });
                                    c.combatPresent = receipt != weapons.damage().end();
                                }
                                c.sequence = economicSequence =
                                    std::max(economicSequence, economicSnapshot->player.lastCommand) + 1;
                                if (economy->command(c))
                                    pendingSecuritySequence = c.sequence;
                                lastSecurityReport = activity.seconds;
                            }
                        }
                    }
                    const auto recoveryTarget = activityBody();
                    const bool holdingRecovery = keys[SDL_SCANCODE_0] && !marketOpen && !crewOpen &&
                                                 !crewFlightOpen &&
                                                 (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
                    const bool recovered =
                        recovery.update(universe, player, recoveryTarget, dt, holdingRecovery,
                                        renderer.submittedGeometry(nav.anchor).get());
                    if (holdingRecovery && recoveryTarget && economicSnapshot && economy &&
                        !economicSnapshot->catchingUp && !pendingRecoverySequence &&
                        !pendingEconomicSequence && !pendingCrewSequence && !pendingSecuritySequence &&
                        !pendingWreck) {
                        const auto wreck = std::find_if(
                            weapons.damage().begin(), weapons.damage().end(), [&](const auto& d) {
                                return d.id == recoveryTarget->id &&
                                       (d.hull <= 0 || d.systems[size_t(ShipSystem::Engines)] <= 0);
                            });
                        const auto receipt =
                            std::find_if(economicSnapshot->wrecks.begin(), economicSnapshot->wrecks.end(),
                                         [&](const auto& w) { return w.id == recoveryTarget->id; });
                        if (wreck != weapons.damage().end() &&
                            (receipt == economicSnapshot->wrecks.end() || recovered)) {
                            EconomicCommand c;
                            c.port = recoveryTarget->id;
                            c.combatTarget = recoveryTarget->id;
                            c.wreck = *wreck;
                            c.action = receipt == economicSnapshot->wrecks.end()
                                           ? EconomicAction::RegisterWreck
                                           : EconomicAction::RecoverWreck;
                            c.targetDistance = std::max(0., recovery.range() - recoveryTarget->radius);
                            c.relativeSpeed = recovery.relativeSpeed();
                            c.combatPresent = !player.field && !player.landed && player.size >= .99 &&
                                              player.hull > 0 && !pilot.ship().cruise;
                            c.crewAboard = unsigned(crew.living());
                            c.sequence = economicSequence =
                                std::max(economicSequence, economicSnapshot->player.lastCommand) + 1;
                            if (economy->command(c)) {
                                pendingRecoverySequence = c.sequence;
                                if (recovered)
                                    recovery.reset();
                            }
                        }
                    }
                    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) && !pendingCrewSequence)
                        crew.advance(dt, pilot.instruments().properG, !npcCombat.shots().empty(),
                                     weapons.firingMask() != 0, weapons.firingMask());
                }
                if (flightProof)
                    flightProof->advance(flight, renderer);
                if (streamingProof)
                    streamingProof->advance(flight, renderer);
                if (pilotProof)
                    pilotProof->advance(renderer);
                if (economyProof)
                    economyProof->advance(renderer);
                if (npcProof)
                    npcProof->advance(renderer);
                if (crewProof)
                    crewProof->advance(renderer);
                if (crewTradeProof)
                    crewTradeProof->advance(renderer);
                if (crewFlightProof)
                    crewFlightProof->advance(renderer);
                if (crewGunsProof)
                    crewGunsProof->advance(renderer);
                if (stationSwapProof)
                    stationSwapProof->advance(renderer);
                if (combatOperationsProof)
                    combatOperationsProof->advance(renderer);
                if (uiProof)
                    uiProof->advance(renderer);
                if (polarProof)
                    polarProof->advance(renderer);
                if (cloudProof)
                    cloudProof->advance(renderer);
                if (celestialProof)
                    celestialProof->advance(renderer);
                if (landingLightProof)
                    landingLightProof->advance(renderer);
                if (weatherProof)
                    weatherProof->advance(renderer);
                if (stellarDiversityProof)
                    stellarDiversityProof->advance(renderer);
                if (diversityProof)
                    diversityProof->advance(renderer);
                auto cockpit = pilot.instruments();
                if (crewProof) {
                    crewView = crewProof->view();
                    renderer.setCrew(&crewView);
                } else if (!opt.smoke) {
                    crewView = crew.view(crewSelected, crewStation);
                    crewSelected = crewView.selected;
                    crewView.open = crewOpen;
                    crewView.flightOpen = crewFlightOpen && pilotEnabled;
                    crewView.message = crewFeedback;
                    for (unsigned i = 0; i < maxWeaponMounts; ++i)
                        if (!fireControl.status()[i].empty())
                            crewView.gunStatus[i] = fireControl.status()[i] + " / " + crewView.gunStatus[i];
                    crewView.pending = pendingCrewSequence != 0;
                    if (crewOpen) {
                        if (auto i = crewPort()) {
                            crewView.connected = true;
                            crewView.port = economicSnapshot->network->sites[*i].name + " / " +
                                            std::to_string(economicSnapshot->player.credits / 100) + " CR";
                            auto describe = [&](ShipService service, std::string label) {
                                auto q = economicServiceQuote(*economicSnapshot, *i, service, 1);
                                return label + " / " + std::to_string(q.totalCents() / 100) + " CR / " +
                                       q.status;
                            };
                            auto offers = economicCrewOffers(*economicSnapshot, *i);
                            if (!offers.empty()) {
                                const auto& person = offers[crewOffer % offers.size()];
                                const auto q = economicCrewQuote(*economicSnapshot, *i, person, true,
                                                                 crew.living() +
                                                                     economicSnapshot->player.rescued.size());
                                crewView.recruit = "H BUY " + person.name + " / " + crewMoney(q.buyCents) +
                                                   " CR / BRACKETS CYCLE / " + q.status;
                            } else
                                crewView.recruit = "PORT CREW STOCK EMPTY";
                            if (crewView.selected < crew.members().size()) {
                                const auto& person = crew.members()[crewView.selected];
                                const auto q =
                                    economicCrewQuote(*economicSnapshot, *i, person, false, crew.living());
                                crewView.sell = "BACKSPACE SELL " + person.name + " / " +
                                                crewMoney(q.sellCents) + " CR / " + q.status;
                            }
                            crewView.supply =
                                describe(ShipService::CrewSupplies, "S +10 SPARES / +5 MEDICAL");
                            crewView.care = describe(ShipService::CrewCare, "L SHORE CLINIC / SURVIVORS");
                        } else
                            crewView.port = "PORT SERVICES OFFLINE / BERTH OR LAND / FULL SIZE AND STOPPED";
                    }
                    renderer.setCrew(&crewView);
                }
                if (crewTradeProof) {
                    crewTradeProof->observe(renderer, opt.captures);
                    if (crewTradeProof->finished())
                        running = false;
                }
                if (crewFlightProof) {
                    crewView = crewFlightProof->view();
                    renderer.setCrew(&crewView);
                    crewFlightProof->observe(renderer, opt.captures);
                    if (crewFlightProof->finished())
                        running = false;
                }
                if (crewGunsProof) {
                    crewGunsProof->observe(renderer, opt.captures);
                    if (crewGunsProof->finished())
                        running = false;
                }
                if (stationSwapProof) {
                    stationSwapProof->observe(renderer, opt.captures);
                    if (stationSwapProof->finished())
                        running = false;
                }
                npcCombat.annotate(cockpit);
                cockpit.crewFire =
                    "GUNNERS " + std::string(fireOrderName(fireControl.order())) +
                    (crewFlightOpen ? " / CTRL6 HOLD 7 DEF 8 FOCUS" : " / 6 HOLD 7 DEF 8 FOCUS");
                if (economicSnapshot) {
                    for (const auto& c : economicSnapshot->combatContracts)
                        if (c.phase == CombatJobPhase::Accepted || c.phase == CombatJobPhase::Claimable)
                            cockpit.combatMission = c.phase == CombatJobPhase::Claimable
                                                        ? "SECURITY COMPLETE / F2 JOBS / F4 CLAIM PORT"
                                                    : c.kind == CombatJobKind::Escort
                                                        ? "ESCORT " +
                                                              std::to_string(int(c.protectedSeconds)) +
                                                              "/90S / STAY WITHIN 5KM"
                                                        : "NAMED BOUNTY ACTIVE / F2 JOBS / F4 HOSTILE";
                }
                weapons.annotate(cockpit,
                                 cockpit.fieldActive || cockpit.landed || marketOpen || crewOpen ||
                                     std::abs(pilot.ship().logSize - pilot.ship().targetLogSize) > .01);
                stationControl.annotate(cockpit, crew, weapons);
                cockpit.recoveryStatus = pendingRecoverySequence ? "RECOVERY TRANSFER PENDING"
                                         : crewFlightOpen && !recovery.status().empty()
                                             ? "CLOSE F9 / HOLD 0 TO RECOVER"
                                             : recovery.status();
                cockpit.systemFault =
                    "ENG " + std::to_string(int(crew.systemHealth(ShipSystem::Engines))) + " / RCS " +
                    std::to_string(int(crew.systemHealth(ShipSystem::Thrusters))) + " / O2 " +
                    std::to_string(int(crew.systemHealth(ShipSystem::LifeSupport)));
                if (auto body = activityBody()) {
                    cockpit.systemTarget = "9 TARGET " + std::string(shipSystemName(targetSystem)) + " " +
                                           std::to_string(int(targetSystem == ShipSystem::Hull
                                                                  ? body->hull
                                                                  : body->systems[size_t(targetSystem)])) +
                                           "%";
                    const auto point =
                        body->position + body->orientation * (systemOffset(targetSystem) * body->radius);
                    const auto relative = universe.pointRelative(
                        body->id.home,
                        point / std::pow(10., universe.describe(body->id.home).frame.logRadius), nav.anchor,
                        0);
                    cockpit.localSystemTarget =
                        glm::inverse(nav.orientation) *
                        (relative - nav.cameraPosition() * std::pow(10., nav.logDistance));
                }
                if (combatOperationsProof) {
                    combatOperationsProof->annotate(cockpit);
                    combatOperationsProof->observe(renderer, opt.captures);
                    if (combatOperationsProof->finished())
                        running = false;
                }
                renderer.setCockpit(pilotEnabled ? &cockpit : nullptr);
                if (motionProof)
                    motionProof->apply(renderer);
                if (pointLightProof)
                    pointLightProof->apply(renderer);
                auto stats =
                    renderer.render(nav, cache,
                                    overlay && !opt.polarTest && !opt.cloudTest && !opt.celestialTest &&
                                        !opt.landingLightTest && !opt.weatherTest && !opt.diversityTest,
                                    fps, persistence.markerCount(),
                                    noticeTime > 0 && !opt.starStabilityTest ? notice : "", looking);
                if (pointLightProof) {
                    pointLightProof->observe(stats, cache, renderer, opt.captures);
                    if (pointLightProof->finished())
                        running = false;
                }
                if (npcProof) {
                    npcProof->observe(stats, renderer, opt.captures);
                    if (npcProof->finished())
                        running = false;
                }
                if (crewProof) {
                    crewProof->observe(renderer, opt.captures);
                    if (crewProof->finished())
                        running = false;
                }
                if (flightProof) {
                    flightProof->observe(stats, renderer, opt.captures);
                    if (flightProof->finished())
                        running = false;
                }
                if (streamingProof) {
                    streamingProof->observe(stats, renderer, opt.captures);
                    if (streamingProof->finished())
                        running = false;
                }
                if (terrainProof) {
                    terrainProof->observe(stats, cache, renderer, opt.captures);
                    if (terrainProof->finished())
                        running = false;
                }
                if (planetProof) {
                    planetProof->observe(stats, cache, renderer, opt.captures);
                    if (planetProof->finished())
                        running = false;
                }
                if (uiProof) {
                    uiProof->observe(stats, cache, renderer, opt.captures);
                    if (uiProof->finished())
                        running = false;
                }
                if (polarProof) {
                    polarProof->observe(stats, cache, renderer, opt.captures);
                    if (polarProof->finished())
                        running = false;
                }
                if (celestialProof) {
                    celestialProof->observe(stats, cache, renderer, opt.captures);
                    if (celestialProof->finished())
                        running = false;
                }
                if (landingLightProof) {
                    landingLightProof->observe(stats, cache, renderer, opt.captures);
                    if (landingLightProof->finished())
                        running = false;
                }
                if (cloudProof) {
                    cloudProof->observe(stats, cache, renderer, opt.captures);
                    if (cloudProof->finished())
                        running = false;
                }
                if (surfaceProof) {
                    surfaceProof->observe(stats, cache, renderer, opt.captures);
                    if (surfaceProof->finished())
                        running = false;
                }
                if (weatherProof) {
                    weatherProof->observe(stats, cache, renderer, opt.captures);
                    if (weatherProof->finished())
                        running = false;
                }
                if (stellarDiversityProof) {
                    stellarDiversityProof->observe(stats, cache, renderer, opt.captures);
                    if (stellarDiversityProof->finished())
                        running = false;
                }
                if (diversityProof) {
                    diversityProof->observe(stats, cache, renderer, opt.captures);
                    if (diversityProof->finished())
                        running = false;
                }
                noticeTime -= dt;
                ++frame;
                if (frame > 10)
                    frameTimes.push_back(wall * 1000);
                if (economyProof) {
                    economyProof->observe(stats, renderer, opt.captures);
                    if (economyProof->finished())
                        running = false;
                }
                if (pilotProof) {
                    pilotProof->observe(stats, renderer, opt.captures);
                    if (pilotProof->finished())
                        running = false;
                }
                if (motionProof) {
                    motionProof->observe(stats, cache, renderer, opt.captures);
                    if (motionProof->finished())
                        running = false;
                }
                auto gen = cache.stats();
                if (!gen.error.empty())
                    throw std::runtime_error(gen.error);
                if (profile)
                    profile << frame << ',' << kindName(nav.anchor.kind()) << ',' << nav.logDistance << ','
                            << wall * 1000 << ',' << stats.cpuMs << ',' << stats.gpuMs << ','
                            << stats.drawCalls << ',' << stats.visibleObjects << ',' << stats.atoms << ','
                            << gen.bytes << ',' << stats.gpuBytes << ',' << gen.pending << ','
                            << stats.lodTransitions << ',' << stats.sections << ',' << stats.submitMs << ','
                            << stats.vertices << ',' << stats.triangles << ',' << stats.particles << ','
                            << stats.uploadBytes << ',' << nav.transitioning << ',' << gen.queued << ','
                            << gen.cancelled << ',' << gen.evicted << ',' << stats.atomTriangles << ','
                            << stats.uploadDeferred << ',' << renderModeName(renderer.mode()) << ','
                            << stats.effectBytes << ',' << stats.terrainTiles << ',' << stats.rootCells << ','
                            << stats.activityBodies << ',' << stats.activityVisible << ','
                            << stats.activityTriangles << ',' << stats.activityBytes << ','
                            << stats.materialBytes << ',' << stats.surfaceSites << ','
                            << stats.surfaceBuildings << ',' << stats.surfaceResidents << ','
                            << stats.surfaceTriangles << ',' << stats.surfaceBytes << ','
                            << stats.surfaceGpuBytes << ',' << stats.surfacePending << ','
                            << stats.weatherCells << ',' << stats.weatherParticles << ',' << stats.shadowBytes
                            << ',' << stats.shadowActive << ',' << stats.shadowUpdates << ','
                            << stats.weatherPending << ',' << stats.weatherCacheBytes << ','
                            << stats.weatherGpuBytes << ',' << stats.weatherVolumes << '\n';
                if (opt.tour) {
                    if (!nav.transitioning && stats.visibleObjects > 0 && gen.pending == 0 &&
                        gen.queued == 0 && stats.surfacePending == 0 && stats.weatherPending == 0 &&
                        stats.lodTransitions == 0 && stats.uploadDeferred == 0)
                        ++stable;
                    else
                        stable = 0;
                    if (captureHold > 0)
                        --captureHold;
                    if (stable >= 35 && captureHold == 0) {
                        if (opt.presentationTest) {
                            auto geometry = renderer.submittedGeometry(nav.anchor);
                            if (!geometry)
                                throw std::runtime_error("Missing comparison geometry");
                            auto hash = geometryFingerprint(*geometry);
                            if (!captureQueued) {
                                compareHash = hash;
                                compareCamera = nav.serialize();
                                compareGeneration = gen;
                            } else if (hash != compareHash || nav.serialize() != compareCamera ||
                                       gen.completed != compareGeneration.completed ||
                                       gen.misses != compareGeneration.misses)
                                throw std::runtime_error(
                                    "Rendering mode changed scene, camera or generation state");
                            if (!captureQueued) {
                                if (!opt.captures.empty())
                                    renderer.screenshot(
                                        (std::filesystem::path(opt.captures) /
                                         (std::to_string(tourStage) + "-" +
                                          std::string(kindName(nav.anchor.kind())) + "-" +
                                          std::string(renderModeName(renderer.mode())) + ".ppm"))
                                            .string());
                                captureQueued = true;
                                captureHold = 4;
                            } else {
                                std::cout << "PRESENTATION_PROOF " << nav.anchor.str() << " geometry=" << hash
                                          << " camera=identical generation=unchanged renderer=HYPERION\n";
                                captureQueued = false;
                                stable = 0;
                            }
                            if (captureQueued)
                                continue;
                        }
                        if (!opt.presentationTest && !opt.captures.empty() && !captureQueued) {
                            renderer.screenshot(
                                (std::filesystem::path(opt.captures) /
                                 (std::to_string(tourStage) + "-" +
                                  std::to_string(static_cast<int>(nav.anchor.kind())) + ".ppm"))
                                    .string());
                            captureQueued = true;
                            captureHold = 4;
                        } else {
                            stable = 0;
                            auto node = universe.describe(nav.anchor);
                            captureQueued = false;
                            if (proof)
                                proof->observe(node, renderer.submittedGeometry(nav.anchor));
                            std::cout << "TOUR " << tourStage << " " << kindName(nav.anchor.kind()) << " "
                                      << nav.anchor.str() << " scale=" << nav.logDistance
                                      << " draws=" << stats.drawCalls << " atoms=" << stats.atoms
                                      << " mineral=" << static_cast<unsigned>(node.environment.mineral)
                                      << '\n'
                                      << std::flush;
                            ++tourStage;
                            if (!returning) {
                                visited.emplace_back(nav.anchor, node.seed);
                                ++tourVisited;
                                if (nav.selected)
                                    nav.focus();
                                else
                                    returning = true;
                            }
                            if (returning) {
                                auto found = std::find_if(visited.begin(), visited.end(),
                                                          [&](auto& v) { return v.first == nav.anchor; });
                                if (found == visited.end() || found->second != node.seed)
                                    throw std::runtime_error("Tour revisit identity mismatch");
                                if (nav.anchor.kind() ==
                                    (opt.presentationTest ? Kind::Cosmic : Kind::Galaxy)) {
                                    tourPassed = true;
                                    opt.tour = false;
                                    notice = "Journey complete / the same galaxy, star, planet, rock and "
                                             "material survived the return";
                                    noticeTime = 7;
                                    if (opt.smoke)
                                        running = false;
                                } else
                                    nav.parent();
                            }
                        }
                    }
                }
                if (opt.frames > 0 && frame >= opt.frames)
                    running = false;
                if (opt.smoke &&
                    std::chrono::duration<double>(now - start).count() >
                        (opt.crewTest || opt.crewTradeTest || opt.crewFlightTest || opt.crewGunsTest ||
                                 opt.stationSwapTest || opt.starStabilityTest || opt.combatOperationsTest
                             ? 240
                         : opt.npcTest                                   ? 180
                         : opt.economyTest                               ? 330
                         : opt.diversityTest || opt.stellarDiversityTest ? 360
                         : opt.celestialTest || opt.weatherTest || opt.cloudTest || opt.polarTest ||
                                 opt.surfaceTest
                             ? 210
                         : opt.pilotTest ? 180
                                         : 100))
                    throw std::runtime_error("Native GPU tour timed out before completion");
                if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            if (!opt.profile.empty()) {
                profile.flush();
                if (!profile)
                    throw std::runtime_error("Could not finish profiling CSV (check free disk space)");
            }
            if (economy)
                economy->flush();
            if (!opt.smoke) {
                persistence.saveState(nav.serialize());
                auto saveText = [&](const char* name, const std::string& text) {
                    auto path = data / name, pending = path;
                    pending += ".pending";
                    {
                        std::ofstream out(pending);
                        out << text << '\n';
                        out.flush();
                        if (!out)
                            throw std::runtime_error("Unable to save flight settings");
                    }
                    std::filesystem::rename(pending, path);
                };
                saveText(("ship-" + std::to_string(universe.seed()) + ".txt").c_str(), pilot.serialize());
                saveText(("activity-" + std::to_string(universe.seed()) + ".txt").c_str(),
                         activity.serialize());
                saveText(("combat-" + std::to_string(universe.seed()) + ".txt").c_str(), weapons.serialize());
                saveText(("fire-control-" + std::to_string(universe.seed()) + ".txt").c_str(),
                         fireControl.serialize());
                saveText(("npc-combat-" + std::to_string(universe.seed()) + ".txt").c_str(),
                         npcCombat.serialize(universe.seed()));
                saveText(("crew-" + std::to_string(universe.seed()) + ".txt").c_str(), crew.serialize());
                // Rendering is fixed to Hyperion; historical display.txt preferences are ignored.
                saveText("cockpit-motion.txt", renderer.cockpitMotionEnabled() ? "1" : "0");
            }
            cache.wait();
            if (proof)
                proof->finish();
            if (!frameTimes.empty()) {
                std::sort(frameTimes.begin(), frameTimes.end());
                std::cout << "FRAME median_ms=" << frameTimes[frameTimes.size() / 2]
                          << " p95_ms=" << frameTimes[static_cast<size_t>(frameTimes.size() * .95)]
                          << " frames=" << frame << " cache_bytes=" << cache.stats().bytes << '\n';
            }
#if defined(__APPLE__) || defined(__linux__)
            struct rusage usage{};
            getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
            std::cout << "peak_rss_bytes=" << usage.ru_maxrss << '\n';
#else
            std::cout << "peak_rss_bytes=" << usage.ru_maxrss * 1024 << '\n';
#endif
#endif
            if (opt.starStabilityTest) {
                if (!pointLightProof->finished())
                    throw std::runtime_error("Native star stability validation incomplete");
            } else if (opt.combatOperationsTest) {
                if (!combatOperationsProof->finished())
                    throw std::runtime_error("Native combat operations incomplete");
            } else if (opt.stationSwapTest) {
                if (!stationSwapProof->finished())
                    throw std::runtime_error("Native station swap validation incomplete");
            } else if (opt.crewGunsTest) {
                if (!crewGunsProof->finished())
                    throw std::runtime_error("Native crewed guns validation incomplete");
            } else if (opt.crewFlightTest) {
                if (!crewFlightProof->finished())
                    throw std::runtime_error("Native crew flight validation incomplete");
            } else if (opt.crewTradeTest) {
                if (!crewTradeProof->finished())
                    throw std::runtime_error("Native crew trading incomplete");
            } else if (opt.crewTest) {
                if (!crewProof->finished())
                    throw std::runtime_error("Native crew validation incomplete");
            } else if (opt.npcTest) {
                if (!npcProof->finished())
                    throw std::runtime_error("Native NPC combat validation incomplete");
            } else if (opt.cockpitMotionTest) {
                if (!motionProof->finished())
                    throw std::runtime_error("Native cockpit motion validation incomplete");
            } else if (opt.stellarDiversityTest) {
                if (!stellarDiversityProof->finished())
                    throw std::runtime_error("Stellar diversity gallery incomplete");
            } else if (opt.diversityTest) {
                if (!diversityProof->finished())
                    throw std::runtime_error("Planet diversity fixture incomplete");
            } else if (opt.weatherTest) {
                if (!weatherProof->finished())
                    throw std::runtime_error("Weather visibility fixture incomplete");
            } else if (opt.landingLightTest) {
                if (!landingLightProof->finished())
                    throw std::runtime_error("Landing-light fixture incomplete");
            } else if (opt.celestialTest) {
                if (!celestialProof->finished())
                    throw std::runtime_error("Celestial graphics fixture incomplete");
            } else if (opt.cloudTest) {
                if (!cloudProof->finished())
                    throw std::runtime_error("Native cloud validation incomplete");
            } else if (opt.polarTest) {
                if (!polarProof->finished())
                    throw std::runtime_error("Native polar optics validation incomplete");
            } else if (opt.uiFitTest) {
                if (!uiProof->finished())
                    throw std::runtime_error("Native UI fit validation incomplete");
            } else if (opt.planetTest) {
                if (!planetProof->finished())
                    throw std::runtime_error("Native planet stability validation incomplete");
            } else if (opt.surfaceTest) {
                if (!surfaceProof->finished())
                    throw std::runtime_error("Native surface life validation incomplete");
            } else if (opt.terrainTest) {
                if (!terrainProof->finished())
                    throw std::runtime_error("Native terrain validation incomplete");
            } else if (opt.economyTest) {
                if (!economyProof->finished())
                    throw std::runtime_error("Native economic validation incomplete");
            } else if (opt.pilotTest) {
                if (!pilotProof->finished())
                    throw std::runtime_error("Native piloting test incomplete");
            } else if (opt.streamingTest) {
                if (!streamingProof->finished())
                    throw std::runtime_error("Native streaming test incomplete");
            } else if (opt.flightTest) {
                if (!flightProof->finished())
                    throw std::runtime_error("Native automatic flight incomplete");
            } else if (opt.smoke) {
                if (proof)
                    std::cout << "PROOF recipe=" << generationRecipeVersion << " objects=" << proof->objects()
                              << " revisits=" << proof->revisits() << " digest=" << proof->digest()
                              << " (all LODs, descriptors, materials and atomic sites)\n";
                if (!tourPassed || tourVisited != (opt.presentationTest ? 9 : 8))
                    throw std::runtime_error("Native GPU tour incomplete");
                std::cout << "PASS: native GPU journey visited " << tourVisited
                          << " scales and returned with matching identities.\n";
            }
        }
        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Infinite: " << e.what() << '\n';
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}
