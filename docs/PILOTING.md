# Hyperion piloting — Infinite 0.9

This records the 0.9 integration. The current ship model, controls and shrinking ability are documented in [OPERATING_SCALES.md](OPERATING_SCALES.md).

The pilot is a consumer of the existing universe. Generation recipe 3, all original geometry generators, hierarchical addresses, coordinate transforms, environmental fields and deterministic seeds remain authoritative. Checkpoint `256eb8e` preserves the working 0.8 application before this change. The baseline build passed all 19 tests with desktop access; an initial sandboxed attempt could not connect to the macOS window server.

## Flight and scale

`Pilot` owns throttle, frame-relative **SI velocity**, body-relative angular velocity and thruster effort, assistance, cruise, probe, gear, hull and touchdown state. `Navigation` still owns the hierarchical camera pose. `FlightController` still performs asynchronous proximity queries, prefetch and reference-frame/precision changes. Its only new world-facing output is clearance in metres. A small SI position remainder accumulates sub-ULP displacements when a coarse camera frame cannot yet represent a small movement.

Changing render units never multiplies ship velocity. Crossing a local reference frame rotates velocity by the existing relative-frame quaternion; angular velocity remains in body axes. The integrator substeps at at most 10 ms and bounds spatial advancement by available clearance, with at most 32 substeps per display frame. Cache requests never wait on the rendering thread. An excessive incoming speed can limit simulated advancement rather than tunnelling through a refinement or blocking rendering.

Hyperion's actual `src/flight/handling.cpp` supplies the handling model: persistent throttle at .36/s, bounded pitch/yaw/roll rate commands, 4.2 rad/s² torque authority, asymmetric 22/26/52 thrust and 32 reverse authority, convex allocation reserving support thrust first, .12 s engine spool, and trapezoidal position integration. `controls.hpp` supplies mouse-delta-to-rate conversion. Assistance regulates velocity; disabling it retains momentum and spin. Braking commands reverse/manoeuvring jets and takes finite time.

The local drive has a 350 m/s base envelope. The explicitly labelled scale drive raises its envelope with available physical clearance; this is fictional travel propulsion over Infinite's uncompressed distances. It does not compress distances or change object sizes. Boost, precision and speed adjustment affect the drive command and authority, not stored momentum. Assisted cruise/probe propulsion also applies explicit, clearance-dependent field braking before interstellar momentum reaches local thruster range. The HUD labels that phase; disabling assistance preserves vacuum coasting. The displayed stopping distance estimates current reverse authority and spool delay; terrain proximity, gravity and an active drive governor can change the eventual stopping distance.

Near a planet, ship-only point gravity uses the existing radius and material density as a bulk-density estimate. Assistance reserves thrust to support the ship. This does not alter planetary mass descriptors, orbital snapshots, histories or other simulations. It is a vehicle-handling approximation, not a new n-body world simulation.

**Z** enables `SURVEY / VIRTUAL PROBE`: the same SI state and hierarchical navigation remain active, with contact disabled and an envelope that can shrink below a metre. This makes existing rocks, cavities, mineral grains and atomic lattices accessible without shrinking their geometry. Returning to ship mode inside solid terrain is refused. The HUD reports view scale in physical units and whether it is refining, widening or stable. **V** parks motion and restores the original explorer controls. F, Backspace, wheel and the guided tour remain explicit explorer tools.

## Destination, approach, touchdown, departure

Click selects the same existing address used by navigation. **J / Enter** pins that destination. The controller aligns with bounded angular acceleration, travels under thrust, and brakes using range and available reverse authority. Planetary destinations approach a point 5 km above the existing terrain on the observer's side. Surface destinations use their inherited geographic direction. A distant site behind the planet receives above-surface spherical waypoints instead of a direct path through the body. Manual helm input or B cancels navigation.

**L** selects the real surface underfoot, lowers the gear and begins descent. It queries `terrainHeight` and `terrainNormal`; it never adds a pad, flattens geology or regenerates the planet. Touchdown requires gear, descent and lateral drift below 2 m/s, tilt below approximately 15° and terrain slope below 35°. Unsafe contact arrests penetration, damps motion and can damage hull. Gear origin is held 2 m above the surface. **Enter** then performs a four-second vertical departure under thrust at the same physical position; it does not teleport out of the surface.

Per-seed `ship-<seed>.txt` stores versioned velocity, rotational/engine state, throttle, hull, control modes, destination and landed-site information. Loading validates data before using it; autopilot is disarmed on restore. Camera and discoveries retain their existing SQLite persistence. `display.txt` remembers Original, Hyperion or hybrid; the first interactive launch selects Hyperion. Native legacy fixtures explicitly retain their previous presentation and camera.

## Presentation from the actual Hyperion source

The source remains the native game at `../Hyperion`, currently 3.68, with the earlier material/display port documented in [HYPERION_RENDERING.md](HYPERION_RENDERING.md).

