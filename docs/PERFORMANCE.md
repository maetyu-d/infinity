# Performance and detail — application 0.4.0

This pass preserves procedural recipe 3, addresses, physical descriptors and inherited environments. It changes sampling, mesh storage, scheduling and GPU submission. Physical fields remain authoritative; additional LODs refine those same fields and atomic site IDs.

## Measurement method

Measurements use the local **Apple M1 / arm64**, macOS **26.4.1**, **Metal**, **1920×1080**, four-sample MSAA and enabled VSync. Both builds use the same Release compiler/dependency configuration and default universe seed. Three fresh processes per version perform the same fixed-step galaxy → lattice → galaxy tour, with 35 settled frames at each stop. Timing/capture proof workers are excluded from these performance tours. The separate correctness tour still regenerates every descriptor and LOD and checks final framebuffer pixels.

The baseline is the actual version-0.3 executable with only timing/detail counters and `--benchmark-tour` instrumentation added before algorithm changes. Its frozen executable is `build/performance/before/infinite`; its original source is archived in [baseline-source.tar.gz](performance/baseline-source.tar.gz). The source archive predates the instrumentation. Both measured native builds use the same core timing/detail counters and fixed-step tour option.

CPU submission time covers scene preparation, visibility, buffer preparation and bgfx command submission before `bgfx::frame()`. The CSV separately records that call's elapsed time as `submit_ms`. `wall_ms` covers the complete frame loop, including display pacing. GPU times come from bgfx's timestamp queries and are delayed relative to CPU submission.

The first ten frames of **each** process are excluded. Settled-view comparisons additionally require a settled camera, no pending generation, no LOD blend, and—where applicable—no queued task or deferred upload. The table pools the remaining settled samples across all three runs. Detail columns show the maximum submitted count at settled views, including enclosing context meshes. They are sampling/submission counts rather than counts of uniquely visible pixels.

Each headless generation run samples each requested level three times. The generation table reports the median of the three per-process medians. These are small local benchmarks, not statistically certified latency bounds. Equal requested LOD numbers can now produce more detail; the raw CSV includes resulting vertex/atom counts. The retained cosmic reference kernel is measured separately against the optimized kernel on identical query points, five paired repetitions of 12,000 samples.

## Measured results

| Settled view | CPU median before → after | CPU P95 before → after | Submitted detail before → after |
|---|---:|---:|---|
| GALAXY | 5.752 → 3.295 ms | 6.216 → 3.881 ms | 19,606 → 33,606 particles |
| STAR SYSTEM | 0.608 → 0.447 ms | 1.052 → 0.811 ms | 1,408 → 2,808 particles |
| PLANET | 0.520 → 0.246 ms | 0.810 → 0.357 ms | 23,424 → 84,480 triangles |
| SURFACE | 0.549 → 0.257 ms | 0.815 → 0.404 ms | 37,844 → 78,818 triangles |
| MOUNTAIN | 1.372 → 0.407 ms | 1.883 → 0.665 ms | 73,106 → 161,184 triangles |
| ROCK | 2.600 → 0.700 ms | 3.386 → 1.003 ms | 30,052 → 68,564 triangles |
| MINERAL GRAIN | 0.413 → 0.251 ms | 0.652 → 0.379 ms | 10,734 → 26,130 triangles |
| CRYSTAL LATTICE | 0.369 → 0.254 ms | 0.678 → 0.377 ms | 1,026 → 1,395 atoms |

Median peak RSS: 128.4 → 133.5 MiB (+4.0%).
Whole-tour CPU submission P95: 5.772 → 3.280 ms.

