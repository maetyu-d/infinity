# Stellar light and glacial worlds — Infinite 0.32

This pass changes the presentation of existing objects. The 0.31 checkpoint is `7b50bb2`, retained as `checkpoint/infinite-0.31-before-stellar-upgrade`.

## What changed

- **Emissive stars:** existing stellar spheres now emit light instead of receiving the generic rocky-object BRDF. Filtered, animated convection gives their surfaces detail; limb darkening gives the luminous disc depth. The existing primary-star halo instance receives a coronal treatment and retains its compact core when unresolved. Stellar radius, temperature colour, companion geometry, positions and identities come from Infinite.
- **Real solar images:** planetary flight can show its actual parent star beyond the local geometry depth range. Its apparent size and bearing follow the same hierarchical coordinates as the star mesh. Original, Hyperion and Hybrid continue to share the same camera and universe.
- **Eclipses and foreground clipping:** finite-disc overlap uses existing celestial contacts in the camera frame. Opaque scene depth rejects foreground silhouettes while allowing the emitter's own photosphere. Full and partial eclipse controls exercise real generated planets. Cloud extinction and atmospheric perspective affect the source; behind-camera and fully occulted sources produce no added light.
- **Hyperion optics:** the actual game's `skyFragment` solar falloff and `postFragment` streak/ghost techniques are adapted to the existing C++ pipeline. Broad glare uses the existing third-resolution bloom pass, with analytic filtering for thin streaks. Sharp source coverage remains at full resolution. There is no readback, temporal history or additional render target.
- **Solid-world lighting:** Hyperion and Hybrid globe shading remove the broad spherical component of the fixed authoring light before applying live stellar illumination. The compensation is bounded; retained geometry and albedo variation continue to supply geological detail. Inspector lighting now follows the actual stellar direction, while Original retains its previous path.
- **Glacial materials:** the existing ice identity, heights, slopes and temperature control denser blue ice, paler frost, filtered bedding and roughness. These are optical material differences on the retained triangles, with no change to landable terrain or collision.

Star surfaces and halos have dedicated shaders and batches, keeping their material work out of ordinary terrain and starfield draws. The large HDR, bloom, cloud, polar, sun-shadow and material textures retain their previous dimensions and budgets. The existing stellar halo particle supplies its geometry; no new stars or planet types are generated.

The reference is the user's Hyperion game, especially `src/flight/shaders.hpp` (`skyFragment`, `worldFragment`, `postFragment`), `renderer.cpp` and the shared filtered-noise/material techniques. The adaptation preserves Infinite's straight projection and universe geometry. [Reference hashes and 24 unchanged generator, simulation, geometry and layout sources](graphics-0.32/preservation.json).

## Matched views

The capture harness uses existing addressed worlds, identical physical cameras, native Metal at 1440 × 900 and visual time fixed at 12 seconds. Its baseline renderer and shader sources come from the 0.31 checkpoint; the capture-only CLI and no-op optics toggle do not change that rendering.

| Scene | 0.31 | 0.32 |
| --- | --- | --- |
| Close stellar approach | [Before](graphics-0.32/before-sun.png) | [After](graphics-0.32/after-sun.png) |
| Icy world | [Before](graphics-0.32/before-ice.png) | [After](graphics-0.32/after-ice.png) |
| Day-side solar view | [Before](graphics-0.32/before-day.png) | [After](graphics-0.32/after-day.png) |
| Partial eclipse | [Before](graphics-0.32/before-partial.png) | [After](graphics-0.32/after-partial.png) |

## Validation and measurements

All **60 tests passed** in the serial native suite ([full log](graphics-0.32/tests.log)), including planet stability, responsive cockpit layouts, seamless boundary streaming, the galaxy–atomic round trip, and render-mode continuity. After selecting a clearer existing icy moon for the capture fixture, the stellar core test and both native celestial tests were rerun and passed ([final targeted log](graphics-0.32/targeted-tests.log)). Metal runtime checks cover 18 screenshots. All shaders compile for Metal, GLSL and SPIR-V.

