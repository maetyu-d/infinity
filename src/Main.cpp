#include "CelestialValidation.hpp"
#include "CloudValidation.hpp"
#include "CockpitMotionValidation.hpp"
#include "EconomyValidation.hpp"
#include "FlightValidation.hpp"
#include "LandingLightValidation.hpp"
#include "PilotValidation.hpp"
#include "PlanetDiversityValidation.hpp"
#include "PlanetValidation.hpp"
#include "PolarValidation.hpp"
#include "StellarDiversityValidation.hpp"
#include "StreamingValidation.hpp"
#include "SurfaceValidation.hpp"
#include "TerrainValidation.hpp"
#include "UiValidation.hpp"
#include "WeatherValidation.hpp"
#include "WindowSizing.hpp"
#include "infinite/Cockpit.hpp"
#include "infinite/Continuity.hpp"
#include "infinite/Economy.hpp"
#include "infinite/Flight.hpp"
#include "infinite/Piloting.hpp"
#include "infinite/Renderer.hpp"
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
    bool economyTest = false, cockpitMotionTest = false;
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
        else if (arg == "--economy-test") {
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
                   "C mouse helm, arrows pitch/yaw, right-drag look, "
                   "-/+ speed, Shift boost, Ctrl fine, scroll scale, click select, F focus, "
                   "Backspace parent, F1 controls, F7 cockpit motion, Tab overlay, R reset, M marker, P "
                   "screenshot, T tour, Esc "
                   "release mouse / quit.\nCLI: --fresh --tour --smoke-test --frames N --width N "
                   "--height N --seed N "
                   "--benchmark-tour --flight-test --streaming-test --pilot-test --planet-stability-test "
                   "--economy-test --cockpit-motion-test --ui-fit-test --polar-optics-test "
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
                << "F2 market: arrows select goods/quantity, Enter buy, Backspace sell, F3 supply "
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
        unsigned(o.stellarDiversityTest);
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
                            opt.cockpitMotionTest;
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
                    legacyShip =
                        state.rfind("INFINITE_SHIP 3 ", 0) != 0 && state.rfind("INFINITE_SHIP 4 ", 0) != 0;
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
            renderer.setActivity(&activity);
            std::unique_ptr<EconomyRuntime> economy;
            if (!opt.smoke || opt.economyTest)
                economy = std::make_unique<EconomyRuntime>(universe, data);
            std::shared_ptr<const EconomySnapshot> economicSnapshot;
            MarketView marketView;
            bool marketOpen = false;
            std::optional<ActivityId> marketPort;
            size_t marketGood = 1;
            int64_t marketQuantity = 10;
            uint64_t economicSequence = 0, pendingEconomicSequence = 0;
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
            auto submitMarket = [&](EconomicAction action) {
                if (!economy || !economicSnapshot || !marketPort || economicSnapshot->catchingUp)
                    return;
                auto i = economicSnapshot->siteIndex(*marketPort);
                if (!i)
                    return;
                if (pendingEconomicSequence > economicSnapshot->player.lastCommand)
                    return;
                EconomicCommand command;
                command.port = *marketPort;
                command.action = action;
                command.good = Good(marketGood);
                command.kg = marketQuantity;
                bool needsLink = true;
                if (action == EconomicAction::Contract) {
                    auto it = std::find_if(economicSnapshot->contracts.begin(),
                                           economicSnapshot->contracts.end(), [&](const auto& c) {
                                               return c.site == *i && (c.phase == ContractPhase::Open ||
                                                                       c.phase == ContractPhase::Accepted);
                                           });
                    if (it == economicSnapshot->contracts.end())
                        return;
                    command.contract = it->id;
                    command.good = it->good;
                    needsLink = it->phase != ContractPhase::Open;
                }
                if (needsLink && !connectedMarket()) {
                    marketSay("APPROACH PORT / STOP BELOW 3 M/S / LAND AT SURFACE PORTS");
                    return;
                }
                auto q = economicSnapshot->quote(*i, command.good, marketQuantity);
                command.limitCents = action == EconomicAction::Buy ? q.buyCents : q.sellCents;
                economicSequence = std::max(economicSequence, economicSnapshot->player.lastCommand);
                command.sequence = ++economicSequence;
                if (economy->command(command)) {
                    pendingEconomicSequence = command.sequence;
                    pendingCargoKg = action == EconomicAction::Buy ? marketQuantity : 0;
                    pilot.setCargoMass(
                        std::min<int64_t>(4000, economicSnapshot->player.massKg() + pendingCargoKg));
                    marketSay("ORDER SUBMITTED / AWAITING LEDGER");
                }
            };
            std::unique_ptr<EconomyValidation> economyProof;
            if (opt.economyTest)
                economyProof =
                    std::make_unique<EconomyValidation>(universe, nav, pilot, *economy, activity, window);
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
            auto activityBody = [&]() -> std::optional<ActivityBody> {
                if (!activity.selected)
                    return {};
                if (economicSnapshot && economicSnapshot->network) {
                    auto flight =
                        std::find_if(economicSnapshot->flights.begin(), economicSnapshot->flights.end(),
                                     [&](const auto& f) { return f.id == *activity.selected; });
                    if (flight != economicSnapshot->flights.end())
                        return sampleEconomicFreight(universe, *economicSnapshot->network, *flight,
                                                     activity.seconds);
                    if (auto i = economicSnapshot->siteIndex(*activity.selected)) {
                        const auto& site = economicSnapshot->network->sites[*i];
                        ActivityBody body;
                        body.id = site.id;
                        body.kind = site.orbital ? ActivityKind::Harbour : ActivityKind::SurfacePort;
                        body.position = site.metres;
                        body.radius = site.radius;
                        body.name = site.name;
                        body.status = site.role + " / F2 MARKET";
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
                return sampleActivity(universe.describe(activity.selected->home), activity.selected->slot,
                                      activity.seconds);
            };
            auto waypointFor = [](const ActivityBody& body) {
                const bool surface = body.kind == ActivityKind::SurfacePort;
                return FlightWaypoint{body.id.home,
                                      body.position +
                                          (surface ? glm::normalize(body.position) * 60. : glm::dvec3(0)),
                                      body.velocity,
                                      surface                              ? 0.
                                      : body.kind == ActivityKind::Harbour ? body.radius * 4
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
                    if (event.type == SDL_EVENT_MOUSE_MOTION && looking &&
                        (event.motion.xrel != 0 || event.motion.yrel != 0)) {
                        if (pilotEnabled) {
                            mouseX += event.motion.xrel;
                            mouseY += event.motion.yrel;
                        } else
                            nav.rotate(event.motion.xrel, event.motion.yrel);
                        opt.tour = false;
                    }
                    if (marketOpen && event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                        marketGood = (marketGood + goodCount + (event.wheel.y > 0 ? -1 : 1)) % goodCount;
                        continue;
                    }
                    if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                        nav.zoom(event.wheel.y);
                        if (pilotEnabled) {
                            pilotEnabled = false;
                            pilot.resetMotion();
                        }
                        opt.tour = false;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        if (marketOpen) {
                            switch (event.key.scancode) {
                            case SDL_SCANCODE_ESCAPE:
                            case SDL_SCANCODE_F2:
                                marketOpen = false;
                                break;
                            case SDL_SCANCODE_UP:
                                marketGood = (marketGood + goodCount - 1) % goodCount;
                                break;
                            case SDL_SCANCODE_DOWN:
                                marketGood = (marketGood + 1) % goodCount;
                                break;
                            case SDL_SCANCODE_LEFT:
                                marketQuantity = std::max<int64_t>(1, marketQuantity / 10);
                                break;
                            case SDL_SCANCODE_RIGHT:
                                marketQuantity = std::min<int64_t>(1000, marketQuantity * 10);
                                break;
                            case SDL_SCANCODE_RETURN:
                                submitMarket(EconomicAction::Buy);
                                break;
                            case SDL_SCANCODE_BACKSPACE:
                                submitMarket(EconomicAction::Sell);
                                break;
                            case SDL_SCANCODE_F3:
                                submitMarket(EconomicAction::Contract);
                                break;
                            case SDL_SCANCODE_F4:
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
                        switch (event.key.scancode) {
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
                        if (pendingEconomicSequence &&
                            economicSnapshot->player.lastCommand >= pendingEconomicSequence) {
                            pendingCargoKg = 0;
                            pendingEconomicSequence = 0;
                            marketSay(economicSnapshot->message);
                        }
                        pilot.setCargoMass(
                            std::min<int64_t>(4000, economicSnapshot->player.massKg() + pendingCargoKg));
                    }
                    if (marketOpen) {
                        if (!marketPort || (economicSnapshot && !economicSnapshot->siteIndex(*marketPort)))
                            selectMarket();
                        marketView = economicMarketView(economicSnapshot.get(), marketPort, marketGood,
                                                        marketQuantity, connectedMarket());
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
                const bool* keys = SDL_GetKeyboardState(nullptr);
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
                        if (marketOpen)
                            helm = {};
                        helm = resolveHelmInput(helm, marketOpen ? glm::dvec2(0) : glm::dvec2(mouseX, mouseY),
                                                dt, keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]);
                        pilot.update(helm, dt, renderer.submittedGeometry(nav.anchor));
                    } else if (!opt.tour)
                        flight.update(axes, dt, keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT],
                                      keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL],
                                      renderer.submittedGeometry(nav.anchor));
                }
                if (!opt.smoke && !opt.tour && pilotEnabled && pilot.ship().cruise &&
                    !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))
                    pilot.update({}, dt, renderer.submittedGeometry(nav.anchor));
                nav.update((opt.benchmark ? 1.0 / 60 : dt) * (opt.tour ? 3 : 1));
                if (flightProof)
                    flightProof->advance(flight, renderer);
                if (streamingProof)
                    streamingProof->advance(flight, renderer);
                if (pilotProof)
                    pilotProof->advance(renderer);
                if (economyProof)
                    economyProof->advance(renderer);
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
                renderer.setCockpit(pilotEnabled ? &pilot.instruments() : nullptr);
                if (motionProof)
                    motionProof->apply(renderer);
                auto stats =
                    renderer.render(nav, cache,
                                    overlay && !opt.polarTest && !opt.cloudTest && !opt.celestialTest &&
                                        !opt.landingLightTest && !opt.weatherTest && !opt.diversityTest,
                                    fps, persistence.markerCount(), noticeTime > 0 ? notice : "", looking);
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
                if (opt.smoke && std::chrono::duration<double>(now - start).count() >
                                     (opt.diversityTest || opt.stellarDiversityTest ? 360
                                      : opt.celestialTest || opt.weatherTest || opt.cloudTest ||
                                              opt.polarTest || opt.surfaceTest
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
            if (opt.cockpitMotionTest) {
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
