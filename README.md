# INFINITE

A standalone C++20 procedural universe rendered with SDL3 and bgfx. **Infinite 0.23 starts with a full-size ship near an existing planet.** Normal play uses Hyperion-style planetary flight, targeting, approach, landing and launch. Long-range travel is an explicit drive choice; the shrinking field is a separate ship ability.

Version 0.23 improves **surface lighting, material detail, terrain shadows and atmospheric depth**. Solid-world tiles provide separate unlit albedo and field normals, smooth strata bridge orbital colour to close mineral grain, and the existing sun map includes terrain. Atmospheric scattering follows each world’s retained climate and daylight suppresses the optical sky. All generators, original mesh data, identities and flight controls remain intact. [Surface graphics and validation](docs/SURFACE_QUALITY.md).

Version 0.22 makes inhabited worlds easier to find: **green LIFE badges and population counts**, priority system-list rows, a clickable **LIFE DETECTED** panel, green scope/world markers, and **Ctrl-Y** to cycle confirmed inhabited planets and moons. **J** approaches the selected world. Background surveys use the existing surface generator; pending worlds show SCANNING, and settlements in sealed colonies count as life. [Life-detection HUD](docs/LIFE_HUD.md).

Version 0.21 adds **cached sunlight shadows, reflective architectural glass, prismatic ceramic materials and clearer atmospheric lighting**. Existing buildings and street furniture cast shadows onto the actual terrain; metre-based material detail stays attached through flight, zoom and precision-frame changes. Original mode and all procedural structures remain intact. [Graphics notes and comparisons](docs/GRAPHICS_0_21.md).

Version 0.20 develops surface settlements into **connected, mixed-use cities**. Planned, radial, organic and contour-led street networks determine parcels and building frontage. Housing courts, stepped towers, civic halls, row houses, industrial sheds and protected habitat clusters follow district function and access. Organic towns grow irregular blocks and T-junctions; pavements fit the actual terrain, and residents follow home-associated street routes. Existing settlement names, seeds, landing locations and the underlying universe remain intact. [CITY_GENERATION.md](docs/CITY_GENERATION.md) records the research, algorithms and validation.

Version 0.19 adds deterministic **planetary weather and inhabited surfaces**. Atmospheric retention, temperature and inherited water availability control cloud fronts, wind, rain, snow and dust. Suitable solid worlds can contain cities, sealed colonies, mining or research outposts, buildings and moving residents with persistent home identities. Sites use the existing landing height field and leave terrain untouched. **Shift-K** cycles nearby surface ports, **J** approaches their clear landing area, **L** lands and **Enter** launches. Many worlds remain uninhabited. [WEATHER_AND_SURFACE_LIFE.md](docs/WEATHER_AND_SURFACE_LIFE.md) documents the generation, safeguards, controls and validation.

Version 0.18 adds real local terrain to selected expanded solid planets: **Highlands**, **Volcanic**, **Canyons** and **Ice ridges**. Their deterministic spherical height fields drive visible geometry, streaming, collision and landing together; gas envelopes have no terrain or landing surface. The original planet set and moons retain their existing geological fields, and all seeds/orbits stay intact. Terrain LOD now refines near the ship more accurately. Use **Y** to select a world with one of these terrain names, **J** to approach and **L** to land at a gentle site. [SOLID_TERRAIN.md](docs/SOLID_TERRAIN.md) records the algorithms, compatibility and native captures.

Version 0.17 ports Hyperion's baked close-material texture: filtered mineral relief, fracture seams, frost and granular shading, with complete mipmaps and anisotropic filtering. The shared texture bakes asynchronously and uses 1.33 MiB of GPU memory; the original geometry and all universe seeds remain unchanged. Materials retain their phase through scale and floating-origin changes. [MATERIAL_DETAIL.md](docs/MATERIAL_DETAIL.md) records the source algorithm, comparisons and measured cost.

