# Graphics quality — Infinite 0.16

The existing psychedelic Hyperion presentation now has stronger directional lighting and a quieter sky, so planets, rocks and ships read as volumes. The generators, coordinates, seeds, object identities, navigation, ship dynamics and scale hierarchy remain unchanged. `80ca1b7` is the working 0.15 checkpoint.

## Presentation changes

- Geological meshes use an optional, compact normal stream derived from their existing vertices and triangles. Duplicate iso-surface positions share lighting; creases exceeding 50 degrees retain their faces. Crystal grains and lattice host geometry retain flat facets. The input mesh is immutable.
- Normal preparation uses the existing oneTBB dependency, with one background task at a time, cancellation on eviction/destruction and a 65,536-vertex task limit. Larger meshes remain drawable with derivative normals. The rendering thread never waits for the result. Each normal occupies four GPU bytes; it shares the existing 32 MiB geometry eviction budget and 2 MiB frame upload budget. No extra render target or dependency is introduced.
- Hyperion's `worldFragment` sky/ground irradiance, broad reflection and roughness-dependent lighting make day/night and surface relief more legible. Spectral paint and fine luminous seams keep their inherited, object-anchored coordinates. Geological geometry and cavities continue to determine the silhouette.
- Close-range relief combines three footprint-filtered grain frequencies. Distant detail converges to its average instead of sparkling. Material roughness varies with resolved grains.
- The sky's folded colour and silk patterns retain the existing galaxy orientation and phase, with lower luminance. Less bloom allows actual celestial lights and surface contours to stand out.
- Both final display paths share an HDR-safe edge resolver. It chooses between short and wider edge samples using the local luminance range, preserves hue through the existing tone curve, and adds stable sub-code-value dithering to gradients. No temporal history is used across scale changes.
- Atoms above 48 projected pixels can use one additional shared 24×40 sphere display mesh in Hyperion/Hybrid. All atom centres, radii, elements, site IDs and bonds are unchanged. Original mode retains its previous display meshes and shading.

The reference remains the user's game in `../Hyperion`: `src/flight/shaders.hpp` (`worldFragment`, especially the irradiance and spectral surface layers, and `postFragment` edge filtering) and `src/flight/surface_detail.hpp` (`closeMaterial`). The prior port and source analysis are recorded in [HYPERION_RENDERING.md](HYPERION_RENDERING.md) and [PSYCHEDELIC_PRESENTATION.md](PSYCHEDELIC_PRESENTATION.md). The compact normal stream and additional atom display LOD are adaptations to Infinite's existing 3D meshes.

## Images

Identical seeded fixture views, with fixed visual time:

| View | 0.15 | 0.16 |
| --- | --- | --- |
| Orbital planet | [Before](psychedelic/captures/busy-orbit.png) | [After](graphics/busy-orbit.png) |
| Geological rock | [Before](psychedelic/captures/rock.png) | [After](graphics/6-ROCK-HYPERION.png) |
| Stellar background | [Before](psychedelic/captures/stellar-background.png) | [After](graphics/stellar-background.png) |
| Atomic flight | [Before](psychedelic/captures/atomic-shrink.png) | [After](graphics/atomic-shrink.png) |

[Landing approach](graphics/landing-approach.png), [landed](graphics/landed.png), [moon family](graphics/moon-family.png) and [galaxy](graphics/1-GALAXY-HYPERION.png) were also inspected. The terrain silhouette follows the existing generated terrain. The pre-existing distant ancestor-surface precision artifact in the rock overview remains visible in both versions.

## Preservation and validation

[Preservation evidence](graphics/preservation.json) records byte-identical hashes for all 16 generation, coordinates, world streaming, flight and activity source files. The nine-scale, three-mode native round trip records 17 proofs of unchanged geometry/camera/generation, including matching return identities. All 17 Original-mode world crops are pixel-identical to 0.15; [comparison data](graphics/original-image-comparison.json) records the crop and per-view results.

The new surface-shading test covers octahedral packing over 20,000 directions, duplicate-vertex seams, sharp creases, immutable positions/colours/indices, equivalent 16/32-bit indices, scale invariance from 10^-12 to 10^12, cancellation and degenerate triangles. The existing native fixtures cover flight, approach, landing, launch, shrinking, streamed boundaries, all nine scales, all three rendering modes and atomic visibility.

Final release results and before/after timings are recorded in [validation.json](graphics/validation.json) and [release-tests.log](graphics/release-tests.log). Metal is tested natively; Metal, SPIR-V and GLSL shader variants are rebuilt with the project's pinned shader compiler. The macOS package retains the existing save locations and schemas.

At 1440×900 on the same Metal host and deterministic 1,791-frame flight fixture:

| Measurement | 0.15 | 0.16 |
| --- | ---: | ---: |
| Median frame | 16.661 ms | 16.670 ms |
| 95th percentile frame | 17.500 ms | 17.524 ms |
| Stationary orbit CPU median | 2.756 ms | 2.872 ms |
| Stationary orbit GPU median | 8.253 ms | 9.225 ms |
| Peak process resident memory | 148.33 MiB | 151.27 MiB |
| HDR/bloom targets | 17,856,000 bytes | 17,856,000 bytes |

The observed frame rate remains about 60 fps under VSync. The sampled orbit's extra GPU cost is 0.97 ms; peak resident memory grows by 2.94 MiB. Orbit samples are the first uninterrupted Hyperion block, trimmed by five frames at each end (19 samples); these are single-run observations. The additional shared atom display mesh occupies 39,440 bytes. Recorded geometry/activity GPU storage peaks at 1,777,228 → 1,993,012 bytes, with the existing cache budget unchanged.
