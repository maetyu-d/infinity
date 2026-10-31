# Ship handling and roll

## Restored classic controls — Infinite 0.39.1

At the user's request, ordinary assisted manual flight now uses the pre-0.37 controller again. This restores the actual implementation from `7a35cf7`, rather than changing gains in the newer physical controller. Mouse response is linear at the original gain, pitch/yaw/roll return to 1.20/1.10/1.80 rad/s, and the original bounded exponential rate servo controls attitude independently of the translation jets. The velocity-scaled 44/48/52 lateral/vertical/forward jet envelope, 32 reverse authority and 120 ms spool restore the previous translation and drift-correction response. The later normal-flight finite-braking speed cap applies to navigation/departure/inertial regimes, while manual assistance uses the original proximity-governed speed limit.

**W/S**, **A/D**, **Q/E**, mouse/arrows, **U/O** and **Alt+A/D** retain their established bindings. Release the helm to stop rotating at the attained bank. **B** retains its recovery priority over other held controls. **X** remains the explicit inertial option; navigation and launch retain the newer finite-force solver. Cruise, shrinking, collision/terrain safety, cargo access, histories and existing controls remain available. No additional control-mode menu or preference is introduced.

The old controller is deliberately more forgiving about redirecting travel. Its manoeuvring authority scales with SI speed and the active proximity speed envelope, so it is no longer restricted to the newer ordinary-flight hull thrust ceiling. Cargo still adds real mass/inertia and reduces available classic acceleration by the payload ratio. Current gravity and atmosphere queries, collision feedback and cockpit motion remain in place. Braking-time/distance instruments use the active manual authority. Save format 4 and old-save compatibility are unchanged; switching back to the classic controller retains velocity and attitude and clears only obsolete angular actuator demand on the first manual step.

Seven recorded historical handling probes match the restored controller within **1e-8** (the recorded decimal precision). This includes roll rate/release/reversal, assist engagement, two-second drift recovery and a powered turn. The important drift fixture starts with 1,265 m/s of sideways velocity: after two seconds the newer finite-thrust assist left **1,182.48 m/s**, while the restored controller leaves **1.77 m/s**, matching the historical result. Roll returns to **1.799923664 rad/s**, released roll settles in **0.317 s**, and bank remains free through inversion and full barrel rolls. [Before](classic-handling-0.39.1/before.json), [restored](classic-handling-0.39.1/after.json), [historical comparison](classic-handling-0.39.1/legacy-comparison.json).

All **38 core tests** and **four native gameplay/image tests** pass. The handling suite retains **185 checks**, including recorded legacy response, mouse mappings, independent roll under powered strafe/lift, 15/30/60/144 Hz, loaded cargo, B recovery from a tumble, SI frame changes and saved angular state. The physical math/inertial/rendezvous tests remain; the superseded assisted finite-thrust expectation is replaced by classic recovery/continuity. Native checks cover landing, launch, both roll directions/release, atomic shrinking, restored saved flight, cockpit fitting and the economic construction loop. [Core results](classic-handling-0.39.1/core-tests.log), [handling checks](classic-handling-0.39.1/handling-tests.log), [native results](classic-handling-0.39.1/native-tests.log), [native pilot details](classic-handling-0.39.1/native-pilot.log).

**190 protected source files** remain byte-identical to `3b22781`, including every universe generator, spatial address/hierarchy implementation, procedural seed, city/terrain/weather mesh, renderer/shader, economic system, underlying force math and cockpit-motion controller. Only the piloting/controller source changes in that source set. [Preservation hashes](classic-handling-0.39.1/preservation.json). The signed local app is **0.39.1 / build 44**; its 28 Mach-O code/data sections match the tested executable. [Package verification](classic-handling-0.39.1/package.json).

## Hyperion-only graphics — Infinite 0.38.3

Full-strength, colourful Hyperion rendering is the sole graphics style. The Original and Hybrid enum values, renderer branches, legacy instancing program and billboard buffer, H toggle, HUD hints and old mode-comparison utilities have been removed. Existing `display.txt` preferences are ignored and are no longer written. `--render-mode hyperion` remains a compatible explicit launch argument; `original` and `hybrid` are refused. The control guide and README list the current controls. F7 cockpit motion, physical ship handling, saved flights, universe generation and deterministic seeds remain intact.

