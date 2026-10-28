# Material depth and light — Infinite 0.26

This pass improves the visible scene rather than increasing global saturation or exposure. The previous broad ground emission obscured terrain relief and gave many surfaces a similar pastel finish. Terrain is now more matte, recesses are darker, glass and polished materials have directional reflections, and bright emitters keep their own colour through bloom. The spectral Hyperion palette remains in the sky, coatings and highlights.

Baseline **5dc5789** is retained by `checkpoint/infinite-0.25-before-material-depth`. The same universe, geometry, positions, seeds, climate, simulation, navigation and 0.25 ship handling remain authoritative. [Source-preservation hashes](graphics-0.26/preservation.json).

## Material and lighting changes

- **Terrain:** stronger filtered mesoscopic normal relief, shaded mineral recesses and clearer close-grain fractures. Broad pigment fill and unlit seam emission are reduced, revealing the existing height-field lighting. Cold ice, quartz, active hot seams and other inherited minerals retain their own response. The added relief is shading only; collision and silhouettes are unchanged.
- **Directional sky lighting:** the world material now separates hemispherical diffuse illumination, broad reflected sky and the direct sun. Rough stone receives a broad response; ore, ice and atomic sites receive clearer highlights. Reflection strength responds to roughness and Fresnel. This is an analytic sky-light proxy, not scene ray tracing or a captured environment map.
- **Cities:** darker material midtones, more distinct glass, recessed panel-joint normal relief and filtered facade detail. Architectural reflections use the actual planet radial up vector, rotated into the current precision frame, plus retained atmospheric pressure. That read-only metadata reuses the existing environment uniform.
- **Atmosphere and space:** reduced spectral tint in atmospheric scattering preserves more of the underlying optical colour and depth. Darker gaps separate the folded spectral sheets, stars and planetary silhouettes. All addressed stellar and gas particles remain present.
- **Atoms:** reduced flat-colour fill and stronger directional material response make the same atomic spheres read as volumes. Lattice sites, element hues, bonds and enclosing sections are retained, including the precision fix for nano-scale material interpolation.
- **Bloom:** a soft threshold scales all channels together, preserving an emitter's hue. Ordinary materials no longer acquire as much coloured glow. The existing two low-resolution targets and pass count are unchanged.

The actual source reference is the sibling Hyperion game: `src/flight/shaders.hpp::worldFragment` supplies hemispherical irradiance, surface cavity/relief and roughness filtering; `src/flight/architecture.hpp::structureLight` supplies panel relief, reflected sky and film response. These are adapted to Infinite's existing materials and geometry. [Hyperion flight reference](hyperion/reference-flight.png). No Hyperion generator is imported.

Original rendering bypasses the new shaders. Hybrid uses the existing restrained effect weights. H still cycles Original / Hyperion / Hybrid; U/O and Alt+A/D still roll the ship.

## Matched native views

The 0.25 baseline executable is frozen before any shader changes. The same fixture captures native 1440 × 900 views at 250 m, 2 km and 11.8 km above two existing worlds, along with seven city/weather views. Both runs use the same cameras and frozen visual time. [Before surface log](graphics-0.26/before-surface-native.log), [after surface log](graphics-0.26/after-surface-native.log).

| Scene | Before | After |
| --- | --- | --- |
| Thin-air plains, 11.8 km | [Before](graphics-0.26/before-0-2-Plains-HYPERION.png) | [After](graphics-0.26/after-0-2-Plains-HYPERION.png) |
| Highlands, 250 m | [Before](graphics-0.26/before-1-0-Highlands-HYPERION.png) | [After](graphics-0.26/after-1-0-Highlands-HYPERION.png) |
| City approach | [Before](graphics-0.26/before-1-city-approach-HYPERION.png) | [After](graphics-0.26/final-1-city-approach-HYPERION.png) |
| Surface inhabitants | [Before](graphics-0.26/before-2-inhabitants-HYPERION.png) | [After](graphics-0.26/final-2-inhabitants-HYPERION.png) |
| Planet from space | [Before](graphics-0.26/before-3-PLANET-HYPERION.png) | [After](graphics-0.26/after-3-PLANET-HYPERION.png) |
| Atomic lattice | [Before](graphics-0.26/before-8-CRYSTAL%20LATTICE-HYPERION.png) | [After](graphics-0.26/after-8-CRYSTAL%20LATTICE-HYPERION.png) |

