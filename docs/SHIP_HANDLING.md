# Ship handling and roll — Infinite 0.25

The existing ship already had U/O roll, but its slow shared rate governor and remote keyboard position made it easy to miss. This update improves manual handling and makes roll visible in the cockpit. Generation, coordinates, orbital snapshots, geology, cities, object identities, seeds and rendering are retained. Baseline **8d3bf01** is saved at `checkpoint/infinite-0.24-before-handling`.

## Controls

| Input | Action |
| --- | --- |
| U / O | Roll left / right, independent of mouse capture |
| Alt+A / D | Roll left / right near WASD; Option on Mac |
| A / D | Strafe, when Alt is released |
| Q / E | Descend / ascend along the ship's local vertical |
| Mouse / arrows | Pitch and yaw |
| Ctrl | Precision movement and reduced angular rates |
| X | Toggle assisted / inertial handling |
| B | Explicit brake; cancels guidance and assist capture |

F1 lists both roll bindings. The velocity instrument includes signed **ROLL DEG/S** and the U/O hint. Roll rotates around ship forward and can pass freely through inversion and a complete 360° barrel roll. Releasing the assisted helm arrests angular motion while retaining the attained attitude. Inertial mode keeps angular and linear momentum when no thrust is commanded.

## Controller changes

Manual assisted pitch/yaw/roll rate limits are 1.20 / 1.10 / 1.80 rad/s, previously 1.05 / 0.95 / 1.20. Ctrl retains the existing 0.35 angular multiplier. The assisted rate servo uses exact exponential response with stronger release/reversal damping and finite angular acceleration, bounded at 6.5 rad/s² accelerating and 9.5 rad/s² braking. It does not introduce automatic levelling or a fixed galactic up direction.

Assisted manoeuvring authority increases from 22/26 to 44/48 on the lateral/vertical axes, in the same existing speed/size-dependent thrust units. Main and reverse authority remain 52/32. The existing convex force envelope reserves gravity support first, lateral correction second and forward-drive demand last. Lateral velocity feedback increases from 1.6 to 2.4. The engine spool remains 120 ms. The stopping-distance instrument uses the corresponding current authority.

Navigation, controlled landing and thrust-driven departure retain their established torque and force settings. Inertial handling retains its bounded torque and angular governor. All modes continue using the existing physical SI integration, collision and spatial handoff.

Re-enabling assistance borrows Hyperion's actual `src/flight/handling.cpp` velocity-capture technique: an 800 ms smooth blend from the current SI velocity toward the forward-speed command. Translation input, braking, navigation and departure override capture immediately. The captured velocity rotates with reference-frame axes, just like stored ship velocity. This is transient guidance state; the version-3 ship save format is unchanged. Failed restores still leave the live ship intact.

`resolveHelmInput` is shared by the live SDL control path and regression fixtures. It combines bounded keyboard and mouse rate commands, routes modified A/D to roll and leaves Q/E lift intact. Mouse deltas remain distances converted to rates using frame duration; no new mouse-capture requirement or dependency is added.

## Measured handling

Deterministic vacuum fixture, same universe and initial state, 60 Hz input with the existing at-most-10-ms physics substeps. The test executable's `--measure` mode records the same probes against the previous and new controller. These are controller response measurements, not renderer performance claims. [Before](handling-0.25/before.json), [after](handling-0.25/after.json).

| Probe | 0.24 | 0.25 |
| --- | ---: | ---: |
| Assisted roll rate after 1 s | 68.74°/s | 103.13°/s |
| Release until rate falls below 0.02 rad/s | 0.517 s | 0.317 s |
| Additional rotation while stopping | 11.08° | 10.56° |
| Reversal until opposite rotation begins | 0.300 s | 0.200 s |
| Velocity change on first assisted frame, from lateral coasting | 3.367 m/s | 0.00051 m/s |
| Residual lateral drift after 2 s of capture | 24.73 m/s | 1.77 m/s |

The faster full-throttle yaw probe retains approximately the same slip angle (36.67° versus 36.70°), despite its increased turn rate. The ship still has inertia: high-speed turns require time and thrust to redirect momentum. Precision and throttle control remain useful near a landing site.

## Validation

**All 25 CPU checks and four targeted native checks pass.** The dedicated handling test has **137 assertions**. [CPU run](handling-0.25/cpu-tests.log), [handling probes](handling-0.25/after-tests.log), [native surface and flight run](handling-0.25/native-tests.log), [final native roll and image run](handling-0.25/roll-native-tests.log), [combined validation](handling-0.25/validation.json).

The new final screenshot initially needed an asynchronous-readback hold before fixture shutdown. The fixture now keeps rendering the held attitude for six frames; the repeated piloting and all 17 image checks pass.

Tests cover both bindings, opposing inputs, 30/60/144 Hz input, both roll directions, precision, unrestricted barrel rolls, bank retention, inertial coasting, exact saved angular state, an assist-capture rebase and the cosmic-to-atomic return route. Native framebuffer fixtures exercise right bank, left bank and stopped roll alongside the existing destination/landing/launch/shrinking and surface sequences.


Native captures show [right roll](handling-0.25/roll-right.png), [left roll](handling-0.25/roll-left.png), [released roll](handling-0.25/roll-stopped.png) and the [updated controls guide](handling-0.25/controls.png). The native proof measures ±1.8 rad/s and confirms retained forward heading in both directions, then a residual roll rate below 7e-11 rad/s after release. Twenty generation, spatial, simulation and renderer sources remain byte-identical to the baseline; see [preservation hashes](handling-0.25/preservation.json).

The signed local `Infinite.app` is version **0.25.0, build 25**. It resumes the existing flight and ship save.