| Hyperion source | Infinite adaptation |
|---|---|
| `flight/renderer.cpp: CabinMesh`, `rebuildCockpit`, `drawCockpit` | Actual beveled titanium panels, recessed deck, copper rails, luminous indicators and canopy bolts. Dedicated 70° cockpit projection; the source viewport proportions and depth/slant construction are retained. |
| `flight_renderer.cpp`, `flight_hud.cpp` | Native-resolution geometry/text instruments: throttle, velocity, braking distance, hull, AGL, descent, drift/prograde, horizon, landing checks, target range/bearing and flattened contact scope. Every reading comes from the pilot or the existing world. |
| `flight/renderer.cpp: drawMotion` | Bounded world-grid navigation streaks aligned with real velocity. They are optical cues, not generated world objects. |
| `flight/minor_shaders.hpp` | Filtered material grain and derivative relief normals. Coordinates use a double-precision CPU phase and camera-relative GPU differential, preventing a planet-sized coordinate from losing close detail. Periodic visual texture sampling does not displace vertices or change mineral identity. Atoms retain their element colours. |
| `flight/shaders.hpp: atmoFragment` | Exponential-density Rayleigh/Mie optical integration, with sun-path extinction and the exact opaque depth as the ray limit. Eight view samples and two sun samples bound cost. Warmth and inherited water activity gate this optical presentation; cold dry bodies remain airless. Atomic probe views disable it. |
| Existing world/material lighting | Pilot mode derives lighting direction from the existing system star. Existing pigment, BRDF, radial emission, bloom and display processing remain modular. |

Distant stars are **Infinite's existing galaxy particle records and object IDs**, retained after the ancestor mesh fades. Their physical positions determine directions; a far rendering shell keeps those directions visible beyond the depth range. The active system's star is excluded to avoid replacing its physical proxy. No Hyperion stellar distribution or unrelated background scene is imported.

The 0.8 material/display shaders and their six precompiled fragment binaries remain byte-identical; flight-specific shader variants are loaded only for the pilot. Readable opaque depth is allocated only when optical presentation needs it.

The cockpit is composited after world optical processing with its own depth buffer range; instruments follow it at native resolution. This keeps cabin edges and text crisp and avoids corrupting the opaque planetary depth used by scattering. The current Hyperion presentation is its standard solid-material look. Civilisation actors, vegetation, clouds, combat and optional CGA/EGA wireframe presets are separate systems and are not invented by this flight layer.

## Validation and measured costs

`infinite_piloting_tests` flies the new ship inward through all nine scales and back out with throttle and proximity steering, and also exercises physical momentum across all nine scale frames and back, unit changes over 36 decades, inertial coasting and roll, finite braking, persistent throttle, destination arrival, manual takeover, real-terrain touchdown, non-teleporting launch, save/reload and cockpit geometry at multiple aspect ratios.

`--pilot-test` uses the native Metal renderer. It compares identical camera/planet geometry fingerprints across all three cockpit modes, lands safely, restores landed state, lifts off under thrust, then renders the existing atomic specimen in survey mode. Eight captured images verify both world geometry and flight instruments. The existing full deterministic, streaming, navigation, all-scale rendering and mode-preservation tests remain enabled.

Evidence and performance measurements are in [piloting/validation.json](piloting/validation.json), with [captured views](piloting/captures), [baseline test output](piloting/baseline-tests.log) and the final test logs alongside them.

### Performance on this Mac

A single sequential pair ran the same 1440×900 Hyperion viewer tour, with visual time fixed at 12 seconds. The following values give each of the eight scales equal weight and exclude transitions and outstanding generation. macOS/VSync pacing and allocator/driver variance remain in the measurement; these figures do not establish a GPU speedup.

| Measurement | Frozen 0.8 viewer | 0.9 viewer |
|---|---:|---:|
| Renderer CPU (ms) | 3.936 | 4.324 |
| GPU timing (ms) | 5.219 | 4.884 |
| Frame interval (ms) | 16.680 | 16.674 |
| Peak process RSS (MiB) | 124.36 | 134.33 |

The separate full cockpit/landing/survey fixture measured **16.66 ms median / 17.06 ms p95** frame intervals, 4.59 ms median renderer CPU and 6.99 ms median GPU timing. Peak process RSS was 129.19 MiB. These are different scenes from the paired viewer tour, so they are not a before/after cockpit-cost estimate.

The CPU and GPU geometry-cache budgets remain 32 MiB each, with three generation jobs, 512 cache entries and 192 terrain leaves. HDR targets remain **17.03 MiB** at 1440×900. The added static cabin mesh is **26.16 KiB**; HUD geometry and bounded motion cues reuse the existing transient pools. No new runtime dependency was added. Metal was exercised natively; SPIR-V and GLSL were compiled. DXBC/Windows still requires native verification.