Native validation now captures one Hyperion view per scene. `--presentation-test` replaces the comparison tour: it follows nine scales inward and returns through the same addresses, checking geometry, camera and generation state. Weather/city, star, planet, terrain, cockpit-motion and market fixtures retain their substantive tests without rendering duplicate retired styles. Earlier sections below are historical release records and include the former multi-mode comparisons.

Validation: [37 core tests](hyperion-only-0.38.3/core-tests.log), [72,521 presentation preservation checks](hyperion-only-0.38.3/presentation-tests.log) and [21 native checks](hyperion-only-0.38.3/validation.json) passed. The native run covers ignored legacy preferences, refused removed CLI choices, six cockpit-motion cases, trading, stellar/planet varieties, planetary depth stability, cities/weather, terrain, landing/roll, fitted UI and seventeen nine-scale round-trip captures. City-shadow reuse is measured in settled capture/readback frames; terrain streaming and morphing legitimately redraw shadows during loading. [Final image checks](hyperion-only-0.38.3/final-image-tests.log) supersede the old loading-frame cache limit and renamed checker path in the initial run.

The [matched Hyperion world crop](hyperion-only-0.38.3/visual-preservation.json) is bit-identical to 0.38.2 across 1,362,750 colour channels. [138 universe, physics, economy, shader and cockpit-motion source files](hyperion-only-0.38.3/preservation.json) remain unchanged. The [signed 0.38.3 / build 41 app](hyperion-only-0.38.3/package.json) matches all 28 tested code/data sections.

## Cockpit inertia and feedback — Infinite 0.38.2

The cockpit now has a separate presentation-only head-motion controller. It reads actual body-axis proper acceleration, angular velocity, engine demand, hull loss and transient collision impulses. Exact damped springs provide a small lean against applied forces, a lag during turns, cabin parallax and short recoil/impact jolts. Smooth, band-limited vibration grows above 1.3 G and remains quiet during idle/coasting. Soft landings receive a much smaller impulse than a hard contact. No ship force, torque, inertia, control gain, seed or generator was changed.

The visual head is limited to about **0.57 degrees** of total rotation and **9 mm** of translation in the full-size cabin. Translation affects the nearby cockpit mesh; the world retains its existing double-precision eye and receives only the tiny head rotation. Geometry, stars, sky, atmosphere, contact bearings and picking use that same visual rotation. The collimated reticle projects the original ship-forward aim, so shaking the view never changes where the ship points. Instrument cards remain steady and clickable. The effect scales with the hull's existing shrink field, independently of render magnification, and settles when a manoeuvre ends.

**F7** switches cockpit motion off/on. Its preference is saved separately in `user-data/cockpit-motion.txt`; ship save format 4 remains unchanged. Markets and the controls guide temporarily use a neutral view, and the guide includes the new binding. Returning to the explorer camera clears presentation motion.

There is still no player weapon/combat system. As requested, this update provides recoil hooks only: a future weapon should call `Renderer::cockpitImpulse(CockpitImpulse::Recoil, strength)` once per real shot. `strength` is normalized to 0–1. Hull loss already triggers damage feedback automatically; collision/damage pairs are coalesced so a single collision does not shake twice. The hooks never create a projectile, apply ship forces or consume ammunition. Their state and vibration phases are separate from universe random seeds.

Measured Release-build probes on Apple M1: a representative loaded turn produces **0.361 degrees** of lean and **6.12 mm** of cabin displacement; the 80%-strength recoil probe peaks at **0.052 degrees**, and the damage probe at **0.117 degrees**. Feedback decays to neutral. The implementation uses fixed-size controller state and the existing rendering passes, with no new textures, render targets, shaders or dependencies.

The new unit suite covers 6,044 checks for idle quietness, force direction, settling, recoil/impact/damage events, contact deduplication, repeated-fire saturation, invalid input, frame-rate consistency, shrinking to atomic hull sizes, unchanged identities/ranges, stellar alignment, reticle/aim projection and unchanged saved flight. The existing landing suite also verifies that a real geological touchdown publishes the presentation event and that its acceleration/rotation telemetry matches the physical ship.

All [37 core tests](cockpit-motion-0.38.2/core-tests.log) and the expanded [6,015-check landing/flight suite](cockpit-motion-0.38.2/contact-tests.log) passed. Seven existing native UI, flight, roll, landing and galaxy-to-atomic checks passed in the [regression run](cockpit-motion-0.38.2/native-regressions.log). The new fixture initially fell through the generic journey completion check after successfully capturing all eighteen cases; its dispatch was corrected, and both [native motion checks](cockpit-motion-0.38.2/native-motion-tests.log) then passed. The framebuffer checker finds visible feedback in every mode and **zero mean pixel error** between the neutral and fully recovered views. [Detailed native feedback log](cockpit-motion-0.38.2/native-motion.log), [unit probes](cockpit-motion-0.38.2/motion-tests.log), [134 preserved source files](cockpit-motion-0.38.2/preservation.json), and [signed package verification](cockpit-motion-0.38.2/package.json).

