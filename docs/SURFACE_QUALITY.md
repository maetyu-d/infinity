# Surface graphics, Infinite 0.23

This pass implements the five surface improvements identified from the screenshot: field normals, separate albedo and illumination, material detail between orbital and close views, terrain sunlight shadows, and climate-scaled atmosphere/daylight presentation. It changes presentation above the existing universe. Original mode, terrain heights, meshes, collision, identities, seeds, climates and flight dynamics remain authoritative.

## Actual Hyperion source

The reference is the user's game at `../Hyperion`. Source hashes are recorded in [preservation.json](surface-quality/preservation.json).

- `src/flight/renderer.cpp::lookupNoise`: the exact periodic integer lookup and offset colour channels, recreated as one shared 256 × 256 RGBA8 image.
- `src/flight/shaders.hpp::noise` and `src/flight/surface_detail.hpp::closeMaterial`: smooth interpolated strata/deposits, filtered bedding, mineral pigments and bounded material relief. Rock-cell detail remains at close range; enlarging those cells into landscape tiles was rejected after native inspection.
- `src/flight/lighting.hpp::PRISM_NEAR_SUN_GLSL`: receiver-plane-corrected sun-shadow filtering. Infinite extends its existing bgfx light map to existing terrain triangles.
- `src/flight/shaders.hpp::atmoFragment`: occupied ray spans, exponential density, sun optical depth and Rayleigh/Mie phases. Infinite supplies its own retained planetary climate and opaque depth.

The [original Hyperion flight capture](hyperion/reference-flight.png) remains the visual reference. Infinite retains its own planet geometry and orbital population, so surface silhouettes and composition differ deliberately. The folded spectral sky, pigments, restrained mineral emission, filtering and lighting derive from the source techniques; no Hyperion world generator is imported.

## Lighting and material boundary

Terrain workers now retain the unlit `Universe::terrainAlbedo` alongside field-gradient normals for **all** solid-world tiles, including legacy planets and moons. The original baked vertex colour, position, index order and edge stitching are preserved exactly. Hyperion/Hybrid choose the read-only albedo and normal streams; Original still submits its original colours.

Smooth strata cover kilometre and intermediate distances, with 4 km, 1 km, 256 m and 64 m noise wavelengths. The existing 16 m triplanar close-material texture supplies resolved grain, fractures and frost. Derivatives filter unresolved features, mineral/strain/temperature values remain inherited, and broad planetary spectral lines no longer dominate the ground. Small pigment deposits, seams and grazing colour retain the Hyperion palette.

Full-resolution testing also exposed overlapping hierarchy patches: nearby surface/mountain catalogue meshes could draw over streamed tiles of the same height field. Hyperion/Hybrid now select the streamed representation for that entire covered field. The original addressed meshes remain generated, cached and selectable; rocks, cavities and finer structures are still rendered. Original retains its previous presentation. A pure coverage test protects this boundary.

Material frames subtract the camera in doubles before conversion to floats. The close frame wraps at 512 m; the strata frame wraps at 1,048,576 m. Every octave and bedding cycle divides its frame period. World position, magnification and origin changes therefore do not randomize the surface. The noise lookup wraps its integer address before adding the fractional sample, avoiding float quantization at large coordinates. Close-grain filtering uses the largest screen-space footprint and fades normal relief with unresolved detail. Material relief changes shading only.

## Sun shadows and depth

The existing **2048² D16, 8 MiB** sun map now includes the exact current, geomorphed terrain triangles and nearby building/prop instances. Caster visibility uses the light frame independently of the camera frustum. The snapped, power-of-two span follows terrain-relative approach altitude from 2 km to 262 km; terrain shadows operate below 60 km AGL when sunlight is above the horizon. Microscopic/shrinking views do not inherit this field.

Tile/stitch changes, geomorphing, snapped movement and light changes invalidate the map. Stationary views reuse it. Incomplete transient uploads prevent reuse of a partial map. This adds no second shadow texture or full-resolution colour buffer. At larger spans small building shadows lose resolution; near landing, a smaller span restores detail.

Normal Hyperion flight uses a bounded physical near plane, up to 10 m when far above terrain, to improve distant opaque-depth reconstruction. Cockpit projection is separate; Original and shrinking retain their previous range. Coordinates, camera pose, collision and navigation do not change. The atmosphere and shadow receiver use the actual current near plane.

## Atmosphere and sky

The optical ray marcher converts distances from planet radii to actual kilometres. Retained pressure and temperature supply the density proxy, and retained scale height supplies its bounded exponential profile. Per-channel transmission attenuates distant surfaces and the background; the old scalar opacity and hard-coded 9 km conversion are removed. The spectral atmospheric tint is restrained so material and relief remain legible.

Daylight attenuation of the optical galaxy field depends on local atmospheric density and sun elevation. Night, airless worlds and space preserve that field. The underlying addressed stars, planets and galaxies remain present. This remains an eight-step optical approximation with two sun samples, rather than a complete atmospheric simulation.

