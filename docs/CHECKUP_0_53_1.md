# Infinite 0.53.1 checkup

The checkup covers the current Infinite game: generation and continuity, flight and landing, crew and weapons, combat and recovery, economy, exploration, saved-flight recovery and native Metal rendering. The separate Dune Breaker, ellipsoid and fractal experiments in the shared checkout are outside this game checkup.

## Fixes

A survey could finish while a crew, combat, recovery or economy command prevented its submission. The scanner then displayed “submitted” without recording the observation. It now waits for the flight computer and the target's economic system to be ready, preserving its progress. Releasing the key, changing target or losing physical access still cancels incomplete work.

Mining previously treated a busy transaction queue as a released trigger, discarding partial work. It now pauses and resumes without losing that work. If scanning and mining would complete together, the scanner submits first and mining retains its progress for the next available transaction. A paused tool emits no working beam, and provisional cargo is assigned only to an actual extraction submission.

Six regression assertions cover waiting, resumption exactly once and deliberate cancellation for both controllers. The focused exploration suite now contains 61 assertions.

Two image validators still expected differences between the graphics modes removed at the player's request. The terrain validator now checks one Hyperion image per actual world, with matching addresses, resolved terrain, active sun shadows and visible material. The celestial validator retains sun, eclipse, behind-camera and magnification checks; its ice control now checks visible glacial shading and requires the off-screen stellar effect to leave that material unchanged. No rendering algorithm, shader or generated geometry was changed to satisfy these checks.

## Save protection and preservation

Both existing save databases passed SQLite `quick_check`. An online SQLite backup and copies of the text saves were loaded with the current navigation, ship, activity, crew, weapons, fire-control, NPC-combat and economy implementations. The copied economy also initialized the exploration scanner correctly. The original text saves retained their hashes, and all write-capable recovery checks used the isolated backup.

The 218 runtime sources, headers and shaders recorded in the original exploration preservation manifest remain byte-identical. Universe generation, identities, seeds, spatial addressing, scientific geometry, ship dynamics and rendering shaders are preserved. Exploration wallet and database formats are unchanged by this patch.

## Evidence

All **55 core suites and 62 native checks pass**. The full native sweep exposed the two obsolete validator expectations described above; their original failures are retained in the initial results. After fixing the work controllers and validators, all core suites and the six affected native checks passed again. The final native repeat records an unchanged executable checksum throughout its run. Focused ASan/UBSan verification passed all 61 exploration assertions; the remaining linked core library was built in Release mode.

The signed **0.53.1 / build 61** app matches all 28 code/data sections of that tested executable. It also loaded an isolated copy of the existing saved flight, rendered 240 normal gameplay frames, saved and exited successfully. This startup check recorded a 16.66 ms median frame interval at 1383 × 758; it is a short functional check, not a comparative performance benchmark. The updated app is ready for the next launch.

The [validation record](checkup-0.53.1/validation.json) identifies the tested executable and final results. [Core results](checkup-0.53.1/core-tests.log), [native results](checkup-0.53.1/native-results.json), [focused sanitizer results](checkup-0.53.1/sanitizer-tests.log) and [saved-flight recovery](checkup-0.53.1/saved-flight.log) retain the checks. Native tests use isolated saves; regenerated test frames are hashed before cleanup.

These checks exercise macOS/Metal. They do not establish Windows/Linux GPU compatibility or a before/after performance improvement.
