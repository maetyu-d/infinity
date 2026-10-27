# Cockpit — Infinite 0.14

The cockpit now uses a readable flight deck with an open central windscreen. Checkpoint `4c41677` preserves the working 0.13 release before the presentation changes. The universe, planets, moons, material hierarchy, navigation rules and ship dynamics remain intact.

## Hyperion reference

The reference is the user's native Hyperion game. `Hyperion/src/flight_renderer.cpp::Renderer::flightHUD` separates navigation, velocity, scope and ship systems into lower-deck rails. It uses backed context cards, an outlined aiming mark, reserved label space around the aim point, and short contextual actions. Its landing scope changes with the existing flight state. Infinite translates those techniques into its own C++ meshes and bgfx overlay; it does not introduce Hyperion's economy, combat or world coordinates.

`cockpitMesh` retains the existing beveled cabin construction, with its lower deck raised to support the new instruments. The overlay remains downstream of world rendering and colour processing, so instrument text keeps a consistent contrast in all three rendering modes.

## Instruments and interaction

- Four recessed lower-deck panels separate navigation, velocity/braking, the scope, and throttle/hull/gear readings. Navigation shows the actual selected destination, range, ETA and contextual keyboard actions. Velocity remains the ship's physical SI speed.
- Planet/moon and activity lists have dark backing, clearer type hierarchy, selected-row highlighting and explicit occultation. The selected activity appears among the six displayed rows even when it is distant. A display-index mapping preserves the underlying order and IDs used by K cycling.
- The scope is larger, with inner/outer rings, a forward indication, elevation stems, world diamonds, traffic points and an amber selected target. Near terrain, it becomes a landing director driven by the existing pitch/roll relative to the terrain normal. Altitude, vertical speed, drift and gear state accompany it. The orbital altitude display no longer warns about landing attitude from thousands of kilometres away.
- Reference magnification and physical hull size remain separate instruments. Reference changes show REFINING/WIDENING; shrinking shows the requested hull size, automatic/manual state and blocked regrowth. A small ship's parent-terrain altitude is identified explicitly.
- Target brackets use the real camera projection. Targets outside the readable windscreen get a bearing-preserving edge arrow, with an AFT indication where appropriate. Range labels fit within the windscreen instead of extending beyond the window.
- At most three secondary world labels accompany the active target. A rectangle-reservation pass avoids other labels, fixed panels and the aiming reticle. The complete contact lists, scene geometry, lights and picking remain available.
- **F1** opens/closes a backed flight-control guide. The permanent multiline help is removed from the windscreen. The guide blocks click-through selection and does not pause, reset or steer the ship. **Tab** still hides the cockpit/HUD, and **H** still cycles rendering modes.
- Contact drawing and hit testing use the same layout. Instrument backgrounds intercept clicks so world contacts behind them cannot steal selection. Hidden HUD panels do not intercept world selection.

Small units now preserve their case (`mm`, `um`, `nm`, `pm`); numbers shrink to fit available width before a unit is lost. The built-in font merges adjacent lit cells into horizontal strips, reducing duplicate geometry without adding a font library, atlas, image asset or shader pass. The HUD still submits the same three overlay meshes.

## Preservation and validation

[Preservation hashes](cockpit/preservation.json) verify that 16 coordinate, generation, streaming and ship/activity files match the checkpoint exactly. The existing frozen legacy-world fingerprint test and deterministic planet/moon tests remain part of the regression suite.

The new headless cockpit test covers square, 4:3, 16:9 and ultrawide layouts; panel and vertex bounds; exact on-screen target projection; front/back edge cues; dense label overlap and reticle exclusion; large and small SI units; and selected-traffic display without changing its source order. The native flight fixture retains the three-mode camera/geometry proof, harbour picking, landing/launch, micro/nano shrinking and real moon-family rendering. It additionally checks instrument click interception and captures the F1 guide while asserting an unchanged camera.

[Normal orbit](cockpit/captures/busy-orbit.png) · [Landed](cockpit/captures/landed.png) · [Atomic flight](cockpit/captures/atomic-shrink.png) · [Moon family](cockpit/captures/moon-family.png) · [Controls](cockpit/captures/controls.png)

The same normal-orbit fixture before the change is preserved in the [0.13 capture](planetary-populations/captures/busy-orbit.png).

The native cockpit flight fixture measured a **16.66 ms median frame** and **17.21 ms p95** at 1440 × 900 on Metal, with 146.95 MiB peak process RSS. These are single-run measurements of the complete flight fixture, rather than a claim of a general speed-up. [Raw frame data and validation](cockpit/validation.json) accompany the screenshots.

All **27 release tests passed in 166.43 seconds**. The packaged 0.14 app is signed and retains the existing ship/activity save schemas (3/1).
