# Ellipsoid rendering in Dune Breaker 3.0

Dune Breaker starts with an independently implemented, Ecstatica-inspired solid ellipsoid renderer. Version 2 uses an instanced native-resolution Metal analytic path, with the original software reference and LUT paths retained. Version 3 adds one shared cockpit/exterior model, independent wheel articulation, frustum support culling and tight projected ellipsoid bounds. See [the vehicle/physics guide](DUNE_BREAKER_3.md). Cockpit parts, tyres, vehicles, drivers, track marshals, cacti, rocks and rounded buttes are actual implicit ellipsoids. Their visible surfaces are never tessellated. The existing C++/SDL3/bgfx application, first person camera, procedural height field, procedural course, AI, checkpoint rules and saves remain in place; version 2 separately upgrades the driving physics and adds damage.

Press **F2** to visit the animated study scene: a 42-volume human with articulated hands and fingers, a strange 35-volume three-eyed desert creature, and ellipsoid stones, a log, cactus and ruin. The study freezes racing simulation and mutes the engine; F2 returns to the race. **Enter** also returns from the study.

## Useful controls

| Key | Effect |
| --- | --- |
| F2 | Race / ellipsoid study |
| F3 | Native analytic / 128×128 software lookup-table renderer |
| F4 | Fixed cinematic / orbit camera in the study |
| F5 | Native GPU / 320×200 retro scene; `--cpu-renderer --hires` selects CPU 640×400 |
| F6 | Normal / part boundaries and local axes / depth buffer |
| W in the study | Walking / idle poses |
| Hold either mouse button and drag, or arrows | Orbit when F4 orbit is enabled |
| Mouse wheel | Change orbit distance |
| H | Controls |
| F11 / F12 | Fullscreen / screenshot |

Driving controls retain their original meanings outside the study. The original polygon graphics path is available with `--legacy-graphics` for comparison. Analytic ellipsoids are the default; F3 changes the underlying volume rasterizer, not a postprocessing filter.

## Model format and articulation

The format is a typed C++ `infinity::ellipsoid::Model`: arrays of `Joint` records and `Ellipsoid` primitives, declared in `include/infinity/Ellipsoid.hpp`. Models are generated independently in `src/EllipsoidModels.cpp`; there are no game assets, external models, textures or asset downloads.

An `Ellipsoid` contains a local `center` in metres, a quaternion `orientation`, three positive semi-axis `radii`, one of sixteen `Material` values and a `parent` joint index. `parent == -1` attaches directly to the model root. A `Joint` contains a parent joint index, an offset, a local quaternion rotation and a readable name. Joint hierarchy transforms are rigid: a parent's ellipsoid radii never accidentally scale its children.

```cpp
using namespace infinity::ellipsoid;
Model model;
model.joints.push_back({-1, {0, 1, 0}, {1, 0, 0, 0}, "shoulder"});
model.parts.push_back({{0, -.3, 0}, {1, 0, 0, 0},
                       {.12, .35, .14}, Material::Ochre, 0});
auto worldParts = resolve(model, {{2, 0, 5}, {1, 0, 0, 0}});
rasterizer.draw(worldParts, Mode::Analytic);
```

Quaternion components are `(w,x,y,z)`. `resolve` traverses the joints, rejects cycles and invalid references/transforms, then produces world-space ellipsoids. `segment(a,b,r,material)` creates one elongated ellipsoid oriented between its endpoints; it never creates a cylinder mesh. Limb poses interpolate local joint rotations with quaternion slerp. Walking swings hips, knees, shoulders and elbows; idle poses retain breathing, head motion and small body sway. Intersections between component volumes remain visible; there is no metaball blending or polygon skin.

## Native GPU path

`games/desert/GpuRenderer.cpp` supplies original MSL programs through the pinned bgfx Metal source-library container. The OS compiles them directly; no shaderc or additional runtime installation is needed. Per-instance data carries each primitive's camera-space centre, three oriented axes/radii and material. A conservative screen proxy bounds the work; the fragment program solves the transformed quadratic and writes the true surface depth. Hardware instancing batches opaque volumes, and sorted transparent volumes depth-test without writing depth.

The terrain, volume and sky passes share camera parameters and directional lighting. Dune detail is shaded on the terrain, while object surfaces retain their analytic normals. Vehicles remain assemblies of actual ellipsoids, including their deforming panels, suspension parts, tyres and driver. F6 diagnostics select the exact CPU reference. Non-Metal renderers fall back to the software paths described below.

## Software reference rendering stages

