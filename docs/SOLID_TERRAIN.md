# Solid-planet terrain — Infinite 0.18

Selected expanded solid planets now have local highlands, volcanic ridges, incised valleys or ice ridges. These are generated height fields used by the geometry and flight systems. Terrain is visible from approach, supports collision and landing, and stays attached to the same planet through deeper inspection.

Use **Y** to select a world labelled **Highlands**, **Volcanic**, **Canyons** or **Ice ridges**, **J** to approach it and **L** to land at a gentle site. The existing ship, camera and saved-flight controls are retained.

## Availability and compatibility

The new layer applies to 80% of the expanded primary planets, selected by an isolated deterministic feature key. Each system's original 5–9 planets and all moons retain their prior geological fields. Smoother expanded worlds retain their existing broad geology. Planet addresses, seeds, radii, orbits, populations, chemistry and procedural histories are unchanged. Existing plate tectonics, impacts, broad erosion and all rock/mineral/atomic generators remain in place.

The base generation recipe remains 3 and population recipe remains 1. The additive local terrain recipe is 1, and its parameters are included in descriptor fingerprints only for worlds that use it. All **120 records** in the pre-expansion descriptor/geometry snapshot still match exactly. `baa0425` and branch `checkpoint/infinite-0.17-before-terrain` preserve the 0.17 checkpoint.

The current catalogue models condensed rocky/icy bodies. An explicit `GasEnvelope` process and `hasSolidSurface` contract now exclude gaseous descriptors from terrain queries, terrain children, streamed patches, collision ground queries and landing. Gas envelopes use an undeformed sphere representation. This update does not reclassify existing worlds or add a gas-giant population.

## Generation

The existing spherical geological height is refined with a planet-coordinate local field. There are no tile-local seeds or replacement scenes. The existing first plate's Euler motion supplies the terrain fabric direction; the isolated local-relief key supplies reproducible detail.

| Terrain | Constraints and shape |
| --- | --- |
| Highlands | Warped ridges and hills, with uplift strength conditioned by plate convergence. |
| Volcanic | Sharper ridges and rough cooled relief, concentrated by the existing hotspot field and remaining thermal activity. |
| Canyons | Narrow incised channels and subdued hills on hydrated worlds; weathering follows liquid-water availability and age. |
| Ice ridges | Lower-amplitude folded ridges, conditioned by shell extension and shear. |

Characteristic wavelengths range from 500 m to 12 km, scaled by body radius. Relief is bounded and weathering reduces roughness. These are analytic landform and erosion proxies, rather than a fluid or climate simulation. In the synthetic regime tests, nearby local height ranges span roughly 480–850 m. The four real catalogue flyovers span roughly 330–990 m.

`geologySample` supplies one height to orbital meshes, streamed terrain, local sections, surface addresses, normals and pilot ground/landing queries. Rock and atomic children retain their existing identities and refine their actual host terrain. New-world regeneration and the complete terrain-to-lattice return are tested for both descriptors and geometry.

## Streaming and presentation

The terrain planner now measures distance to the clamped cube-face footprint rather than subtracting an overly broad tile radius. It refines coarse neighbours before a dependent fine patch, keeping 2:1 balance while allocating the existing budget. The final balance pass no longer discards the closest detail. Six-face coverage, canonical shared vertices, stitches and geomorphs remain intact; the default patch budget stays 192.

Rugged patches carry normals from the generated height gradient. They are calculated with the existing background generation task and packed into four bytes per rendered vertex. Grazing normals receive a conservative view adjustment to avoid reversed sun lighting on a coarse LOD. No vertex displacement or screen effect supplies the landforms. Original, Hyperion and Hybrid consume the same generated positions; Hyperion's existing material treatment remains available.

## Validation and images

The dedicated tests cover all four regimes, nearby relief, continuity, deterministic regeneration, legacy worlds, orbital placement, exact mesh/height agreement, terrain-normal agreement, near-camera LOD allocation, gas exclusion, safe landing and the solid-world-to-atomic-lattice return.

The native terrain fixture uses four actual catalogue worlds at 1440×900, with identical cameras and geometry in three modes. Capture requests remain in the same mode until the framebuffer is delivered. The full suite also exercises the existing landing/launch/shrinking loop, streaming seams, automatic scale transitions and the nine-scale three-mode round trip.

- [Highlands](solid-terrain/captures/0-Highlands-HYPERION.png)
- [Volcanic terrain](solid-terrain/captures/1-Volcanic-HYPERION.png)
- [Canyons](solid-terrain/captures/2-Canyons-HYPERION.png)
- [Ice ridges](<solid-terrain/captures/3-Ice ridges-HYPERION.png>)

[Validation data](solid-terrain/validation.json), [native terrain proof](solid-terrain/terrain-native.log) and [release results](solid-terrain/release-tests.log) record the actual worlds, seeds, measurements and test outcomes. Run `build/infinite --terrain-test --capture-dir build/terrain-demo/captures --data build/terrain-demo/data` to reproduce the four-world native demonstration without using the normal saved-flight files.

## Measured cost

The four-world terrain demonstration measured a median frame of 16.664 ms and a 95th percentile of 18.205 ms at 1440×900 on Metal (1,582 frames), with peak resident memory of 136.39 MiB. Median performance remains about 60 fps under VSync. No cache, patch, worker or render-target budget was increased.

The existing flight fixture measured 16.662 → 16.663 ms median frame time and 17.984 → 17.102 ms at the 95th percentile. Its sampled orbit GPU median increased from 7.59 to 8.63 ms, while peak resident memory measured 153.48 → 151.94 MiB. The fixture follows the same seeded actions and fixed visual time, but readiness waits changed the frame count from 1,791 to 1,817. These are single-run observations, with the orbit median drawn from 19 frames and the new terrain demonstrated separately. Raw profiles retain those limits.

Release validation: **32/32 tests passed**. The legacy snapshot remains exact, all four new terrain types were captured in three modes, and the existing galaxy-to-lattice journey still returns matching identities.
