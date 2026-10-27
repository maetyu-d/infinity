# Validation and comparisons — Infinite 0.5

The packaged `Infinite.app` is version **0.5.0**, ad-hoc signed and verified. Its compiled Mach-O text matches the tested executable. The tested machine is Apple M1 / arm64 / macOS 26.4.1 / bgfx Metal. [Preservation records and executable hashes](preservation.json) identify Git checkpoint `0d61030` and verify 17 unchanged generator, coordinate, history, cache, navigation and dependency files. Generation recipe remains **3**.

## Preservation and native validation

**13/13 tests passed in 71.06 seconds.** [Complete CTest output](tests.log).

- All existing CPU determinism, physical-process, coordinates, LOD, cancellation, eviction, persistence and restart checks pass.
- The new presentation test passes **2,874 checks**, including three seeds, nine scales, all three modes, shared planet/host-rock material frames and pigment phases, unchanged descriptors/geometry/camera, and fresh regeneration on the return trip.
- The existing independent native all-LOD continuity proof and atomic final-pixel checks pass. [Native continuity log](native-continuity.log).
- The native mode journey passes **17 stops / 9 scales / 51 true 1920×1080 captures**. At every stop it asserts identical geometry fingerprints and camera serialization, with unchanged generation miss/completion counts while switching modes. All eight return addresses match the inward geometry. [Native mode proof](native-modes.log), [frame records](mode-frames.csv).
- ORIGINAL is **pixel-exact** against the frozen pre-integration 0.4 executable across **10,444,800 central world pixels** in eight matched scenes: zero changed RGB channels. The comparison excludes the changing UI clocks/counters. [Per-scene pixel hashes](original-pixel-comparison.json).

Independent continuity proof:

```text
PROOF recipe=3 objects=8 revisits=7 digest=1749542108118747663 (all LODs, descriptors, materials and atomic sites)
```

The complete raw mode captures remain in `build/hyperion-comparison/captures`. The test verifies three distinct world images per scene, native dimensions, unchanged return identities and bounded effect target storage. Geometry is not regenerated to switch styles. No generator or navigation source was rewritten.

## Measured rendering cost

Three sequential runs per case, with the same eight-scale tour and return, 15 camera stops, and the last 25 settled frames per stop: **375 samples per run**. Screenshots and independent proof workers are disabled for profiling. Values below are medians of the three run medians/P95s; peak RSS is the median process peak. The baseline is the frozen 0.4 executable. [Method and executable hashes](performance/method.json), [hardware](performance/hardware.json), [full per-run/per-scale data](performance/summary.json).

| Mode | CPU median / P95 ms | GPU median / P95 ms | Peak RSS MiB | Effect targets MiB |
|---|---:|---:|---:|---:|
| baseline | 0.217 / 2.401 | 1.614 / 3.373 | 131.9 | 0.00 |
| original | 0.279 / 3.041 | 1.581 / 3.201 | 131.1 | 0.00 |
| hyperion | 0.294 / 3.026 | 6.833 / 9.874 | 133.4 | 27.25 |
| hybrid | 0.297 / 2.813 | 6.713 / 9.698 | 133.8 | 27.25 |

All modes retain a roughly **16.67 ms median displayed frame interval** with VSync enabled. Hyperion's three GPU median measurements span **6.34–6.84 ms**; CPU timing varies between runs. The new effects add GPU work; these measurements do not claim an FPS increase. ORIGINAL's GPU time remains comparable to the frozen executable. Its measured CPU P95 is higher in this sample, so no CPU performance improvement is claimed for this integration.

The summary script asserts **equal submitted vertex, triangle, particle, atom and atom-triangle counts at every settled scale in every mode**. Rendering does not obtain its results by reducing procedural complexity or sample counts.

Effect storage is bounded: one RGBA16F scene target, one **D32F depth-only** target and two one-third-resolution RGBA16F bloom targets. At 1080p that is **27.25 MiB** of format storage, excluding driver alignment/overhead. Depth-only storage avoids the expanded depth/stencil representation used by Metal on this device. It preserves depth precision and needs no stencil. Fresh ORIGINAL allocates no effect targets; after using Hyperion they remain reusable for quick switching. Geometry buffers and the existing 32 MiB GPU cache remain shared. Process RSS and GPU format storage are different accounting measures.

