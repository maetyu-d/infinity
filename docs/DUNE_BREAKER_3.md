# Dune Breaker 3.0 — shared car model and vehicle physics

Open **Dune Breaker.app**, press **Enter**, and drive with **WASD** or the arrows. **S** applies the service brake before selecting reverse; **Space** locks the rear handbrake; **Shift** boosts. **C** switches cockpit/chase views. Hold **F** stopped to repair. **R** recovers at the previous gate with a time penalty and retains damage.

## The player buggy

The cockpit and exterior now come from the same vehicle model in the same world coordinates. Looking around no longer rotates a separate imitation bonnet with the camera. The driver's eye sits in the left bucket seat, and the bonnet, front tyres, suspension, dashboard, hands and roll cage all remain attached to the actual sprung body.

A low tapered nose, separate side pods, open cockpit, tubular cage, engine cradle, spare tyre, lamps and whip flag define the silhouette. The driver sits inside the cage with connected arms, legs, helmet and harness. The steering rim, hands, three instrument needles and gear lever have physical geometry. Tyres have layered sidewalls, recessed hubs, spokes, embedded tread blocks and independent rotation. Upper/lower suspension arms and connected coil springs follow each actual wheel. Panel dents and tyre damage remain visible.

The renderer retains the Ecstatica-inspired implicit ellipsoid surfaces. The changes do not replace those surfaces with a triangle skin. The normal GPU, CPU reference, LUT, retro and diagnostics remain available.

## Vehicle simulation

The simulation runs at **240 Hz**, independently of rendering. Geometry and physics share one set of dimensions: 2.72 m wheelbase, 2.02 m track, 0.46 m healthy tyre radius and 0.38 m suspension compression travel.

- Four terrain struts have separate compression, progressive bump stops, different bump/rebound damping, digressive damper response, axle anti-roll forces and damage-dependent support. Contact forces act below the centre of mass and create pitch, roll and load transfer.
- Each wheel has rotational inertia, angular velocity, longitudinal slip, lateral slip relaxation and its own steered contact direction. Ackermann steering gives the two front wheels different angles. Driving/braking and cornering share a finite grip envelope; high loads and damaged tyres change available grip. A locked rear handbrake produces tyre sliding rather than simply reducing a grip constant.
- Engine torque passes through five automatic forward ratios, a reverse ratio, final drive, shift hysteresis and a finite shift interruption. Front/rear torque distribution and axle limited-slip coupling drive the four wheels. Progressive traction assistance limits excessive wheelspin. RPM, wheel motion, braking and the instruments use these live states.
- Gravity, tyre forces, surface normals, aerodynamic drag and rolling resistance determine translation. Body torque and angular inertia determine rotation; quaternion integration permits full rotations. Airborne tyres can spin without producing forward thrust, and an inverted buggy rests on its cage rather than receiving suspension support through the roof. There is no automatic midair levelling.
- Swept chassis lobes replace the old circular obstacle collision. Contact impulses account for angular inertia and surface friction. Glancing impacts retain tangential movement, and off-centre hits turn the car. Car contacts exchange equal and opposite impulses; AI and player use the same rules.

Component damage and repairs remain part of the simulation: damaged engines lose torque, damaged tyres lose grip and radius, bent alignment pulls the steering, and damaged dampers lose authority. A recovery is not a repair.

This is a purpose-built game vehicle model. Struts sample a heightfield, collision shapes approximate the body, tyres use a combined-slip approximation and sand does not deform. The model is not a complete mechanical multibody or tyre carcass simulation. Native graphics and packaging are exercised on macOS arm64/Metal.

## Validation

The desert test suite exercises suspension settling, braking/reverse, boost, seeded scenery, swept collisions, component damage, hard landings, repairs, retained recovery damage, wheel spin, airborne motion, steering, grip bounds, contact conservation, rollovers, timestep consistency and complete seeded races. Ellipsoid tests verify geometry, normals, depth, LUT behavior, articulation, damage deformation and shared vehicle placement.

```sh
cmake --build build --target dune_breaker infinity_desert_tests infinity_ellipsoid_tests -j 4
ctest --test-dir build -R '^(desert_buggy_racing|ellipsoid_geometry_and_depth)$' --output-on-failure
./build/dune_breaker --demo --frames 240 --mute --capture build/dune-v3/cockpit.ppm
./build/dune_breaker --demo --chase --frames 240 --mute --capture build/dune-v3/chase.ppm
./build/dune_breaker --damage-demo --chase --frames 240 --mute
python3 tools/package_desert_macos.py build/dune_breaker 'Dune Breaker.app'
```

Best times use `best-v3-<seed>.txt`; earlier records remain intact because the handling has changed. Damage remains local to the current race. Universe saves are independent.
