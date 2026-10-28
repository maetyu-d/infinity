# Weather and inhabited surfaces — Infinite 0.19

Solid worlds can now have weather, surface cities, sealed colonies, mining and research outposts, terrain-fitted buildings and moving inhabitants. Availability follows the existing world's size, chemistry, temperature, water inventory, geology and landing conditions. Many worlds remain wilderness; gas envelopes have neither settlements nor a landable weather layer.

**Shift-K** selects a nearby surface port. **J** flies to its holding point 60 m above its actual terrain. **L** engages the existing assisted landing controller when nearby, and **Enter** launches. The ordinary **K** contact cycle and clicking work for both ports and orbital traffic. **I** reports population, protection and local weather. **F6** hides activity while its clock continues. The cockpit adds local temperature, pressure, wind and weather conditions. An empty surface-port search reports that no ports are detected rather than silently changing the selected world.

## Preservation boundary

Climate and society are read-only additions in `SurfaceLife.hpp`, `Weather.cpp`, `SurfaceLife.cpp`, `SurfaceLifeCache.cpp` and `SurfaceMeshes.cpp`. `SurfaceRendering.hpp` adapts those descriptors to the existing renderer. Cities never become replacement scenes or segments in the universe's spatial address. Each surface port has an activity identity in reserved slots 2000–2014 of its actual planet. Buildings and inhabitants have separate seeded IDs, and each resident belongs to a specific generated building.

The existing universe, population and terrain recipes remain **3 / 1 / 1**. The new climate and society recipes each start at **1**, using isolated `PlanetClimate` and `SurfaceSociety` feature keys. Existing feature constants, hierarchical seeds, descriptor fingerprints and generator outputs are unchanged. Weather uses the saved activity clock; neither weather evolution nor changing display modes modifies geology, history, orbits or atomic structure.

`Core.cpp`, `Universe.cpp`, `Processes.cpp`, `Terrain.cpp`, `Crystallography.cpp`, `Geometry.cpp`, `Navigation.cpp` and `Streaming.cpp` remain byte-identical to `d5cb05b`. Branch `checkpoint/infinite-0.18-before-weather-settlements` preserves that baseline. Source hashes and the legacy descriptor/geometry snapshot provide independent preservation checks.

## Weather generation

Atmosphere retention uses escape energy relative to thermal energy and inherited volatiles, with losses for very hot bodies and condensation on very cold worlds. Formation pressure is explicitly **not** treated as atmospheric pressure. A scale height follows temperature and the same density-based surface gravity used by flight.

The continuous spherical field combines latitude-dependent circulation, advected fronts and slower evolving eddies. Moisture and temperature distinguish rain and snow. Dry circulation produces dust fronts independently of moisture-driven clouds. Wind remains tangent to the surface, pressure falls with altitude, and precipitation stops above its cloud layer. Airless bodies have no wind, rain or snow. Field values depend only on the climate recipe, direction, altitude and saved time; cube faces, camera cells and render modes do not seed them.

Clouds are depth-tested translucent geometry around their real planet. Rain, snow and dust use bounded local particles/streaks anchored to absolute SI cells. Snow and dust have soft radial footprints with a screen-space size cap, so close particles do not become large solid squares. The existing Hyperion atmospheric pass consumes the same atmospheric descriptor. These are analytic climate and weather proxies, not a fluid solver; wind is currently a weather field and visual cue, without adding new forces to the ship controller.

## Habitation and terrain

Temperate worlds with retained atmosphere, water, suitable chemistry and sufficient age can support open cities. Local altitude, temperature and pressure are checked again at the proposed site. Other feasible worlds use protected colonies, resource-driven mining settlements or research outposts. Molten, excessively cold, extremely high-gravity and very small bodies remain uninhabited. Sealed sites use domed habitats and suited exterior workers.

Site search evaluates the existing spherical height field, terrain normals, water level and a 68 m landing footprint. The first port-served sites search around the pre-existing orbital harbour's fixed subpoint; others survey the globe. No location is generated around the camera. Sites must be naturally gentle, dry where liquid water is present, and separated from other settlements.

Cities concentrate taller buildings toward their centre. Colonies and industrial outposts use smaller protected modules, utilities, workshops and greenhouses. A parcel can be omitted when its footprint is steep or wet. Foundation depth follows the sampled ground beneath each building; the landscape is never flattened or displaced. Segmented roads follow the same height field. Buildings have conservative swept collision bounds, including foundations and rooftop equipment. The central landing area and public concourse stay clear.

Population derives from usable floors and building function. Resident IDs and homes can be reproduced for the full population without storing a person object for every inhabitant. The renderer takes a bounded, stratified sample, varies physical proportions and clothing, and samples continuous walking routes and duty periods. Severe weather keeps residents sheltered. Surface inhabitants are ambient residents; this release does not add conversations, interiors or a trading economy.

