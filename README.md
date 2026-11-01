# INFINITE

**0.47:** named crew can be bought and sold at connected ports. **F2 → F5 → Crew Market** shows individual offers, skills, condition and bid/ask prices; **Enter** buys, **Backspace** sells. People retain their identity and experience through sale, repurchase and reload. **F8** retains stations and damage control, with direct crew trading shortcuts. [Crew market controls and validation](docs/CREW_TRADE_0_47.md).

**0.46:** six named ship crew, connected stations, skills, fatigue, injuries and casualties, fires, oxygen/breaches and finite damage-control stores. **F8** opens the crew deck: assign stations, dispatch repairs/firefighters/first aid, rest relief crew and vent compartments. Connected ports sell recruitment contracts, supplies and shore treatment through the existing economy. [Crew controls and validation](docs/CREW_0_46.md).

**0.45:** hostile raiders acquire, attack, evade and retreat; existing patrols respond to attacks on themselves or nearby civilians. Enemy fire damages the saved player hull. **H** selects a visible hostile; **Space** fires the fitted laser. Guarded ports, landing, cruise and extreme shrinking provide escape options. [NPC combat, controls and validation](docs/NPC_COMBAT_0_45.md).

**0.44:** a starter pulse laser, Space firing with heat/recoil/impact effects, damage and persistent disabled craft, plus three tradable/upgradable weapon grades. **F2 → F5 → Weapons** buys, upgrades and sells the fitted laser using existing port parts, crews and money. Wrecked freighter cargo remains accounted for. [Weapons, controls and validation](docs/WEAPONS_0_44.md).

**0.43:** funded household relief and plant recovery jobs at existing surface settlements, reserved maintenance kits, paid repair crews, and persistent delivery outcomes. **F2 → F5 → Jobs** accepts and delivers recovery supplies; **Outcomes** shows payment and actual health, power, readiness, service and production changes. Partial deliveries, crew pauses, expiry and save reloads retain their real goods and money. [Recovery jobs and delivery reports](docs/ECONOMY_0_43.md).

**0.42:** economic port reports in the HUD, city lighting driven by real power and supply readiness, and detailed manifests for existing freighters. **K** selects, **I** hails, **J** approaches and **F2** opens port markets. Reports show shortages, repair stock, trade hints, cargo, routes, timing and warehouse holds. Structures, generators, save formats and flight controls remain intact. [Economic world feedback and checks](docs/ECONOMY_0_42.md).

**0.41:** stocked port repairs, three persistent cargo-hold refits up to 10 tonnes / 32,000 L, and job planning with direct distance, cargo fit, travel estimates and deadline margin. **F2 → F5** cycles to **Services**; Enter buys the selected service at a connected port. Larger funded hauls and trade orders use the expanded bay, with existing payload inertia and shrinking restrictions. [Services, progression and planning](docs/ECONOMY_0_41.md).

**0.40:** funded hauling jobs, sealed consignments, physical pickup/delivery, late fees and carrier history. **F2 → F5** opens jobs; Enter accepts/loads/delivers, F4 targets the next port, and Backspace returns or withdraws a haul. Supply work and industrial investment remain available. The restored flight controls are unchanged. [Hauling and job controls](docs/ECONOMY_0_40.md).

**0.39.1:** restores the pre-0.37 assisted manual controls: original mouse/turn response and velocity-scaled manoeuvring, with independent roll control. The current economy, landing, cargo, cruise, shrinking and graphics remain intact. [Control restore and checks](docs/SHIP_HANDLING.md).

**0.39:** player-funded warehouse, utility and automation refits, physical construction materials, paid crews and cash-backed dividends. A live trade board finds affordable routes and bookmarks their buyers. **F2** market, **F5** goods/routes/industry; industry **Enter** funds/delivers, **Backspace** collects, **Delete** cancels. Existing universe and saves are retained. [Economic development, controls and checks](docs/ECONOMY_0_39.md).

**0.38.3:** colourful Hyperion rendering is now the only graphics style. Original/Hybrid modes and the H mode toggle have been removed; old display preferences are ignored. Universe generation, saved flights, controls and cockpit motion remain intact.
**0.38.2:** restrained cockpit inertia, high-G vibration, collision/landing jolts and hull-damage feedback. **F7** toggles motion and saves the preference. The physical ship, aim and easier controls remain unchanged; firing recoil hooks are available for a future weapon system. [Motion and controls](docs/SHIP_HANDLING.md).
**0.38.1:** calmer ship controls, fine mouse response, a shared three-axis turn envelope and faster rotational arrest. **Hold B** to recover even while other controls are held. Returning to the cockpit with **V** enables assist; **X** explicitly selects inertial flight. [Handling measurements and validation](docs/SHIP_HANDLING.md).

