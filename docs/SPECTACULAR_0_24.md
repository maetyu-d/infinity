# Spectral presentation, Infinite 0.24

This release strengthens the Hyperion presentation across the existing universe. Every procedural generator, geometry generator, coordinate transform, seed, history, city layout, navigation rule and ship dynamic remains unchanged. The baseline is **fde720e**, retained by `checkpoint/infinite-0.23-before-spectacle`. Twenty source files are verified byte-identical in [preservation.json](spectacular-0.24/preservation.json).

## Rendering changes

The reference remains the actual sibling Hyperion game. `src/flight/shaders.hpp` supplies folded pigment sheets, nested gas/dust emission, filtered stellar spikes, spectral Fresnel, material lighting and the display curve. `surface_detail.hpp` supplies the inherited close-material treatment; `lighting.hpp` supplies the existing sun reception. Source hashes are recorded with the preservation evidence. [Original Hyperion reference](hyperion/reference-flight.png).

- **Space:** stronger layered pigment sheets and illuminated gas edges retain dark gaps; resolved stars have sharper cores, bounded halos and bright-star spikes. Existing addressed particles provide every emitter. The optical medium retains its galaxy orientation and inherited phase through rebasing.
- **Planets:** richer pigment, stronger silhouette colour and pressure-dependent limb light. Only actual gas envelopes receive latitude belts; solid planets keep their terrain. Atmosphere tint varies with horizon and twilight, while the retained optical density and opaque-depth transmission remain authoritative.
- **Ground and minerals:** iridescent deposits and thin mineral seams use existing material frames and inherited mineral, activity and temperature. Warm active material can emit warm seams; ice and quartz have their own response. Broad ground colouring stays subordinate to terrain illumination. No displaced vertices or changed collision.
- **Weather:** stronger forward-lit cloud edges, deeper self-shading and coloured silver lining use the existing weather geometry, density and coverage.
- **Cities and spacecraft:** stronger glass reflection, coloured paint, window emission and inlays. Night windows brighten while daylight emission stays limited. Layouts, inhabitants, traffic and buildings are retained.
- **Cockpit:** the existing cabin mesh receives directional lighting, metal response and restrained spectral edges, with a dark body. Its post-composite material uses the same display curve; instruments and controls retain their layout and data.
- **Atoms:** clearer highlights and brighter existing bonds preserve element colours, lattice sites and their connections. The enclosing mineral cutaway receives lower illumination so the specimen stays legible.

Bloom has a wider footprint and a higher bright-pass threshold, using the same two one-third-size targets and pass count. The hue-preserving display curve has modestly stronger exposure and saturation. Hybrid retains restrained effect weights. Original bypasses these changes, including cockpit material shading.

## Extreme magnification fix

Native comparison exposed an existing false stipple behind the atomic lattice. At that magnification, an inherited physical material tile is locally constant, but floating-point interpolation of its tiny coordinate changes was amplified into normal relief. The screen-footprint filter now fades this shading detail below useful float precision as well as above the usual subpixel limit. The enclosing section and all its triangles remain present. A framebuffer regression checks the actual enclosing surface for excessive pixel-to-pixel contrast; it fails on the 0.23 image.

## Validation and measurements

Metal is exercised on this Mac. SPIR-V and GLSL shaders are rebuilt and manifest-checked; their runtime behaviour is not claimed as tested here.

**All 45 checks are verified.** The complete run passed 44 checks; disk exhaustion truncated the final mode-profile capture. The two mode-tour/image checks were repeated successfully using lossless gzip capture archival, with all pixel and memory assertions retained. [Full run](spectacular-0.24/full-tests.log), [successful repeat](spectacular-0.24/mode-retry-tests.log), [combined validation](spectacular-0.24/validation.json).

