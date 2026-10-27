# Hyperion rendering integration — Infinite 0.5

Infinite retains generation recipe **3**. Hyperion supplies presentation algorithms, never a replacement universe. Press **H** to cycle **ORIGINAL → HYPERION → HYBRID**. The initial mode is ORIGINAL. All existing controls remain available.

## Source reference and inspection

The reference is the user's native game at `/Users/user/Documents/SC tutorial/Hyperion`, not Disney's renderer. The inspected source comprises **133 application source/header files, 12,605 lines**, including both strategy and flight implementations. [The complete source inventory](hyperion/source-inventory.json) records every file's SHA-256, includes, function inventory and drawing-call count. The source checkpoint was `f1b3db19cf97674661336dd9bf4112baeb331d16` (Hyperion 3.64). Vendor raylib and JSON are dependencies, not the authored visual reference.

The renderer dependency chain is `main.cpp → renderer.cpp → flight_scene.cpp → flight/renderer.cpp`. Strategy/UI modules (`*_renderer.cpp`) use raylib 2D drawing; `flight/scene.hpp` supplies presentation views of simulation objects. Flight terrain, settlements, minor bodies, organisms and damaged structures each have their own mesh/material paths. Universe, flight simulation, economy, ecological, recovery and persistence modules feed those scenes. None of them were imported into Infinite.

### Actual rendering techniques