**0.38:** persistent civilian production, local inventories and markets, paid cargo shipments, wages, household supplies and escrow-funded contracts attached to existing ports and settlements. **F2** market, **F3** accept/deliver, **F4** contract destination. Cargo adds ship mass and must be unloaded before shrinking. [Economy, controls and validation](docs/ECONOMY_0_38.md).

**0.37:** finite-thrust ship physics, hull inertia and torque-driven roll, continuous stellar/planetary gravity, atmospheric wind and drag, target-relative rendezvous, braking-time and g-load instruments. Cruise and shrinking remain explicit field abilities. **X** toggles assist, **B** brakes, **U/O** rolls, **N** selects cruise. [Physics, controls and validation](docs/SHIP_PHYSICS_0_37.md).

**0.36:** researched stellar families, O–M spectral types, cooling L/T/Y brown dwarfs, compact remnants and giants, with preserved existing systems. **Shift+N** cycles stellar destinations; **J** navigates. [Research and validation](docs/STELLAR_DIVERSITY_0_36.md).

**0.35:** distinct rocky, ice, oceanic, desert and lava presentation, plus additive gas giants, ice giants, sub-Neptunes, hot giants and procedural rings. [Research, preservation and validation](docs/PLANET_DIVERSITY_0_35.md).

A standalone C++20 procedural universe rendered with SDL3 and bgfx. **Infinite 0.34 starts with a full-size ship near an existing planet.** Normal play uses Hyperion-style planetary flight, targeting, approach, landing and launch. Long-range travel is an explicit drive choice; the shrinking field is a separate ship ability.

Version 0.34 adds **evolving planetary circulation, fronts, rotating storm systems, gusts, snow and dust weather**. Moisture, temperature, pressure, gravity, body size and rotation constrain the deterministic forecasts. Lower cloud banks, motion-blurred rain, drifting flakes and dust, depth-aware weather haze, wet terrain and storm-local lightning make the weather visible from flight. The cockpit shows gusts and visibility in its existing weather panel. Established city planning keeps its original climate recipe, preserving sites, layouts and identities. [Weather generation, captures and validation](docs/WEATHER_0_34.md).

Version 0.33 improves **geological surface colour, dry regolith, airless-world reflections and close landing contact shading**. Pigment now absorbs the broad substrate instead of washing it out; rough ground, exposed bedrock, wet lowlands and ice have distinct optical responses. The existing wet-lowland boundary receives filtered shoreline colour. The real terrain and physical hull size control a soft contact shade near landing, with no new geometry or textures. [Matched views and validation](docs/GRAPHICS_0_33.md).

Version 0.32 adds **luminous stellar surfaces, coronal halos, depth-tested solar discs, Hyperion lens optics and clearer glacial worlds**. Existing parent stars supply their real radius, temperature colour and bearing. Planetary eclipses and opaque scene depth control visibility, inside the existing bloom and post passes. Rocky and icy globe shading removes the broad fixed authoring light before live sunlight; exposed ice and frost respond differently. Universe generation, navigation, atomic depth, Original mode and the fitted UI remain intact. [Before/after images and measurements](docs/GRAPHICS_0_32.md).

Version 0.31 adds **fly-through volumetric weather, cloud shadows, physically scaled construction finishes and wet-surface ripples**. Existing weather drives depth-clipped cloud banks with billow, erosion, self-shadowing and spectral silver edges. Roads now receive construction shading, while glass, ceramic, concrete, roofs and solar arrays respond to the local sun and sky. Daylight suppresses distant stars. Cloud generation runs asynchronously with bounded caches, and atmospheric optics follow the active ship or inspector projection. Original rendering, procedural identities, solid geometry, atomic depth, navigation and the fitted UI are preserved. [Graphics comparisons and measurements](docs/GRAPHICS_0_31.md).

Version 0.30 adds **spectral polar auroras, deeper cloud lighting, clearer geological contrast and more directional city illumination**. Aurora curtains follow retained atmospheric eligibility and climate axes, remain clipped by opaque geometry, and reconstruct from a half-resolution optical buffer. Existing heights and slopes control geological contrast; darker sky/ground bounce gives buildings stronger depth. Materials gain a bounded clarity pass while the cockpit stays crisp. Universe generation, saved flights, all three modes and the 0.29 screen-fitting UI remain intact. [Graphics comparisons and measurements](docs/GRAPHICS_0_30.md).

Version 0.29 fits the **window and cockpit to the display**. Startup reserves space for the title bar, menu bar and Dock; moving between displays or restoring the window refits it to the usable work area. Narrow and portrait windows keep every instrument and the F1 guide inside a centred HUD viewport, with matching mouse selection and world-marker projection. Footer hints have a dark backing and extra edge clearance. [Fit checks and native screenshots](docs/UI_FIT.md).

