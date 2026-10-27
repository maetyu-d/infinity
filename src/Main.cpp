#include "FlightValidation.hpp"
#include "PilotValidation.hpp"
#include "StreamingValidation.hpp"
#include "infinite/Continuity.hpp"
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
#include <thread>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

using namespace infinite;
struct Options {
    bool tour = false, smoke = false, fresh = false, benchmark = false;
    int frames = 0, width = 1440, height = 900;
    uint64_t seed = 0x494e46494e495445ULL;
    std::string captures, profile, data;
    RenderMode renderMode = RenderMode::Original;
    double visualTime = -1;
    bool compareModes = false;
    bool flightTest = false;
    bool streamingTest = false;
    bool pilotTest = false, explicitMode = false;
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
        else if (arg == "--smoke-test")
            o.smoke = o.tour = o.fresh = true;
        else if (arg == "--benchmark-tour")
            o.benchmark = o.smoke = o.tour = o.fresh = true;
        else if (arg == "--flight-test") {
            o.flightTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--streaming-test") {
            o.streamingTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--pilot-test") {
            o.pilotTest = o.benchmark = o.smoke = o.fresh = true;
            o.visualTime = 12;
        } else if (arg == "--fresh")
            o.fresh = true;
        else if (arg == "--render-mode") {
            o.renderMode = parseRenderMode(value());
            o.explicitMode = true;
        } else if (arg == "--visual-time")
            o.visualTime = std::stod(value());
        else if (arg == "--compare-modes") {
            o.compareModes = o.benchmark = o.smoke = o.tour = o.fresh = true;
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
            std::cout << "Infinite pilot: W/S throttle, A/D strafe, Q/E lift, U/O roll, B brake, X assist, "
                         "J navigate, L land, Enter launch, G gear, N cruise, Z survey, V explorer. "
                         "C mouse helm, arrows pitch/yaw, right-drag look, "
                         "-/+ speed, Shift boost, Ctrl fine, scroll scale, click select, F focus, "
                         "Backspace parent, Tab overlay, R reset, M marker, P screenshot, T tour, Esc "
                         "release mouse / quit.\nCLI: --fresh --tour --smoke-test --frames N --width N "
                         "--height N --seed N "
                         "--benchmark-tour --flight-test --streaming-test --pilot-test --capture-dir DIR "
                         "--profile CSV "
                         "--data DIR\n";
            std::cout << "H cycles ORIGINAL / HYPERION / HYBRID. --render-mode original|hyperion|hybrid "
                         "--compare-modes --visual-time SECONDS\n";
            std::exit(0);
        } else
            throw std::invalid_argument("Unknown option " + arg);
    }
    if (o.compareModes) {
        o.renderMode = RenderMode::Original;
        o.visualTime = 12;
    }
    if (((o.flightTest || o.streamingTest) && o.tour) || (o.flightTest && o.streamingTest))
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
                                  SDL_WINDOW_RESIZABLE | (opt.smoke ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0));
        if (!window)
            throw std::runtime_error(SDL_GetError());
        if (opt.smoke) {
            double density = std::max(1.0, static_cast<double>(SDL_GetWindowPixelDensity(window)));
            SDL_SetWindowSize(window, static_cast<int>(std::lround(opt.width / density)),
                              static_cast<int>(std::lround(opt.height / density)));
            SDL_SyncWindow(window);
        }
        int pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        std::cout << "BACKBUFFER " << pixelWidth << 'x' << pixelHeight << " (requested " << opt.width << 'x'
                  << opt.height << ")\n";
        Universe universe(opt.seed);
        Navigation nav(universe);
        if (opt.compareModes)
            nav.parent();
        RegionCache cache(universe);
        FlightController flight(universe, nav, cache);
        Pilot pilot(universe, nav, flight);
        bool pilotEnabled = !opt.smoke || opt.pilotTest;
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
            Renderer renderer(window, universe);
            if (!opt.smoke && !opt.explicitMode) {
                opt.renderMode = RenderMode::Hyperion;
                std::ifstream settings(data / "display.txt");
                std::string selected;
                if (std::getline(settings, selected)) {
                    for (char& c : selected)
                        if (c >= 'A' && c <= 'Z')
                            c = char(c - 'A' + 'a');
                    try {
                        opt.renderMode = parseRenderMode(selected);
                    } catch (...) {
                    }
                }
            }
            renderer.setMode(opt.renderMode);
            std::unique_ptr<PilotValidation> pilotProof;
            if (opt.pilotTest)
                pilotProof = std::make_unique<PilotValidation>(universe, nav, pilot);
            bool shipRestored = false;
            if (!opt.fresh && !opt.smoke) {
                std::ifstream saved(data / ("ship-" + std::to_string(universe.seed()) + ".txt"));
                std::string state;
                if (std::getline(saved, state))
                    shipRestored = pilot.restore(state);
            }
            if (!shipRestored && !opt.smoke && nav.anchor.kind() >= Kind::Rock && !pilot.ship().survey)
                pilot.toggleSurvey();
            renderer.freezeVisualTime(opt.visualTime);
            std::unique_ptr<RoundTripVerifier> proof;
            if (opt.smoke && !opt.benchmark)
                proof = std::make_unique<RoundTripVerifier>(universe);
            std::cout << "Infinite / " << renderer.backend() << " / seed " << universe.seed() << '\n'
                      << std::flush;
            std::ofstream profile;
            if (!opt.profile.empty()) {
                profile.open(opt.profile);
                profile << "frame,kind,view_log_m,wall_ms,cpu_ms,gpu_ms,draws,visible,atoms,cpu_cache_bytes,"
                           "gpu_bytes,pending,lod_transitions,sections,submit_ms,vertices,triangles,"
                           "particles,upload_bytes,transitioning,queued,cancelled,evicted,atom_triangles,"
                           "upload_deferred,render_mode,effect_bytes,terrain_tiles,root_cells\n";
            }
            bool running = true, overlay = true, looking = false;
            bool mouseFlight = false, rightLook = false;
            int frame = 0, tourStage = 0, stable = 0, captureHold = 0;
            bool captureQueued = false, returning = false, tourPassed = false;
            unsigned compareMode = 0;
            uint64_t compareHash = 0;
            std::string compareCamera;
            GenerationStats compareGeneration{};
            size_t tourVisited = 0;
            std::vector<std::pair<Address, uint64_t>> visited;
            std::vector<double> frameTimes;
            double fps = 60, noticeTime = 6;
            std::string notice = "C mouse helm / W-S throttle / J navigate / L land / B brake / V explorer";
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
                            if (auto a = renderer.pick(x, y)) {
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
                    if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0) {
                        nav.zoom(event.wheel.y);
                        if (pilotEnabled) {
                            pilotEnabled = false;
                            pilot.resetMotion();
                        }
                        opt.tour = false;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
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
                            nav.adjustSpeed(1);
                            break;
                        case SDL_SCANCODE_MINUS:
                        case SDL_SCANCODE_KP_MINUS:
                        case SDL_SCANCODE_LEFTBRACKET:
                            nav.adjustSpeed(-1);
                            break;
                        case SDL_SCANCODE_F:
                            pilotEnabled = false;
                            pilot.resetMotion();
                            nav.focus();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_BACKSPACE:
                            pilotEnabled = false;
                            pilot.resetMotion();
                            nav.parent();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_V:
                            pilotEnabled = !pilotEnabled;
                            pilot.resetMotion();
                            if (pilotEnabled && nav.anchor.kind() >= Kind::Rock && !pilot.ship().survey)
                                pilot.toggleSurvey();
                            opt.tour = false;
                            notice = pilotEnabled
                                         ? "Pilot helm engaged / W-S throttle / B brake"
                                         : "Explorer camera / existing WASD controls / V returns to ship";
                            noticeTime = 5;
                            break;
                        case SDL_SCANCODE_B:
                            if (pilotEnabled)
                                pilot.brake();
                            break;
                        case SDL_SCANCODE_X:
                            if (pilotEnabled)
                                pilot.toggleAssist();
                            break;
                        case SDL_SCANCODE_N:
                            if (pilotEnabled)
                                pilot.toggleCruise();
                            break;
                        case SDL_SCANCODE_G:
                            if (pilotEnabled)
                                pilot.toggleGear();
                            break;
                        case SDL_SCANCODE_Z:
                            if (pilotEnabled)
                                pilot.toggleSurvey();
                            break;
                        case SDL_SCANCODE_J:
                            if (pilotEnabled && nav.selected)
                                pilot.navigate(*nav.selected);
                            break;
                        case SDL_SCANCODE_L:
                            if (pilotEnabled) {
                                notice =
                                    pilot.land()
                                        ? "Landing site acquired on existing terrain / assisted approach"
                                        : "Approach a planet in ship mode before selecting a landing site";
                                noticeTime = 5;
                            }
                            break;
                        case SDL_SCANCODE_RETURN:
                            if (pilotEnabled) {
                                if (pilot.ship().landed)
                                    pilot.launch();
                                else if (nav.selected)
                                    pilot.navigate(*nav.selected);
                            }
                            break;
                        case SDL_SCANCODE_TAB:
                            overlay = !overlay;
                            break;
                        case SDL_SCANCODE_H:
                            renderer.setMode(nextRenderMode(renderer.mode()));
                            notice = std::string(renderModeName(renderer.mode())) + " / H cycles render mode";
                            noticeTime = 3;
                            break;
                        case SDL_SCANCODE_R:
                            nav.reset();
                            pilot.resetMotion();
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
                const bool* keys = SDL_GetKeyboardState(nullptr);
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
                        helm.pitch = std::clamp(-mouseY * .15 / (60 * dt), -1., 1.) + keys[SDL_SCANCODE_UP] -
                                     keys[SDL_SCANCODE_DOWN];
                        helm.yaw = std::clamp(mouseX * .15 / (60 * dt), -1., 1.) + keys[SDL_SCANCODE_RIGHT] -
                                   keys[SDL_SCANCODE_LEFT];
                        helm.roll = double(keys[SDL_SCANCODE_O]) - keys[SDL_SCANCODE_U];
                        helm.boost = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                        helm.precision = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
                        helm.brake = keys[SDL_SCANCODE_B];
                        pilot.update(helm, dt, renderer.submittedGeometry(nav.anchor));
                    } else if (!opt.tour)
                        flight.update(axes, dt, keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT],
                                      keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL],
                                      renderer.submittedGeometry(nav.anchor));
                }
                nav.update((opt.benchmark ? 1.0 / 60 : dt) * (opt.tour ? 3 : 1));
                if (flightProof)
                    flightProof->advance(flight, renderer);
                if (streamingProof)
                    streamingProof->advance(flight, renderer);
                if (pilotProof)
                    pilotProof->advance(renderer);
                renderer.setCockpit(pilotEnabled ? &pilot.instruments() : nullptr);
                auto stats = renderer.render(nav, cache, overlay, fps, persistence.markerCount(),
                                             noticeTime > 0 ? notice : "", looking);
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
                noticeTime -= dt;
                ++frame;
                if (frame > 10)
                    frameTimes.push_back(wall * 1000);
                if (pilotProof) {
                    pilotProof->observe(stats, renderer, opt.captures);
                    if (pilotProof->finished())
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
                            << stats.effectBytes << ',' << stats.terrainTiles << ',' << stats.rootCells
                            << '\n';
                if (opt.tour) {
                    if (!nav.transitioning && stats.visibleObjects > 0 && gen.pending == 0 &&
                        gen.queued == 0 && stats.lodTransitions == 0 && stats.uploadDeferred == 0)
                        ++stable;
                    else
                        stable = 0;
                    if (captureHold > 0)
                        --captureHold;
                    if (stable >= 35 && captureHold == 0) {
                        if (opt.compareModes) {
                            auto geometry = renderer.submittedGeometry(nav.anchor);
                            if (!geometry)
                                throw std::runtime_error("Missing comparison geometry");
                            auto hash = geometryFingerprint(*geometry);
                            if (compareMode == 0 && !captureQueued) {
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
                            } else if (compareMode < 2) {
                                ++compareMode;
                                captureQueued = false;
                                captureHold = 8;
                                renderer.setMode(static_cast<RenderMode>(compareMode));
                            } else {
                                std::cout << "MODE_PROOF " << nav.anchor.str() << " geometry=" << hash
                                          << " camera=identical generation=unchanged modes=3\n";
                                compareMode = 0;
                                captureQueued = false;
                                stable = 0;
                                renderer.setMode(RenderMode::Original);
                                // Continue through the ordinary tour advancement below.
                            }
                            if (compareMode != 0 || captureQueued)
                                continue;
                        }
                        if (!opt.compareModes && !opt.captures.empty() && !captureQueued) {
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
                                if (nav.anchor.kind() == (opt.compareModes ? Kind::Cosmic : Kind::Galaxy)) {
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
                if (opt.smoke && std::chrono::duration<double>(now - start).count() > 100)
                    throw std::runtime_error("Native GPU tour timed out before completion");
                if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
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
                saveText("display.txt", std::string(renderModeName(renderer.mode())));
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
            if (opt.pilotTest) {
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
                if (!tourPassed || tourVisited != (opt.compareModes ? 9 : 8))
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
