#include "infinite/Continuity.hpp"
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
        else if (arg == "--fresh")
            o.fresh = true;
        else if (arg == "--frames")
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
            std::cout << "Infinite: WASD fly, right-drag look, scroll scale, click select, F focus, "
                         "Backspace parent, Tab overlay, R reset, M marker, P screenshot, T tour, Esc "
                         "quit.\nCLI: --fresh --tour --smoke-test --frames N --width N --height N --seed N "
                         "--benchmark-tour --capture-dir DIR --profile CSV --data DIR\n";
            std::exit(0);
        } else
            throw std::invalid_argument("Unknown option " + arg);
    }
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
        RegionCache cache(universe);
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
                           "upload_deferred\n";
            }
            bool running = true, overlay = true, looking = false;
            int frame = 0, tourStage = 0, stable = 0, captureHold = 0;
            bool captureQueued = false, returning = false, tourPassed = false;
            size_t tourVisited = 0;
            std::vector<std::pair<Address, uint64_t>> visited;
            std::vector<double> frameTimes;
            double fps = 60, noticeTime = 6;
            std::string notice =
                "F: enter the selected system. Keep pressing F to follow matter to the atomic scale.";
            auto previous = std::chrono::steady_clock::now();
            auto start = previous;
            while (running) {
                auto now = std::chrono::steady_clock::now();
                double wall = std::chrono::duration<double>(now - previous).count();
                previous = now;
                double dt = std::clamp(wall, .0001, .05);
                fps = glm::mix(fps, 1 / std::max(wall, .0001), .035);
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_EVENT_QUIT)
                        running = false;
                    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                        int w, h;
                        SDL_GetWindowSizeInPixels(window, &w, &h);
                        renderer.resize(w, h);
                    }
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                        if (event.button.button == SDL_BUTTON_RIGHT) {
                            looking = true;
                            SDL_SetWindowRelativeMouseMode(window, true);
                        } else if (event.button.button == SDL_BUTTON_LEFT) {
                            int w, h, pw, ph;
                            SDL_GetWindowSize(window, &w, &h);
                            SDL_GetWindowSizeInPixels(window, &pw, &ph);
                            if (auto a = renderer.pick(event.button.x * pw / w, event.button.y * ph / h)) {
                                nav.select(*a);
                                notice = "Selected " + universe.describe(*a).label() + " / F to focus";
                                noticeTime = 4;
                            }
                        }
                    }
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
                        looking = false;
                        SDL_SetWindowRelativeMouseMode(window, false);
                    }
                    if (event.type == SDL_EVENT_MOUSE_MOTION && looking)
                        nav.rotate(event.motion.xrel, event.motion.yrel);
                    if (event.type == SDL_EVENT_MOUSE_WHEEL)
                        nav.zoom(event.wheel.y);
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        switch (event.key.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            running = false;
                            break;
                        case SDL_SCANCODE_F:
                            nav.focus();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_BACKSPACE:
                            nav.parent();
                            opt.tour = false;
                            break;
                        case SDL_SCANCODE_TAB:
                            overlay = !overlay;
                            break;
                        case SDL_SCANCODE_R:
                            nav.reset();
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
                nav.move({static_cast<double>(keys[SDL_SCANCODE_D]) - keys[SDL_SCANCODE_A],
                          static_cast<double>(keys[SDL_SCANCODE_E]) - keys[SDL_SCANCODE_Q],
                          static_cast<double>(keys[SDL_SCANCODE_S]) - keys[SDL_SCANCODE_W]},
                         dt, keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                nav.update((opt.benchmark ? 1.0 / 60 : dt) * (opt.tour ? 3 : 1));
                auto stats = renderer.render(nav, cache, overlay, fps, persistence.markerCount(),
                                             noticeTime > 0 ? notice : "");
                noticeTime -= dt;
                ++frame;
                if (frame > 10)
                    frameTimes.push_back(wall * 1000);
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
                            << stats.uploadDeferred << '\n';
                if (opt.tour) {
                    if (!nav.transitioning && stats.visibleObjects > 0 && gen.pending == 0 &&
                        gen.queued == 0 && stats.lodTransitions == 0 && stats.uploadDeferred == 0)
                        ++stable;
                    else
                        stable = 0;
                    if (captureHold > 0)
                        --captureHold;
                    if (stable >= 35 && captureHold == 0) {
                        if (!opt.captures.empty() && !captureQueued) {
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
                                if (nav.anchor.kind() == Kind::Galaxy) {
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
            if (!opt.smoke)
                persistence.saveState(nav.serialize());
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
            if (opt.smoke) {
                if (proof)
                    std::cout << "PROOF recipe=" << generationRecipeVersion << " objects=" << proof->objects()
                              << " revisits=" << proof->revisits() << " digest=" << proof->digest()
                              << " (all LODs, descriptors, materials and atomic sites)\n";
                if (!tourPassed || tourVisited != 8)
                    throw std::runtime_error("Native GPU tour incomplete");
                std::cout
                    << "PASS: native GPU journey visited 8 scales and returned with matching identities.\n";
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
