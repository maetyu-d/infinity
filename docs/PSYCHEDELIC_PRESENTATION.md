# Psychedelic presentation — Infinite 0.15

Infinite now renders its existing universe with the psychedelic branch of the user's Hyperion game. The previous port explicitly set `psychedelic` to zero and mixed much of the old vertex colour back into the result. This release enables the actual interference tapestry, stronger material pigments and hue-preserving highlights, while retaining dark gaps and physical lighting contrast.

Checkpoint `db01c26` preserves the working 0.14 cockpit release. Its executable is frozen as `build/infinite-aesthetic-baseline-0.14`. **No universe geometry, coordinates, identities, seeds, generator results, histories, or ship dynamics are changed.**

## Actual Hyperion source

The reference remains `/Users/user/Documents/SC tutorial/Hyperion`, the user's native game. The source shaders were inspected again and the running game was opened to compare the luminous ribbons, dark space and material contrast. Its look preference was left unchanged. [Reference source hashes and preservation evidence](psychedelic/preservation.json) record the files used.

| Source | Application to Infinite |
|---|---|
| `src/flight/shaders.hpp`, `PRISM_PIGMENT_GLSL::ecstatic`, `silk`, `thread`, `pigment` | The actual warped interference folds, spectral sheets, filtered fine fibres and exponent-1.85 psychedelic pigment palette. |
| `skyFragment` | Direction-anchored folded light with ink gaps and dust extinction. Infinite's existing host galaxy supplies orientation, palette key and gas abundance. The shader does not synthesize stars or celestial objects. |
| `worldFragment`, psychedelic mineral and filtered-vein branches | Material pigments, terrain-anchored interference seams and coloured grazing light on the existing meshes. Terrain colour follows its actual sampled radial elevation, rather than an unrelated replacement height field. |
| `src/flight/surface_detail.hpp::closeMaterial` | Filtered close mineral grain, seam colour, small emissive accents and opalescent grazing sheen. Existing material coordinates and inherited material response remain authoritative. |
| `atmoFragment` | Spectral scattering tint within the existing depth-limited atmospheric ray integration. Existing inherited warmth and water still gate the atmosphere. |
| `postFragment`, psychedelic branch | Saturation 1.19, bloom weight .82, peripheral subpixel colour fringes and .86 hue-preserving ACES highlight blending. The cockpit and instruments are composed afterwards. |

Infinite adapts brightness because its vertex colours already include baked illumination. Dark gaps, reduced broad albedo and derivative-filtered veins preserve shape and depth instead of covering every surface in a uniform bright wash. The standard and psychedelic source algorithms remain recognisable; Hyperion's world generation, buildings, planetary geography, stars and simulation are not imported.

## Different treatment at each scale

- **Cosmic web and galaxy:** the original generated particle positions and density remain intact. Gas and filament instances receive spectral optical materials; real stars retain their temperature colours and get smaller bright cores with bounded halos. No fake constellation or replacement galaxy is generated.
- **System and planetary flight:** a read-only optical sky is anchored to the current galaxy's axes. It remains stable when the camera changes precision frames. The actual star, planet, moon, harbour and traffic geometry is drawn in front of it with ordinary depth and occultation.
- **Planets and terrain:** shared planet coordinates keep the same pigment phase across orbit, surface and mountain views. Existing radial elevation supplies the broad mineral/contour organisation. Broad sphere lighting is smoothed while steep geometric slopes remain visible; vertices, silhouettes and LODs are untouched. Close grain and seams use the existing reduced, periodic material coordinates.
- **Rocks and minerals:** existing host-rock coordinates and inherited keys keep seams and sheen attached to their actual specimens. Roughness and metallic response still depend on the existing mineral. The treatment changes shading, never density, fractures or grain placement.
- **Atomic lattices:** element body colours, all sites, defects and bonds remain identifiable. Atoms receive lighting and a restrained grazing sheen, rather than decorative fibres. The optical sky fades out smoothly as magnification reaches microscopic scales, leaving the specimen and host matter as the visual context.

`ORIGINAL`, `HYPERION` and `HYBRID` remain available through **H**. HYPERION uses the new psychedelic presentation; HYBRID reduces its strength. Changing mode only changes presentation state. It does not invalidate geometry or generation caches, alter the camera, resize the ship or reset activity time.