Version 0.28 improves **glacial materials, geological colour, wet lowland reflections, sunset lighting, opalescent clouds and the depth behind existing glass windows**. Magnified materials use smoother filtering and a split noise origin to retain fine camera motion. Planetary colour follows the existing vertex heights, while additional atmosphere samples improve the horizon. The same universe, geometry, controls and reversed-depth fix remain intact. [Matched scenes, preservation and measured cost](docs/GRAPHICS_0_28.md).

Version 0.27 fixes **flickering planetary surfaces across magnification changes** using floating-point reversed-Z world depth in all three rendering modes. Atmospheric depth reading, terrain, cities, traffic and atomic rendering use the matching convention. A new native primary/moon fixture reproduces the old flashing patches and verifies identical geometry at different rendering unit sizes. The universe and saved flight remain intact. [Cause, matched captures and regression checks](docs/PLANET_STABILITY_0_27.md).

Version 0.26 gives **terrain, cities and planets stronger depth and material definition**. Shaded mineral recesses and filtered relief replace the broad ground glow, glass reflects a planetary sky gradient, facade joints receive subtle relief, and atoms retain clearer light/shadow separation. Darker gaps in the spectral sky improve celestial silhouettes; a hue-preserving bloom pass keeps bright emitters distinct. Geometry, seeds and ship handling are preserved. [Matched images, validation and measured cost](docs/GRAPHICS_0_26.md).

Version 0.25 improves **ship handling and unrestricted roll**. Assisted steering responds more quickly, releasing the helm arrests rotation while retaining bank, and stronger manoeuvring jets control lateral drift during turns. Re-engaging assist blends smoothly from the current velocity. **U/O** rolls left/right; **Alt+A/D** adds a nearby roll binding while ordinary A/D strafes and Q/E provides lift. The cockpit displays signed roll rate. Existing saves, universe generation and automatic scale changes are retained. [Handling measurements and validation](docs/SHIP_HANDLING.md).

Version 0.24 strengthens **Hyperion's spectral sky, stellar highlights, planetary colour, mineral seams, cloud illumination, city glass and lighting, cockpit materials and atomic bonds**. Physical gas-envelope and climate metadata control planetary effects; microscopic detail fades below floating-point resolution. The same geometry, identities, seeds and flight behaviour remain intact. The current game uses the full Hyperion presentation throughout. [Visual changes, matched screenshots and measurements](docs/SPECTACULAR_0_24.md).

Version 0.22 makes inhabited worlds easier to find: **green LIFE badges and population counts**, priority system-list rows, a clickable **LIFE DETECTED** panel, green scope/world markers, and **Ctrl-Y** to cycle confirmed inhabited planets and moons. **J** approaches the selected world. Background surveys use the existing surface generator; pending worlds show SCANNING, and settlements in sealed colonies count as life. [Life-detection HUD](docs/LIFE_HUD.md).

Version 0.21 adds **cached sunlight shadows, reflective architectural glass, prismatic ceramic materials and clearer atmospheric lighting**. Existing buildings and street furniture cast shadows onto the actual terrain; metre-based material detail stays attached through flight, zoom and precision-frame changes. Original mode and all procedural structures remain intact. [Graphics notes and comparisons](docs/GRAPHICS_0_21.md).

Version 0.20 develops surface settlements into **connected, mixed-use cities**. Planned, radial, organic and contour-led street networks determine parcels and building frontage. Housing courts, stepped towers, civic halls, row houses, industrial sheds and protected habitat clusters follow district function and access. Organic towns grow irregular blocks and T-junctions; pavements fit the actual terrain, and residents follow home-associated street routes. Existing settlement names, seeds, landing locations and the underlying universe remain intact. [CITY_GENERATION.md](docs/CITY_GENERATION.md) records the research, algorithms and validation.

Version 0.19 adds deterministic **planetary weather and inhabited surfaces**. Atmospheric retention, temperature and inherited water availability control cloud fronts, wind, rain, snow and dust. Suitable solid worlds can contain cities, sealed colonies, mining or research outposts, buildings and moving residents with persistent home identities. Sites use the existing landing height field and leave terrain untouched. **Shift-K** cycles nearby surface ports, **J** approaches their clear landing area, **L** lands and **Enter** launches. Many worlds remain uninhabited. [WEATHER_AND_SURFACE_LIFE.md](docs/WEATHER_AND_SURFACE_LIFE.md) documents the generation, safeguards, controls and validation.

Version 0.18 adds real local terrain to selected expanded solid planets: **Highlands**, **Volcanic**, **Canyons** and **Ice ridges**. Their deterministic spherical height fields drive visible geometry, streaming, collision and landing together; gas envelopes have no terrain or landing surface. The original planet set and moons retain their existing geological fields, and all seeds/orbits stay intact. Terrain LOD now refines near the ship more accurately. Use **Y** to select a world with one of these terrain names, **J** to approach and **L** to land at a gentle site. [SOLID_TERRAIN.md](docs/SOLID_TERRAIN.md) records the algorithms, compatibility and native captures.

