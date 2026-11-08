# Rare regional cities and metropolises

Large cities are additive development around an existing procedurally generated settlement. The original town-centre generator, town seeds, building IDs, roads, landing pad, orientation and site slot remain intact. Planet geology, terrain, hierarchical addresses, orbits, flight handling and the atomic hierarchy are unchanged.

## Canonical growth and scarcity

A separate, versioned SurfaceSociety feature stream evaluates one opportunity per world. Its 8.5% opportunity ceiling is further restricted by settlement maturity, suitable solid terrain, gravity, temperature, water, inherited construction materials, energy and access to the existing orbital/surface trade network. Metropolises need stronger support, an older open city or a mature, well-powered sealed industrial settlement, and the rarer 2.6% opportunity band. These are gameplay parameters, not measured real-world urbanisation rates; actual settlement incidence is lower than either opportunity ceiling.

The existing founding history determines maturity. Local catchment grades and bulk planetary composition supply materials; water/ice and climate moisture constrain water support. Every candidate already has a retained surface port. Closeness to the orbital harbour improves access. The canonical decision never depends on the camera, cache pressure, worker ordering or the mutable economic ledger, so power shortages and repeated visits cannot replace the city.

A selected regional city is roughly 8–14 km across; metropolises can span roughly 21–36 km, bounded by planetary size and successful construction. Some sites support only part of that footprint. Wet, steep or inaccessible blocks stay undeveloped; a failed opportunity leaves the original colony untouched. At most one settlement per world receives this regional expansion.

## Physical structure and presentation

Commercial centres and satellite centres create the skyline. Housing, civic sites, industrial belts, agriculture, utilities and green corridors have distinct forms and height distributions. Industry uses the frozen planning wind where an atmosphere supplies one. Sealed settlements have protected greenhouses rather than exposed trees.

Each block has 36 addressable parcels. Buildings have stable IDs, individual dimensions, terrain-fitted floors/foundations, homes and jobs. Streets sample the authoritative spherical terrain on both sides. Water and grades reject unsuitable roads; a connectivity pass removes isolated districts. Checked access roads join the retained town graph without moving terrain or cutting through an old building. Existing neighbouring colonies keep their footprints and become part of the surrounding settlement landscape.

The renderer reconstructs the same parcels from compact descriptors. It distributes screen-space detail across the full footprint within the existing 6,000-instance limit, retains full nearby collision geometry, and refines road tessellation with distance. Night lights follow actual junctions and district centres and respond to economic power. Nearby inhabitants keep stable homes. District rovers follow real work orders and freeze their activity clock during holds. These actors are bounded presentation samples; they do not duplicate ledger cargo or add millions of independent simulation agents.

All generation still runs in the existing cancellable workers and LRU surface cache. There is no new browser, graphics dependency, shader, render target or unbounded city cache. Detailed building collision is independent of the rendering LOD and uses a local grid query.

## Economy, navigation and existing saves

The life scanner, contact names and economic descriptors include regional population. Population and jobs are summed from actual parcels. Utilities, industry, agriculture, worker capacity, stock targets and household demand flow into the existing production, freight, shortage, contract and recovery systems. F5 reconnaissance marks nearby district street junctions; services and the ordinary landing approach remain at the original port.

Existing economic saves retain their stocks, reservations, funds, people, contracts and port IDs. The network records its prior canonical fingerprint, material endowment and monetary endowment. Restore accepts only the current or explicitly retained prior endowment, checking both material and money conservation. After migration, resaving and restarting retain the old endowment basis while production and demand use the expanded city's current infrastructure. The established version-11 save format is unchanged; no stock or money is invented to cover the new population.

## Verification

The city regression checks rarity, maturity and hostile-world rejection; repeated generation; the spherical coordinate round trip; terrain and foundations; stable parcel/home identities; real collision; connected access; district diversity; sealed-world vegetation; cancellation; population totals; cache bounds; and legacy ledgers through a second restart. Natural fixtures were discovered by scanning valid existing galaxy/system/planet addresses, then rendered without hand-placing any settlement.

Representative deterministic examples in the default universe:

| Address | Existing site slot | Development |
| --- | ---: | --- |
| `0,0,0/1:0/2:26/3:14` | 1 | Regional city, about 13.2 km across, 11,205 additional buildings, 1,842,030 residents |
| `0,0,0/1:0/2:167/3:0` | 2 | Metropolis, about 23.2 km across, 21,099 additional buildings, 4,828,443 residents |
| `0,0,0/1:0/2:353/3:0` | 6 | Night-side metropolis, about 31 km across, 11,614 additional buildings, 8,734,662 residents |

The first two expanded region descriptions occupy about 0.29 and 0.56 MiB; their entire surface worlds remain about 0.38 and 0.59 MiB, inside the existing 4 MiB cache budget. Population here describes additional regional residents; retained town-centre residents are included separately in the total shown by the game.

All **79 headless regressions passed** in 271.24 seconds, including the galaxy-to-atomic round trip and the new natural regional/metropolis cases. The natural city test includes more than 77,000 assertions. The separate pre-change-system migration test restores all **148 original port IDs**, stock and monetary endowments, continues production and survives a second restart. The current build is packaged and strictly signature-verified.

The native Metal fixture renders matched overview, approach and street cameras. The 23 km city's original town fingerprint **3716621533864739036**, planet descriptor **3510800695248573630**, original 730 residents and all three terrain digests match the checkpoint exactly. The nine preserved generator, coordinate, city-core, mesh and flight sources also retain their checkpoint hashes. The city-core shader meshes remain shared; the same 6,000-instance ceiling applies. Full nearby parcels are retained while distant parcels receive a stratified share of the budget across the entire visible footprint.

| Final daytime capture | CPU frame median | Visible regional buildings |
| --- | ---: | ---: |
| Overview | 9.06 ms | 4663 |
| Approach | 9.15 ms | 4276 |
| Street | 8.93 ms | 3215 |

These are sampled CPU costs at 1440 × 900, not total frame time or a controlled before/after speed comparison; background load differed between capture runs. Canonical world generation is more expensive and runs asynchronously in the existing cancellable workers. The daytime capture measured about 308 ms for its complete world generation, compared with about 17 ms for the small pre-change town; no equivalent work is performed on the render thread. Its additional city description uses 588,384 bytes, and all shared surface GPU meshes visible during the close view total 284,928 bytes.

The night-side fixture rendered 746 junction/district light samples in its overview and retained actual homes and nearby residents at street level. Its larger existing world plus the regional expansion uses 1,864,434 bytes, also below the unchanged 4 MiB world-cache budget.

[Before overview](rare-cities/before-overview.png) · [Expanded overview](rare-cities/after-overview.png) · [Approach](rare-cities/after-approach.png) · [Street](rare-cities/after-street.png)

[Night overview](rare-cities/night-overview.png) · [Night approach](rare-cities/night-approach.png) · [Night street](rare-cities/night-street.png)

[79 regression results](rare-cities/headless-79.log) · [Before capture](rare-cities/metropolis-before.log) · [Final capture](rare-cities/metropolis-after.log) · [Night checks](rare-cities/night-native.log) · [Real old-ledger migration](rare-cities/migrated-economy.log) · [Preserved source hashes](rare-cities/preservation.json)

[Native capture source](rare-cities/capture.cpp) uses the current application's compile/link dependencies with `URBAN_AFTER`; it receives a natural planet address, output directory, existing settlement slot and regional radius. It never opens a player save. [Save-compatibility source](rare-cities/save-compatibility.cpp) can create a system-26 ledger with the checkpoint library and check it against the expanded generator.