## Rendering and budgets

All three rendering modes consume the same structures. The existing Hyperion material, colour and compositing passes shade shared surface templates; Original retains its existing shading path. Hyperion's original `src/flight/settlements.cpp::buildPort`, `pavedRoad` and terrain-following `surface` frame were inspected as references for paved access, clear landing space and grounded facilities. Infinite's own society generation supplies the structures; Hyperion's world simulation is not imported.

Settlement descriptions generate on the existing oneTBB runtime, with at most two jobs and a **4 MiB LRU cache**. Leaving a planet cancels stale work. Completed publication also enforces the memory cap. Screen-space selection uses shared GPU templates at three detail levels; at most 6,000 surface instances and 96 resident samples per nearby settlement are considered. A global instance cap still applies. Clouds are bounded to 512 cells, local precipitation to 96 marks. No new shader dependency, render-target family or external library is introduced.

## Reproducing validation

```sh
cmake --build build -j 4
ctest --test-dir build --output-on-failure
build/infinite --surface-test --capture-dir build/surface-demo/captures --profile build/surface-demo/frames.csv --data build/surface-demo/data
```

The native demonstration uses separate data and does not reset the normal saved flight. It finds real catalogue examples of an open city and sealed colony, and captures weather from orbit, city approach, inhabitants, colony approach and a precipitation front in Original, Hyperion and Hybrid. The camera and actual streamed terrain geometry are compared across modes.

Core checks cover climate replay and continuity, atmospheric exclusion, rain/snow/dust, terrain-safe sites, foundations, roads, protected residents, home identities, movement, collision sweeps, cache budgets and eviction, cancellation, surface destination persistence, approach–landing–launch and preservation of the planet-to-lattice hierarchy. Existing full-scale, streaming, flight, persistence and legacy population checks remain in the suite.

Release results, measured profiles and representative images are stored in [weather-and-surface-life](weather-and-surface-life/).

## Release results and measurements

The full release suite passed **35/35 tests** in 260.21 seconds, including the 120-record legacy snapshot and nine-scale three-mode return. After the final weather footprint refinement and a direct cancellation assertion, all three focused core/native/image checks passed again in 30.30 seconds. The native surface proof contains 15 captures across five views. It verifies the same streamed geometry and camera in every display mode. The tested code/data match all 31 Mach-O sections of the ad-hoc signed 0.19.0 app; normal saved-flight startup was verified in its live cockpit.

Actual default-seed examples include **CITY 97221**, with 37,983 residents on galaxy 0 / system 137 / planet 0, and **COLONY 80584**, with 2,345 residents on galaxy 0 / system 0 / planet 8. System 0 / planet 7 also contains mining settlements. These examples do not relocate the normal saved flight.

| Metal, 1440×900 | 0.18 pilot loop | 0.19 pilot loop |
| --- | ---: | ---: |
| Frames | 1,817 | 1,817 |
| Median frame | 16.663 ms | 16.661 ms |
| 95th percentile | 17.102 ms | 17.467 ms |
| Peak RSS | 151.94 MiB | 144.92 MiB |
| Stationary orbit CPU | 2.972 ms | 3.275 ms |
| Stationary orbit GPU | 8.626 ms | 7.804 ms |

The separate final weather/city/colony fixture measured **16.666 ms median**, **17.085 ms at the 95th percentile**, and **134.42 MiB peak RSS** over 1,507 frames. Its peak surface cache was 905,196 bytes, with 206,560 bytes of uploaded surface templates. It drew up to 68,604 surface triangles, 72 resident samples and 50 local weather marks. All 33 possible shared templates total 251,072 bytes.

These are single-run, VSync-limited observations. The pilot comparison follows the same seeded actions and fixed visual time; its stationary orbit block contains only 19 samples per run. RSS and GPU changes include ordinary run-to-run variation and do not establish a causal performance improvement. The new inhabited scenes are measured separately because 0.18 had no corresponding surface settlements. Raw profiles and their limits are retained in [measurements.json](weather-and-surface-life/measurements.json).

- [City approach](weather-and-surface-life/captures/1-city-approach-HYPERION.png) and [identical Original-mode view](weather-and-surface-life/captures/1-city-approach-ORIGINAL.png).
- [Residents](weather-and-surface-life/captures/2-inhabitants-HYPERION.png).
- [Sealed colony](weather-and-surface-life/captures/3-sealed-colony-HYPERION.png).
- [Weather from orbit](weather-and-surface-life/captures/0-weather-orbit-HYPERION.png) and [local snowfall](weather-and-surface-life/captures/4-precipitation-HYPERION.png).
