# Planetary populations — Infinite 0.13

The former system catalogue contained 5–9 planets and no addressable moons. Each system now retains those exact worlds and appends 8–12 primary worlds, for **13–21 total**. Regular moon families are generated where the host's gravity permits them. The default starting system has **13 primary worlds and 44 moons**, compared with five planets before this change.

These are actual universe objects. Each new body has a persistent address, orbit, inherited environment, spherical terrain, geology, rocks, minerals and atomic specimens. They are available to the existing scene extraction, asynchronous generation, landing and shrinking systems.

## Generating processes

`Universe::planetOrbits` retains the original orbital prefix exactly. New IDs try three compact inner slots above the stellar/irradiation exclusion region, then gaps between existing orbital excursions with conservative Hill margins. Bodies that cannot fit go into a more widely spaced outer family. Inner exclusions grow for the existing companion-star case. New primary radii span roughly 250–2,820 km, adding smaller rocky and dwarf worlds alongside the original larger planets. Temperature and volatile retention still derive from the star and inherited chemistry.

`Universe::moonCount` computes the host's Hill sphere at pericentre using its existing radius and mineral density. The inner boundary lies outside a conservative Roche limit, including the lowest-density ice case. Moon semi-major axes use a 1.8 spacing ratio and remain inside 0.32 Hill radii; eccentricity and inclination are small and deterministic. The desired family has 2–6 moons, reduced to the available capacity. Some close-in worlds correctly have zero or one. Moon mass is capped at 0.4% of the host's approximate mass.

Moon positions add a host-relative Kepler orbit to the actual primary position. Their orbital gravitational parameter uses the host's mass. Smaller bodies retain less water, cool more rapidly and inherit the host's chemical reservoirs and environmental history before their own geological fields are generated. New-body equilibrium temperatures have a 2.725 K floor.

These are analytic, physically constrained procedural recipes, rather than a long-term N-body stability simulation. The existing renderer still uses its fixed orbital epoch; this change does not introduce moving planetary ephemerides. Existing planets retain the original recipe even where a newer physical constraint could produce a different result.

## Identity and continuity

Checkpoint `b6dd70c` preserves the working 0.12 tree. Before changing the generator, `infinite_population_snapshot` recorded **120 descriptor and geometry fingerprints** from that implementation. The checked-in [legacy snapshot](../tests/data/legacy-0.12.txt) covers three seeds, galaxy/system descriptors, every original planet in nine systems, and full surface-to-lattice specimen branches. The preservation test compares current output against those frozen values; it does not regenerate its expectations.

The original generation recipe remains 3. The population extension has its own version 1. Original IDs and their `childSeed(parent, kind, id)` values remain unchanged. Additional primaries use appended integer IDs. Moons use a reserved high-bit ID containing the primary ID and moon ordinal. They occupy the existing `Planet` body level under `System`, with an explicit `satelliteOf` relationship. No hierarchy levels or enum values move, so all original surface, rock, grain and lattice addresses remain valid. A moon refines through that same material hierarchy.

The system's old logarithmic coordinate unit is preserved. Its physical population envelope is now queried separately for flight transitions and visible bounds; outer additions do not move established planets by silently changing the frame unit. Camera and ship-save formats remain unchanged.

## Flight and presentation

- **Y** cycles primary planets and dwarfs. **Shift-Y** cycles moons of the selected or current primary. **J** approaches the selected body; **L** uses the existing terrain landing controller. Automatic proximity flight can enter a moon's frame directly, without F.
- The cockpit reports total planets/moons and lists the nearest eight celestial contacts, keeping a selected distant destination visible. The full catalogue feeds the scope, lights and picking. The default catalogue allowance is 512, above the maximum population; explicit smaller caller limits still work.
- Resolved sibling planets and moons use their generated sphere meshes while the current body's terrain streams. Previously, enabling terrain also suppressed sibling planet geometry; that condition now applies only to the active terrain ancestry.
- Distant point-light occultation uses only screen-resolvable occluders. A resolved celestial disc is considered hidden only when fully covered, avoiding the disappearance of partially visible moons.
- Orbital activity retains full sampling near the ship and for a pinned target. Far primary worlds use their harbour/yard beacons until approached. Moons use their natural surface geometry. The existing worker, CPU/GPU cache and terrain budgets remain in force; orbit layouts use a separate bounded 64-system memo.

All three rendering modes consume the same addresses and geometry. Changing mode does not regenerate a world. A normal saved view resumes in place; R still returns to the established original starting planet.

## Evidence and performance

The population regression passes 9,123 checks across 12 systems (197 primary worlds and 655 moons). It checks deterministic parallel/reverse-order replay, complete catalogues, large-ID persistence, inherited thermal constraints, host-relative positions, Hill/Roche limits, moon mass ratios, direct automatic frame entry, geological landing and a moon-to-atomic-lattice round trip. The native cockpit fixture also requires submitted moon geometry and captures the moon beside its real primary. All **26 release tests passed in 194.05 seconds**, including native automatic flight, cross-scale round trips and rendering-mode image checks. The packaged app passed live Shift-Y selection, J moon approach and B cancellation checks in isolated test data; ship/activity save schemas remain 3/1.

Metal, 1440 × 900, same seed, physical starting planet and identical three-mode orbital cameras. The before executable was frozen from 0.12 before editing. The after loop adds an 80-frame moon-family view. No build or other test ran alongside either measured loop. Single-run values include scheduling variation; VSync limits wall time.

| Measurement | 0.12 | 0.13 |
|---|---:|---:|
| Starting system primary worlds | 5 | 13 |
| Starting system moons | 0 | 44 |
| Median complete-loop frame | 16.66 ms | 16.66 ms |
| p95 complete-loop frame | 19.62 ms | 17.16 ms |
| Stationary Hyperion orbit CPU median | 4.70 ms | 3.15 ms |
| Stationary Hyperion orbit GPU median | 3.91 ms | 3.56 ms |
| Peak process RSS | 144.13 MiB | 149.81 MiB |

Peak RSS increases by 5.69 MiB (3.95%). The single-run timing differences do not establish a general speed-up. The new catalogue substantially increases real destinations while screen-space drawing and bounded distant activity keep the measured scene responsive. [Raw measurements and logs](planetary-populations/validation.json) retain the exact sample definitions.

[Real moon and host planet](planetary-populations/captures/moon-family.png) · [Normal starting orbit](planetary-populations/captures/busy-orbit.png) · [Expanded system contacts](planetary-populations/captures/system-contacts.png) · [Preserved atomic flight](planetary-populations/captures/atomic-shrink.png)
