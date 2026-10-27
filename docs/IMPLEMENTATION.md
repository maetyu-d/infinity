# Implementation plan and status

The first deliverable is a native C++20 vertical slice, in the requested order:

1. Build pinned dependencies. Implement hierarchical signed cell addresses, counter-based seeds, local frames and logarithmic navigation; test these without a GPU.
2. Generate a lazy galaxy → system → planet → surface → mountain → rock → grain → lattice hierarchy with inherited chemistry and formation events.
3. Render those nodes in a shared camera-relative coordinate system with SDL3/bgfx. Add picking, smooth focusing, parent navigation, asynchronous mesh generation and bounded caches.
4. Add sparse SQLite discoveries/annotations, regression tests, benchmark instrumentation and a scripted native GPU tour.

This is a vertical slice, not completion of all six research phases. See README.md for the precise algorithms and limitations. No generated geometry is persisted as universe state.

## Foundation and vertical-slice outcome

The C++20 build, SDL3 window, real Metal/bgfx rendering, free flight, logarithmic focusing, hierarchical frames, deterministic descriptors, lazy geometry, sparse discoveries and camera persistence work. Eight scales are navigable in both directions. Background generation, stale task cancellation, projected culling, bounded CPU/GPU caches and atom instancing are active.

Verification: 250 core assertions, three passing CTest processes including a persistence restart fixture, per-scale generation benchmarks, finite geometry checks and a real 1920×1080 native GPU tour with captures. See VALIDATION.md for measured frame times and memory.

Incomplete: the advanced research algorithms, editable matter/terrain, physically complete chemistry, camera-driven stitched terrain streaming, inter-cell traversal, exact cross-compiler floating-point fixtures and Windows/Linux runtime validation. Current microscopic inspection uses spatially anchored cutaways, not molecular dynamics or an optical simulation.

Next concrete implementation task: replace whole-node terrain refinement with a neighbour-balanced, screen-error-driven cube-sphere streaming quadtree; stitch shared edges and test mesh-level seam continuity during movement. Then add spatially coherent adjacent-root-cell streaming and cross-platform CI. The detailed remaining work is itemised in README.md.

## Continuity pass — version 0.2

Implemented canonical addresses, precise addressed points, reversible navigation/breadcrumb persistence, transported terrain frames, unified terrain colour/lighting, nested sampling, host-constrained grain/lattice geometry, site-addressed atomic defects, LOD hysteresis/blending and local parent-material sections. A full round-trip proof now compares descriptors and every LOD; the native fixture also checks actual atomic pixels. All eight configured desktop tests passed. See CONTINUITY_AUDIT.md for findings, compatibility, captures and exact test coverage. Mesh-level terrain stitching remains the next task.

## Structural process pass — version 0.3

Implemented a cellular collapse web, four galaxy/population processes, shared plate kinematics and geological regimes, seven rock origins, material-specific mineral habits, graphite, a 28-site olivine cell, BCC/FCC iron and inherited thermomechanical defect fields. Atom bonds use local spatial bins. Process descriptors are immutable and memoized in a bounded thread-safe cache; the native proof regenerates them independently. The nine-case suite passes, including 66,204 process checks and 61,289 continuity checks. See PROCESS_DIVERSITY.md for the exact models, scientific references, evidence, recipe-3 compatibility and approximation limits.

## Performance pass — version 0.4

Implemented camera-facing/projected-size LOD, higher sampling limits, shared field work, indexed SDF meshes and compact index storage. Generation and child catalogs use bounded prioritized workers, cancellation tokens and capacity-aware eviction. Stars/dust use shared billboard instances; atomic instance buffers are prepared once and cached, with sphere detail selected by projected size. The renderer retains ready geometry and limits uploads. See PERFORMANCE.md for the repeated baseline/optimized measurements and raw evidence.

## Hyperion presentation — version 0.5

Inspected the actual Hyperion game, recorded its complete source inventory and rendering architecture, and preserved Infinite 0.4 in Git checkpoint `0d61030`. Added read-only scene presentation, source-traced pigment/silk/BRDF/glow shaders, HDR bloom and flight display processing. H selects original, Hyperion or hybrid presentation without regenerating geometry or moving the camera. Canonical planet/host-rock material frames span sampling windows and nested scales. The original generator, coordinate, history, navigation, persistence and cache sources remain unchanged. See HYPERION_RENDERING.md for source mapping, native mode proofs, image comparisons and measured cost.

## Free flight — version 0.6

Mouse look now rotates about the eye instead of orbiting the anchor. C toggles mouse capture; right-drag retains temporary look. WASD/QE move along the camera axes, -/+ or brackets adjust scale-relative speed, Shift boosts and Ctrl slows movement. Captured selection uses a centre crosshair. Escape and window focus loss release capture; an unfocused window cannot continue moving. Manual movement/look cancels camera transitions, and movement/look/scroll cancels the guided tour. Invalid input is rejected and long frame intervals are capped. Saved camera format and procedural generation remain unchanged. Dedicated navigation tests cover every scale and three seeds, alongside the existing round-trip/presentation tests.

## Continuous scale flight — version 0.7

Added a read-only proximity controller that shares asynchronous catalogs and immutable displayed star samples with rendering. Physical clearance controls render units and movement speed; early frame entry preserves precision across scale gaps, wider exits prevent chatter, and coarse geometry is prefetched before entry. Unit changes rescale the whole eye position, while frame changes use existing addressed relative transforms. Terrain queries resolve the actual level-8 tile under the camera and hand off laterally. Manual focus, zoom and parent views remain available. Tests steer complete cosmic-to-atomic and outward flights without focus/zoom commands, across three seeds and signed root-cell extremes. The native fixture renders all nine levels, including actual atom instances. Universe generators, seeds, histories, geometry recipes, spatial addresses and save format are unchanged. See SEAMLESS_FLIGHT.md for evidence and remaining streaming limits.

## Spatial streaming — version 0.8

Extended relative-frame math, navigation and saved breadcrumbs across signed root cells. The physical camera centres a prefetched cosmic neighbourhood; existing cell contents retain their seeds/positions, with deterministic blended collars added in formerly unsampled boundary bands. Cosmic emission retains angular size and a stable visual frame across rebases. Replaced bounded terrain windows in the visible scene with complete sphere coverage, adaptive balanced tiles, collapsed-index seam stitches and a shared mesh morph. Tiles query the existing planetary field and retain double positions until camera subtraction. Generation remains asynchronous and cancellable; cache/work budgets remain bounded. Existing node/LOD geometry remains available for identity/inspection references. Microscopic terrain clipping uses the existing inspection-cavity model. See STREAMING.md for proofs, native images and remaining numerical/resolution bounds.