Version 0.16 improves depth and clarity while retaining the psychedelic Hyperion palette: directional day/night lighting, smooth geological shading that retains sharp creases, filtered close-range grain, clearer spectral reflections, quieter sky glow and improved edge filtering. Surface shading runs asynchronously with compact GPU normals; atoms gain an extra close-range display LOD. Generated geometry, positions, identities and flight behaviour stay unchanged. [GRAPHICS_QUALITY.md](docs/GRAPHICS_QUALITY.md) records image comparisons, preservation checks and measured performance.

Version 0.15 applies Hyperion's **psychedelic** rendering to the existing universe: direction-anchored folded spectral light, mineral pigments, filtered luminous seams, iridescent grazing light, coloured atmospheric scattering and a hue-preserving HDR display curve. The same geometry, objects, seeds, worlds and ship state remain authoritative. **HYPERION** uses the new aesthetic, **HYBRID** reduces its strength, and **ORIGINAL** retains the previous rendering. **H** cycles them. [PSYCHEDELIC_PRESENTATION.md](docs/PSYCHEDELIC_PRESENTATION.md) records the source reference, preservation evidence, comparisons and measured cost.

Version 0.14 improves the cockpit with recessed, high-contrast instruments, larger navigation and velocity readings, a larger contact scope, a terrain-relative landing director, and uncluttered target cues. Labels avoid each other and the reticle; off-screen targets retain their actual bearing. **F1** opens the control guide. All universe generators and ship dynamics remain unchanged. [COCKPIT.md](docs/COCKPIT.md) records the Hyperion source reference, interface changes and validation.

Version 0.13 expands each system to **13–21 primary worlds**, with real moons wherever the host's Hill sphere permits stable satellite orbits. Existing planets keep their exact identities, orbits and material structures. New rocky and dwarf worlds fill suitable inner or orbital gaps and extend the outer family. Moons have their own terrain, geology and atomic depth. **Y** cycles primary worlds; **Shift-Y** cycles the selected or current planet's moons; **J** approaches and **L** lands. The cockpit shows system totals and the nearest eight celestial contacts, including a selected destination. [PLANETARY_POPULATIONS.md](docs/PLANETARY_POPULATIONS.md) records generation, preservation and validation.

Version 0.12 adds persistent orbital activity above the existing world: harbours and service yards, scheduled freighters and ferries, patrol circuits, survey craft and mineral ejecta. Click a traffic contact or press **K**, **I** to hail, and **J** to approach a harbour or match a moving ship's course. **F6** hides/shows activity. The denser stellar sky refines the same addressed population. [ORBITAL_ACTIVITY.md](docs/ORBITAL_ACTIVITY.md) documents the Hyperion source, deterministic schedules, controls and validation.

Version 0.11 makes the surrounding universe visible from the cockpit: all existing system planets and the sun appear on the contact scope and list, distant unresolved planets retain selectable light proxies, and addressed stars, gas and neighbouring galaxies supply the sky. The lights retain their true bearings, distances and destination addresses. [CELESTIAL_CONTEXT.md](docs/CELESTIAL_CONTEXT.md) explains the visibility fixes and comparisons.

The universe remains at its original physical scale. Existing hierarchical addresses, seeds, geology, rocks, minerals and atomic structures are preserved when populations expand. Camera precision changes never resize the normal ship. See [OPERATING_SCALES.md](docs/OPERATING_SCALES.md) for the three operating regimes, controls and validation.

Version 0.8 removes the isolated terrain windows and single-root-cell flight restriction. A complete, stitched planetary surface streams beneath the camera, with continuous mesh morphing as detail changes. Neighbouring cosmic cells preload and hand off without changing the physical camera; blended collars fill their former sampling gaps. The same world fields, seeds and objects remain authoritative. See [STREAMING.md](docs/STREAMING.md) for implementation, image checks and resource bounds.

Version 0.7 adds continuous automatic scale flight. It follows actual star IDs, queries the canonical terrain tile under the camera, prefetches nearby refinements, and preserves the physical camera through every unit/frame change. Approach locator lights to find smaller structures; flying away returns to larger scales. F and Backspace remain available as manual focus shortcuts. See [SEAMLESS_FLIGHT.md](docs/SEAMLESS_FLIGHT.md) for implementation and validation.

