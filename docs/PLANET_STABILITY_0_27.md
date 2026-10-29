# Planet depth stability — Infinite 0.27

The flickering planet regression is reproduced in native Metal captures. The old projection mapped a very large range (`0.00005` to `10000` rendering units) into conventional floating-point depth. Distant surfaces collapsed onto the same depth values, sometimes the clear value. Small magnification changes then selected different front/back terrain triangles, producing flashing patches. This was a rendering error, independent of procedural generation.

## Fix

World views now use reversed-Z: near is one, far is zero, the world depth clear is zero, and world geometry uses greater/greater-or-equal comparisons. Original mode explicitly requests D32F swap-chain depth, and Hyperion/Hybrid retain their existing D32F scene attachment. Terrain, celestial geometry, atoms, cities, traffic, clouds, weather and trails share the corrected convention. The atmosphere reconstructs distances using the same near/far values and reversed-depth equation. Shadow maps and the cockpit retain their independent conventional projections and matching depth tests. Screen-space selection brackets bypass world depth.

The clipping distances, X/Y projection, field of view, vertices, indices, materials and geometry caches remain unchanged. No additional render targets, textures, dependencies or generation work are introduced. Generator, navigation, flight, ship handling, seed and identity sources remain byte-identical to **a80c241**. [Preservation hashes](planet-stability-0.27/preservation.json). The checkpoint is `checkpoint/infinite-0.26-before-planet-stability`.

## Reproduction and regression

`--planet-stability-test` renders a real solid primary and one of its addressed moons, each from four planetary radii, in all three modes. It uses the same physical eye and orientation at the ordinary unit size, then makes rendering units 1000 times smaller (the planet is now 4000 rendering units away), then perturbs that unit size by one millionth of a decade. The geometry and terrain digests must remain identical. All eighteen framebuffers are captured after generation and transitions settle.

The pixel check compares the interior of the same planetary disc. Its fixed acceptance bounds are RMS < 6/255 and fewer than 2% of channel samples differing by more than 48/255. All twelve baseline comparisons fail; all twelve corrected comparisons pass.

| Matched view, ordinary units vs far-depth units | Before RMS / 255 | After RMS / 255 |
| --- | ---: | ---: |
| Primary, Original | 43.7723 | 0.2391 |
| Primary, Hyperion | 23.5717 | 0.0152 |
| Primary, Hybrid | 31.6369 | 0.0042 |
| Moon, Original | 49.6717 | 1.0357 |
| Moon, Hyperion | 41.7620 | 1.1864 |
| Moon, Hybrid | 63.6381 | 0.6321 |

Residual differences include subpixel rasterisation and foreground optical proxies. The largest corrected error fraction above 48/255 is 0.011%; the baseline reaches 46%. These are image-consistency measurements, not frame-rate claims.

| Same physical orbit and generated terrain | Image |
| --- | --- |
| Baseline, ordinary units | [Reference](planet-stability-0.27/before-hyperion-0.png) |
| Baseline, far-depth units | [Broken patches](planet-stability-0.27/before-hyperion-1.png) |
| Corrected, far-depth units | [Stable planet](planet-stability-0.27/after-hyperion-1.png) |

[Baseline comparisons](planet-stability-0.27/before-comparison.json), [corrected comparisons](planet-stability-0.27/after-comparison.json). The first baseline capture harness completed all eighteen images but returned an unrelated generic-tour completion error; its images, digests and pixel failure are retained, and the dedicated fixture completion check is fixed in 0.27.

The CPU regression tests the exact float matrix arithmetic used by Metal, confirms the old 4000-unit clear-value failure as a negative control, checks depth ordering and atmosphere reconstruction, and verifies unchanged X/Y projection. It also checks both zero-to-one and homogeneous clip conventions mathematically. Metal is exercised natively; GLSL and SPIR-V shaders are rebuilt but those backends are not claimed as runtime-tested here.

Run with:

```sh
cmake --build build -j4
ctest --test-dir build -R 'multiscale_depth_precision|native_planet_stability' --output-on-failure
```

GPU tests require configuring with `INFINITE_GPU_TESTS=ON` and a native desktop session. Each fixture uses its own data directory and leaves the saved flight untouched.

## Final verification

**All 49 checks pass:** 26 CPU checks and 23 native rendering/image checks. The existing native suite verifies city/weather rendering, solid terrain, Retina views, grazing sunlight and terrain shadows, landing and roll, boundary coverage, automatic flight, and galaxy-to-atomic return journeys. The three-mode tour records seventeen identical camera/geometry/generation proofs across nine scales. After explicitly selecting D32F for Original mode, the new planetary test and the city/weather, solid-terrain and surface-quality fixtures are repeated successfully on the final build.

[Validation summary](planet-stability-0.27/validation.json), [CPU checks](planet-stability-0.27/cpu-tests.log), [native suite](planet-stability-0.27/native-regressions.log), [final native repeats](planet-stability-0.27/final-native-tests.log), [mode round trip](planet-stability-0.27/mode-round-trip.log), [automatic scale flight](planet-stability-0.27/seamless-flight.log).

The packaged local application is **0.27.0, build 27**, ad-hoc signed and strictly verified. [Build metadata](planet-stability-0.27/build.json). The existing saved flight and ship format are preserved.