The new negative controls add **zero pixels** for fully eclipsed and behind-camera sources. Render-unit changes preserve the physical camera, addressed descriptor and submitted body/terrain geometry; [pixel measurements](graphics-0.32/celestial-comparison.json) confirm stable results. Thirteen sampled Original-mode world crops remain pixel-identical to 0.31 ([comparison hashes](graphics-0.32/original-comparison.json)). The source-preservation hashes were rechecked after the final build.

Measured on an Apple M1 MacBook Air with 16 GiB RAM, macOS 26.4.1, Metal, at 1440 × 900. Both benchmark executables disable VSync; ordinary gameplay keeps its existing VSync setting. Each scene below uses the last 20 settled frames before the matched capture, after generation and uploads settle. Visual time is fixed at 12 seconds.

| Scene | 0.31 median ms | 0.32 median ms |
| --- | ---: | ---: |
| Stellar approach | 20.04 | 23.57 |
| Full eclipse | 29.00 | 35.03 |
| Star behind camera | 32.00 | 36.20 |
| Day-side solar view | 20.90 | 22.61 |
| Icy moon | 31.12 | 30.70 |
| Partial eclipse | 33.01 | 28.26 |

Across the complete tours, including generation and capture overhead, median frame interval was **23.12 → 25.60 ms**, with p95 **51.92 → 42.98 ms**. Peak process RSS was **133.69 → 142.14 MiB**. This upgrade has a measurable rendering cost; these single serial runs on a shared desktop are not a general speedup claim. Generation scheduling changes the number of warm-up frames, so the settled scene windows are the more useful comparison.

Both tours retained **21,600,000 bytes** of effect/shadow render targets, **1,660,244 bytes** of material textures and **87,384 bytes** of weather GPU fields. New shader programs and a bounded batch of existing stellar instances add no new full-screen targets. Broad glare runs in the existing third-resolution bloom pass; source coverage and foreground rejection remain sharp at full resolution.

[Complete measurements and executable hashes](graphics-0.32/performance.json), [0.31 timing trace](graphics-0.32/baseline-celestial-frames.csv), [0.32 timing trace](graphics-0.32/final-celestial-frames.csv), [baseline native log](graphics-0.32/baseline-native.log), [final native log](graphics-0.32/celestial-native.log). CSV row counts, consecutive frame numbers and settled-window counters were validated.

The test harness checks visible, fully eclipsed, behind-camera and partially eclipsed sources, plus the actual icy-world material. Each view is captured with optics disabled/enabled and after a render-unit change; the ice view also compares Original with Hyperion. Hidden geometry is recorded as absent rather than forcing an off-screen mesh to be submitted. Core tests cover real parent-star bearings under camera roll, physical radii, temperature colours, full/partial/annular disc overlap and unchanged source geometry.

Early profiling exposed a truncated CSV during low disk space. Old build captures were losslessly archived, and profiling now checks its final write. Damaged or incomplete timing traces are excluded from the measurements above. An additional stale-executable comparison was rejected by the image checks, the restored source timestamps were refreshed, and the final executable was rebuilt and rechecked. Its SHA-256 differs from the frozen baseline, as recorded in the measurements. Earlier runs interrupted by host sleep are also excluded; the final serial checks used a temporary `caffeinate -di` wrapper. Metal is runtime-tested; GLSL and SPIR-V are compiled and require platform testing.

The universe, seeds, object identities, histories, solid terrain, cities, ship dynamics, controls, atomic depth and fitted cockpit remain intact.


## Reproducing the checks

Build with the existing CMake project, then run the core and native suite serially with `ctest --test-dir build --output-on-failure -j 1` (native tests require a desktop and `INFINITE_GPU_TESTS=ON`). The focused fixture is `--celestial-graphics-test --width 1440 --height 900 --capture-dir OUTPUT/captures --profile OUTPUT/frames.csv --data OUTPUT/data`; it uses isolated test data. `tools/check_celestial_optics.py OUTPUT` verifies the images and logs. The test setup is saved in `cmake/RunCelestialOptics.cmake`.