## Rendering boundary and cost

`ScenePresentation` still holds a const reference to the original geometry. Its additional material domain describes the existing object class and inherited environment. `SpectralSky` contains only palette, gas, opacity and camera-to-field orientation. The shared fragment implementation now lives in `hyperion_material.sh`, used by both flight and explorer shader paths.

The optical sky uses one fullscreen triangle before all world geometry, writes no depth, and allocates no texture or framebuffer. Existing gas classifications fit in the unused alpha channel of the existing 80-byte billboard instances; original-mode instance data retain their former values. The HDR/depth and two reduced-resolution bloom targets retain their previous size. Worker limits, geometry caches, upload budgets and GPU mesh representations are unchanged.

The shaders compile for Metal, SPIR-V and GLSL using the existing pinned bgfx shader compiler. The source/binary manifest is updated. Metal is exercised natively here; other backend binaries are compiled, without a claim of runtime validation on those platforms.

## Validation and comparisons

All 16 checked coordinate, generation, streaming and ship/activity files remain byte-identical to 0.14. The frozen legacy-world descriptors/geometry and the deterministic planet/moon population tests still pass. Presentation tests cover saturated HDR highlight retention, unchanged geometry references and fingerprints, identical pigment orientation after a precision-frame change, inherited material domains, and restoration after atomic-scale inspection.

The native flight fixture compares the same camera and submitted geometry in all three modes, tests actual celestial/harbour picking, lands and launches, shrinks through micro/nano scales, renders a real moon beside its primary, and checks the cockpit guide. Automated capture windows now keep their requested pixel size; normal play remains resizable.

[Planetary orbit](psychedelic/captures/busy-orbit.png) · [Stellar sky](psychedelic/captures/stellar-background.png) · [Moon family](psychedelic/captures/moon-family.png) · [Landing](psychedelic/captures/landed.png) · [Atomic lattice](psychedelic/captures/atomic-shrink.png)

The same normal-orbit scene before this change is preserved in the [0.14 capture](cockpit/captures/busy-orbit.png). The original Hyperion reference images remain in [flight](hyperion/reference-flight.png) and [galaxy](hyperion/reference-galaxy.png).


## Measured rendering cost

Same 1440 × 900 Metal native flight fixture, seed and fixed visual time. The 0.14 baseline and final 0.15 loop each contain 1,791 frames, including the three identical orbital views, landing/launch, micro/nano flight, real moon family and control guide. The stationary interval is the first contiguous Hyperion block with five transition frames removed at either end. These are single-run observations; VSync limits median wall time.

| Measurement | 0.14 | 0.15 |
|---|---:|---:|
| Complete-loop median frame | 16.66 ms | 16.66 ms |
| Complete-loop p95 frame | 17.21 ms | 17.50 ms |
| Stationary Hyperion render CPU median | 3.02 ms | 2.76 ms |
| Stationary Hyperion GPU median | 3.48 ms | 8.25 ms |
| Effect render-target storage | 17,856,000 bytes | 17,856,000 bytes |
| Peak process RSS | 146.95 MiB | 148.33 MiB |

The visual work adds GPU cost while preserving the measured 60 FPS envelope on this Mac at this resolution. Peak RSS increases about 1.38 MiB. No render-thread generation or new texture cache is introduced. Higher resolutions and other GPUs can have different costs; HYBRID and ORIGINAL remain selectable. [Raw measurements](psychedelic/validation.json) and frame CSVs retain the evidence.

All **27 release tests passed in 189.11 seconds**. The native mode tour produced 51 full-HD images and 17 exact camera/geometry/generation proofs across nine scales and the return journey. The signed 0.15 app retains ship/activity save schemas 3/1. Additional examples: [cosmic web](psychedelic/captures/cosmic-web.png), [galaxy](psychedelic/captures/galaxy.png), [rock](psychedelic/captures/rock.png), [grain](psychedelic/captures/grain.png), [lattice](psychedelic/captures/lattice.png).

As an additional preservation check, all **17 ORIGINAL world-image crops match the 0.14 baseline pixel-for-pixel** (1920 × 660 world region, excluding the diagnostic HUD). [Per-view image comparison](psychedelic/original-image-comparison.json) records zero channel error for every stop.