## Matched native images

The fixture uses the screenshot's **Planet 93612** and **Highlands 63563**, with the same orientation and physical camera placements before/after: 250 m, 2 km and 11.8 km above the existing field. It captures Original, Hyperion and Hybrid without moving the camera or regenerating the universe. A second fixture repeats those views through a canonical surface address at **2840 × 1800**. [Retina capture](surface-quality/after-retina-surface-frame.png), [frame comparison](surface-quality/retina-comparison.json). The 1024 × 512 ground comparison against the same physical planet-frame reference has mean absolute channel error 0.397/255 and maximum error 5/255, including the final filtering change.

| View | Before | After |
| --- | --- | --- |
| Planet 93612, 11.8 km | [Before](surface-quality/before-0-2-Plains-HYPERION.png) | [After](surface-quality/after-0-2-Plains-HYPERION.png) |
| Planet 93612, 250 m | [Before](surface-quality/before-0-0-Plains-HYPERION.png) | [After](surface-quality/after-0-0-Plains-HYPERION.png) |
| Highlands, 250 m | [Before](surface-quality/before-1-0-Highlands-HYPERION.png) | [After](surface-quality/after-1-0-Highlands-HYPERION.png) |

The fixed-camera shadow-off diagnostic retained all 18 terrain/planet hashes. On Highlands it changes 265, 1,650 and 10,793 RGB components by more than one level in the same ground crop at 250 m, 2 km and 11.8 km respectively (maximum difference 62/255). The smooth plains produce no terrain-shadow difference, as expected. [Measurements](surface-quality/shadow-comparison.json), [Highlands with shadows disabled](surface-quality/shadow-off-1-2-Highlands-HYPERION.png).

Also inspected: [city approach](surface-quality/after-1-city-approach-HYPERION.png), [street inhabitants](surface-quality/after-2-inhabitants-HYPERION.png), and all three heights/modes in the native fixture. An existing smooth world remains smooth; this pass does not add mountains to its generator.

## Preservation and validation

`checkpoint/infinite-0.22-before-surface-pass` preserves baseline **b92a8b7**. [Source preservation](surface-quality/preservation.json) verifies 16 byte-identical generation, geometry, simulation, history, city and navigation source files. `Streaming.cpp` only retains additional read-only lighting metadata.

CPU tests compare **18 baseline terrain mesh fingerprints**, including original positions, colours and indices, across three LODs and three stitch masks on legacy and rugged worlds. They also check field normals, unlit albedo, metadata-independent identity, periodic material frames through rebasing/magnification, lookup continuity and daylight behaviour. Native tests verify identical terrain/planet hashes and serialized cameras across rendering modes. All **six Original terrain crops are pixel-identical** to baseline (640 × 210 pixels, excluding cockpit/HUD); see [comparison.json](surface-quality/comparison.json).

**All 41 tests pass in 332.24 seconds**, including the full-resolution surface-frame fixture, city/inhabitant/weather images, landing/departure, streaming, shrinking and galaxy-to-atomic rendering-mode return journeys. The image-budget checks account for the exact 256 KiB lookup allocation. [Full test log](surface-quality/full-tests.log).

Shader binaries and their manifest were rebuilt for Metal, SPIR-V and GLSL. Metal is exercised natively on this Mac; other backends are compiled, not claimed as runtime tested.

```sh
python3 tools/compile_shaders.py --shaderc build/shader-tool/shaderc
cmake --build build -j 4
ctest --test-dir build --output-on-failure
# Additional fixed-view diagnostic; does not affect saved play:
build/infinite --terrain-quality-test --no-sun-shadows --capture-dir build/shadow-off/captures --data build/shadow-off/data
```

## Measured cost

1440 × 900, native Metal, VSync enabled, one before and one after run. Values below are medians of the last 16 settled Hyperion frames at each of six matched views (90 qualifying frames/run). This is a diagnostic comparison, not a controlled throughput benchmark. Raw profiles: [before](surface-quality/before-frames.csv), [after](surface-quality/after-frames.csv).

| Metric | Before | After |
| --- | ---: | ---: |
| Frame interval | 16.63 ms | 16.62 ms |
| CPU frame | 8.81 ms | 8.81 ms |
| GPU frame | 7.26 ms | 7.73 ms |
| Shared material textures | 1,398,100 B | 1,660,244 B |
| Maximum CPU generation cache | 10,414,608 B | 10,325,232 B |
| Scene/effect targets in these views | 17,856,000 B | 26,244,608 B |
| Peak process RSS | 149,504,000 B | 146,948,096 B |

The new lookup adds **256 KiB** to the global material budget. These previously empty terrain views now allocate the already-supported 8 MiB shadow map; the renderer's maximum shadow-target budget is unchanged. Extra tile metadata is accounted in cache residency and remains subject to existing eviction. Terrain GPU positions/indices remain transient; no per-tile textures or persistent terrain GPU meshes are added. Frame pacing remains near 60 Hz here, with approximately 0.47 ms more measured GPU work.