All procedural generation remains on the existing asynchronous workers; presentation introduces no generation waits, geometry copies or replacement LOD. GPU shader/target setup happens on first use. Timing results above measure settled rendering rather than first-use compilation or screenshot readback.

## Actual Hyperion references

These were captured by running the user's existing **Hyperion 3.64** native game with no-save graphics fixtures. They are its actual 2880×1800 screenshots. Infinite's comparison captures below are 1920×1080.

| Hyperion galaxy/chart renderer | Hyperion 3D flight renderer |
|---|---|
| ![Hyperion galaxy reference](reference-galaxy.png) | ![Hyperion flight reference](reference-flight.png) |

The source game shows folded spectral sheets/threads, luminous points, restrained dark gaps, material sheen and HDR highlight rolloff. Infinite uses those actual shader functions and composition algorithms on its own generated geometry. Its galaxy keeps its physical stellar/gas distribution; its ice rock keeps its existing cavities; its graphite specimen keeps its atomic sites and bonds. The source game's decorative sky field, terrain generation, buildings, combat and weather are not inserted into Infinite. [Source-by-source audit and intentional differences](../HYPERION_RENDERING.md).

## Identical Infinite scenes

Camera and geometry are identical across each row. Material animation is frozen at 12 seconds. The source-derived effects are strongest on material surfaces and emission, with fibres suppressed on atoms to keep crystallographic structure readable. Around terrain patches, both the patch and enclosing section share the planet's material coordinates; the sampling rectangle is not coloured independently.

| Scene | ORIGINAL | HYPERION | HYBRID |
|---|---|---|---|
| Cosmic web | ![Cosmic web original](captures/cosmic-original.png) | ![Cosmic web hyperion](captures/cosmic-hyperion.png) | ![Cosmic web hybrid](captures/cosmic-hybrid.png) |
| Galaxy | ![Galaxy original](captures/galaxy-original.png) | ![Galaxy hyperion](captures/galaxy-hyperion.png) | ![Galaxy hybrid](captures/galaxy-hybrid.png) |
| Planet | ![Planet original](captures/planet-original.png) | ![Planet hyperion](captures/planet-hyperion.png) | ![Planet hybrid](captures/planet-hybrid.png) |
| Terrain | ![Terrain original](captures/surface-original.png) | ![Terrain hyperion](captures/surface-hyperion.png) | ![Terrain hybrid](captures/surface-hybrid.png) |
| Rock / cavities | ![Rock / cavities original](captures/rock-original.png) | ![Rock / cavities hyperion](captures/rock-hyperion.png) | ![Rock / cavities hybrid](captures/rock-hybrid.png) |
| Mineral grain | ![Mineral grain original](captures/grain-original.png) | ![Mineral grain hyperion](captures/grain-hyperion.png) | ![Mineral grain hybrid](captures/grain-hybrid.png) |
| Atomic lattice | ![Atomic lattice original](captures/lattice-original.png) | ![Atomic lattice hyperion](captures/lattice-hyperion.png) | ![Atomic lattice hybrid](captures/lattice-hybrid.png) |

## Reproduction and limits

```sh
cmake -S . -B build -DINFINITE_GPU_TESTS=ON
cmake --build build -j 3
ctest --test-dir build --output-on-failure
python3 tools/profile_render_modes.py --binary build/infinite \
  --baseline build/infinite-original-0.4 --output build/render-profile --runs 3
python3 tools/summarize_render_modes.py build/render-profile
```

The frozen baseline executable is a local validation artifact; Git checkpoint `0d61030` is its source checkpoint. The scene comparison fixture is `--compare-modes`; H switches modes during ordinary navigation. No browser or raylib runtime is embedded.

Metal was exercised on hardware. SPIR-V and GLSL shaders compile successfully but have not been runtime-tested here. Windows uses the same shader sources and needs the pinned Windows bgfx shaderc to produce DXBC at configure time; Windows hardware validation remains outstanding. Existing limits of the procedural universe, educational lattice model and interactive root-cell streaming remain documented in the README. This rendering integration does not claim new simulation capabilities.
