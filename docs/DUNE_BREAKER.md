# Dune Breaker 4.20

A standalone first-person C++ desert buggy racer using Infinity's deterministic core math, GLM, SDL3 and bgfx Metal stack. Open **Dune Breaker.app** and click **Start race** or press **Enter** to race against three rivals. The series alternates closed circuits and timed races along an endless generated route.

Version 4.20 makes Tow harpoon the unlimited basic weapon. Aim with the mouse and hold left-click to launch and reel; release to pay out slack, and right-click to cut the cable. Hooks attach to opposing cars or solid terrain, applying bounded point forces and torque to the actual chassis. Cables snag on terrain, checkpoint posts and intervening cars, and break under excess load or range. The gunner's panel shows cable load. Recovering detaches every cable attached to that car.

Rut stitcher and Dune jack are track pickups, available to players and rivals. Driving through a marked wireframe crate grants three Rut stitcher shots or two Dune jack shots; collected pickups respawn after 18 seconds of active play. Mouse-wheel selection switches between the harpoon and stored pickups. Inventory holds up to six rut shots and four jack shots; exhausting a selected pickup returns to the harpoon. Rut stitcher cuts three winding, nine-metre grooves into the persistent B-spline sand, up to 32 cm deep. Dune jack grows a 1.15 m mound over about a third of a second. Aim the terrain tools at reachable ground; they can trip rivals or build a ramp for the driver. The raised terrain is part of the shared mathematical surface, so ray marching, tyres, chassis collisions, weapon rays and tow-cable obstruction checks all see it. Grooves and mounds persist across laps and recovery; a new race clears them. Jacks cannot overlap, and the oldest is recycled after 32 active mounds.

The left view is driven with WASD or arrows; the right view is the second local player's mouse-controlled rear turret. Mouse capture starts when racing, and Escape/P pauses and releases the cursor. Multicolour acetate zones retain blue/cyan/green driver colours and coral/magenta/yellow gunner colours, with white/amber gate highlights. Vehicle studies retain a single orbit view. The compact rear launcher has enough depression to place terrain tools without shooting through its own chassis.

Version 4.19 restores crisp split-screen graphics. Vector cores, vehicle edges and text render in a separate sharp buffer up to 2048×1536, then combine directly at display resolution with the smaller CRT glow and glass layer. The live image is no longer enlarged from the 1024-wide CRT composite. The default split-screen terrain-ray floor is 384 pixels across both panes, with a 640-pixel ceiling; all nine fractal bands remain. Rendering each car component within its own bounds cuts repeated whole-car ray tests. F5 selects the faster 1024-wide beam setting. Multicolour player overlays, curved glass, glow, phosphor trails and gate highlights remain. The full-resolution setting favours clarity, with roughly 45 FPS accepted for split-screen play.

Version 4.13 replaces the interface with smooth, Retina-resolution text, clear instrument panels and clickable menus. The HUD keeps speed, gear, boost, race progress and a corner roadbook visible; damage indicators appear when needed, with one priority warning at a time. Pause, controls and results use larger panels with mouse buttons and keyboard hints. Results show your record, lap times where applicable, and the next event. Opening controls pauses driving and closing them resumes it; controls opened from an explicit pause keep the race paused.

Circuits start at three laps around a seeded loop, roughly 1 km long. Every lap crosses the same twelve gates and the same ground. All four cars' ruts, compaction, damage and suspension state persist across the start line, so later laps have a changed driving surface. Complete every gate in order to finish; the fastest time wins. Enter on the results starts the next event: drive as far as possible along an endless route before its three-minute clock expires. The route and checkpoint posts continue generating beyond the old 4.32 km finish. The next pairs use four laps/four minutes, then five laps/five minutes, before cycling. Each new event has a fresh seed and untouched terrain; R retries the same event.

