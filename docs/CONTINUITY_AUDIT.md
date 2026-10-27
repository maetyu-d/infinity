# Cross-scale continuity audit — recipe 2 / application 0.2.0

Historical validation for application 0.2.0 / recipe 2. Current generating processes and evidence are in [PROCESS_DIVERSITY.md](PROCESS_DIVERSITY.md).

The hierarchy now uses the same addressed objects and fields from the galaxy to the atomic specimen. Scale changes affect sampling, representation and the inspection window; they do not choose a new object seed. This audit includes the failures found during implementation, the fixes, executable regression coverage and remaining limits.

## Findings and fixes

| Area | Discontinuity found | Implemented behaviour |
|---|---|---|
| Addresses | Tile IDs with unused high bits could alias the same spatial tile. Parsing accepted incomplete or noncanonical paths. | Canonical signed root cells, ordered typed paths, strict delimiters and exact tile round trips. Invalid planet indices are rejected before orbital calculations can overflow. |
| Coordinates | Distant galaxy samples stored positions as floats, losing detail before camera-relative conversion. First divergent translations were scaled before subtraction. | Double-precision sample positions, addressed system IDs and a point transform that subtracts the first divergent terms in local units before scaling. Only final GPU coordinates become floats. |
| Navigation | Backspace selected child zero, losing a deliberately chosen system/planet/rock. Ancestor camera poses were not retained. Outward zoom stayed anchored to an atom indefinitely. | Remembered branch selections and exact local camera views; physically invariant rebasing; outward zoom promotes frames when the parent is numerically well-conditioned. |
| Outward focus | Interpolating a parent-sized offset in atomic render units could send the camera beyond the parent. | Expand around the departing child first, then recenter when the parent becomes visible. Saved parent views are restored exactly. |
| Persistence | Only the current camera was saved, so the return path could not survive restart. | Versioned `INFINITE_NAV_2` state retains bounded breadcrumbs and selections. Legacy single-view saves still load. Separate writer/reader processes test a restart at the lattice followed by the full ascent. |
| Terrain frames | Recomputing tangent bases could flip heading at a polar basis seam. | Child tangent frames are parallel-transported from the parent; height, material colour and lighting normals use the same planet-space fields. |
| Terrain LOD | Region grids used unrelated resolutions and region-specific colour noise. | Nested power-of-two samples and shared planetary colour/normal queries. Fine grids retain the coarse sample locations. |
| Stellar appearance | A galaxy dot and its detailed star used different colour recipes. | One temperature-based stellar colour function and the same physical system descriptor. The coarse addressed emitter hands over to its detailed representation at the same location. |
| Rock → grain | A ray search assumed the rock centre was solid; the detailed grain was not constrained to the host density. | Bracket the outermost real solid crossing, intersect the grain cell with the host rock SDF, and use those same grain descriptors for coarse material patches. |
| Grain → lattice | The old lattice origin could lie outside the grain. Vacancy randomness depended on enumeration order. | Choose an interior specimen, retain inherited mineral identity, and address sites by integer unit cell and motif. Vacancy streams use site identity; changing query bounds preserves existing sites, vacancies and bonds. |
| LOD presentation | Completed background meshes replaced the current representation immediately. | Screen-size hysteresis, canonical detail levels for invariant geometry, retention during generation, and a reversible 0.3-second geometry crossfade. Blending rejects different procedural identities. |
| Surrounding context | Oversized ancestors faded away completely. | Background-generated local sections resample the actual enclosing object's density. Terrain remains a surface; microscopic inspection opens a labelled cavity in the enclosing rock or grain. These are view-dependent cutaways of the same object. |
| Render validation | Matching seeds and submitted atom counts could pass even when an opaque section hid every atom. | The GPU fixture now checks final framebuffer pixels. It caught this bug; sections round their sampling bounds upward, and stale windows cannot occlude the focused specimen. |

## Seed and recipe compatibility

The original hierarchical seed hash and address IDs are preserved. Eight golden seeds pin the previous default galaxy-to-lattice path. New feature streams are domain-separated from that hierarchy; neither LOD, cache contents, worker scheduling nor visitation order enters an object's seed.