| Generation workload | Median before → after | Speedup |
|---|---:|---:|
| GALAXY, requested detail 4 | 17.990 → 30.297 ms | 0.59× |
| STAR SYSTEM, requested detail 0 | 1.137 → 1.394 ms | 0.82× |
| PLANET, requested detail 2 | 21.383 → 5.456 ms | 3.92× |
| SURFACE, requested detail 4 | 84.872 → 29.267 ms | 2.90× |
| MOUNTAIN, requested detail 4 | 84.871 → 29.169 ms | 2.91× |
| ROCK, requested detail 2 | 50.648 → 9.990 ms | 5.07× |
| MINERAL GRAIN, requested detail 2 | 30.290 → 3.971 ms | 7.63× |
| CRYSTAL LATTICE, requested detail 0 | 1.754 → 1.800 ms | 0.97× |

Cosmic field microbenchmark: **110.11 → 27.72 ms** for 12,000 identical queries (**3.97×**), with equal checksums and a separate exact-equivalence regression. This compares the retained old kernel with the new kernel in one executable; it is not a second full native baseline run.

The maximum tracked GPU cache was **5.36 → 4.96 MiB**. The maximum tracked CPU payload cache was **16.22 → 11.65 MiB**, with the accounting change described below. Median peak RSS is **133.5 MiB**, **+4.0%** relative to baseline; worst observed optimized peak was **134.6 MiB**.

Display-paced frame times remain approximately 16.7 ms. The optimization creates CPU headroom and supports more geometry; it does not imply an equivalent increase in displayed FPS. See the raw logs for per-run frame P95 and peak RSS.

Galaxy and star-system generation do more work: the galaxy's maximum sample count rises from 19,000 to 33,000 stellar samples, and the system doubles belt particles and refines its star sphere. Their generation jobs can therefore cost more even though render submission costs less. The lattice's finest representation also draws more sites and smoother spheres, increasing GPU work. These costs are visible in the raw measurements and remain off the rendering thread where applicable.

CPU-cache accounting is more conservative after optimization: it counts allocated vector capacities and cached instance payloads, rather than only populated elements. GPU-cache counters cover cached mesh/instance buffers; shared primitive meshes, transient buffers and driver allocations are separate. **Process peak RSS is the cross-version memory comparison**, rather than treating either cache counter as total application memory.

## Changes implemented

- **Adaptive sampling:** projected object diameter drives a hysteretic per-kind cell-size target. Planet sampling has four subdivision levels and six camera-facing hemisphere keys. Rock and grain grids extend from 32 to 64 cells per axis. The highest terrain-patch grid extends from 128 to 192 cells per axis. Cosmic sampling extends from 18,000 to 54,000 candidates; galaxies expose denser stable sample prefixes; lattice windows expand from nine to thirteen cells per axis while keeping the same bounded physical specimen and site keys.
- **Field work:** cosmic queries visit nearby cells first, reject cells using a conservative distance bound, compare squared distances and take only four final square roots per field level. A retained pre-optimization kernel verifies exact sampled equality. Rock samplers prepare immutable fracture/pit parameters once per job. Terrain meshes reuse one geological sample per grid point, including a halo for mesh-normal estimation, instead of repeatedly querying the whole field for lighting.
- **Overlapping terrain:** worker-generated parent sections clip out the focused patch footprint and transform lighting gradients into the same material frame. This prevents coarse context from hiding or flickering through fine terrain. A regression checks that the coarse section has no triangles inside the covered footprint.
- **Indexed surfaces:** marching tetrahedra shares edge vertices and uses a fixed case table. Lighting gradients come from the sampled field. Normalized scratch densities/normals use floats; hierarchical coordinates and authoritative physical fields retain doubles. Distant points outside a conservative crystal surface band avoid unnecessary host-field evaluation. Final mesh indices use 16 bits when possible, with a tested 32-bit fallback.
- **Asynchronous catalogs:** child descriptors are prepared in worker jobs. The renderer consumes immutable ready catalogs and assembles their relative frames from the existing lineage. Per-frame pose/frame caches remove repeated hierarchy reconstruction.
- **Prioritized generation:** requests enter a bounded demand queue. Focus and its coarse fallback outrank surrounding objects. Up to three cache tasks run concurrently, including catalogs; ready coarse geometry remains visible while finer geometry is pending. The render loop never joins generation workers.
- **Cancellation and eviction:** stale jobs are cancelled after a missed-frame grace period. Scalar-field rows, particle loops and lattice stages poll their tokens. Publication checks token identity, so an obsolete completion cannot overwrite a newer entry with the same key. The 32 MiB CPU payload budget and 96-entry geometry limit remain; old completed entries are evicted. Catalog count and GPU storage are also bounded.
- **GPU representation:** stars and dust use one shared billboard mesh plus instance data. Atomic transforms/colours are baked in workers and uploaded once to cached instance buffers; they are not rebuilt every frame. Shared sphere meshes use three projected-size tiers. GPU eviction releases the corresponding instance buffers. New uploads have a two-MiB frame allowance; one oversized first asset may be admitted to avoid starvation, and additional uploads are deferred while the ready representation remains in use.
- **Instrumentation:** CSVs include CPU/GPU/submission times, mesh/particle/site counts, queue/running/cancellation/eviction counts, upload bytes and atomic triangle counts. `--benchmark-tour` uses fixed navigation steps and omits independent proof work; `--smoke-test` retains the complete correctness audit.

