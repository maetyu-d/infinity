# Continuous scale flight — 0.7

Version 0.8 removes the spatial streaming limits recorded here; see [STREAMING.md](STREAMING.md). This document retains the 0.7 implementation and validation history.

Capture the mouse with C, steer toward a locator or visible object, and fly with WASD/QE. Approaching stars, planets, terrain, rocks, minerals and atomic specimens automatically changes the physical viewing scale and local reference frame. Fly away to return to larger scales. Shift boosts movement, Ctrl provides precision, and -/+ adjusts the speed multiplier. F, Backspace, wheel zoom, saved views and guided tours remain available.

## Implementation

`FlightController` consumes the same immutable descriptors, asynchronous catalogs and addressed star samples used by rendering. A displayed star's `objectId` resolves its actual existing system, including IDs outside the small locator catalog. Cube-face lookup resolves the canonical level-8 surface tile beneath the camera; neighbouring tiles hand off during lateral flight. A closer sibling can replace an early precision frame, so its broad capture region cannot trap travel to another star, planet or mineral.

Clearance determines metres per render unit and therefore movement speed. Unit changes are limited to four decades per second. `Navigation::setRenderScale` rescales the complete eye position; `changeFrame` applies the existing precise relative transform. Both preserve the physical eye and orientation. Automatic entry does not invoke the explicit focus animation or recenter the camera. Wider exit regions and sibling hysteresis prevent boundary chatter. A movement step is capped at 20% of available clearance, including at maximum boost.

Nearby coarse geometry and the next catalog are prefetched before entry. Interactive navigation never waits for a worker. The existing generation queue, cancellation and cache budgets remain authoritative. One shared galaxy sample buffer is retained for nearby-system queries; particle data is inspected in place. Renderer extraction includes discovered terrain tiles/star IDs beyond the survey catalog and exposes small-scale locator lights. Hyperion materials, GPU passes and all three render modes remain available.

Explicit click selection stays selected. An explicit focus/parent/zoom view remains stable until flight resumes. Automatic units/frames use the existing camera save format and retain ancestry breadcrumbs. No generator, geometry recipe, seed, history, material environment or coordinate address rule was changed.

## Verification

`infinite_flight_tests` performs 24,816 checks. It flies from a cosmic region to an atomic specimen and back for three seeds at signed root-cell extremes, using only steering and forward movement during the journeys. It checks every level, saved camera state, descriptor/geometry fingerprints, unit/frame continuity, finite orientations, bounded cache concurrency, stationary hysteresis, cube-face/tile boundaries, lateral terrain handoffs, explicit selection/manual views and switching to a displayed star outside the locator catalog.

`--flight-test` runs an independent flight through the real SDL3/bgfx renderer. The fixture steers physical waypoints; the production controller decides scale and frame transitions. It requires all nine levels to render, actual atom instances at the final level, and successful outward return, without focus or wheel commands. The automated native case uses Hyperion at 1920×1080. The existing native round-trip and three-mode image tests check the original procedural scene and rendering compatibility.

The complete sixteen-case suite is recorded in [flight/ctest.log](flight/ctest.log). Native flight output, frame timings and representative captures are in [flight](flight). Historical Hyperion integration evidence remains in [HYPERION_RENDERING.md](HYPERION_RENDERING.md).

The final 1920×1080 Metal/Apple M1 Hyperion flight recorded 2,178 frames, a median frame interval of 16.68 ms and a 95th percentile of 17.39 ms with VSync. It displayed 4,554 atomic instances; peak process RSS was 155.7 MiB and the final CPU payload cache was 12.32 MiB. See [validation.json](flight/validation.json), [native.log](flight/native.log) and [frame samples](flight/frames.csv). Eight inward and eight outward level handoffs are accompanied by additional sibling handoffs in the trace. The full suite passed in 108.94 seconds.

Reproduce:

```sh
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/infinite --flight-test --render-mode hyperion --width 1920 --height 1080 \
  --capture-dir build/seamless-flight/captures --profile build/seamless-flight/frames.csv
```

Enable `INFINITE_GPU_TESTS` at configure time to include the native cases. Those cases require an active desktop. The automated pilot is a validation fixture; normal flight remains user-steered.

## Existing model boundaries

Flight operates on the existing finite procedural samples and valid addressed regions. Adjacent root-cell spatial streaming remains future work. Atomic specimens remain bounded static material structures. Lateral terrain handoffs share the existing planetary field; neighbour-balanced quadtree streaming, crack stitching and removal of bounded inspection-window edges require further terrain work. The implementation preserves the existing geometry and its complexity.
