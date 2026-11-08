# Dune Breaker 2.0 — driving, damage and native graphics

Open **Dune Breaker.app** and press **Enter** to race. The new version keeps the procedural desert and real ellipsoid geometry, with a new driving model, component damage and a native-resolution GPU renderer on this Mac.

## Controls

| Control | Action |
| --- | --- |
| W / Up | Accelerate |
| S / Down | Service brake, then reverse |
| A / D or arrows | Steer |
| Space | Rear handbrake; allows the back of the buggy to slide |
| Shift | Nitro boost |
| F, held while stopped | Field repairs; the race clock continues |
| R | Recover to the last gate, +5 seconds; damage and remaining nitro are retained |
| C | Cockpit / chase camera |
| Escape / P | Pause |
| Enter from results or R while paused | Restart with a fresh buggy |
| F2 | Articulated ellipsoid study / race |
| F3 | Native analytic / software LUT renderer |
| F4 | Fixed / orbit camera in the study |
| F5 | Native graphics / 320×200 retro scene |
| F6 | Parts/axes/depth diagnostics; uses the CPU reference path |
| H / M / F11 / F12 | Help / mute / fullscreen / screenshot |

Start gently on steering inputs at speed. Braking before a turn leaves more tyre grip for the corner; a held handbrake releases rear lateral grip. Loose sand outside the compacted trail has less grip and more rolling resistance. Boost can carry the buggy over dune crests, where hard landings may damage it.

## Handling

The previous velocity-aligned steering is replaced by an inertial vehicle model. World velocity is retained through turns. Front/rear tyre slip angles, bounded lateral tyre forces, a braking/traction friction envelope and yaw inertia determine the resulting motion. Steering responds progressively and limits the wheel angle at higher speeds. Engine power, aerodynamic drag, rolling resistance, automatic gears, brake/reverse transitions and a limited reverse ratio determine acceleration and top speed.

Four terrain contact points each have their own suspension compression, spring/damper force, normal load, tyre state and visual travel. Contact forces support the chassis and generate pitch/roll torque; acceleration and cornering produce weight transfer. The buggy follows banked terrain, extends its suspension in the air and lands with finite damping. Gravity, short substeps and a chassis hard stop keep the simulation stable. Camera motion responds to the sprung body, acceleration and impact jolts.

This is a game-oriented four-contact suspension and two-axle tyre model, not a complete multibody motorsport simulator. The AI uses the same physics, grip and damage as the player, with curvature anticipation and steering feedback.

## Damage and recovery

Rocks, larger desert formations, cacti, checkpoint uprights and other buggies have collision proxies. The proxies come from the same seeded placement as the visible scenery. Small stones, scrub and route marker sticks remain cosmetic. Swept tests catch static obstacles; vehicle-to-vehicle contacts exchange impulses. Only closing impacts cause collision damage, so resting against an object does not continuously destroy the car.

Damage has separate chassis, engine, suspension, alignment and four tyre states:

- Engine damage reduces acceleration and disables boost when sufficiently severe. Critical chassis or engine damage disables propulsion.
- Damaged tyres lose grip and ride height. Uneven front tyre damage and bent alignment pull the steering.
- Damaged suspension loses spring/damper authority. Hard landings can hurt suspension and tyres.
- Dented panels deform the actual ellipsoid model. Tyres flatten and wobble, the buggy can sit unevenly, and low engine condition produces smoke. Impacts produce brief camera feedback, dust/debris and crash sound.

The vehicle status panel shows hull, engine, shocks and individual tyres. **Hold F below walking speed** to repair. Repair holds the buggy, restores components and progressively removes panel deformation; the status bar reflects its actual condition. It takes race time. **R does not repair or refill the buggy.** A new stage starts with a fresh vehicle. Damage is a race-session state; there is no persistent garage/career save.

## Graphics and sound

On Metal, original MSL programs ray-intersect instanced ellipsoids per fragment and write actual camera-space surface depth into the same hardware depth buffer as terrain. Their proxy quads only bound candidate fragments; visible object silhouettes are still implicit volumes. The GPU receives batches of centres, rotations, radii and materials. There is no polygon skin over the characters or vehicles.

Native graphics add smoothly rounded material shading, fine dune ripples and grain, atmospheric distance haze, a moving procedural sky and sun, more detailed mechanical models and contact shadows. The buggy has tread blocks, hubs/spokes, exposed spring/damper assemblies, engine/exhaust detail, safety belts, lights and a flexible flag. The cockpit has articulated steering, gloved hands, moving front suspension and physical gauge faces with moving needles.

Bounded transparent ellipsoid particles supply tyre dust, engine smoke, impact debris and sparks. They depth-test against the real scene and render after opaque geometry. The rendering remains matte and sculptural, without a PBR/plastic or bloom-heavy treatment. Engine pitch follows the live RPM, with tyre slip and impact transients in the synthesized sound.

The original 128×128 LUT and exact CPU reference paths remain available for comparison. F5 selects the 320×200 retro scene. `--cpu-renderer --hires` selects a 640×400 CPU reference. Diagnostic/high-resolution CPU modes are more expensive than the normal GPU path. Other platforms retain their existing software path; only macOS arm64/Metal has been exercised.

## Validation and reproduction

```sh
cmake --build build --target dune_breaker infinity_desert_tests infinity_ellipsoid_tests -j 4
ctest --test-dir build -R '^(desert_buggy_racing|ellipsoid_geometry_and_depth)$' --output-on-failure
./build/dune_breaker --demo --frames 360 --mute --capture build/dune-v2/cockpit.ppm
./build/dune_breaker --demo --chase --frames 360 --mute --capture build/dune-v2/chase.ppm
./build/dune_breaker --damage-demo --chase --frames 300 --mute --capture build/dune-v2/damage.ppm
./build/dune_breaker --retro
python3 tools/package_desert_macos.py build/dune_breaker 'Dune Breaker.app'
```

`--damage-demo` drives into a generated solid rock using the actual collision path; it does not just set a health meter. The test suite covers finite suspension, terrain support, braking/reverse, frame-rate-independent substeps, component damage, landing damage, retained damage through recovery, effective stationary repairs, damaged-engine acceleration, seeded obstacle stability and swept solid collisions. Complete races run on three seeds with the new handling. Ellipsoid geometry, normal, depth, LUT and articulation checks are retained.

Version 2 records use `best-v2-<seed>.txt` in the existing application data directory. Earlier best-time files are preserved separately because the handling model changed. Universe saves are independent of this game.