| Aspect | Source implementation and recognisable result | Integration |
|---|---|---|
| Architecture | `src/renderer.hpp`, `renderer.cpp:44–70,420–467`: raylib/OpenGL 3.3, strategy ping-pong backgrounds and separate 3D flight renderer. `flight/renderer.cpp:129–197,417–573`: float HDR scene, weather/depth targets, bloom and display composition. | Keep SDL3/bgfx; add a presentation adapter and separate GPU pass owner. |
| Visual composition | Dark blue ink, luminous but bounded spectral sheets, fine threads, material relief, coloured grazing light; native-resolution UI after world processing. Flight cockpit is actual beveled 3D geometry. | Apply spectral fields to existing objects and their emissions; retain Infinite's overlay and open camera. |
| Geometry | `renderer.cpp:23–42`: rings, gradients, ear-clipped filled polygons and dark/bright double outlines. `flight/renderer.cpp:32–128`: tubes, two-sided triangle annuli, uploaded indexed terrain-displaced globes. `terrain.cpp`, `settlements.cpp`, `minor_renderer.cpp`, `world_damage_renderer.cpp`: independent cached meshes. | Submit Infinite's original indexed meshes, lines, particles and atom instances without changing vertices, indices, density functions or draw LOD. |
| Colour/contrast | `flight/shaders.hpp:10–51` `PRISM_PIGMENT_GLSL`: `.5+.5*cos(2π*(h+(0,.68,.34)))`, exponent 1.35 in standard look. `silk()` uses two warped folds, broad sheets, derivative-filtered fine threads/lace and crests. `renderer.cpp:13` defines ink/pearl/mint. | Port the actual pigment/silk/thread functions; rename the HLSL-reserved variable `line` to `strand`. Keep the standard look; the optional `ecstatic()` psychedelic variant is not enabled. |
| Lighting/shading | `flight/shaders.hpp:215–384`: GGX distribution, Schlick Fresnel, Smith masking, roughness filtering from normal variance, ambient bounce, spectral Fresnel, filtered contour traces. `minor_shaders.hpp`: restrained stone pigments, triplanar close material, cavity and metallic/ice roughness. | Port BRDF, variance filter, spectral rim and filtered trace accents. Read existing mineral identity for roughness/metallicity; keep baked vertex shading as part of the result. Derive geometric normals in the fragment shader, with analytic sphere normals for atoms. |
| Particles/trails | `flight/combat_renderer.cpp`: cached radial glow sprite, additive layers, analytic bounded sparks, layered tubes, swept resonance strands and shock fronts. `minor_renderer.cpp:66–84`: two tapered comet-tail triangle sheets. `flight/renderer.cpp:399–416`: bounded velocity-aligned navigation streaks. `dustFragment` uses a radial opacity envelope. | Port the radial glow envelope onto existing particle instances. No invented combat, comet bodies, trajectories or dust population is added. Infinite's scene does not supply those simulation events. |
| Accumulation/feedback | Strategy `sceneFragment` in `src/shaders.hpp:92–101` mixes advected prior **background** with `.34*pow(clamp(persistence,0,.85),delta*60)` and bounded compression. `renderer.cpp:58–70` ping-pongs it. The 3D flight renderer does **not** accumulate prior frames. Mode handoff holds one frame for a short transition. | Follow the flight pipeline: no temporal accumulation of 3D geometry. This avoids trails across unrelated physical magnifications. Document the strategy technique; do not silently apply it to terrain or atoms. |
| Layering/compositing | Flight: opaque/depth, atmospheres, depth-reconstructed clouds/aurora, minor bodies/structures/life, additive emission and motion, cockpit, bloom, post, HUD. Weather reconstruction rejects foreground depth leaks. | Opaque/line/instance views share one HDR/depth target; additive existing emitters, horizontal/vertical bloom, final display, then unchanged overlay. Weather/cockpit geometry is not fabricated. |
| Texture/image processing | `flight/renderer.cpp:57–65,157–231`: RGBA16F HDR, 256² noise lookup, baked material/geology/detail maps, repeating mip chains, trilinear and optional 16× anisotropic filtering. `flight/shaders.hpp` blur/post: 5 bilinear taps per axis, weights .227027/.316216/.070270, offsets 1.384615/3.230769, threshold .72; edge AA, conservative clarity .24, bloom .38, vignette .10, exposure 1.30, hue-preserving ACES mix .48, gamma 1/2.2. | Port bloom, edge AA, clarity, vignette and the complete standard flight exposure/display curve. Use analytic material fields instead of importing Hyperion's baked geography. |
| Camera/projection | Strategy modes pan/zoom charts; flight uses a 70° perspective camera, a long 1024-unit sight vector to avoid float direction jitter, clip interval 1–60000; cockpit has its own near interval and tiny depth range. | Preserve Infinite's existing 1-radian projection, hierarchical double-precision camera and local frame conversion. No Hyperion camera, cockpit, control or flight simulation is installed. |
| Motion/depth | Direction/object-anchored patterns, derivatives/mips suppress subpixel fibre shimmer, actual 3D occlusion, optical atmosphere, resolved cloud depth, additive emissions without depth writes. World geometry does not move with the visual colour field. | Same camera/depth/geometry in every mode; silk's slow colour motion is presentation-only. Canonical planet coordinates span planet/surface/mountain patches; host-rock coordinates span nested matter. Atoms keep element colours and disable fibres. |
| Performance | Cached shader locations/meshes/fields, near-only field residency, view/distance/pixel culling, LOD hysteresis, explicit effect budgets, shared noise samples, palette hoisting outside volume loops, cached snapped sun shadows, reduced-resolution bloom/clouds and depth-aware reconstruction. | Reuse Infinite's asynchronous generation, cancellation, bounded caches, compact indices and static GPU atom instances. No second mesh copy; two one-third-resolution bloom targets and one HDR/depth target are allocated lazily. Three additional fullscreen draws. |

The distinguishing visual source is specifically **`PRISM_PIGMENT_GLSL`, worldFragment's pigment/BRDF/Fresnel treatment, combatGlow's radial sprite, and the flight blur/post pair**. It is not a generic bloom preset or a newly generated nebula backdrop.

## Preserved architecture and integration boundary

Before modifications, Infinite 0.4 built successfully and passed **all 10 tests**, including the native Metal all-LOD round trip and atomic pixel checks. Git checkpoint **`0d61030`** preserves the original sources; `build/infinite-original-0.4` freezes the working executable for pixel/performance comparison.

The original interfaces were `GeometryGenerator::{generate,section}`, immutable `Geometry` (solid/line meshes, particles, lattice and pre-baked instance transforms), `RegionCache::{request,requestSection,catalog}`, `Renderer::{render,pick,screenshot,submittedGeometry}`, and `Navigation`. Rendering requests screen-space detail, consumes completed immutable cache entries, preserves previous detail while refining, then transforms local geometry through hierarchical frames.