1. **Frame setup.** Allocate a bounded 320×200 or 640×400 software colour/depth frame. Generate camera rays with camera-local +Z forward. A fixed 256-colour palette supplies sixteen material ramps of sixteen shades. Scene pixels are palette entries; debug axes and depth use diagnostic colours.
2. **Background geometry.** The existing procedural terrain is sampled from its current tile cache and software-rasterized. Nearby tiles retain the original mesh; distant tiles use coarser shared height samples. Triangle near-plane clipping and reciprocal-Z interpolation write positive camera-space Z. Ground is the only mesh-based scene surface in this path; rounded models contain no hidden triangle silhouettes.
3. **Volume bounds.** Transform each ellipsoid into camera space. Its oriented bounding box supplies conservative clipped screen bounds; near-plane-crossing/inside volumes receive bounded full-frame coverage. No candidate loop extends outside the framebuffer.
4. **Volume surface.** Evaluate the selected analytic or LUT path within the projected bounds, column by column. Every successful fragment tests the same camera-Z buffer, including cockpit parts, people, vehicles and ground. Submission order is not the visibility rule.
5. **Palette shading.** Use outward inverse-transpose normals and restrained directional diffuse lighting. There is no PBR, specular plastic, normal mapping or bloom. Material palettes and the low scene resolution supply the early software-rendered appearance.
6. **Presentation.** Group equal-colour horizontal pixel runs into screen rectangles and submit them through Infinity's existing bgfx colour shader. These rectangles present already-resolved pixels; they are not ellipsoid surface geometry. Enlargement has no texture interpolation. The existing readable HUD is a separate native overlay.

### Analytic reference

Transform the ray into the unit sphere using inverse rotation and component-wise inverse radii. Solve the quadratic for the nearest root beyond the near clip; use the far root when the camera is inside. The standalone `intersect()` helper uses a cancellation-resistant quadratic solution. Surface normals use `normalize(R * (q/r))`, not a sphere normal incorrectly transformed through non-uniform scale.

Rasterizer rays have camera-local Z equal to one. Consequently the ray parameter `t` is directly comparable to camera-space triangle Z, independent of ray angle or ellipsoid orientation. This avoids mixing Euclidean range with view depth.

### Original-style LUT mode

`LookupTables` procedurally generates a 128×128 unit-sphere visibility mask and rounded surface-depth profile. A directional 128×128 shade map is regenerated for the camera-relative light. Material-specific palette ramps turn shade indices into frame colours.

For each projected ellipsoid, the quadratic discriminant defines a two-dimensional perspective conic. A Cholesky transform maps that footprint into the unit-sphere table. Bounded columns sample the profile to recover the front surface depth and sample the shade map using the transformed surface normal. The resulting fragments use the same depth buffer as the analytic renderer. Camera-inside and near-plane-crossing cases fall back to the exact surface solver, since their projection need not be a bounded ellipse.

This is a modern LUT approximation inspired by the original pipeline. It does not claim bit-identical projection, 1994 fixed-point arithmetic or reproduction of the original game's palette/assets. At intersecting silhouettes the sampled profile can differ slightly from the analytic surface; F3 makes comparison immediate. The test fixture measures a maximum difference of about 0.046 metres for the overlapping five-metre reference scene.

### Background masks and diagnostics

`Rasterizer::background(indices, depths)` accepts a per-pixel palette-index image and matching positive camera-space Z mask. A size mismatch or invalid nonpositive mask is rejected; positive infinity denotes empty sky. Tests exercise foreground and background occlusion. The interactive scene is fully generated in 3D and does not require a pre-rendered background.

F6 part view colours independently visible volumes, traces their visible boundaries and unit-sphere bands, and draws local RGB axes. Depth view displays logarithmic camera-space depth, including scenery, terrain and cockpit. These diagnostics do not change racing collision geometry.

## Build and verify

```sh
cmake --build build --target dune_breaker infinity_ellipsoid_tests infinity_desert_tests -j 4
ctest --test-dir build -R '^(ellipsoid_geometry_and_depth|desert_buggy_racing)$' --output-on-failure
./build/dune_breaker --ellipsoid-demo
./build/dune_breaker --ellipsoid-demo --lut --orbit
./build/dune_breaker --ellipsoid-demo --ellipsoid-debug 1
./build/dune_breaker --demo --frames 240 --mute --capture build/ellipsoid-native/race.ppm
python3 tools/package_desert_macos.py build/dune_breaker 'Dune Breaker.app'
```

`Launch Ellipsoid Study.command` starts the study directly from the packaged app. The renderer library and its tests need only GLM and build without the native viewer. Tests cover rotated anisotropic volumes, numerical implicit-surface normals, tangency, camera-inside roots, order-independent intersecting volumes, off-axis conic coverage/depth, near-plane fallback, triangle and background-mask occlusion, palette membership, hierarchy cycles, articulated idle/walk poses and continuous animation. Existing complete-race tests still run on three terrain seeds.

Native screenshots are verified for both surface modes, racing, orbit, part diagnostics and depth diagnostics. The exercised platform remains macOS arm64 with Metal; no new runtime dependency or shader compiler is required.

## Reference study

The supplied [Ecstatica reimplementation](https://github.com/xesf/ecstatica) was read for rendering principles. [ellipse.c](https://github.com/xesf/ecstatica/blob/main/src/ellipse.c) shows projected bounds, column traversal, profile/shade tables and palette selection. [asm_f.c](https://github.com/xesf/ecstatica/blob/main/src/asm_f.c) shows per-pixel mask/depth comparisons. [display.c](https://github.com/xesf/ecstatica/blob/main/src/display.c) shows hierarchical joint and primitive transforms. The implementation here, its procedural palette, models, animation and demonstration assets were written independently.