Version 0.17 ports Hyperion's baked close-material texture: filtered mineral relief, fracture seams, frost and granular shading, with complete mipmaps and anisotropic filtering. The shared texture bakes asynchronously and uses 1.33 MiB of GPU memory; the original geometry and all universe seeds remain unchanged. Materials retain their phase through scale and floating-origin changes. [MATERIAL_DETAIL.md](docs/MATERIAL_DETAIL.md) records the source algorithm, comparisons and measured cost.

Version 0.16 improves depth and clarity while retaining the psychedelic Hyperion palette: directional day/night lighting, smooth geological shading that retains sharp creases, filtered close-range grain, clearer spectral reflections, quieter sky glow and improved edge filtering. Surface shading runs asynchronously with compact GPU normals; atoms gain an extra close-range display LOD. Generated geometry, positions, identities and flight behaviour stay unchanged. [GRAPHICS_QUALITY.md](docs/GRAPHICS_QUALITY.md) records image comparisons, preservation checks and measured performance.

Version 0.15 introduced Hyperion's **psychedelic** rendering: direction-anchored folded spectral light, mineral pigments, filtered luminous seams, iridescent grazing light, coloured atmospheric scattering and a hue-preserving HDR display curve. Hyperion is now the sole rendering style. The same geometry, objects, seeds, worlds and ship state remain authoritative. [PSYCHEDELIC_PRESENTATION.md](docs/PSYCHEDELIC_PRESENTATION.md) retains the historical source reference and comparisons.

Version 0.14 improves the cockpit with recessed, high-contrast instruments, larger navigation and velocity readings, a larger contact scope, a terrain-relative landing director, and uncluttered target cues. Labels avoid each other and the reticle; off-screen targets retain their actual bearing. **F1** opens the control guide. All universe generators and ship dynamics remain unchanged. [COCKPIT.md](docs/COCKPIT.md) records the Hyperion source reference, interface changes and validation.

Version 0.13 expands each system to **13–21 primary worlds**, with real moons wherever the host's Hill sphere permits stable satellite orbits. Existing planets keep their exact identities, orbits and material structures. New rocky and dwarf worlds fill suitable inner or orbital gaps and extend the outer family. Moons have their own terrain, geology and atomic depth. **Y** cycles primary worlds; **Shift-Y** cycles the selected or current planet's moons; **J** approaches and **L** lands. The cockpit shows system totals and the nearest eight celestial contacts, including a selected destination. [PLANETARY_POPULATIONS.md](docs/PLANETARY_POPULATIONS.md) records generation, preservation and validation.

Version 0.12 adds persistent orbital activity above the existing world: harbours and service yards, scheduled freighters and ferries, patrol circuits, survey craft and mineral ejecta. Click a traffic contact or press **K**, **I** to hail, and **J** to approach a harbour or match a moving ship's course. **F6** hides/shows activity. The denser stellar sky refines the same addressed population. [ORBITAL_ACTIVITY.md](docs/ORBITAL_ACTIVITY.md) documents the Hyperion source, deterministic schedules, controls and validation.

Version 0.11 makes the surrounding universe visible from the cockpit: all existing system planets and the sun appear on the contact scope and list, distant unresolved planets retain selectable light proxies, and addressed stars, gas and neighbouring galaxies supply the sky. The lights retain their true bearings, distances and destination addresses. [CELESTIAL_CONTEXT.md](docs/CELESTIAL_CONTEXT.md) explains the visibility fixes and comparisons.

The universe remains at its original physical scale. Existing hierarchical addresses, seeds, geology, rocks, minerals and atomic structures are preserved when populations expand. Camera precision changes never resize the normal ship. See [OPERATING_SCALES.md](docs/OPERATING_SCALES.md) for the three operating regimes, controls and validation.

Version 0.8 removes the isolated terrain windows and single-root-cell flight restriction. A complete, stitched planetary surface streams beneath the camera, with continuous mesh morphing as detail changes. Neighbouring cosmic cells preload and hand off without changing the physical camera; blended collars fill their former sampling gaps. The same world fields, seeds and objects remain authoritative. See [STREAMING.md](docs/STREAMING.md) for implementation, image checks and resource bounds.

Version 0.7 adds continuous automatic scale flight. It follows actual star IDs, queries the canonical terrain tile under the camera, prefetches nearby refinements, and preserves the physical camera through every unit/frame change. Approach locator lights to find smaller structures; flying away returns to larger scales. F and Backspace remain available as manual focus shortcuts. See [SEAMLESS_FLIGHT.md](docs/SEAMLESS_FLIGHT.md) for implementation and validation.