Version 0.6 adds stationary free look, a **C** mouse-capture toggle, adjustable flight speed, boost and precision movement. Manual navigation takes over immediately from guided focus or the tour. The camera, scale and breadcrumbs still save between sessions; procedural recipe 3 is unchanged.

Version 0.5 adds source-traced rendering from the user's Hyperion game: press **H** for **ORIGINAL / HYPERION / HYBRID**. The same immutable geometry and camera feed all modes; generators, seeds, spatial hierarchy and controls remain intact. [HYPERION_RENDERING.md](docs/HYPERION_RENDERING.md) documents the actual source techniques, integration boundary, screenshots and validation. Original rendering is the default. Procedural recipe 3 remains unchanged.

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
| Mouse / arrow keys; U / O | Pitch/yaw; roll |
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
| - / + or [ / ] | Normal speed multiplier, or cruise speed ceiling |
| Z | Activate automatic shrinking / request full-size return |
| Comma / period | Manually request smaller / larger hull |
| Shift-Z | Request full size, subject to available space |
| Shift / Ctrl | Boost / precision handling |
| V | Original explorer / deploy ship at the inspected scale |
| H / Tab | Cycle rendering modes / hide cockpit and HUD |
| F1 | Show/hide the flight control guide |
| F / Backspace / wheel | Original inspector focus / parent / scale navigation |
| R | Return to planetary orbit in pilot mode; galaxy overview in explorer mode |
| M / P / T | Discovery / screenshot / original guided tour |
| Escape | Release mouse; press again to save and quit |

Camera state and compressed discovery notes are stored in `build/user-data/universe.sqlite` for the command-line executable, or `user-data/universe.sqlite` beside `Infinite.app`. `--data DIR` changes the location. `--fresh` ignores the saved camera and ship state. Ship motion is stored beside it in a versioned `ship-<seed>.txt`; `display.txt` remembers the render mode. `--seed N` selects a universe (persistence is namespaced by seed). Generated geometry is never saved as authoritative world data.

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

The smoke test uses the real native renderer, visits all eight scales, returns through the same addresses, and compares submitted geometry with independently regenerated complete descriptors and every LOD. `--tour` performs the journey without the extra proof workload and leaves the window open. `--compare-modes` adds the cosmic web and captures all three rendering modes at each identical camera, asserting unchanged geometry and generation counts. `--flight-test` steers through all nine scales and back using only the automatic flight controller, without focus or wheel commands. Screenshots are PPM for a dependency-free lossless capture path. Enable `-DINFINITE_GPU_TESTS=ON` for native flight/tours, atomic visibility and mode image checks in CTest; these require an active desktop.

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

`infinite_continuity_tests` verifies full non-default round trips, every LOD, shared terrain samples/materials, SDF convergence, query-stable sites/vacancies/bonds for all five minerals, saved ancestry, exact camera restoration, local sections and parallel regeneration. Restart processes repeat the ascent after SQLite reopen. `infinite_process_tests` checks structural diversity, environmental causality, population statistics, phase-specific stoichiometry and coordination, and inherited constraints. `infinite_navigation_tests` checks stationary free look, frame rates, speed controls, interrupted transitions and saved flying views at all nine scales and three seeds. `infinite_flight_tests` adds complete automatic inward/outward flights, canonical terrain lookup, lateral tile handoffs, camera continuity and manual-view compatibility. The 32-case desktop suite includes streaming topology/morph proofs, native boundary pixels, automatic flight, presentation preservation, a three-mode round trip and 51 mode-image captures. Current operating-scale validation is in [OPERATING_SCALES.md](docs/OPERATING_SCALES.md); the earlier piloting integration and measurements are in [PILOTING.md](docs/PILOTING.md); the earlier shader integration is in [HYPERION_RENDERING.md](docs/HYPERION_RENDERING.md); generation/performance invariants remain documented in [PERFORMANCE.md](docs/PERFORMANCE.md). Recipe-3 model evidence is in [PROCESS_DIVERSITY.md](docs/PROCESS_DIVERSITY.md); [CONTINUITY_AUDIT.md](docs/CONTINUITY_AUDIT.md) and [VALIDATION.md](docs/VALIDATION.md) retain historical evidence for versions 0.2 and 0.1.

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
