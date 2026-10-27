# Orbital activity — Infinite 0.12

Infinite now has a bounded activity simulation attached to its existing planets, alongside denser sampling of its existing stellar population. Checkpoint `c508f03` preserves the working 0.11 release. No world generator, hierarchical seed, spatial address, planetary orbit, geology field, rock, mineral or atomic lattice is replaced.

## Hyperion reference

The source reference is the user's native C++ Hyperion game, not Disney's renderer. These specific source routines inform the new layer:

- `Hyperion/src/flight/settlements.cpp::buildCraft`: the wedge ferry, box-and-pod freighter, twin-boom patrol and oblate survey silhouettes, glazed cabins and paired engines.
- `orbitalCore` in that file: bonded warehouse cross-members, habitat rings and illuminated orbital structure.
- `drawFactionCraft`: shared craft assets, body-relative orientation and scale, two engine traces with a bounded sinusoidal length.
- `drawPortActivity`: the moving cargo hook and timed harbour lights.
- `Hyperion/src/flight_game.cpp::Game::updateFlight` and its shipment views: stable contact identity, route progress, loading/unloading states and a separate scene description consumed by the renderer.
- `Hyperion/src/flight_renderer.cpp`: the flattened contact scope, destination selection and contextual contact information.

Infinite ports those presentation techniques to its own bgfx meshes, instancing, HDR/glow shaders and cockpit instruments. It does not import Hyperion's planets, compressed world coordinates, economy or combat simulation.

## Activity and density

Each existing planet hosts two station-kept orbital facilities: a harbour and a service yard. Their positions are fixed in the planet's authoritative axes, with the first harbour near the established planetary insertion viewpoint. They are not spawned around the camera or teleported into view. The same location exists when approached from another direction or after a long trip.

Freighters and ferries follow continuous raised transfer lanes between distinct berths, with finite loading and unloading intervals. Cubic endpoint easing and a quartic arch give zero velocity at each stop. Patrols fly closed harbour circuits. Survey craft and ejecta follow orbital trajectories whose angular rates derive from the existing planet's radius and mineral density. Impact strength controls debris density; mineral composition controls its appearance. Temperature and water availability increase civilian traffic, while harsh planets retain smaller research/industrial populations. Increasing traffic density appends slots while retaining the original actors' identities and paths.

The new `@activity1:` identity namespace is a planet Address plus a stable slot. Its seed stream is independent of every existing hierarchical/feature seed. Actors do not become fake `Universe::children` entries. Their poses are analytic functions of the existing planet, slot and activity time, so loading order, cached LOD, renderer mode and camera magnification cannot change them.

The galactic cockpit sky now requests existing LOD 4 rather than LOD 2: up to 33,000 addressed stars instead of 19,000. Lower-level star samples remain exact prefixes, with the same IDs and positions. The world-generating algorithms and recipe 3 are unchanged.

## Interaction and persistence

The left cockpit panel lists nearby traffic and its real loading, transit, patrol or scan state. The scope uses actual bearings. Click a craft/harbour light or contact-list row, or use **K** to cycle contacts. **I** hails the selected contact and reports its sampled operational state. **J** or **Enter** approaches the facility's holding point or matches a moving vessel's course. **B** brakes and cancels automatic pursuit; **Y** returns to planetary destinations. Existing planetary landing, launch, full-size flight, long cruise and shrinking controls remain available.

Rendezvous targets carry a real reference frame, metre position, SI velocity and stand-off distance. Craft pursuit keeps at least 750 m of stand-off, leaving the ship clearly visible inside the ordinary planetary depth range. Harbour holding points stand four facility radii away. The ordinary ship controller applies finite thrust and braking, adding target velocity as feed-forward during pursuit. It never snaps the ship to a contact. The route remains valid when render units or reference-frame axes change. Hailing is local simulation dialogue; no external messages are sent.

