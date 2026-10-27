# INFINITE

A standalone C++20 procedural universe rendered with SDL3 and bgfx. Pilot a ship with throttle, inertia, assisted or inertial handling, a beveled Hyperion cockpit and a destination–approach–landing–launch loop. The same galaxy, star systems, planetary geology, rocks, mineral grains and atomic specimens remain explorable. Viewing units and local reference frames adapt automatically while physical velocity remains in metres per second.

Version **0.9** adds the piloting layer without changing the universe recipe, fields, seeds, identities or original geometry generators. See [PILOTING.md](docs/PILOTING.md) for controls, source references and validation.

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

The application starts at the saved view with the pilot cockpit and the last rendering choice (Hyperion on a first launch). Press **C** for the mouse helm. **W/S changes persistent throttle**; releasing W keeps the chosen throttle. **B** commands a controlled stop. **X** switches flight assistance; with assistance off the ship keeps its momentum and rotation.

Click a visible object and press **J** (or Enter) to navigate toward it. On planetary approach press **L** to choose the real terrain underfoot as a landing site, lower the gear and descend under thrust. **Enter** launches after touchdown. No pad or flattened terrain is generated; steep terrain is rejected. The HUD shows bearing/range, prograde drift, speed, stopping distance, altitude, descent, throttle, hull and gear state.

The cruise drive automatically changes its speed envelope as you approach smaller structures. The HUD distinguishes **CRUISE / SCALE DRIVE**, **LOCAL / SHIP** and **SURVEY / VIRTUAL PROBE**, and indicates refining/widening view scale. **Z** engages the survey probe to inspect rocks, minerals and atoms without ship collisions; approaching and retreating still changes reference frames automatically. Render-unit changes never rescale physical momentum. Ship mode cannot materialize inside solid terrain.

**V** parks the helm and restores the original explorer camera. The original focus, parent, wheel, discovery and guided-tour controls remain available. Focus/parent/wheel commands explicitly enter explorer mode; V resumes piloting at that view.

| Control | Pilot action |
|---|---|
| W / S | Increase / decrease persistent throttle |
| A / D; Q / E | Strafe; descend / ascend |
| Mouse / arrow keys | Pitch and yaw rate commands |
| U / O | Roll |
| C / right drag | Capture mouse helm / temporary helm |
| B / X | Brake / toggle flight assistance |
| Shift / Ctrl | Boost / precision handling |
| J or Enter | Navigate to the selected destination |
| L / G | Assisted landing underfoot / toggle gear |
| Enter when landed | Lift off under thrust |
| N / Z | Cruise envelope / virtual survey probe |
| V | Switch pilot / original explorer controls |
| - / + or [ / ] | Halve / double drive speed envelope |
| Click | Select an object at pointer or central reticle |
| H | Original → Hyperion → hybrid; choice is saved |
| F / Backspace / wheel | Explorer focus / parent / logarithmic navigation |
| Tab | Hide cockpit and HUD |
| R / M / P / T | Reset / save discovery / screenshot / original guided tour |
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
- Systems: approximate stellar mass/radius/temperature/luminosity relations, optional companion, 5–9 planets, analytical eccentric Kepler orbits and a sampled asteroid belt. The viewer uses a fixed orbital epoch. An irradiation-driven condensation limit bounds inner orbits; irradiation controls planet temperature and volatile retention; chemical differentiation reweights inherited reservoirs.
- Planets: mobile-lid, stagnant-lid or icy-shell fields selected from heat retention, size, age, water and composition. Spherical plates carry Euler-pole velocities; relative motion supplies convergence, extension and shear. Uplift, subduction trenches, hotspot shields, impact bowls/rims/ejecta and resurfacing-dependent crater survival share a continuous unit-direction field. Water enables an explicit analytic incision/deposition approximation.
- Terrain: adaptive cube-sphere samples, sparse survey sites and mountain-scale patches refine the same planet field in transported tangent frames. Rocks are placed on that field. A mountain address denotes a terrain neighbourhood; its centre need not be a peak.
- Rocks: jointed plutonic blocks, vesicular volcanic material, bedded sediments, folded/foliated metamorphic material, impact breccia aggregates, pitted metallic fragments or glacial ice. Formation pressure, cooling, water, strain, chemistry and impacts select the process. Density, material bands and grain placement use the same descriptor.
- Minerals: quartz, iron, olivine, ice or graphite are selected only from available chemical reservoirs, with temperature/pressure eligibility. Slow cooling produces larger grains; strain shapes growth and retained defects. Hexagonal prisms/terminations, cubic facets, orthorhombic habits and basal plates replace the former generic polyhedra. Grains intersect the actual host density, and atomic specimens remain inside them.
- Atoms: trigonal alpha quartz, BCC/FCC iron selected by temperature, 28-site orthorhombic forsterite with chemical Fe/Mg substitution, the ice-Ih oxygen network and AB-stacked graphite. Thermal expansion, bounded elastic compression, static thermal displacement, quenched vacancies, annealed impact damage, strain-dependent boundaries and dislocations are site-addressed. Element/distance rules determine bonds through spatial bins. These are static educational representations; hydrogen positions, full phase equilibria and molecular dynamics remain outside the model.
- History: accretion/collision/heating/cooling/deposition/crystallisation summaries derive their intensities from inherited process strengths. A lattice retains its grain's formation history. There is no independent random history substituted at atomic scale.

