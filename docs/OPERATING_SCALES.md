# Planetary flight, long voyages and a shrinking ship — Infinite 0.10

The universe remains generation recipe 3. Its geometry generators, physical descriptors, spatial addresses, histories, procedural seeds, orbital snapshots, terrain/geology and crystallography are unchanged. Checkpoint `4906530` preserves the working 0.9 ship integration. This change replaces the always-scaling cruise/probe model with three explicit operating regimes.

## Normal flight among planets

New games start in orbit around an existing temperate planet, selected deterministically from the existing galaxy. The ship has a nominal **12 m hull** and **2 m contact radius**, with long-range cruise off. Merely approaching an object or changing camera/render units does not shrink it.

The nominal open-space envelope is **85 km/s**, based on Hyperion's local-flight pacing. It reduces continuously toward **350 m/s** near terrain; boost and the existing speed multiplier remain available. This preserves Infinite's actual planetary sizes and orbital distances. It does not compress a star system into a replacement Hyperion world.

**Y** cycles the current system's existing planets, including targets too distant to resolve as meshes. **J** selects assisted transit toward that address. The approach/landing loop, persistent throttle, inertial/assisted handling, braking, gear and launch remain available. Selecting a departure destination while landed first releases the clamps and performs the ordinary thrust-driven lift-off. Paths below a planetary horizon receive above-surface waypoints.

## Optional long-range cruise

**N** engages a distinct long-range drive. Its initial ceiling is **100 light years per second**; **-/+** halves/doubles that ceiling while engaged, bounded between 1,000 km/s and 10^28 m/s. This allows both lengthy interstellar/intergalactic voyages and faster inspection runs. Proximity can reduce the effective limit well below the selected ceiling.

Travel integrates a continuous physical trajectory. It never replaces a journey with a destination teleport. Velocity remains in SI units and rotates only when changing coordinate-frame axes. The existing streamed root cells and local reference frames continue to hand off beneath the ship.

The cockpit displays elapsed flight time, travelled distance and ETA to an active destination. Those counters and the chosen drive ceiling are persisted. Cruise continues with neutral helm input when the open application loses focus; ordinary close-quarters flight pauses in that situation. Closing the app saves the current pose and motion; offline time is not fast-forwarded. Navigation assistance is disarmed on restart, while destination and physical velocity remain saved.

The renderer hands the same addressed galaxy particle records to the flight controller. This lets a ship that starts among planets subsequently approach stars outside the small locator catalogue, without first visiting a galaxy overview. Background lights and entered systems retain the same IDs and generated positions.

## The actual ship shrinks

**Z** engages a fictional shrinking field. It initially targets a **12 mm hull**, then fits smaller surroundings using actual surface clearance, object scale and inherited mineral cell dimensions. The smallest supported hull is **12 pm**. **Comma / period** switches to manual control and requests a smaller/larger hull by one decade. Z again or **Shift-Z** requests full size.

Size changes occur at at most two decades per simulated second. `logSize` and `targetLogSize` describe the vehicle, independently of `Navigation::logDistance`, which describes render precision. The cockpit separately reports **ship size**, requested size and **view scale**. Its meshes are authored in body-relative cabin units, so the interior retains its proportions while the physical vehicle footprint changes.

The field does not multiply velocity or move the camera when activated. Braking and acceleration act over time. The 2 m contact radius scales with the hull; landing height, safe contact speeds and lift-off commands scale accordingly. The field compensates bulk gravity to preserve body-relative handling. Long-range cruise is inhibited while the ship is reduced or the shrinking field is active.

An adaptive **scale transit** envelope allows a tiny ship to cross the gaps between nearby structures without requiring years to traverse a centimetre. It slows near actual material surfaces and atoms. This is propulsion from the special field, not a change in world distances or an automatic full-size-ship warp mode.

Rock and grain contacts query the existing authoritative SDFs. Once the hull is smaller than one tenth of the inherited reference cell dimension, the field accesses interatomic space and bulk continuum contact yields to resolved atomic contacts. Atomic spheres come from the same immutable generated lattice used by rendering; swept contact checks prevent a fast tiny ship from tunnelling through them. Chemical bonds remain graph visualizations, not invented solid bars. The existing inspection sections expose nested material without rewriting bulk density fields, atom sites or object geometry.

Growth checks available space before enlarging the hull. **GROWTH BLOCKED / MOVE TO OPEN SPACE** leaves both the surrounding material and the ship's position intact. It does not carve a larger cavity or eject the ship through a surface. The same body-size state and collision checks operate in Original, Hyperion and hybrid rendering modes.

Planetary atmosphere follows the actual medium and observer location, not the shrinking toggle. Extremely short optical paths are skipped because they contribute negligible scattering; a smaller ship does not turn the planet's sky into a different scene.

## Persistence and compatibility

`ship-<seed>.txt` now uses `INFINITE_SHIP 3`. It stores size, requested size, automatic/manual fitting, drive selection and ceiling, voyage counters, velocity, angular velocity, thruster effort, hull, gear and destination alongside the existing camera database. Versions 1 and 2 remain readable. Old overview saves migrate into planetary play on first upgrade; current saves preserve an ongoing large-scale voyage exactly.

**V**, F, Backspace, wheel navigation and the guided tour retain the original inspector. Returning from a micro/nano inspector explicitly deploys a correctly scaled ship at that view. This compatibility operation is separate from activating the gradual shrinking power during flight.

## Validation

The operating-scale tests check planetary startup, full-size normal flight, a smooth planetary speed envelope, explicit cruise activation without position/velocity jumps, root-cell crossing under propulsion, voyage counters and persistence, micro/picometre size transitions, size-independent render-unit changes, safe restoration in open space, blocked growth inside a specimen, swept atomic contacts and entry into a rendered star after a planetary start.

The existing ship handling, landing, navigation, deterministic round-trip, structural generation, streaming, native boundary and three-mode image checks remain enabled. Native cockpit fixtures additionally capture a real micrometre-size ship and a 12 pm ship among the existing atoms, after landing and launching on unchanged terrain.

Release logs, geometry-preservation hashes, measurements and captured images are stored in [operating-scales](operating-scales/).
