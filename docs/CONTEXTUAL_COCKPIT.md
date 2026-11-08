# Contextual cockpit

The cockpit keeps its original frame, pixel lettering, four lower instruments and colourful universe. The default view now concentrates on flying. This is a read-only presentation change: generators, identities, seeds, coordinates, ship physics, controls and save formats are unchanged.

## Flying and working

A pure state resolver chooses travel, approach, landing, docking, combat, surface work or the probe camera. Warp takes precedence over saved gear and approach states, so it cannot show a landing director or a terrain-alignment instruction. The landing director appears for a real landing maneuver or a low-altitude gear approach; after touchdown it becomes a surface workspace. A passive geological prospect hint does not turn ordinary planetary flight into mining mode.

The lower console always shows the destination, velocity, appropriate scope and hull/throttle gauges. Essential numbers are larger. The destination action changes with the task; units retain their case at microscopic and atomic sizes. Altitude and weather appear when relevant. Physical shrink state, fragility and changing reference magnification remain distinct. The scope retains nearby world/traffic context and highlights selected objects and threats.

The default contact card shows at most three useful entries, prioritizing selection and hostiles, then living worlds and visible traffic. Its green life badge still cycles confirmed inhabited worlds. The full source contact lists remain available: **Ctrl-Tab** or click the Contacts heading. **Y**, **Shift-Y**, **Ctrl-Y**, **K**, **Shift-K** and debris selection retain their existing meanings. Expanded catalogues display the same stable identities; hidden rows no longer accept accidental clicks.

## Alerts and access

One fixed alert channel ranks missiles, hull damage, incoming fire, terrain closure, blocked regrowth, environmental exposure and crew emergencies before ordinary receipts. A transaction headline can accompany the urgent warning, with its complete message in the expanded flight computer. Normal crew status and idle/inhibited weapons do not occupy permanent panels. Gunner fire policy appears during combat; weapons, recovery and mining controls appear when useful. Routine system health, crew, journey totals and weather are in the expanded computer.

The F1 guide is wider, with larger lettering and complete shortcut descriptions, including in square windows. All consoles remain available without leaving flight:

| Control | System |
| --- | --- |
| F1 | Complete controls guide |
| F2 | Market, jobs, services, equipment and reports |
| F3 | Minimal expedition planner; D opens details |
| F5 | Terrain reconnaissance; Shift-F5 selects, F4 routes |
| Ctrl-F5 / Alt-F5 | Navigator assessment / engineer watch |
| F8 / F9 | Crew deck / live orders and damage control |
| F10 | Gunner/helm handover |
| F11 / F12 | Journal / survey |
| Ctrl-P / Alt-P | Probe launch / first-person feed |
| Ctrl-Tab | Contact catalogues and full flight messages |
| Tab | Hide/show cockpit and HUD |

Crew helm-order shortcuts retain their existing precedence. Click targets come from the final rendered overlay and use the same Retina/safe-viewport transform as drawing. Modal screens and alerts remove covered contact hit regions.

## Verification

The cockpit regression covers mode precedence, removal of orbital landing instructions, alert priority, receipt retention, contact identity and visible-only selection. Layout stress tests exercise collapsed and expanded states, every market page, help, crew, gun stations, tiny hulls and the probe feed. The native fixture captures eight states at an identical generated terrain/settlement camera. The native fit fixture checks real world/traffic clicks in small, portrait, Retina and wide windows. Measurements and representative screenshots are stored alongside this document.

All **72 non-desert regressions passed** on the final build in 81.52 seconds. The six separate Desert tests also passed in the [initial broad run](contextual-cockpit/initial-78.log); that run exposed the planner-alert layering and legacy countdown wording checks, both fixed and passing in the final run. Native Metal verified **eight contextual states** and **twelve display/modal layouts**, with zero text overlaps, real world/activity selection, life-badge targeting and catalogue heading hit tests. The final F1 readability adjustment also passed [four focused regressions](contextual-cockpit/guide-final-tests.log) and a fresh twelve-layout native run. The packaged app passes strict signature verification.

| Controlled scene | Characters before | Characters after | Reduction |
| --- | ---: | ---: | ---: |
| Warp | 1443 | 399 | 72.3% |
| Approach | 1411 | 428 | 69.7% |
| Landing | 1311 | 397 | 69.7% |
| Docking | 538 | 509 | 5.4% |
| Combat | 1332 | 535 | 59.8% |
| Surface | 1418 | 531 | 62.6% |
| Micro | 616 | 457 | 25.8% |
| Catalogue | 1427 | 1289 | 9.7% |

Counts use the same 20 worlds and 20 activity contacts, immutable instrument snapshots and 1440 × 900 canvas. They include every text run in the final overlay. The open catalogue intentionally retains detail. Captures use the same real generated settlement and terrain, frozen visual time and camera. Terrain digest **5149391451716758143** is identical across all eight before/after states; planet descriptor and city fingerprints are checked during capture. CPU frame times in the raw logs were taken under different background load and are not a controlled renderer performance comparison. No new GPU pass, render target, font dependency or texture is introduced.

[Before warp](contextual-cockpit/before-warp.png) · [After warp](contextual-cockpit/after-warp.png) · [Landing](contextual-cockpit/after-landing.png) · [Combat](contextual-cockpit/after-combat.png) · [Expanded contacts](contextual-cockpit/after-catalogue.png)

[Portrait](contextual-cockpit/portrait.png) · [Controls](contextual-cockpit/controls.png) · [Crew](contextual-cockpit/crew.png) · [Market](contextual-cockpit/market.png) · [Probe](contextual-cockpit/probe_portrait.png)

[Regression results](contextual-cockpit/regressions-72.log) · [Native state checks](contextual-cockpit/native-after.log) · [Native fitting checks](contextual-cockpit/native-fit.log) · [Counts](contextual-cockpit/text-comparison.json) · [Capture source](contextual-cockpit/capture.cpp)

The capture source is an isolated native validation entry point using the application's existing renderer and dependencies. Compile it with `HUD_AFTER` against the current core, replacing Main.cpp in the application's compile/link command. Its eight states exercise compact world/activity/life and catalogue heading clicks directly through Renderer, without opening a player save. The ordinary headless tests and `--ui-fit-test` remain integrated into the existing CTest setup.
