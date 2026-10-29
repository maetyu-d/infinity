# Volumetric weather and construction materials — Infinite 0.31

This upgrade changes how the existing universe is presented. Baseline `726cda0` is retained by `checkpoint/infinite-0.30-before-volume-upgrade` and a frozen executable used for matched captures.

## Visible changes

- **Fly-through clouds:** Hyperion's actual `cloudFragment` and `cloudResolveFragment` supply hollow-shell ray integration, billow and erosion, density extinction, self-shadow, powder scattering, silver edges, cirrus and spectral sheen. These replace the thin cloud-shell presentation in Hyperion and Hybrid modes. Infinite's existing weather determines coverage, storms and precipitation. Climate pressure, moisture, scale height, cloud altitude, wind and inherited phase constrain the optical volume.
- **Cloud depth and composition:** real opaque depth stops each ray at terrain, buildings and other solid surfaces. Full-resolution occupied-ray rejection and four depth-matched taps reconstruct the lower-resolution cloud image without painting it across foreground silhouettes. Atmospheric perspective shortens through opaque banks, avoiding haze from the hidden ground being applied to foreground clouds. Clouds also occlude auroras.
- **Cloud shadows:** the same weather snapshots project soft cloud cover along the existing stellar direction onto retained terrain and construction. Structural sunlight shadows remain independent and cached.
- **Construction finishes:** streets previously used the generic craft material path. The adapter now supplies their actual construction role, planetary environment and stable metre coordinates. Roads, pads, concrete, ceramic, roofs and solar arrays get distinct responses. Filtered aggregate, pores, runoff, wear, joints and fasteners reuse the existing material image. Fixed lighting baked into surface-mesh colours is removed in the shader before directional sun and sky illuminate the same triangles. Glass retains reflected sky and optical room interiors.
- **Wet surfaces:** Hyperion's `water.hpp` supplies filtered ripple normals on the existing warm, wet lowland coating. Unresolved ripples widen highlights instead of generating point glints. This does not add a sea plane, change terrain heights or alter landing/collision surfaces.
- **Daylight and projection:** local atmospheric transmission now suppresses actual distant stellar and galaxy sprites as well as the spectral sky field. Local sun proxies remain visible. Planetary optics work with the ship and inspector cameras' actual fields of view. Shrinking views retain their existing material and atomic presentation.

The reference is the user's sibling Hyperion game, including `src/flight/shaders.hpp`, `renderer.cpp`, `architecture.hpp`, `surface_detail.hpp` and `water.hpp`. [Reference hashes and preservation proof](graphics-0.31/preservation.json) record the reference revisions and 24 unchanged Infinite files covering generators, geometry, coordinates, histories, weather, cities, flight, reversed depth and window fitting. The shaders compile for Metal, GLSL and SPIR-V; Metal is runtime-tested.

## Bounded asynchronous work

The new weather adapter samples the existing `sampleWeather` function on worker threads. Two eight-second snapshots interpolate on the GPU. Each 128 × 64 RGBA8 atlas includes a complete mip chain and uses 43,692 bytes. A four-entry cache admits at most two jobs, cancels stale requests and evicts unused snapshots. The renderer holds its last complete pair during time boundaries and prefetches the next snapshot, preventing a worker delay from flashing the old shell back on screen. No GPU calls occur on worker threads.

Cloud integration uses one-third output resolution, capped at 960 × 600, with 12–20 occupied samples in Hyperion and 12 in Hybrid. Sample footprints filter unresolved density detail. Integration skips empty shell intervals and terminates when opaque. Full-resolution depth reconstruction preserves solid silhouettes. Material, noise, sun-shadow and polar-light texture dimensions are unchanged.

At 1440 × 900 the added cloud framebuffer uses **1,152,000 bytes**, and the two GPU weather atlases use **87,384 bytes**. Completed weather-cache storage is bounded at **174,768 bytes**; retained snapshots and in-flight worker storage are additional small bounded allocations. At extreme output sizes the cloud framebuffer cannot exceed 4,608,000 bytes. This is optical detail; no new universe meshes or simulation populations are resident.

## Matched images

Native Metal at 1440 × 900, with identical addressed worlds, physical cameras and visual time frozen at 12 seconds.