Generator/simulation dependencies remain `Core`, `Universe`, `Processes`, `Crystallography`, `Geometry`, `World`, `Navigation` and `Continuity`, linked against the existing pinned GLM/FastNoise2/oneTBB/SQLite/Zstd libraries. **Their source files are unchanged.** No new runtime dependency was added.

`Presentation.hpp/.cpp` extracts inherited material response and a canonical material frame from a const `Node` and const `Geometry`. It holds a reference to the same geometry, never an adapted replacement mesh. The descriptor is the exact `Node` already supplied by scene traversal/LOD, without a fresh universe query for shading. `HyperionRenderer` owns only bgfx programs, uniforms and render targets. `Renderer.cpp` supplies the existing geometry, transforms, camera and instance buffers to the selected presentation path. `setMode()` only assigns an enum; it never invalidates generation or caches.

- **ORIGINAL:** original shader binaries, vertex/index/instance data, billboard mesh, blend/depth state and backbuffer path.
- **HYPERION:** source-traced object/material lighting, filtered pigment, radial emission, HDR bloom/display pipeline.
- **HYBRID:** 24% material response and 36% final processing, retaining original geometric/material readability.

The fine terrain patch initially exposed its sampling rectangle when authored in tile-local colour coordinates. The final adapter uses the same planet material frame for both the patch and its enclosing section. This is a renderer correction; the geometry exclusion/clipping and physical samples remain untouched.

## Intentional differences

Hyperion already has a 3D flight renderer, so this is a backend and scene-model port rather than a 2D-to-3D conversion. Its separate 2D chart backgrounds, procedural stars/nebulae, planet fields, ocean geography, buildings, weather and combat entities are not transplanted: doing so would introduce structures outside Infinite's generators. Existing galaxy gas/star positions therefore supply the macro composition. The same spectral field is more restrained on rocks, and nearly absent on atoms, to retain mineral morphology and crystallographic site/element readability.

Original vertex colours contain baked diffuse shading. The new shader blends the ported BRDF response with those colours instead of inventing albedo data or changing generator formats. Derivative geometric normals can make coarse surrounding section facets more visible than in Hyperion's separately authored normal maps. The underlying indexed geometry is retained in full. Planetary atmosphere/cloud/aurora integration and sun shadow maps were analysed but are not included in this integration: no corresponding new medium or world simulation is fabricated.

## Build and reproducibility

Ordinary macOS/Linux builds use checked-in Metal, SPIR-V and GLSL binaries embedded in the executable, compiled with the exact pinned bgfx shaderc revision (shaderc 1.19.167, glslang 16.5.0 for these binaries). Source shaders remain in `shaders/`. `shaders/manifest.json` records source and binary digests; CMake rejects stale shader sources/binaries. Rebuild with:

```sh
python3 tools/compile_shaders.py --shaderc /path/to/pinned/bgfx/shaderc
cmake -S . -B build
cmake --build build -j 3
ctest --test-dir build --output-on-failure
```

Windows uses the same sources and requires the pinned Windows `shaderc` on PATH (or `-DINFINITE_SHADERC=...`) at configure time to generate DXBC. This is a build tool, not a new runtime dependency. Metal was tested natively; SPIR-V and GLSL were compiled, not exercised on hardware here. Windows/DXBC requires verification on Windows.

```sh
build/infinite --render-mode hyperion
build/infinite --compare-modes --width 1920 --height 1080 \
  --capture-dir build/hyperion-comparison/captures \
  --profile build/hyperion-comparison/frames.csv --data build/hyperion-comparison/data
```

The comparison journey visits cosmic web → galaxy → system → planet → surface → mountain → rock → grain → lattice and returns. At every stop it switches modes without moving the camera, asserts unchanged geometry fingerprint/camera serialization/generation miss and completion counts, and captures all three images. Visual time is frozen at 12 seconds. Ordinary navigation and animated material colour remain available outside this fixture.

See [validation, measured costs and screenshots](hyperion/VALIDATION.md).
