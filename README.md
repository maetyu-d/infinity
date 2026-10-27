# INFINITE

A standalone C++20 procedural-universe vertical slice, rendered with SDL3 and bgfx. The camera follows stable addresses through a galaxy, star system, planet, terrain region, mountain-scale patch, SDF rock, mineral grain and material-specific atomic specimen. Backspace reverses the journey.

Version 0.4 adds adaptive screen-space sampling, prioritized asynchronous geometry/catalog jobs, cooperative cancellation, compact indexed meshes and instanced particle/atomic rendering. The measured before-and-after results are in [PERFORMANCE.md](docs/PERFORMANCE.md). Procedural recipe 3 and its physical descriptors remain unchanged; [PROCESS_DIVERSITY.md](docs/PROCESS_DIVERSITY.md) documents the underlying generating processes.

## Build and run

Requires CMake 3.24+, a C++20 compiler, Python 3.9+, and SQLite development headers/library (3.35+). Dependencies are local source builds; first-time bootstrap needs internet access. Reserve approximately 600 MB for sources and a Release build; shader compiler tools and large example assets are deliberately excluded.

```sh
python3 tools/bootstrap.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target infinite infinite_tests infinite_continuity_tests infinite_process_tests infinite_performance_tests infinite_benchmark -j 4
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
cmake --build build-core --target infinite_tests infinite_continuity_tests infinite_process_tests infinite_performance_tests infinite_benchmark -j 4
ctest --test-dir build-core --output-on-failure
```

## Explore

Press **F** repeatedly to follow the selected object down the hierarchy. Click visible objects/locators to choose a different child. Logarithmic focusing crosses the actual physical scale gap; ancestors and children use the same camera-relative transforms throughout. Tiny distant objects have screen-size locator lights so that planets and rocks remain discoverable.

| Control | Action |
|---|---|
| WASD | Fly relative to camera orientation |
| Q / E | Descend / ascend |
| Right mouse drag | Look in 3D |
| Mouse wheel | Logarithmic scale navigation (0.23 decades per wheel unit) |
| Shift | Faster movement at the current physical scale |
| Click | Select a nearby projected object |
| F | Smoothly focus the selected child |
| Backspace | Focus the parent |
| Tab | Toggle the minimal overlay |
| R | Reset to the galaxy |
| M | Persist a discovery for the current address |
| P | Save a lossless PPM screenshot |
| T | Guided descent to the lattice and return |
| Escape | Save camera state and quit |

Camera state and compressed discovery notes are stored in `build/user-data/universe.sqlite` for the command-line executable, or `user-data/universe.sqlite` beside `Infinite.app`. `--data DIR` changes the location. `--fresh` ignores the saved camera. `--seed N` selects a universe (persistence is namespaced by seed). Generated geometry is never saved as authoritative world data.

Backspace restores the previous local camera view and selected branch. New saves retain those breadcrumbs across restarts. Outward wheel navigation promotes the reference frame while preserving the physical zoom pivot.

```sh
./build/infinite --smoke-test --width 1920 --height 1080 \
  --capture-dir build/captures --profile build/frame-profile.csv
./build/infinite --frames 300 --fresh
./build/infinite_benchmark > build/generation.csv
```

The smoke test uses the real native renderer, visits all eight scales, returns through the same addresses, and compares submitted geometry with independently regenerated complete descriptors and every LOD. `--tour` performs the journey without the extra proof workload and leaves the window open. Screenshots are PPM for a dependency-free lossless capture path. Enable `-DINFINITE_GPU_TESTS=ON` to include the native tour and final-pixel atomic visibility checks in CTest; these require an active desktop.

## Architecture and identity

- **Core:** signed 64-bit root-cell coordinates, a vector of typed integer child IDs, explicit parent frames, counter-indexed SplitMix64 hashing, cube-face/tile addressing, screen-space LOD and relative-frame math. No mutable global random stream.
- **Universe/processes:** lazy immutable descriptors with a bounded, thread-safe 1,024-object memo, inherited chemistry and formation parameters, separate feature seed domains, galaxy populations, stellar relations, Kepler orbits and shared spherical geological fields.
- **Matter/geometry:** lithology-specific density fields, crystallographic grain facets clipped to host matter, growth/fabric tint, crystallographic unit cells, site-keyed defects, spatially binned bonds and shared atom meshes.
- **World:** prioritized oneTBB geometry/catalog jobs, progressive coarse fallback, cooperative cancellation, capacity-accounted CPU cache eviction, SQLite discoveries and explicit navigation state. Zstandard compresses modification payloads.
- **Rendering:** shared billboard instances, cached atomic instance buffers, three sphere resolutions, compact mesh indices, projected-sphere culling, camera-facing terrain, bounded uploads/GPU cache and a minimal overlay.
- **Application:** SDL3 input, free flight, logarithmic focusing/rebasing and instrumentation.

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

