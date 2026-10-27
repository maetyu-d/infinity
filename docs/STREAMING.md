# Terrain and root-cell streaming — 0.8

Terrain now covers the whole planet continuously, and root cells stream around the physical camera. WASD/QE, automatic scale flight, C mouse capture, manual focus and all three rendering modes remain available.

## Terrain coverage and transitions

The renderer uses a complete six-face cube-sphere frontier. Screen-space error chooses refinements near the camera, and a balance pass keeps edge neighbours within one level. Finer boundary indices collapse onto the coarser edge's samples, including across cube faces. Canonical dyadic vertex keys make positions and baked shading identical on shared vertices.

Coarse parents remain until every required child and stitch is ready. A new complete cut replaces the previous one atomically. A shared 0.2-second morph starts from the previous surface's triangle intersections, so adjacent tiles use the same position throughout the transition. The frontier always covers the full sphere; no rectangular sampling-window edge is exposed.

Worker jobs query the existing planet's height, normal and albedo fields. No planet process, lithology, erosion approximation, rock, mineral, lattice, object seed or history is replaced. Terrain positions remain doubles in planet-local units until the renderer subtracts the camera; GPU floats are local to the eye. The existing microscopic inspection cavity is clipped per fragment rather than exposing a terrain sampling box.

## Root-cell traversal

Cell centres are two root units apart; a root unit remains 10^26 metres. The camera moves through half-open local cell coordinates [-1,1), then changes its reference cell without changing physical position or orientation. Integer cell indices are subtracted before conversion to floating point, preserving adjacent-cell offsets even near signed 64-bit extremes. Selection, focus and saved breadcrumbs support multiple cells.

The visible neighbourhood follows the physical eye independently of the focused object, including during focus transitions. A 3×3×3 neighbourhood loads asynchronously, with forward prefetch during cosmic flight. A smooth spherical distance falloff finishes inside that neighbourhood, so the loading frontier is invisible. Distant cosmic particles preserve their actual directions and angular sizes beyond the local depth range; their visual pattern uses a stable frame rather than the current cell's phase.

Existing cell content is unchanged. Additional samples fill the old unsampled collar from 0.96 to 1 root unit. These samples use an independent deterministic feature counter and a partition-of-unity blend of the neighbouring cells' existing cosmic fields. The density agrees on both sides of cell faces, edges and corners. No old galaxy, star or other addressed object is moved or reseeded.

## Work and storage bounds

The active terrain plan has at most 192 leaves, with complete fallback coverage. Tiles contain 8×8 cells and support the address format's 24 levels. Tile/collar jobs share the existing three-worker queue, cancellation and 32 MiB payload cache. The entry cap increases from 96 to 512 to hold the active tree and its fallback/stitch variants. Old cells and tiles are evicted and regenerate from their original addresses when revisited.

Terrain uses the existing bounded transient GPU buffers, without duplicate static terrain uploads. The static GPU cache remains capped at 32 MiB. Full-cube numerical precision, address ranges and resolution remain finite; these are detail/numerical bounds, rather than the former terrain-window or single-cell travel boundary.

## Validation

The dedicated streaming test checks full coverage, 2:1 neighbours, index stitching, shared vertex positions/colours, morph continuity, density collars, cell arithmetic, camera continuity, cross-cell persistence, cache bounds and traversal across coordinate extremes. The native fixture flies over a root-cell face, captures the same physical view in two reference cells, then crosses a terrain cube-face seam and captures Original, Hyperion and Hybrid.

The image checker compares the actual cell-handoff pixels and checks the terrain region for exposed sky/coverage holes. Native images, profiles, test output and validation metadata are saved in [streaming](streaming).

```sh
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/infinite --streaming-test --render-mode hyperion --width 1920 --height 1080 \
  --capture-dir build/streaming-native/captures --profile build/streaming-native/frames.csv
python3 tools/check_streaming_captures.py build/streaming-native/captures
```

Native tests require `INFINITE_GPU_TESTS=ON` and an active desktop. Metal on macOS arm64 is the exercised backend; the updated Metal, SPIR-V and GLSL shaders are included.