Smooth generated ground remains smooth; this pass does not invent mountains or change geology. Facades and lattice organization remain Infinite's existing structures. The intentionally reduced terrain glow keeps the spectral palette concentrated in illumination and material response.

## Measured cost

Native Metal, 1440 × 900. One before and one final after run, six matched Hyperion surface views, the last 15 fully settled frames per view (90 samples per run). The paired capture runs had no CPU test run or compilation alongside the surface samples. [Before profile](graphics-0.26/before-surface-frames.csv), [after profile](graphics-0.26/after-surface-frames.csv), [summary](graphics-0.26/performance.json).

| Metric | 0.25 | 0.26 |
| --- | ---: | ---: |
| Median renderer CPU | 9.604 ms | 8.641 ms |
| Median GPU frame | 5.819 ms | 5.697 ms |
| Scene/effect/shadow textures | 26,244,608 B | 26,244,608 B |
| Shared material textures | 1,660,244 B | 1,660,244 B |
| Maximum settled generation cache | 10,255,560 B | 10,359,144 B |
| Peak process RSS | 142.86 MiB | 146.64 MiB |

Frame interval medians were 16.638 ms and 9.014 ms. macOS window/compositor pacing differed between these native fixture runs, so this is not evidence of an uncapped throughput gain. GPU work remained comparable. Cache residency and process RSS vary with asynchronous task completion and driver allocation; their existing bounds remain unchanged.

There are no new render targets, image assets, runtime dependencies or generator jobs. Reflection and material relief are analytic shader work; existing material textures, asynchronous generation and eviction remain in use. Shader binaries and the manifest are rebuilt for Metal, SPIR-V and GLSL. Metal is exercised natively; the other backends are compiled, not claimed as runtime-tested here.

## Validation

The complete validation run is recorded in [full-tests.log](graphics-0.26/full-tests.log). It checks CPU generation/identity, persistence, flight handling and rendering preservation, plus native city/weather, surface/Retina, grazing-sun and actual ridge-shadow fixtures, landing, roll, streaming and all nine scales through the rendering-mode return journey. Large test frames may be losslessly gzip-archived while retaining the exact P6 pixel and allocation assertions.


A previous streaming image check inferred holes from the most common colour at the top of the picture. With the new blue ice material, ordinary shaded ground legitimately matched that colour. The replacement diagnostic rerasterizes the **same terrain tile digest, positions, indices, projection and depth test** using an opaque mask. It retains the 0.1% uncovered-pixel limit and verifies a deliberately injected 64 × 64 hole is rejected. [Coverage result](graphics-0.26/coverage.json), [mask](graphics-0.26/terrain-coverage-mask.png), [normal scene](graphics-0.26/terrain-coverage-scene.png), [successful boundary checks](graphics-0.26/coverage-tests.log). The diagnostic shader loads only for this fixture; normal play retains its existing GPU allocation budget.

The complete run initially passed 45 of 46 checks; the colour-based boundary heuristic was the sole failure. The two boundary checks pass with the stronger raster-coverage test. All 25 CPU checks also pass on the final build, including the 137 handling assertions. [Final CPU checks](graphics-0.26/final-cpu-tests.log). The final nine-scale, three-mode return tour is repeated after the diagnostic integration. [Final mode checks](graphics-0.26/final-mode-tests.log).


**All 46 checks are verified**, combining the complete run with the corrected coverage checks and final CPU/mode rechecks. All **23 Original world crops are pixel-identical** to the frozen 0.25 executable: six surface views and 17 stops in the nine-scale return tour. [Combined validation](graphics-0.26/validation.json), [pixel comparisons](graphics-0.26/original-comparison.json). Terrain uncovered-mask fractions are below 0.000003 in every mode; the root-reference image RMS difference is 0.00255/255.

The packaged local `Infinite.app` is **0.26.0, build 26**, signed and verified. It resumes the existing flight and save format. [Retina surface capture](graphics-0.26/final-retina.png), [native cockpit orbit](graphics-0.26/final-orbit.png).