Up to three concurrent cache tasks, four oneTBB workers, a 32 MiB CPU payload budget, at most 96 geometry entries, a 32 MiB GPU cache budget and bounded transient buffers. Requests are prioritized and child catalogs are asynchronous. CPU accounting includes allocated vector capacities; new GPU uploads have a per-frame allowance. Obsolete jobs receive cancellation flags; the current LOD remains usable while refinement is generated. Hysteresis avoids threshold chatter and a reversible 0.3-second crossfade handles completed meshes. Briefly retained blend layers and proof workers add to the cache budget; process memory is measured separately. Whole objects outside the projected frustum are culled. Both generation and frame metrics are measured rather than assuming 60 FPS.

`infinite_tests` currently exercises 251 checks covering hash vectors, address parsing, signed-coordinate extremes, generation order, independent threaded generation, cube/tile boundaries, LOD field consistency, finite transforms spanning 36 decades, material inheritance, Kepler periodicity, all five mineral lattices, valid finite mesh buffers at every scale, cancellation, cache eviction and camera serialization. CTest also launches separate writer/reader processes to check SQLite persistence across application restarts. See [generation benchmarks](docs/benchmark-v0.3.csv) and [benchmark memory](docs/benchmark-memory-v0.3.txt) for current measured generation costs on this machine. The native tour checks and records the actual backbuffer resolution; on Retina screens it uses a smaller logical window to render the requested pixel count.

`infinite_continuity_tests` verifies full non-default round trips, every LOD, shared terrain samples/materials, SDF convergence, query-stable sites/vacancies/bonds for all five minerals, saved ancestry, exact camera restoration, local sections and parallel regeneration. Restart processes repeat the ascent after SQLite reopen. `infinite_process_tests` checks structural diversity, environmental causality, population statistics, phase-specific stoichiometry and coordination, and inherited constraints. The ten-case desktop suite includes a real 1080p GPU tour and final-pixel atom visibility. Performance invariants and current measurements are in [PERFORMANCE.md](docs/PERFORMANCE.md); recipe-3 model evidence is in [PROCESS_DIVERSITY.md](docs/PROCESS_DIVERSITY.md); [CONTINUITY_AUDIT.md](docs/CONTINUITY_AUDIT.md) and [VALIDATION.md](docs/VALIDATION.md) retain historical evidence for versions 0.2 and 0.1.

## Current limits and next concrete work

This delivers the foundation and an initial end-to-end slice; the advanced six-phase specification is not complete.

1. The renderer exposes a finite sparse sample per addressed region. Root cell addresses can be generated through the API, but interactive cross-cell spatial streaming and recursive octree queries remain to be implemented. There is no global star catalogue or simulation of infinitely many objects.
2. Focus transitions preserve spatial identity, camera views and selected branches. Oversized terrain is resampled from the same field; microscopic enclosing solids use labelled, spatially anchored material sections. The cutaway is an inspection model. No optical microscope or quantum scale imaging is simulated.
3. Terrain uses adaptive cube-sphere sampling and screen-size LOD, but full camera-driven per-tile streaming, mesh geomorphing and robust crack stitching remain. Region/mountain patches share the scalar field. The next terrain task is a neighbour-balanced streaming quadtree with stitched boundaries and error bounds.
4. No full erosion solver, time-integrated tectonic dynamics, reaction–diffusion, cellular automata, diffusion-limited aggregation, volumetric octree, fluid atmosphere, molecular dynamics or ongoing planet simulation is implemented. The algorithms listed above are the implemented ones.
5. Discovery markers and saved camera state are the sparse modification layer. Editable terrain/chemistry and persistent simulation changes need a versioned modification schema and replay before they can be exposed.
6. Deterministic addresses, seeds and generation order are tested within a build. Recipe 3 preserves the hierarchical seed hash and canonical addresses while deliberately replacing physical layouts, environments, materials and atomic recipes. Exact floating-point mesh bits across different compilers, libm implementations and CPU architectures are not certified. FastNoise2 strict-FP is enabled; it affects inclusion colour only. Cross-platform golden fixtures remain future work.
7. Lattice bond discovery uses local spatial bins. Specimens remain bounded to at most 17 cells per axis; larger interactive specimens need view-dependent lattice chunks.
8. GPU setup, bounded buffer uploads, navigation descriptor cache misses, local transforms and UI run on the render thread. Meshes, local sections and child catalogs are generated in workers. Ready coarse representations remain visible during refinement. Profiling and platform CI should guide the next performance work.

Dependencies, pins, shader provenance and licence locations are documented in `THIRD_PARTY.md`.
