# Close material rendering — Infinite 0.17

The next graphics pass ports the original Hyperion game's packed close-material texture. Existing terrain, rocks, mineral grains, cavities and lattice geometry remain authoritative. The source checkpoint is `b0dfeb7` (Infinite 0.16).

## Source and translation

The source is `../Hyperion/src/flight/surface_detail.hpp`: `detailBakeFragment` constructs a periodic cellular field with skipped cells, nearest/second-nearest mineral boundaries, domed grains, chips and fine grain. Its four channels store height, fracture, mineral tint and grain. `closeMaterial` samples that field in three planes. `../Hyperion/src/flight/renderer.cpp::lookupNoise` supplies the source's exact integer lookup; `renderer.cpp` builds mipmaps and enables anisotropic filtering for its highest graphics quality.

Infinite translates the bake into C++20 and runs it once in an existing oneTBB background task. The shader samples a shared 512×512 RGBA8 texture, including every mip level through 1×1. Compared with Hyperion's larger quality-dependent textures, this uses **1,398,100 bytes (1.33 MiB)**. There is no per-object texture allocation or extra render target.

The original generated vertex colours, mineral type, temperature, water activity, strain and inherited material phase cross the read-only adapter. Source-inspired close shading uses the texture's mineral relief, dark fractures, frost and roughness. Frost respects temperature; strain controls fracture contrast. The effects change illumination and surface colour only. The generator still determines all geometric cracks, cavities and mineral boundaries.

## Stability and resource handling

- Baking uses the source's fixed integer lookup and never reads or advances universe random streams. The result is shared across every scale.
- One cancellable task owns the temporary CPU image. The render thread polls its completion and uploads the result without waiting. Destruction cancels and joins the bake before releasing GPU resources.
- The upload is charged against the existing per-frame static upload budget. `material_bytes` in the frame profile exposes the resident image separately from HDR targets and geometry caches.
- Three explicit-gradient planar samples use full mipmaps and anisotropic filtering. Unresolved detail fades to its mean; atom sites and bonds retain their existing presentation.
- Planet detail uses the existing 8 m coordinate spacing, with 16 m texture repetition. Rocks use their inherited host-rock spacing. Both sampling frequencies divide the existing modulo-64 floating-origin period exactly. The material phase comes from the existing inherited key, so zooming and rebasing cannot replace the material field.
- Original mode retains its previous rendering. Hyperion and Hybrid use the new material field in both cockpit flight and free inspection.

## Verification

The material test checks deterministic repeated baking, cancellation, the complete fixed-size mip chain, periodic wrap boundaries, origin-period invariance, distinct fracture/mineral regions, fracture relief and conserved mip averages. Presentation tests verify inherited material inputs alongside geometry/descriptor/camera immutability.

Native checks cover the flight/approach/landing/launch loop, shrinking to atomic scales, streamed boundaries and the complete three-mode galaxy-to-atom round trip. The cockpit fixture also checks that the baked image was published and stayed within its fixed GPU budget. [Preservation evidence](material-detail/preservation.json) records the 16 unchanged generator, coordinate, streaming, ship and activity files.

Final results, images and measurements are recorded in `material-detail/` after the release run. The procedural recipes and save schemas are unchanged.

## Measured rendering cost

Same deterministic 1,791-frame piloting fixture at 1440×900 on Metal:

| Measurement | 0.16 | 0.17 |
| --- | ---: | ---: |
| Median frame | 16.669 ms | 16.662 ms |
| 95th percentile frame | 17.524 ms | 17.984 ms |
| Stationary orbit GPU median | 9.225 ms | 7.593 ms |
| Surface-frame GPU median | 8.448 ms | 8.688 ms |
| Peak resident memory | 151.266 MiB | 153.484 MiB |

Median performance remains about 60 fps under VSync. The orbit sample becomes cheaper after replacing procedural shader noise; the surface-frame sample adds roughly 0.24 ms while displaying filtered fracture relief. Peak process memory grows by 2.22 MiB. HDR/bloom targets remain 17,856,000 bytes; the new shared material image occupies 1,398,100 bytes.

These are single-run observations. The orbit sample is the first uninterrupted Hyperion block, trimmed by five frames at each end (19 frames). The surface sample comprises 1,233 Hyperion frames with a SURFACE anchor and no active navigation transition. Those phases include surface flight and shrinking, so the aggregate is not an isolated material-shader benchmark. [Raw profiles and validation](material-detail/validation.json) retain the limits and measurements.

## Image comparisons

| View | 0.16 | 0.17 |
| --- | --- | --- |
| Landed | [Before](graphics/landed.png) | [After](material-detail/captures/landed.png) |
| Landing approach | [Before](graphics/landing-approach.png) | [After](material-detail/captures/landing-approach.png) |
| Rock overview | [Before](graphics/6-ROCK-HYPERION.png) | [After](material-detail/captures/rock.png) |
| Atomic ship | [Before](graphics/atomic-shrink.png) | [After](material-detail/captures/atomic-shrink.png) |

Release validation: **29/29 tests passed**. All **17/17 Original-mode world crops are pixel-identical** to 0.16. The three-mode round trip verified 51 images and 17 unchanged scene/camera/generation checkpoints across nine scales. [Detailed results](material-detail/release-tests.log), [Original image proof](material-detail/original-image-comparison.json).