## Resource limits and verification

Up to three concurrent cache tasks, four oneTBB workers, a 32 MiB CPU payload budget, at most 512 geometry entries, a 32 MiB GPU cache budget and bounded transient buffers. The terrain frontier uses at most 192 leaves; complete parent coverage remains while worker-generated children/stitches load. Requests are prioritized and child catalogs are asynchronous. CPU accounting includes allocated vector capacities; static GPU uploads have a per-frame allowance and terrain uses the existing bounded transient buffers. Obsolete jobs receive cancellation flags. Existing object LODs crossfade over 0.3 seconds; terrain cuts share a 0.2-second position morph. Briefly retained blend layers, tile fronts/morph sources and proof workers add to the cache budget; process memory is measured separately. Whole objects outside the projected frustum are culled. Both generation and frame metrics are measured rather than assuming 60 FPS.

`infinite_tests` currently exercises 251 checks covering hash vectors, address parsing, signed-coordinate extremes, generation order, independent threaded generation, cube/tile boundaries, LOD field consistency, finite transforms spanning 36 decades, material inheritance, Kepler periodicity, all five mineral lattices, valid finite mesh buffers at every scale, cancellation, cache eviction and camera serialization. CTest also launches separate writer/reader processes to check SQLite persistence across application restarts. See [generation benchmarks](docs/benchmark-v0.3.csv) and [benchmark memory](docs/benchmark-memory-v0.3.txt) for current measured generation costs on this machine. The native tour checks and records the actual backbuffer resolution; on Retina screens it uses a smaller logical window to render the requested pixel count.

`infinite_continuity_tests` verifies full non-default round trips, every LOD, shared terrain samples/materials, SDF convergence, query-stable sites/vacancies/bonds for all five minerals, saved ancestry, exact camera restoration, local sections and parallel regeneration. Restart processes repeat the ascent after SQLite reopen. `infinite_process_tests` checks structural diversity, environmental causality, population statistics, phase-specific stoichiometry and coordination, and inherited constraints. `infinite_navigation_tests` checks stationary free look, frame rates, speed controls, interrupted transitions and saved flying views at all nine scales and three seeds. `infinite_flight_tests` adds complete automatic inward/outward flights, canonical terrain lookup, lateral tile handoffs, camera continuity and manual-view compatibility. The 22-case desktop suite includes streaming topology/morph proofs, native boundary pixels, automatic flight, presentation preservation, a three-mode round trip and 51 mode-image captures. Piloting validation and current rendering measurements are in [PILOTING.md](docs/PILOTING.md); the earlier shader integration is in [HYPERION_RENDERING.md](docs/HYPERION_RENDERING.md); generation/performance invariants remain documented in [PERFORMANCE.md](docs/PERFORMANCE.md). Recipe-3 model evidence is in [PROCESS_DIVERSITY.md](docs/PROCESS_DIVERSITY.md); [CONTINUITY_AUDIT.md](docs/CONTINUITY_AUDIT.md) and [VALIDATION.md](docs/VALIDATION.md) retain historical evidence for versions 0.2 and 0.1.

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