All **23 Original world crops** are pixel-identical to 0.23: six surface views and 17 stops in the cosmic-to-atomic return tour. [Comparison hashes](spectacular-0.24/original-comparison.json). The enclosing-material stipple metric falls from **39.87% to 0.00%** of adjacent pixel pairs, against a 3% regression limit.

The native fixtures exercise city approaches, street inhabitants, weather, solid terrain, terrain sun shadows, the saved grazing view, landing/departure, boundary streaming, shrinking, and all nine scales in three rendering modes. The mode tour checks exact camera state and generation fingerprints at 17 stops, including the return journey. The CPU presentation suite has **3682 preservation checks**, including inherited gas/pressure metadata and actual enclosing-section geometry.

| Matched scene | 0.23 | 0.24 |
| --- | --- | --- |
| Planet in space | [Before](spectacular-0.24/before-3-PLANET-HYPERION.png) | [After](spectacular-0.24/after-3-PLANET-HYPERION.png) |
| Highlands, 11.8 km | [Before](spectacular-0.24/before-1-2-Highlands-HYPERION.png) | [After](spectacular-0.24/after-1-2-Highlands-HYPERION.png) |
| Thin-air plains, 250 m | [Before](spectacular-0.24/before-0-0-Plains-HYPERION.png) | [After](spectacular-0.24/after-0-0-Plains-HYPERION.png) |
| Mineral grain and enclosing rock | [Before](spectacular-0.24/before-7-MINERAL%20GRAIN-HYPERION.png) | [After](spectacular-0.24/after-7-MINERAL%20GRAIN-HYPERION.png) |
| Atomic lattice and enclosing mineral | [Before](spectacular-0.24/before-8-CRYSTAL%20LATTICE-HYPERION.png) | [After](spectacular-0.24/after-8-CRYSTAL%20LATTICE-HYPERION.png) |

Also inspected: [city approach](spectacular-0.24/after-1-city-approach-HYPERION.png), [street view](spectacular-0.24/after-2-inhabitants-HYPERION.png), and [2840 × 1800 surface/cockpit view](spectacular-0.24/after-retina.png). These are actual native captures. Terrain and building silhouettes deliberately remain Infinite's; Hyperion supplies lighting and presentation. Airless skies can show the full spectral field, while daylight atmospheres attenuate it. Existing smooth terrain remains smooth.

## Measured cost

1440 × 900, native Metal, VSync enabled. One baseline run and one final run, six matched Hyperion views, the last 15 fully settled frames per view (90 samples per run). This measures presentation cost and pacing; a single VSync-limited run does not establish an uncapped throughput improvement. Raw [before](spectacular-0.24/before-frames.csv) and [after](spectacular-0.24/after-frames.csv) profiles, plus [per-view results](spectacular-0.24/performance.json).

| Metric | 0.23 | 0.24 |
| --- | ---: | ---: |
| Median frame interval | 16.605 ms | 16.612 ms |
| Median CPU frame | 8.798 ms | 8.863 ms |
| Median GPU frame | 6.499 ms | 5.686 ms |
| Material textures | 1,660,244 B | 1,660,244 B |
| Scene/effect/shadow targets | 26,244,608 B | 26,244,608 B |
| Maximum CPU generation cache in settled views | 10,255,560 B | 10,255,560 B |
| Peak process RSS | 150.00 MiB | 148.80 MiB |

Terrain skips expensive full-object tapestry evaluation where the existing local strata material already supplies detail. Bloom reuses its existing targets. The cockpit reuses the existing material program and adds one 80-byte transient instance; the read-only environment uniform adds 16 bytes per binding. There are no new image assets, render targets, per-planet texture allocations or dependencies. The existing asynchronous generation, cancellation, eviction, LOD and upload budgets remain in place.

```sh
python3 tools/compile_shaders.py --shaderc build/shader-tool/shaderc
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

The packaged `Infinite.app` is version **0.24.0, build 24**, locally signed and verified. H cycles Original / Hyperion / Hybrid. The existing save remains the default flight state.
