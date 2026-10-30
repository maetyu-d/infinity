# Geological surfaces and landing light — Infinite 0.33

This pass improves the appearance of Infinite's existing solid worlds. The verified 0.32 build is retained at `473944e`, with checkpoint `checkpoint/infinite-0.32-before-surface-upgrade`.

## What changed

- **Clearer geological colour:** the existing metre-anchored strata pigments now absorb the broad substrate instead of adding a pale colour term. This exposes stronger colour separation in the retained deposits without evaluating more noise or changing the terrain field.
- **Dry ground and exposed bedrock:** the actual surface slope, mineral, ice and wet-lowland coverage control regolith roughness and its dielectric coating. Dusty flats no longer inherit polished-metal responses from a bulk iron composition. Steep exposed material, wet coatings and ice retain distinct highlights.
- **Airless lighting:** rocky worlds without atmospheric pressure reflect a subdued space field instead of a bright terrestrial sky. Rough Fresnel reflection limits the broad white grazing sheen that previously hid geological detail. Atmospheric skies keep their directional colour, and city glazing retains its existing reflection function.
- **Wet-lowland boundaries:** a derivative-filtered shoreline colour follows the existing elevation/temperature/water rule. This is a material coating on the same triangles; there is no new water plane, ocean simulation, coastline geometry or collision surface.
- **Landing contact shade:** Hyperion's actual `worldFragment` gear-contact treatment now sits beneath a nearby full-size ship. The retained terrain height, physical hull size and altitude determine its point, footprint and falloff. The world-space contact is calculated in doubles before camera subtraction and float conversion. It fades by 38 m for a 12 m ship, scales with the actual hull size, resets every frame and is scoped to the matching planet. Shrinking, inspector views and Original mode do not receive it.

The reference is the user's Hyperion game, especially `src/flight/shaders.hpp::worldFragment`: land/water roughness, mineral pigments, luminous shores and `groundClearance`/`gearPosition` contact shading. The existing Infinite BRDF and analytic environment are adapted to retain that material separation. [Reference and preservation hashes](graphics-0.33/preservation.json) verify 27 unchanged generator, geometry, navigation, ship, cockpit, coordinate, weather, city and stellar sources. Object identities, seeds, histories and atomic depth are intact.

The implementation adds two uniform vectors and a bounded physical contact calculation near landing. It adds no render passes, textures, history buffers, readbacks or dependencies. Existing shaders and GPU representations remain in use.

## Matched views

The before renderer is the unmodified 0.32 executable. Both use Metal at 1440 × 900, visual time 12 seconds and the same generated worlds and physical cameras. The terrain tour checks 250 m, 2 km and 11.8 km approaches in Original, Hyperion and Hybrid modes. The contact views compare the same final scene with its optical shade disabled and enabled.

| View | Before | After |
| --- | --- | --- |
| Atmospheric plains, 250 m | [0.32](graphics-0.33/before-plains.png) | [0.33](graphics-0.33/after-plains.png) |
| Airless highlands, 250 m | [0.32](graphics-0.33/before-highlands.png) | [0.33](graphics-0.33/after-highlands.png) |
| Airless approach, 11.8 km | [0.32](graphics-0.33/before-approach.png) | [0.33](graphics-0.33/after-approach.png) |
| Grazing ground view | [0.32](graphics-0.33/before-grazing.png) | [0.33](graphics-0.33/after-grazing.png) |
| Physical landing contact, 8 m | [Shade disabled](graphics-0.33/contact-off.png) | [Shade enabled](graphics-0.33/contact-on.png) |

## Validation

All **62 checks passed across the complete suite and the final targeted rerun**: 29 core checks and 33 native/image checks. The full suite passed 61 checks; its only failure was a landing-image threshold that expected Hybrid's reduced shade to exceed the same four-code-value cutoff as Hyperion. The checker now measures Hybrid at three code values and additionally verifies that its peak change is smaller than Hyperion's. The native fixture and checker both passed again. [Complete run](graphics-0.33/tests.log), [final rerun](graphics-0.33/final-tests.log).

The suite covers unchanged planet geometry, stable far-depth magnifications, cloud and polar composition, cities, terrain at three approach heights, Retina, grazing shadows, ship handling, cockpit fitting, unbounded streaming, the galaxy–atomic round trip and Original/Hyperion/Hybrid continuity. All shaders compile for Metal, GLSL and SPIR-V ([compiler log](graphics-0.33/shader-build.log)).

Across the matched 0.32/0.33 surface tours, **all 18 body and terrain geometry digests are identical** ([geometry proof](graphics-0.33/geometry-comparison.json)). Six sampled Original world crops are pixel-identical ([pixel proof](graphics-0.33/original-comparison.json)). [Landing image measurements](graphics-0.33/landing-comparison.json) show exactly zero contact pixels at 100 m and in Original mode, reduced Hybrid strength, and stable render-unit changes. [Native camera and geometry log](graphics-0.33/landing-native.log).

The new native landing fixture captures a real, landable airless highland world. It verifies unchanged descriptors, submitted terrain/body geometry and physical camera, and compares enabled/disabled shading at 8 m and 100 m. Original is an exact negative control; Hybrid receives the reduced presentation strength. A legal render-unit rebase exercises the same physical footprint while keeping the enclosing planet within the retained far plane.

## Performance

A final serial pair used the retained grazing-surface camera in `tests/data/surface-view-0.23.txt`, Metal at 1440 × 900, time fixed at 12 seconds and all three rendering modes. Both benchmark executables disable VSync; normal gameplay keeps its previous VSync setting. Body and terrain digests match between executables.

| Measurement | 0.32 | 0.33 |
| --- | ---: | ---: |
| Median frame interval | 11.08 ms | 7.67 ms |
| p95 frame interval | 22.30 ms | 13.84 ms |
| Peak process RSS | 138.75 MiB | 135.22 MiB |
| Effect targets, including sun shadow | 29,988,608 B | 29,988,608 B |
| Material textures | 1,660,244 B | 1,660,244 B |
| Weather GPU fields | 87,384 B | 87,384 B |

These are whole-run measurements, including asynchronous warm-up, mode changes and screenshot overhead, from a single pair on a shared Apple M1 MacBook Air with 16 GiB RAM running macOS 26.4.1. They establish observed performance for this view, rather than a general speedup or FPS guarantee. The new surface response uses the existing noise samples and render targets; the contact treatment adds no texture reads or draws.

[Measurements and executable hashes](graphics-0.33/performance.json), [baseline trace](graphics-0.33/before-close-frames.csv), [final trace](graphics-0.33/after-close-frames.csv), [baseline log](graphics-0.33/before-close-native.log), [final log](graphics-0.33/after-close-native.log). CSV frame numbers, complete rows, logged counts and absence of sleep-sized intervals were checked.

An earlier long baseline repeat was interrupted by clamshell sleep, with a 912-second frame interval. It is excluded. The final short pair completed successfully after wake, under a temporary `caffeinate -di` wrapper. The separate 18-view surface tour supplies the approach screenshots and geometry proof; its logs and traces are retained as [before log](graphics-0.33/before-native.log), [after log](graphics-0.33/after-native.log), [before frames](graphics-0.33/before-frames.csv) and [after frames](graphics-0.33/after-frames.csv).


Metal is runtime-tested; the GLSL and SPIR-V shader variants compile and require their own platform runtime testing. The macOS application is packaged as 0.33.0, build 33. Normal controls, ship dynamics, cockpit layout and saved-flight loading are unchanged.