Recipe **2** intentionally corrects geometry derived from those identities: transported terrain frames, grain/host intersections, interior lattice placement, formation-event ages and site-addressed vacancies. Old recipe-1 mesh bytes and vacancy arrangements are therefore not claimed to be unchanged. Existing discovery addresses retain their identity. CPU/GPU geometry caches are transient and rebuild automatically. New camera state is versioned independently from SQLite's unchanged sparse-storage schema.

Every geometry result carries its address, seed, complete descriptor fingerprint, recipe version, detail level and reference frame. A local section additionally identifies its enclosing object, focus reference and sampling scale. It never introduces a replacement universe seed.

## Automated coverage

The final configured desktop build passed **8/8 CTest tests**:

1. Existing foundation regressions: **250 checks**.
2. Sparse persistence writer process.
3. Sparse persistence reader process.
4. Full cross-scale regression: **45,976 checks**, including three universe seeds, non-default branches, extreme signed root cells, every canonical LOD, projected invariance during rebasing, exact restored camera/selection state, nested terrain samples, SDF convergence, local-section reproducibility, parallel regeneration and cache eviction.
5. Deep-navigation restart writer.
6. Independent restart reader: return from lattice to galaxy while checking descriptors, all LOD hashes and saved views.
7. Real Metal native tour at **1920×1080**: eight objects, seven revisits, fifteen settled stops. Submitted geometry is compared with independent regeneration of all LODs in background workers.
8. Native capture validation: correct dimensions, matching return addresses and visible atomic pixels. The quartz image contains **121,028 oxygen-red pixels** in the central image region, excluding interface text. The deliberately occluded earlier image failed with zero.

The native all-LOD/descriptor/site proof digest for this build and default universe is `2126210663436087699`. This digest is a local regression result, not a cross-compiler floating-point guarantee.

Evidence: [CTest results](continuity-tests.log), [native proof and addresses](native-continuity.log), [pixel check](continuity-pixel-check.txt), [individual frames](continuity-frame-profile.csv), [visible lattice](continuity-captures/lattice.png), [grain and enclosing rock](continuity-captures/grain.png), [planetary surface](continuity-captures/surface.png).

The full verification tour also regenerates all recipes and writes fifteen lossless captures. Its measured median frame time was 16.72 ms and P95 was 47.70 ms; it is deliberately heavier than ordinary navigation and is not evidence of a sustained 60 FPS guarantee. Peak resident memory was approximately 145.8 MiB.

## Reproduce

Headless continuity and restart tests:

```sh
cmake -S . -B build -DINFINITE_GPU_TESTS=OFF
cmake --build build --target infinite_tests infinite_continuity_tests -j 4
ctest --test-dir build --output-on-failure
```

Complete desktop validation, including the native renderer and pixels:

```sh
cmake -S . -B build -DINFINITE_VIEWER=ON -DINFINITE_GPU_TESTS=ON
cmake --build build --target infinite infinite_tests infinite_continuity_tests -j 4
ctest --test-dir build --output-on-failure
```

The GPU pair requires an active desktop. On this Mac, `ctest` is `/Users/user/.local/opt/cmake/CMake.app/Contents/bin/ctest`. Native artifacts are written below `build/native-continuity/`. `infinite --smoke-test` also enables the complete background regeneration proof without CTest.

## Remaining limits

- Terrain meshes share nested samples and continuous scalar fields, but neighbour-balanced streaming, crack stitching and full geometric morphing are still pending. Timed blending is not a proof that every intermediate mesh approximation is exact.
- Local rock/grain cavities are explicitly an inspection visualization. They preserve material and spatial provenance; they do not model optical microscopy, excavation or molecular dynamics.
- Tests cover three complete seeded branches and all four lattice motifs. They do not exhaust the address space. Windows/Linux GPU execution and cross-compiler floating-point fixtures remain unverified.
- Adjacent root-cell streaming and persistent terrain/matter edits remain separate future work.

The next continuity task is neighbour-balanced cube-sphere streaming with stitched mesh edges and screen-error bounds, using these shared fields and regression proofs.
