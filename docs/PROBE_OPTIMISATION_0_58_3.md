# Probe rendering optimisation — 0.58.3

The drill, rollers, hydraulics, lighting, dust, geometry complexity and mining behavior are retained. Five drilling/fuse/blast/beacon/portrait captures are byte-for-byte identical to the reference renderer. The optimisation removes work that cannot affect those images.

## Measured results

Two runs of each path used the same 639-step probe trajectory at 1,440 × 900, with a fixed environment time and no screenshots or video encoding during measurement. Reference and optimised runs alternate in reference/optimised/optimised/reference order. Results below are medians of the two runs for each path.

| Metric | Reference | Optimised |
| --- | ---: | ---: |
| Median frame time | 12.79 ms | 5.75 ms |
| 95th-percentile frame time | 48.03 ms | 43.11 ms |
| Median CPU frame preparation | 9.55 ms | 4.17 ms |
| Median probe drawing preparation | 0.378 ms | 0.337 ms |
| Reported upload bytes over the route | 69.07 MB | 22.75 MB |
| Work-light colour updates | 518  | 400  |
| Median peak process memory | 124.76 MB | 118.79 MB |

This is a **55.1% reduction in median frame time** and **67.1% reduction in reported upload bytes** on the scripted probe route. Peak process memory varies between runs; the measured medians are shown above. These measurements describe this workload and machine, rather than general gameplay FPS. The original CPU sample showed most main-thread samples waiting in the rendering pipeline, which motivated eliminating occluded exterior work.

[Full measurements](probe-optimisation-0.58.3/comparison.json) · [Verification](probe-optimisation-0.58.3/verification.json).

## Changes

- **Conservative cavern occlusion.** Exterior space and covered terrain are not drawn when ready cavern geometry encloses the camera and the shaft aperture lies outside a conservative view cone. The test accounts for the actual portal rim, viewport aspect and near-plane reach. Looking toward the exit immediately retains exterior drawing. The enclosing cave is kept ahead of the eight-cave draw limit, including with a long excavation history. Aperture clipping jobs and terrain streaming remain warm. Local geological objects are retained, and explorer, shrinking, specimen and microscopic views retain their existing paths.
- **Resident work-light colours.** A cave's dynamic light buffer replaces its unused static GPU vertex buffer within the existing cache budget. The exact physical camera/light state determines whether colours need updating. Frozen probe views reuse them; moving lamps, drilling, fuse pulses and blasts still update. Cached resources are released with the existing geometry eviction path.
- **Less repeated CPU work.** Light constants are prepared once per surface, particle meshes retain bounded scratch capacity, and dust-circle coordinates are reused. Shaft/cavern topology, particle counts, rotations and effect timing are unchanged.

No universe generator, procedural seed, address, mining reserve, saved flight, save format, shader binary or dependency was changed. The in-game rendering remains Hyperion; the CLI reference switch exists only for validation.

## Validation and reproduction

Both probe suites passed: **117 excavation/refit/camera checks** and **561 presentation/visibility/light checks**. The prepared lighting matches the original vertex colours. Visibility tests cover the shaft aperture, camera frustum, near-plane clipping, surface approach and exterior view. The final native sequence passed **11 stages**, adding an upward view through the shaft and an exterior entrance approach to the previous nine scenes.

The signed 0.58.3 / 69 app matches all 28 code/data sections of the tested executable and passed a normal 120-frame startup using an isolated temporary save.

[Pixel comparison hashes](probe-optimisation-0.58.3/pixel-comparison.json) · [Native log](probe-optimisation-0.58.3/native.log) · [Probe tests](probe-optimisation-0.58.3/probe-tests.log) · [Presentation tests](probe-optimisation-0.58.3/presentation-tests.log).

Reproduce the measurements with the built executable and a new output directory:

```sh
python3 tools/benchmark_probe.py build/infinity /tmp/probe-benchmark --repeats 2
```

The benchmark uses isolated temporary saves and verifies matching bore identity, planet descriptor and trajectory length. It captures neither screenshots nor video during timing.