| Scene | 0.30 | 0.31 |
| --- | --- | --- |
| Weather from orbit | [Before](graphics-0.31/before-0-weather-orbit-HYPERION.png) | [After](graphics-0.31/after-0-weather-orbit-HYPERION.png) |
| City approach | [Before](graphics-0.31/before-1-city-approach-HYPERION.png) | [After](graphics-0.31/after-1-city-approach-HYPERION.png) |
| Street and inhabitants | [Before](graphics-0.31/before-2-inhabitants-HYPERION.png) | [After](graphics-0.31/after-2-inhabitants-HYPERION.png) |

The cloud fixture also captures an actual atmospheric world [from orbit](graphics-0.31/cloud-orbit.png), [below its clouds](graphics-0.31/cloud-below.png) and [inside a bank](graphics-0.31/cloud-inside.png). A dense bank can obscure the surface or sky, following the retained weather rather than guaranteeing a clear view.

## Measured cost

Six matched Hyperion terrain views at 1440 × 900 use the last ten fully settled frames with the complete material image published: 60 samples per run. The frozen 0.30 baseline was run twice. Runs are sequential, without a second Infinite instance or CPU test. [Exact sample frames and measurements](graphics-0.31/performance.json), [first baseline](graphics-0.31/baseline-first-frames.csv), [baseline repeat](graphics-0.31/baseline-repeat-frames.csv), [final frames](graphics-0.31/final-frames.csv).

| Metric | 0.30 first / repeat | 0.31 final |
| --- | ---: | ---: |
| Median reported GPU interval | 8.33 / 7.84 ms | 6.58 ms |
| Median CPU render work | 9.07 / 8.73 ms | 8.76 ms |
| Median wall frame interval | 16.65 / 10.97 ms | 9.00 ms |
| Active scene/effect/shadow targets | 28,836,608 B | 29,988,608 B |
| Shared material/noise textures | 1,660,244 B | 1,660,244 B |
| Optical weather GPU textures | 0 B | 87,384 B |
| Process peak RSS | 149.39 / 145.31 MiB | 146.63 MiB |

The first volumetric implementation measured 12.34 ms GPU and 19.52 ms wall time. Reducing the cloud integration target from half to one-third resolution and capping occupied samples at 20, while retaining full-resolution silhouette rejection, removed that measured regression. The final sample is faster than both baseline samples, but the large wall-time variation demonstrates that these short runs do not establish a general throughput improvement or an isolated per-effect cost. Clouds, atmosphere and nearby geometry vary substantially by viewpoint. The **1.18 MiB additional GPU texture storage** at this size is an exact allocation difference, independent of timing variation.

## Validation

All **57 check names have passing results across the broad run and affected reruns**. The complete run passed 56/57: its ground-aurora image assertion was obscured by the newly opaque cloud banks. The fixture now holds clouds disabled while testing emission. All 15 affected checks then passed, including cloud optics, isolated auroras, planet-depth stability, cities, terrain quality, Retina and the complete nine-scale rendering-mode round trip. The final paving-origin fix passed all four city and terrain-quality checks again. [Broad run](graphics-0.31/complete-tests.log), [28 core checks](graphics-0.31/core-tests.log), [15 affected checks](graphics-0.31/optimised-tests.log), [final material checks](graphics-0.31/material-tests.log).

All **13 matched Original-mode world crops are pixel-identical** to 0.30. The comparison excludes fixed cockpit panels and startup text. [Pixel comparison](graphics-0.31/original-comparison.json). Native mode changes retain submitted planet/terrain/city data and physical cameras. New core checks verify deterministic weather samples/mips, atlas pole continuity, cancellation, cache eviction and framebuffer size/aspect bounds. [Cloud pixel and render-unit checks](graphics-0.31/cloud-comparison.json), [native geometry/camera proof](graphics-0.31/cloud-native.log), [planet stability](graphics-0.31/planet-stability.json), [isolated polar checks](graphics-0.31/polar-comparison.json).

The local macOS bundle is **0.31.0, build 31**, ad-hoc signed and strictly verified. It was launched successfully and visibly resumed the existing full-size saved flight in Hyperion mode. The complete cockpit and instrument panels fit the live window.

The native cloud fixture checks the same physical camera and submitted planet/terrain geometry with volumes disabled, enabled and after changing render units. Ground and airless controls must receive exactly zero cloud pixels. The aurora fixture holds clouds disabled to isolate emission; otherwise an existing overcast sky can correctly hide the ground view of the curtains. Integrated city, weather and planetary captures exercise the combined presentation.

Original mode retains its existing drawing path. Geometry, object identities, seeds, city layouts, simulation rules, ship handling, controls, saved flights and the fitted cockpit layout remain intact.