Same physical camera and world: [neutral cockpit](cockpit-motion-0.38.2/hyperion-neutral.png), [high-G head motion](cockpit-motion-0.38.2/hyperion-turn.png). The change is deliberately small in a still image; the spring's lag and settling supply the movement during play.

## Control recovery — Infinite 0.38.1

The physical torque/inertia model introduced in 0.37 retained the earlier fast helm settings. Combined pitch/yaw/roll commands could reach 2.46 rad/s, and releasing roll at 1.8 rad/s allowed a further 13.3 degrees of rotation. B enabled braking but did not suppress a concurrently held turn or throttle command. Returning from the explorer camera with V could also retain the previous assist-off setting.

The manual assisted rate envelope is now 0.70 / 0.65 / 0.95 rad/s for pitch/yaw/roll, with normalized combined inputs. Mouse gain is 40% lower and a continuous soft-centre curve gives small corrections finer control. The explorer camera's mouse gain is halved. Full keyboard input and unrestricted 360-degree roll remain available. Ctrl still provides precision flight.

Released, reversed and overspeed axes use stronger rate feedback and faster valve response. Actual RCS torque still passes through the existing shared actuator allocation and physical rotation integrator. The hull, payload inertia, thrust limits, unassisted conservation, gravity, atmospheric model, navigation and universe generators remain authoritative. There is no artificial angular-velocity clamp or automatic levelling.

**Hold B** to recover: it enables assist and overrides all simultaneous helm, translation and boost input. Rotation arrests promptly; linear stopping still takes the time permitted by available thrust. **V** enables assist on returning to the ship. **X** remains an explicit choice of inertial flight, with a warning that spin persists. The cockpit shows the inertial recovery hint together with field/limited-thrust status. Save format 4 is unchanged.

Measured in the same deterministic Release-build vacuum fixture on Apple M1, with 60 Hz input and at-most-10-ms physics substeps; baseline commit `5910563`. These measure controls, not renderer performance.

| Probe | 0.38.0 | 0.38.1 |
| --- | ---: | ---: |
| Assisted roll rate | 103.13 deg/s | 54.43 deg/s |
| Roll release settling below 0.02 rad/s | 0.267 s | 0.150 s |
| Additional roll while stopping | 13.29 deg | 3.44 deg |
| Roll reversal | 0.233 s | 0.117 s |
| Peak simultaneous pitch/yaw/roll | 140.94 deg/s | 45.50 deg/s |
| Spin 0.5 s after releasing all axes | 0.0270 rad/s | 0.000000238 rad/s |
| Small yaw correction (15% helm) | 9.45 deg/s | 2.50 deg/s |
| Slip after the existing powered-turn probe | 47.38 deg | 26.79 deg |

The handling suite additionally covers roll at 15/30/60/144 FPS, all three axes with a full 4,000 kg cargo bay at 15/60/144 FPS, combined-input release, B recovery from an inertial tumble while all controls remain held, full barrel rolls, left/right symmetry, unchanged inertial coasting, frame/scale continuity and save round trips. Low-FPS settling comparisons allow one sampled display frame of timing quantization. The recovery fixture retains nonzero spin on its first frame, arrests spin below 0.001 rad/s within two seconds, and slows its initial 45.8 m/s translation through finite thrust rather than an instantaneous reset.

Evidence: [before](control-recovery-0.38.1/before.json), [after](control-recovery-0.38.1/after.json), [185 handling checks](control-recovery-0.38.1/handling-tests.log), and [133 unchanged generation, rendering, physics and economy sources](control-recovery-0.38.1/preservation.json).

All [36 core tests](control-recovery-0.38.1/core-tests.log) and [seven native checks](control-recovery-0.38.1/native-tests.log) passed. The native run verifies both roll directions at 0.95 rad/s, release to rest, landing/launch, the fitted cockpit, seamless flight and the galaxy-to-atomic return. The [signed 0.38.1 / build 39 package](control-recovery-0.38.1/package.json) matches all 28 code/data sections of the tested executable.

## Original handling upgrade — Infinite 0.25

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