The world combines terraced hills, smoother basins, elongated dune ridges and rougher high ground in a nine-band fractal heightfield. Ray marching and collisions share that mathematical surface, with fine geometry down to a 1.5 m noise-cell scale. The route does not reshape it. Version 4.15 renders it as a detailed Vectrex-style line display. A multiscale vector net and height contours trace the actual fractal and deformable surface on black; car edges, wheel rings and rotating spokes use the same box and cylinder geometry as before. Bright beams receive fixed screen-space overlays using [pnoq's RGB/CMY palette](https://store.steampowered.com/app/4408100/pnoq/), through a curved CRT face. Version 4.16 adds rounded glass, a dark bezel, edge-dependent beam focus, layered bloom, short phosphor afterglow, faint glass reflections and subtle analogue brightness variation. Live beam cores stay sharp in their own buffer; only their faint glow persists. The colour sheet stays fixed above the drifting beam. The UI uses an original single-stroke font and outlined instruments, and mouse targets follow the curved display. F8 switches between the CRT and clean vectors; `--sharp` starts in the clean mode. Colour comes from the screen overlay rather than solid object materials. Gate posts are highlighted separately with a bright white core and amber halo, so they stand apart from the terrain in every overlay zone. Version 4.17 targets a paced 60 FPS. It adjusts terrain ray counts using recent frame costs, while reconstructing continuous vector lines from the actual fractal slopes. The beam image is capped at 1024×768 pixels, composited through curved glass at that bounded resolution, then smoothly presented at the display's full resolution. Cars, wheels, gates and UI use the beam resolution independently of terrain rays. All nine fractal bands, 1.5 m fine geometry, physical ruts, colour overlays, glow and afterglow remain. Fixed GPU buffers avoid allocation stalls when the ray budget changes, and terrain-cache refreshes are spread across compact tiles. macOS pacing follows the display clock. `--retina` permits a higher terrain-ray ceiling within the same budget; `--uncapped` removes the CPU frame cap for measurement. Press F7 for a close terrain study. See [the graphics and terrain guide](DUNE_BREAKER_4.md).

Each car is one rectangular block, four plain cylindrical wheels and a rear gun turret. All four are drawn as visible vector edges, tinted by the overlay zone they occupy. Steering, suspension and damage still affect those shapes. The first-person camera sits above the block; there is no cockpit or driver model.

## Controls

| Key | Action |
| --- | --- |
| Enter | Start, resume or advance to the next race format |
| W / Up | Accelerate |
| S / Down | Service brake, then reverse |
| A / D or arrows | Steer |
| Space / Shift | Rear handbrake / boost |
| C | First-person / chase view |
| Mouse / hold left button | Gunner aims / fires; hold to reel the harpoon |
| Release left / right button | Pay out slack / cut the tow cable |
| Mouse wheel during racing | Select harpoon or collected pickup weapons |
| Mouse drag in vehicle study | Orbit the car |
| Hold F stopped | Field repair |
| R | Recover to the previous gate and wait five seconds; retains damage and ruts |
| Escape / P | Pause |
| R while paused or finished / N | Retry this race / generate a new track of this format |
| F2 / F3 | Buggy study / native or software rendering |
| F7 | Landscape fractal study / race |
| F8 | CRT glass and phosphor / clean vectors |
| F4 / F5 / F6 | Study orbit / faster rendering / field diagnostics |
| H or F1 / M / F11 / F12 | Controls / mute / fullscreen / PPM screenshot |
| Tab after finishing | Switch between results and free roam |
| Q from menu, pause or results | Quit |

Suspension, tyre slip, wheel rotation, gears, damage and field repairs remain mechanical. Circuit records store the lowest time for that seed and lap count; timed records store the greatest distance for that seed and duration. Combat races use separate `best-420-circuit-*` and `best-420-endless-*` files in `~/Library/Application Support/Infinity/DuneBreaker/`. The combat stage uses `best-420-stage-*`. Earlier non-combat 4.12–4.17 and machine-gun 4.18–4.19 records remain separate. `--data DIR` overrides the game data directory; use an absolute path when launching through macOS `open`. A record-save failure appears on the results screen and does not close the game.

Version 4.6 adds firmer tyre grip with progressive sliding, faster steering corrections, velocity-based suspension damping, and body collision solving at 120 Hz. Cars exchange friction and angular momentum across contact areas. Continuous translation and rotation checks catch fast impacts, while full block and wheel queries stop thin checkpoint posts from slipping through gaps between body samples.

All four cars now leave persistent tracks and deform the sand. Normal rolling presses shallow ruts; wheelspin and sliding cut deeper, up to 32 cm. The visible grooves use the same surface as suspension and body collisions. Compacted sand gains grip, while deeper ruts add rolling resistance. Tracks survive recovery and travel elsewhere; restarting or generating a new desert clears them. `--track-study` drives a short automated run, then opens an orbit view looking back over the actual tracks.

Version 4.8 improves tyre support at groove shoulders, adds progressive bump-stop damping, and uses a damped steering rack with a finite movement rate and feedback from front-tyre forces. Reset and recovery use the same four tyre-contact patches as driving, including damaged tyre sizes and suspension sag. Driving tests compare cars on clean and rutted ground to verify changes in suspension load, body motion, heading, steering response and coasting distance.

Version 4.10 solves all four tyres, wheel spin and finite brake torque together. The tyres respond to the chassis motion caused by the other tyres during the same physics step, including pitch, roll and yaw. Stopped tyres can hold the buggy on a slope without requiring continuous sideways creep. Brakes hold a rotor only when they have enough torque; rolling resistance from deep ruts also acts through wheel rotation and available grip. Braking, cornering and wheelspin share the same load-limited contact budget. The steering rack receives the resulting front-tyre forces. Engine braking now resists either rotation direction without incorrectly accelerating or reversing a rotor.

Version 4.11 doubles the rut limit from 16 to 32 cm and makes typical rolling grooves about twice as deep. Digging and compaction integrate the tyre footprint over the distance it travels, so short and long physics segments leave the same channel. Repeated passes deepen it smoothly toward its limit. Wheelspin still digs while stationary, but stationary tyres without slip do not. Unchanged saturated soil no longer triggers redundant terrain uploads. Collision and rendering use updated bounds for the steeper walls, and rolling resistance retains its dependence on physical depth.

Version 4.12 adds regional landforms instead of using the same roughness everywhere. Banks connect terrace levels spaced 12 m apart, wind-aligned ridges cross the plateaus, and finer fractal ridges strengthen toward high country. The same nine noise evaluations produce this variety. Height, slope and curvature bounds, normals and collision queries all use the new shape. The 32 cm deformable sand layer continues to affect tyre grip, suspension and rolling resistance.

## Build

```sh
cmake --build build --target dune_breaker infinity_fractal_tests infinity_desert_tests infinity_terrain_deformation_tests infinity_race_format_tests infinity_tyre_solver_tests -j 4
ctest --test-dir build -R '^(fractal_geometry_and_contacts|desert_buggy_racing|desert_persistent_terrain_deformation|desert_alternating_race_formats|desert_coupled_tyres_and_brakes)$' --output-on-failure
./build/dune_breaker
python3 tools/package_desert_macos.py build/dune_breaker 'Dune Breaker.app'
```

The native renderer is exercised on macOS arm64/Metal. The CPU renderer is a slow diagnostic reference. The shader embeds `include/infinity/FractalField.inl` at build time, so the app needs no external models, textures or shader source files. Audio is synthesized with AudioQueue.

`--circuit --laps 4` selects the starting circuit length. `--endless --duration 240` starts with a four-minute timed race; subsequent events still alternate. `--stage` retains the earlier 24-gate point-to-point format for development and regression comparisons. The timed clock pauses with the game and keeps running during recovery; recovery holds the vehicle for five seconds instead of subtracting time twice. Up to 32 fixed physics steps per rendered frame keep the simulation clock aligned through brief rendering stalls.

The older [ellipsoid notes](ELLIPSOID_RENDERER.md) and version 2/3 guides document earlier releases; the current default renderer is the fractal field described above.