The screen-space policy targets projected sampling size. It is not a certified mesh-curvature error bound. Terrain uses quantized hemisphere views and whole-object jobs; neighbor-balanced streaming, crack stitching and geometric morphing remain future work. Navigation may still need a cold descriptor lookup, and small cache locks/driver calls remain on the UI thread. Bulk field generation and catalog construction are worker tasks.

## Validation and evidence

**All 10 tests passed** in **28.95 seconds**: **64,404 performance invariants**, **66,204 process checks**, **61,289 continuity checks** and **251 foundation checks**, plus restart and GPU fixtures. The final suite includes performance invariants, structural process checks, existing foundation/continuity tests, separate restart processes, a real Metal round trip and final-pixel atomic visibility.

New invariants cover exact sampled cosmic-kernel equivalence, prepared transform cancellation at stellar/atomic scales, prepared lithology/host fields, monotonic/hysteretic LOD, camera-facing terrain identity, compact high-resolution terrain, indexed manifold topology, 16/32-bit index safety, atomic site/instance identity, cancellation, bounded worker admission, asynchronous catalogs and cache eviction. Timing assertions are deliberately excluded from portable correctness tests.

Evidence: [test summary](performance/tests.log), [individual test output](performance/test-details.log), [native proof](performance/native.log), [pixel validation](performance/pixel-check.txt), [cosmic paired timings](performance/cosmic-kernel.csv), [comparison data](performance/comparison.json), [hardware/settings](performance/hardware.json), [baseline raw runs](performance/before/), [optimized raw runs](performance/after/).

The native proof digest is recorded in [native.log](performance/native.log). Captures show the [planet](performance/captures/planet.png), [finer surface](performance/captures/surface.png), [rock](performance/captures/rock.png) and [atomic lattice](performance/captures/lattice.png).

## Reproduce

```sh
cmake -S . -B build -DINFINITE_GPU_TESTS=ON
cmake --build build --target infinite infinite_tests infinite_continuity_tests infinite_process_tests infinite_performance_tests infinite_benchmark -j 4
ctest --test-dir build --output-on-failure
./build/infinite --benchmark-tour --width 1920 --height 1080 --profile build/frames.csv --data build/perf-data
./build/infinite_benchmark > build/generation.csv
./build/infinite_performance_tests --cosmic-benchmark > build/cosmic-kernel.csv
python3 tools/compare_performance.py build/performance/before build/performance/after docs/performance/comparison
```

The comparison tool expects `frames-1.csv` through `frames-3.csv`, matching `tour-N.log`, and matching `generation-N.csv` in each run directory. Performance tours omit capture I/O and proof regeneration. Keep that workload identical when comparing changes. GPU fixtures require an active desktop; eight tests run headlessly with `INFINITE_GPU_TESTS=OFF`.
