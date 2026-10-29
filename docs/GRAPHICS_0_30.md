# Polar light and planetary contrast — Infinite 0.30

This rendering pass preserves the existing universe, geometry and flight state. Baseline `369e11f` is retained by `checkpoint/infinite-0.29-before-planet-light` and a frozen executable used for matched captures.

## Visible changes

- **Polar light:** Hyperion’s `auroraFragment` supplies the hollow-shell ray integration, moving oval, filtered rays, height envelope and spectral emission. Infinite supplies the existing climate axis, pressure, scale height and inherited visual phase. Eligible solid atmospheres can show curtains on their night sides. Airless worlds and shrinking views receive no aurora. This is optical presentation, not a new magnetic simulation or inhabited-world rule.
- **Depth and stability:** shell radii use planet-relative units so changing rendering units cannot resize the effect. Reversed opaque depth and the real planetary surface bound the occupied ray. Conservative latitude/daylight bounds skip empty pixels. Integration uses 16 steps at half resolution; a full-resolution occupied-ray check and four depth-matched taps reconstruct the emission. This adapts Hyperion’s `cloudResolveFragment` approach and avoids adding light across foreground silhouettes. Near-surface emission is reduced and fine strand frequency follows atmospheric thickness.
- **Clouds:** the retained weather coverage and shell geometry still own every bank. The shader now uses Hyperion’s existing filtered noise lookup, three samples toward the sun, density extinction and billow-normal lighting. Darker undersides, powder light and silver edges give the same banks greater depth. These are shaded cloud shells, rather than a new volumetric weather simulation.
- **Geological contrast:** the actual vertex heights and surface slopes modulate the inherited mineral coating across globe and terrain LODs. This reuses authoritative geology instead of evaluating another regional noise field at every fragment. Existing close-material relief, frost and fractures remain active.
- **Cities:** hemispherical sky/ground bounce replaces broad uniform fill; existing surface pressure and stellar direction control warm sunlight. Darker footings and reflected sky make the retained buildings, ceramic surfaces and glass easier to distinguish. No buildings, rooms, streets or residents are added by this pass.
- **Clarity:** Hyperion’s bounded contrast recovery reuses the edge-filtering taps. Silhouettes, tiny highlights and stars are excluded. The cockpit is composited afterwards and retains the fitted 0.29 layout.

The references are the sibling Hyperion game’s `src/flight/shaders.hpp`, `src/flight/renderer.cpp`, `surface_detail.hpp` and `architecture.hpp`. [Reference hashes and preservation proof](graphics-0.30/preservation.json) record 24 unchanged files covering generation, coordinates, histories, geometry, flight, weather, cities, depth precision and window fitting. Shader sources and binaries are rebuilt for Metal, GLSL and SPIR-V. Metal is runtime-tested; other backends require platform testing.

## Matched images

Native Metal at 1440 × 900, with identical addressed worlds and cameras and visual time frozen at 12 seconds.

| Scene | 0.29 | 0.30 |
| --- | --- | --- |
| Weather from orbit | [Before](graphics-0.30/before-0-weather-orbit-HYPERION.png) | [After](graphics-0.30/after-0-weather-orbit-HYPERION.png) |
| City approach | [Before](graphics-0.30/before-1-city-approach-HYPERION.png) | [After](graphics-0.30/after-1-city-approach-HYPERION.png) |
| Street and inhabitants | [Before](graphics-0.30/before-2-inhabitants-HYPERION.png) | [After](graphics-0.30/after-2-inhabitants-HYPERION.png) |

[Polar light off](graphics-0.30/night-0.png) · [Polar light on](graphics-0.30/night-1.png). These two captures use the same 0.30 shader configuration and scene, changing only the optical-emission switch.

The existing landscape remains authoritative. Shading and emission do not add terrain relief, collision, weather cells or simulation objects. [Original-mode pixel comparisons](graphics-0.30/original-comparison.json).

## Measured cost

Six matched Hyperion terrain views at 1440 × 900, using the last ten fully settled frames with the complete material texture published: 60 samples per run. The 0.29 baseline was run twice. No other Infinite instance or CPU test ran during these samples. [Exact sample frames and measurements](graphics-0.30/performance.json), [first baseline](graphics-0.30/baseline_first-frames.csv), [baseline repeat](graphics-0.30/baseline_repeat-frames.csv), [final frames](graphics-0.30/final-frames.csv).

| Metric | 0.29 first / repeat | 0.30 final |
| --- | ---: | ---: |
| Median GPU frame | 8.311 / 11.207 ms | 8.689 ms |
| Median renderer CPU | 8.659 / 18.330 ms | 13.410 ms |
| Median frame interval | 16.462 / 19.171 ms | 16.845 ms |
| Scene/effect/shadow texture storage | 26,244,608 B | 28,836,608 B |
| Shared material textures | 1,660,244 B | 1,660,244 B |

Relative to the first baseline, measured GPU cost increases by **0.378 ms (4.5%)**. The repeat demonstrates substantial CPU/GPU timing variation, so these short samples do not establish a general throughput change. The six terrain views do not represent the cost of every auroral viewing angle. The half-resolution optical buffer adds **2.47 MiB** at this output size; scene, bloom, sun-shadow and material texture sizes are unchanged. Its size is bounded by output resolution and it is released/recreated with the other targets when resizing.

## Validation

The **complete 54-check suite passed**. After replacing an expensive regional coating with height/slope contrast, **all 12 affected native rendering checks passed again**, including real cities/weather, terrain, Retina, polar light, the planet-depth regression and all three multiscale rendering modes. [Complete suite](graphics-0.30/complete-tests.log), [post-optimisation checks](graphics-0.30/optimised-tests.log).

All **13 matched Original-mode world crops are pixel-identical** to 0.29. The comparison excludes the fixed cockpit panels and startup banner. Native rendering also verifies identical planet/terrain/city data across rendering-mode changes. The original generators, geometry, coordinates, histories, seeds, ship rules and fitted UI source remain unchanged.

The polar fixture captures night, day, surface, ground-occluded and airless scenes with emission disabled/enabled and with changed render-unit sizes. Daylight, solid-ground and vacuum controls add zero emission pixels. Magnification comparisons remain below 0.033 RMS RGB levels out of 255. These checks exercise half-resolution integration and full-resolution depth reconstruction. [Polar pixel checks](graphics-0.30/polar-comparison.json), [native identity/camera proofs](graphics-0.30/polar-native.log).

The local macOS app is **0.30.0, build 30**, ad-hoc signed and strictly verified. It uses the existing saved flight and control bindings.

Additional emission-on/off measurements use the last ten settled frames at each identical polar-fixture camera. [Frame-indexed polar measurements](graphics-0.30/polar-performance.json), [raw frames](graphics-0.30/polar-frames.csv), [final polar checks](graphics-0.30/polar-cost-tests.log).

The short polar on/off GPU readings are too variable to isolate the emission cost: even the airless control, which submits no aurora draw in either state, changed from 29.19 to 35.42 ms. They are retained as raw evidence rather than interpreted as a speed-up or a per-effect cost guarantee.