Version 0.6 adds stationary free look, a **C** mouse-capture toggle, adjustable flight speed, boost and precision movement. Manual navigation takes over immediately from guided focus or the tour. The camera, scale and breadcrumbs still save between sessions; procedural recipe 3 is unchanged.

Version 0.5 introduced source-traced rendering from the user's Hyperion game. The current game retains its full-strength Hyperion presentation; generators, seeds and spatial hierarchy remain intact. [HYPERION_RENDERING.md](docs/HYPERION_RENDERING.md) retains the original integration history and comparisons. Procedural recipe 3 remains unchanged.

The adaptive LOD, asynchronous generation, cancellation, compact indexed meshes and instancing introduced in 0.4 remain available in every mode. Its performance results are in [PERFORMANCE.md](docs/PERFORMANCE.md); [PROCESS_DIVERSITY.md](docs/PROCESS_DIVERSITY.md) documents the underlying generating processes.

## Build and run

Requires CMake 3.24+, a C++20 compiler, Python 3.9+, and SQLite development headers/library (3.35+). Dependencies are local source builds; first-time bootstrap needs internet access. Reserve approximately 600 MB for sources and a Release build. Precompiled Metal/SPIR-V/GLSL presentation shaders are included; Windows additionally needs the pinned bgfx `shaderc` on PATH to generate DXBC at configure time (or set `INFINITE_SHADERC`). No new runtime library is required.

```sh
python3 tools/bootstrap.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/infinite --fresh
```

On this Mac, open `Infinite.app` or `Launch Infinite.command`. CMake is installed at `/Users/user/.local/opt/cmake/CMake.app/Contents/bin`; `ctest` is in the same directory. No heavyweight engine, browser or external runtime is used by the executable. Pinned bgfx shaders are embedded into the executable at build time. No shader compiler, source checkout or full Xcode installation is needed to run the Metal build. The included arm64 app was built for macOS 26.0 or newer; rebuild from source for other deployment targets.

To refresh the macOS app after rebuilding: `python3 tools/package_macos.py build/infinite Infinite.app`.

Platform setup:

- **macOS:** Apple command-line tools (`xcode-select --install`), CMake/Ninja. Uses Metal and the SDK SQLite library.
- **Linux:** C++20 compiler, CMake/Ninja, `libsqlite3-dev`, X11 development libraries and a Vulkan driver. With a Wayland desktop, launch with `SDL_VIDEODRIVER=x11`. SDL/bgfx use the X11 window handle in this version.
- **Windows:** Visual Studio 2022 C++ tools or clang-cl, CMake and SQLite (for example vcpkg's `sqlite3` with its CMake toolchain). Uses Direct3D 11. For multi-configuration generators, add `--config Release` and run `build/Release/infinite.exe`.

Only the macOS arm64 build and Metal runtime have been exercised here. Windows/Linux paths are implemented but require platform CI before claiming support.

Headless core build:

```sh
cmake -S . -B build-core -DINFINITE_VIEWER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core -j 4
ctest --test-dir build-core --output-on-failure
```

## Explore

New games begin in planetary orbit with a **12 m ship** and cruise disengaged. Existing version-3 saves resume their exact view, motion, size and drive settings. **C** captures the mouse helm; **W/S** changes persistent throttle and **B** brakes.

Use **Y** to cycle the current system's primary planets, or **Shift-Y** for the selected/current planet's moons. **Ctrl-Y** or the **LIFE DETECTED** panel cycles confirmed inhabited worlds; **J** navigates to the selection. **L** chooses the real terrain underfoot for an assisted landing; **Enter** lifts off. Normal handling has a nominal 85 km/s open-space envelope that decreases smoothly toward 350 m/s near terrain, with boost and speed adjustment available.

The cockpit shows planet/moon totals and the nearest eight celestial contacts with range and ahead/aft/occulted status; a selected destination stays in the list. Its scope retains every body's actual bearing, and unresolved planets and moons have lights in the forward view. Click a contact row, celestial light, star or distant galaxy to select its actual address, then press **J** to travel; **N** supplies the optional long-range drive for astronomical distances.

**N** explicitly enables long-range cruise. Its initial ceiling is **100 light years per second**, with proximity braking near structures. **-/+** changes the cruise ceiling while it is engaged. Voyages remain continuous and can last as long as desired; elapsed flight time, travelled distance and ETA are displayed and saved. Cruise continues when the open app loses focus.

**Z** activates the ship's shrinking field. It starts at millimetre size and automatically fits progressively smaller surroundings, down to a **12 pm hull**. **Comma / period** manually request smaller/larger sizes by decades; **Shift-Z** or Z again requests full size. The real contact radius and handling change with the hull. Regrowth is blocked where the ship would overlap material. A size-dependent transit field keeps movement between nearby micro/nano structures practical; contacts resolve against existing SDFs and generated atoms. Camera magnification is displayed separately from ship size.

| Control | Action |
|---|---|
| W / S | Increase / decrease persistent throttle |
| A / D; Q / E | Strafe; descend / ascend |
| Mouse / arrow keys | Pitch / yaw |
| U / O or Alt+A / D | Roll left / right (Option on Mac) |
| C / right drag | Capture / temporarily use mouse helm |
| B / X | Brake / toggle flight assistance |
| Y / J | Select next planet / navigate to selected destination |
| Shift-Y | Cycle moons of the selected/current planet |
| Ctrl-Y / click LIFE DETECTED | Cycle confirmed inhabited planets and moons |
| K / I | Select next port/traffic contact / hail its actual status |
| Shift-K | Cycle nearby surface settlements; J approaches, L lands |
| F6 | Hide/show orbital and surface activity; its clock continues |
| L / G / Enter | Assisted landing / gear / lift off when landed |
| N | Toggle long-range cruise |
| Shift+N | Cycle real stellar destinations; J navigates |
| - / + or [ / ] | Normal speed multiplier, or cruise speed ceiling |
| Z | Activate automatic shrinking / request full-size return |
| Comma / period | Manually request smaller / larger hull |
| Shift-Z | Request full size, subject to available space |
| Shift / Ctrl | Boost / precision handling |
| V | Original explorer / deploy ship at the inspected scale |
| Tab | Hide cockpit and HUD |
| F1 | Show/hide the flight control guide |
| F / Backspace / wheel | Original inspector focus / parent / scale navigation |
| R | Return to planetary orbit in pilot mode; galaxy overview in explorer mode |
| M / P / T | Discovery / screenshot / original guided tour |
| Escape | Release mouse; press again to save and quit |

Camera state and compressed discovery notes are stored in `build/user-data/universe.sqlite` for the command-line executable, or `user-data/universe.sqlite` beside `Infinite.app`. `--data DIR` changes the location. `--fresh` ignores the saved camera and ship state. Ship motion is stored beside it in a versioned `ship-<seed>.txt`; historical `display.txt` mode preferences are ignored. `--seed N` selects a universe (persistence is namespaced by seed). Generated geometry is never saved as authoritative world data.

Backspace restores the previous local camera view and selected branch. New saves retain those breadcrumbs across restarts. Outward wheel navigation promotes the reference frame while preserving the physical zoom pivot.

```sh
./build/infinite --smoke-test --width 1920 --height 1080 \
  --capture-dir build/captures --profile build/frame-profile.csv
./build/infinite --frames 300 --fresh
./build/infinite --flight-test --render-mode hyperion --capture-dir build/flight-captures
./build/infinite --streaming-test --render-mode hyperion --capture-dir build/streaming-captures
./build/infinite --pilot-test --capture-dir build/pilot-captures
./build/infinite_benchmark > build/generation.csv
```

The smoke test uses the real native renderer, visits all eight scales, returns through the same addresses, and compares submitted geometry with independently regenerated complete descriptors and every LOD. `--tour` performs the journey without the extra proof workload and leaves the window open. `--presentation-test` adds the cosmic web and captures Hyperion at nine scales, asserting unchanged geometry and generation counts. `--flight-test` steers through all nine scales and back using only the automatic flight controller, without focus or wheel commands. Screenshots are PPM for a dependency-free lossless capture path. Enable `-DINFINITE_GPU_TESTS=ON` for native flight/tours, atomic visibility and Hyperion image checks in CTest; these require an active desktop.

## Architecture and identity

- **Core:** signed 64-bit root-cell coordinates, a vector of typed integer child IDs, explicit parent frames, counter-indexed SplitMix64 hashing, cube-face/tile addressing, screen-space LOD and relative-frame math. No mutable global random stream.
- **Universe/processes:** lazy immutable descriptors with a bounded, thread-safe 1,024-object memo, inherited chemistry and formation parameters, separate feature seed domains, galaxy populations, stellar relations, Kepler orbits and shared spherical geological fields.
- **Matter/geometry:** lithology-specific density fields, crystallographic grain facets clipped to host matter, growth/fabric tint, crystallographic unit cells, site-keyed defects, spatially binned bonds and shared atom meshes.
- **World:** prioritized oneTBB geometry/catalog/tile jobs, complete coarse terrain coverage, cooperative cancellation, capacity-accounted CPU cache eviction, SQLite discoveries and explicit navigation state. Zstandard compresses modification payloads.
- **Rendering:** shared billboard instances, cached atomic instance buffers, three sphere resolutions, compact mesh indices, projected-sphere culling, a balanced stitched terrain frontier, camera-relative double-to-float tile extraction, bounded uploads/GPU cache and a minimal overlay. A read-only presentation adapter supplies existing scene descriptors/geometry to optional Hyperion material, emission, bloom and display passes.
- **Application:** SDL3 input, proximity-controlled free flight, physically continuous scale/frame changes, logarithmic focusing/rebasing and instrumentation. Flight shares asynchronous catalogs and prefetches with the renderer.

An address looks like `0,0,0/1:0/2:0/3:0/...`: the root cell followed by canonical `(kind, child ID)` segments. Child seeds hash the parent seed, kind and ID. A node stores its origin in parent-local units, a quaternion and log10 metres per unit. Relative transforms start **after the common ancestor** and subtract first divergent terms **before scaling**. Addressed double-precision points remain precise through CPU reference-frame conversion; GPU floats are camera-relative only. The frame interfaces accept roughly `10^26` to `10^-10` metres, with finite integer ranges and finite local precision; this is not arbitrary-precision arithmetic.

## Generation actually implemented

- Cosmic web: a two-scale cellular collapse skeleton. Equal-distance sheets, triple-junction filaments and four-way cluster knots surround underdense void interiors; fine structure is conditioned on coarse collapse. Galaxy placement samples the same field. This is an adhesion-inspired analytic approximation, not an N-body simulation.
- Galaxies: exponential surface-density discs, logarithmic density-wave arms, central bars, truncated Hernquist bulges/haloes, quenched elliptical spheroids and shared irregular associations. Population age, gas, environment and merger proxies affect structure. A broken power-law stellar mass function is conditioned on survival age. Detailed addressed systems reuse exactly the sampled star's position, mass, age and colour.
- Systems: approximate stellar mass/radius/temperature/luminosity relations, optional companion, 13–21 primary worlds, Hill/Roche-limited moon families, analytical eccentric Kepler orbits and a sampled asteroid belt. The original 5–9 planets remain exact; additional inner/gap/outer worlds append stable IDs. The viewer uses a fixed orbital epoch. An irradiation-driven condensation limit bounds inner orbits; irradiation controls planet temperature and volatile retention; chemical differentiation reweights inherited reservoirs.
- Planets: mobile-lid, stagnant-lid or icy-shell fields selected from heat retention, size, age, water and composition. Spherical plates carry Euler-pole velocities; relative motion supplies convergence, extension and shear. Uplift, subduction trenches, hotspot shields, impact bowls/rims/ejecta and resurfacing-dependent crater survival share a continuous unit-direction field. Water enables an explicit analytic incision/deposition approximation.
- Terrain: adaptive cube-sphere samples, sparse survey sites and mountain-scale patches refine the same planet field in transported tangent frames. Rocks are placed on that field. A mountain address denotes a terrain neighbourhood; its centre need not be a peak.
- Rocks: jointed plutonic blocks, vesicular volcanic material, bedded sediments, folded/foliated metamorphic material, impact breccia aggregates, pitted metallic fragments or glacial ice. Formation pressure, cooling, water, strain, chemistry and impacts select the process. Density, material bands and grain placement use the same descriptor.
- Minerals: quartz, iron, olivine, ice or graphite are selected only from available chemical reservoirs, with temperature/pressure eligibility. Slow cooling produces larger grains; strain shapes growth and retained defects. Hexagonal prisms/terminations, cubic facets, orthorhombic habits and basal plates replace the former generic polyhedra. Grains intersect the actual host density, and atomic specimens remain inside them.
- Atoms: trigonal alpha quartz, BCC/FCC iron selected by temperature, 28-site orthorhombic forsterite with chemical Fe/Mg substitution, the ice-Ih oxygen network and AB-stacked graphite. Thermal expansion, bounded elastic compression, static thermal displacement, quenched vacancies, annealed impact damage, strain-dependent boundaries and dislocations are site-addressed. Element/distance rules determine bonds through spatial bins. These are static educational representations; hydrogen positions, full phase equilibria and molecular dynamics remain outside the model.
- History: accretion/collision/heating/cooling/deposition/crystallisation summaries derive their intensities from inherited process strengths. A lattice retains its grain's formation history. There is no independent random history substituted at atomic scale.

## Resource limits and verification

Up to three concurrent cache tasks, four oneTBB workers, a 32 MiB CPU payload budget, at most 512 geometry entries, a 32 MiB GPU cache budget and bounded transient buffers. The terrain frontier uses at most 192 leaves; complete parent coverage remains while worker-generated children/stitches load. Requests are prioritized and child catalogs are asynchronous. CPU accounting includes allocated vector capacities; static GPU uploads have a per-frame allowance and terrain uses the existing bounded transient buffers. Obsolete jobs receive cancellation flags. Existing object LODs crossfade over 0.3 seconds; terrain cuts share a 0.2-second position morph. Briefly retained blend layers, tile fronts/morph sources and proof workers add to the cache budget; process memory is measured separately. Whole objects outside the projected frustum are culled. Both generation and frame metrics are measured rather than assuming 60 FPS.

`infinite_tests` currently exercises 251 checks covering hash vectors, address parsing, signed-coordinate extremes, generation order, independent threaded generation, cube/tile boundaries, LOD field consistency, finite transforms spanning 36 decades, material inheritance, Kepler periodicity, all five mineral lattices, valid finite mesh buffers at every scale, cancellation, cache eviction and camera serialization. CTest also launches separate writer/reader processes to check SQLite persistence across application restarts. See [generation benchmarks](docs/benchmark-v0.3.csv) and [benchmark memory](docs/benchmark-memory-v0.3.txt) for current measured generation costs on this machine. The native tour checks and records the actual backbuffer resolution; on Retina screens it uses a smaller logical window to render the requested pixel count.

`infinite_continuity_tests` verifies full non-default round trips, every LOD, shared terrain samples/materials, SDF convergence, query-stable sites/vacancies/bonds for all five minerals, saved ancestry, exact camera restoration, local sections and parallel regeneration. Restart processes repeat the ascent after SQLite reopen. `infinite_process_tests` checks structural diversity, environmental causality, population statistics, phase-specific stoichiometry and coordination, and inherited constraints. `infinite_navigation_tests` checks stationary free look, frame rates, speed controls, interrupted transitions and saved flying views at all nine scales and three seeds. `infinite_flight_tests` adds complete automatic inward/outward flights, canonical terrain lookup, lateral tile handoffs, camera continuity and manual-view compatibility. The desktop suite includes streaming topology/morph proofs, native boundary pixels, automatic flight, presentation preservation and a Hyperion round trip with seventeen captures. Current operating-scale validation is in [OPERATING_SCALES.md](docs/OPERATING_SCALES.md); the earlier piloting integration and measurements are in [PILOTING.md](docs/PILOTING.md); the earlier shader integration is in [HYPERION_RENDERING.md](docs/HYPERION_RENDERING.md); generation/performance invariants remain documented in [PERFORMANCE.md](docs/PERFORMANCE.md). Recipe-3 model evidence is in [PROCESS_DIVERSITY.md](docs/PROCESS_DIVERSITY.md); [CONTINUITY_AUDIT.md](docs/CONTINUITY_AUDIT.md) and [VALIDATION.md](docs/VALIDATION.md) retain historical evidence for versions 0.2 and 0.1.

## Current limits and next concrete work

This delivers the foundation and an initial end-to-end slice; the advanced six-phase specification is not complete.

1. Root cells now stream around the physical camera, with automatic cross-cell reference changes. Active data remains bounded and is regenerated deterministically when revisited. The model still uses sparse samples within each region; there is no global star catalogue or simulation of infinitely many objects. Coordinates use signed 64-bit cells and finite local floating-point precision.
2. Focus transitions preserve spatial identity, camera views and selected branches. Oversized terrain is resampled from the same field; microscopic enclosing solids use labelled, spatially anchored material sections. The cutaway is an inspection model. No optical microscope or quantum scale imaging is simulated.
3. Terrain now uses a full-sphere, neighbour-balanced streaming quadtree, stitched mixed-LOD edges and shared mesh morphing. Sampling tiles have no visible outer patch boundary. Resolution remains subject to the address format's 24 tile levels, screen-space error target and work budget; this limits detail rather than travel extent. Microscopic solid inspection cavities remain part of the existing visibility model.
4. No full erosion solver, time-integrated tectonic dynamics, reaction–diffusion, cellular automata, diffusion-limited aggregation, volumetric octree, fluid atmosphere, molecular dynamics or ongoing planet simulation is implemented. The algorithms listed above are the implemented ones.
5. Discovery markers and saved camera state are the sparse modification layer. Editable terrain/chemistry and persistent simulation changes need a versioned modification schema and replay before they can be exposed.
6. Deterministic addresses, seeds and generation order are tested within a build. Recipe 3 preserves the hierarchical seed hash and canonical addresses while deliberately replacing physical layouts, environments, materials and atomic recipes. Exact floating-point mesh bits across different compilers, libm implementations and CPU architectures are not certified. FastNoise2 strict-FP is enabled; it affects inclusion colour only. Cross-platform golden fixtures remain future work.
7. Lattice bond discovery uses local spatial bins. Specimens remain bounded to at most 17 cells per axis; larger interactive specimens need view-dependent lattice chunks.
8. GPU setup, bounded buffer uploads, navigation descriptor cache misses, local transforms and UI run on the render thread. Meshes, local sections and child catalogs are generated in workers. Ready coarse representations remain visible during refinement. Profiling and platform CI should guide the next performance work.

Dependencies, pins, shader provenance and licence locations are documented in `THIRD_PARTY.md`.
