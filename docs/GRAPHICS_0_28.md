# Surface optics and architectural depth — Infinite 0.28

This pass develops the existing Hyperion presentation through material-specific lighting, geological colour, richer cloud illumination and optical depth behind glass. It preserves the universe and its geometry. Baseline **1b1819e** is retained by `checkpoint/infinite-0.27-before-surface-optics` and a frozen executable used for matched captures.

## What changes

- **Planetary height colour:** the vertex shader passes each generated vertex's canonical height to the material. Measuring radius after interpolating a large triangle instead measured the chord through the planet, creating false lowland/contour bands. Silhouettes, positions, indices and the terrain height function are unchanged.
- **Ice and minerals:** glacial geology and inherited mineral identity now control stronger blue depth tint, darker fractures, matte frost, grazing sheen and roughness. Geological pigment lives in the reflectance rather than a broad glow overlay. Dry deposits retain a different response from ice. Existing warm, hydrated lowlands receive darker, smoother coatings; there is no new ocean mesh, water simulation or flattened terrain.
- **Atmospheric light:** retained pressure and the existing stellar direction control warmer low-angle illumination. Atmospheric integration increases from 8 view / 2 sun samples to 12 / 4, within Hyperion's source integration approach. Opalescent colour is concentrated toward the horizon and twilight. Reversed-depth reconstruction remains intact.
- **Clouds:** Hyperion's light-facing density gradient, powder scattering, silver lining, warm sunset light and opal palette are applied to the existing weather banks. Weather coverage, wind, precipitation and cloud mesh positions are unchanged. These are enhanced optical materials on the retained shells, not a new volumetric weather simulation.
- **Cities:** Hyperion's `windowInterior` ray-box shading is adapted to the metre grid behind existing glass panes. Floors, back walls, desks and lamps are optical detail; the simulation does not gain rooms or buildings. Recessed panel joints and small fasteners are filtered, and normal variance broadens distant highlights.
- **Close-material stability:** magnified height textures receive cubic B-spline reconstruction; minified material continues to use its complete anisotropic mip chain. Insignificant triplanar projections are skipped. A separate precision fix splits the material-noise origin in doubles, packs integer cells into the existing matrix, and adds them only after local floor/fract. The old float coordinate could lose a four-millimetre camera movement. A negative-control regression reproduces that loss and verifies that all four sampled octaves preserve canonical phase with the corrected frame.

The actual references are the sibling Hyperion game's `src/flight/shaders.hpp` (`worldFragment`, `cloudFragment`, `atmoFragment`), `surface_detail.hpp` and `architecture.hpp` (`windowInterior`, panel relief and coherent projection pruning). Cubic reconstruction and the split origin adapt its material sampling to Infinite's precision requirements. No new full-screen pass, image asset, dependency, generation job or render target is added.

## Matched views

Native Metal, 1440 × 900, identical addressed worlds and cameras, visual time frozen at 12 seconds. The baseline and final capture logs retain geometry/terrain digests and city counts.

| Scene | 0.27 | 0.28 |
| --- | --- | --- |
| Glacial plains, 250 m | [Before](graphics-0.28/before-0-0-Plains-HYPERION.png) | [After](graphics-0.28/after-0-0-Plains-HYPERION.png) |
| Rugged terrain, 250 m | [Before](graphics-0.28/before-1-0-Highlands-HYPERION.png) | [After](graphics-0.28/after-1-0-Highlands-HYPERION.png) |
| Weather from orbit | [Before](graphics-0.28/before-0-weather-orbit-HYPERION.png) | [After](graphics-0.28/after-0-weather-orbit-HYPERION.png) |
| City approach | [Before](graphics-0.28/before-1-city-approach-HYPERION.png) | [After](graphics-0.28/after-1-city-approach-HYPERION.png) |
| Street and glass facades | [Before](graphics-0.28/before-2-inhabitants-HYPERION.png) | [After](graphics-0.28/after-2-inhabitants-HYPERION.png) |

The geometric character of each world remains Infinite's. Smooth generated ground remains smooth. Close material relief and window interiors affect shading and do not provide new collision or walkable geometry. [Source preservation and Hyperion reference hashes](graphics-0.28/preservation.json).

## Measured cost

One baseline and one final run; six matched Hyperion surface views, the last 15 fully settled frames per view, 90 samples per run. No other native game instance or CPU test run was active during these surface samples. [Measurements](graphics-0.28/performance.json), [before frames](graphics-0.28/before-surface-frames.csv), [after frames](graphics-0.28/after-surface-frames.csv).

| Metric | 0.27 | 0.28 |
| --- | ---: | ---: |
| Median GPU frame | 7.479 ms | 7.805 ms |
| Median renderer CPU | 17.601 ms | 8.755 ms |
| Scene/effect/shadow texture storage | 26,244,608 B | 26,244,608 B |
| Shared material textures | 1,660,244 B | 1,660,244 B |
| Maximum settled generation cache | 10,255,560 B | 10,237,416 B |
| Peak process RSS | 147.3 MiB | 149.6 MiB |

The measured GPU cost increases by **0.326 ms (4.4%)**. CPU and frame pacing varied between these single runs; the 18.970 → 16.579 ms frame interval is not evidence of an uncapped throughput gain. Generation-cache variation follows asynchronous completion. GPU texture allocation stays fixed; shader code and one additional interpolated height scalar carry the new presentation detail.

## Validation

**All 49 checks pass** on the final build, including the new precision assertions inside the existing surface-shading test. Native tests exercise all three modes, city/weather views, close materials, Retina, grazing-light and actual terrain shadows, landing/roll, boundary coverage, the 0.27 planet-depth regression and automatic galaxy-to-atomic flight. The mode tour records seventeen matching camera/geometry/generation proofs across nine scales.

All **30 Original world crops are pixel-identical** to 0.27: six surface views, seven city/weather views and seventeen scale-tour stops. The comparison excludes cockpit instruments and the transient startup control banner. Geometry and terrain digests remain the same through mode changes. [Pixel comparison](graphics-0.28/original-comparison.json), [validation summary](graphics-0.28/validation.json), [complete test log](graphics-0.28/final-tests.log), [planet-depth checks](graphics-0.28/planet-stability.json), [mode tour](graphics-0.28/mode-round-trip.log).

Shaders are rebuilt for Metal, GLSL and SPIR-V with the pinned shader compiler. Metal is runtime-tested; the other backends are compiled. The local `Infinite.app` is **0.28.0, build 28**, signed and strictly verified. It resumes the existing saved flight.