`activity-<seed>.txt` stores `INFINITE_ACTIVITY 1`, elapsed activity time, visibility preference and selected contact. It is saved atomically alongside the existing files. The activity clock advances while the app is open, including while landed or looking down to atoms; hidden activity still advances. Restart restores the clock and selected identity, with navigation assistance disarmed as before. Offline time is not fast-forwarded.

**F6** hides/shows the layer without changing the world or restarting its schedules. Macro/micro travel leaves the same actor state behind. Orbital facilities and vehicles do not shrink with the player; the microscope view continues to use Infinite's existing material and atomic generators.

## Rendering and scope

Six roles have three shared mesh detail levels. Meshes are uploaded lazily and instanced by role/LOD. All eighteen templates total 170,468 bytes. Screen size selects geometry or an unresolved navigation light. Craft positions subtract the double-precision eye before GPU upload; a separate camera-relative pass keeps hull shading stable during close approaches. Normal opaque depth and physical celestial occultation hide traffic behind planets. The existing worker count, CPU geometry-cache budget and world streaming remain in force.

This release adds ambient civilian traffic, research activity and rendezvous. Loading is a scheduled activity state, not a commodities economy. There are no new combat, damage, cargo-trading, station-interior or craft-collision systems. Harbour approaches stop outside the structure; terrain landing retains the existing physical contact system.

## Validation

The activity regression tests cover stable IDs and seeded replay, inherited density, trajectories and analytic velocities, safe orbital altitudes, clock/selection persistence, malformed saves, mesh index/memory bounds, full-size harbour approach, moving-target rendezvous and rendering-unit independence. They also verify activity sampling leaves authoritative planet descriptors and geometry unchanged.

Native fixtures retain the three-mode camera/geometry proof, landing/launch, microscopic shrinking and atomic flight. New captures verify actual harbour geometry and picking, and visible traffic from the normal starting orbit. Release logs, screenshots, source-preservation hashes and before/after measurements are stored in [orbital-activity](orbital-activity/).

All 24 release tests passed in 185.97 seconds. Four focused tests passed after the final contact-range readout correction; the activity test contains 22,079 checks, including holding-camera alignment and true vessel range. Live packaged checks verified K/I/J/B/F6, saved contact selection and clock continuation on restart. Sixteen generator, coordinate, streaming, spatial-flight and shader-manifest files remain byte-identical to the checkpoint. The original ship-save schema remains version 3.

## Measurements

Metal at 1440 × 900, the same seed and fixed visual time. The before and after loops share the orbital three-mode views, galaxy-facing sky, landing, launch and micro/nano phases. The after loop adds two forty-frame phases for close harbour geometry and the normal starting orbit. VSync limits wall time; GPU differences at this level are ordinary run-to-run variation.

| Measurement | 0.11 before | 0.12 after |
|---|---:|---:|
| Median complete-loop frame time | 16.66 ms | 16.66 ms |
| p95 complete-loop frame time | 17.14 ms | 17.07 ms |
| Stationary-orbit rendering CPU median | 2.45 ms | 3.39 ms |
| Stationary-orbit GPU median | 4.71 ms | 4.25 ms |
| Peak process RSS | 139.83 MiB | 147.42 MiB |
| Addressed stars in the same galaxy-facing view | 16,889 | 29,334 |
| Sampled activity bodies in the fixture's existing system | 0 | 209 |

The extra stellar refinement and activity extraction add about 0.94 ms of rendering CPU time in the common stationary orbital phase. Peak RSS increases by 7.59 MiB (5.4%). The standard insertion view contains fifteen visible activity objects; the close harbour view contains ten, with 1,650 activity triangles. The actor pass uses only 64,800 bytes of templates in this fixture, below the 170,468-byte total for all eighteen templates.

[Normal starting orbit](orbital-activity/captures/busy-orbit.png) · [Harbour and ships](orbital-activity/captures/orbital-activity.png) · [Denser addressed sky](orbital-activity/captures/stellar-background.png) · [Preserved atomic flight](orbital-activity/captures/atomic-shrink.png)
