# Display and HUD fitting — Infinite 0.29

The previous 1440 × 900 startup request used logical window points without reserving desktop decorations. On smaller desktops the title bar, menu bar and Dock could leave lower instruments outside the usable screen. Tall windows also produced invalid instrument widths because the cockpit assumed at least a square drawing area.

## Changes

Startup fits and centres the decorated window within SDL’s usable desktop bounds, with a small margin. Restoring the window or moving to a different display refits its position and size without enlarging it. Minimum sizes respect small work areas. Desktop calculations use logical units; GPU and pointer calculations use framebuffer pixels. The pinned SDL Cocoa backend does not expose border measurements, so the application reserves a conservative 32-point title bar and 8-point frame when the query is unsupported.

The cockpit fits uniformly inside a centred safe viewport. Portrait windows retain a square instrument canvas instead of squeezing panels into negative widths. World rendering continues to fill the whole window. Target and velocity cues compensate for the fitted viewport so they retain the real world projection; contact rows and instrument click interception use its exact inverse transform. All three rendering modes share this path. Footer hints receive a dark backing and extra bottom clearance.

No universe generator, procedural seed, coordinate, geometry, ship control or save format changes. The fitting logic does not move the camera or regenerate objects. The normal three HUD mesh submissions remain in use, with no new render target or dependency.

## Validation

**31 checks pass:** all 27 CPU regressions plus native UI fitting, its capture checks, the existing native piloting loop and its cockpit image checks. The cockpit test makes 2,115,068 assertions, including mesh bounds, asymmetric safe areas, odd dimensions, exact pointer round trips and world-marker alignment. Window tests cover decoration budgets, small/portrait desktops, negative multi-display coordinates, aspect preservation and stable repeated fitting.

The native fixture first exercises production startup fitting against the actual desktop. It then renders 800 × 500, 600 × 600, 600 × 900, 1920 × 1200 and 1440 × 900 framebuffers, plus the F1 guide at 800 × 500. Every case verifies unchanged camera state, real celestial/traffic row selection and blocked click-through. CPU assertions cover each HUD and guide vertex; capture checks confirm title and footer visibility. Native Metal was exercised on macOS; other desktop backends require platform testing.

[Retina landscape](ui-fit-0.29/retina.png) · [Portrait](ui-fit-0.29/portrait.png) · [Controls at 800 × 500](ui-fit-0.29/controls.png)

[Native fit proofs](ui-fit-0.29/native.log) · [Native tests](ui-fit-0.29/native-tests.log) · [CPU tests](ui-fit-0.29/cpu-tests.log)

Checkpoint: `checkpoint/infinite-0.28-before-ui-fit`. The rebuilt local app is 0.29.0, build 29, ad-hoc signed and strictly verified.
