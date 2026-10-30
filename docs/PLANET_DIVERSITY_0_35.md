# Planet diversity — Infinite 0.35

Infinite now generates gas giants, ice giants and sub-Neptunes as additional addressed destinations. They join the existing rocky and icy worlds rather than replacing them. Large giants have resolved cloud belts, shear and oval vortices; smaller volatile-rich envelopes have cooler colours and more muted bands. Suitable worlds have generated rings with optical depth, radial structure and gaps. Solid worlds receive distinct rocky, iron-rich, desert, oceanic, ice and lava presentation derived from their existing geology, temperature, mineral and water reservoirs.

## Research and modelling choices

[NASA’s planet classification](https://science.nasa.gov/exoplanets/planet-types/) distinguishes terrestrial, super-Earth, Neptunian and gas-giant populations, with substantial variation within each group. Infinite keeps material family, physical radius and thermal regime separate: a cold ice-covered solid is different from an ice giant with a deep envelope, and a temperate temperature alone does not establish habitability. The radius cutoffs are game-model approximations, not a universal observational classification.

[NASA’s formation overview](https://science.nasa.gov/exoplanets/how-do-planets-form/) motivates separate condensed solids and retained gas reservoirs. Four additive formation channels provide two large giants, an ice giant and a sub-Neptune. One large-giant channel can occupy a clear inner orbit; otherwise it remains outside the established system. This deliberately provides a richer gameplay population rather than reproducing measured planet-occurrence rates.

Density and volume determine the new bodies’ masses. Moon periods and orbiting survey/debris motion use that bulk mass; established bodies retain the previous condensed-material rule. Mutual-Hill spacing uses those masses, rather than the density of a selected solid mineral. Effective temperature adds a bounded, age- and mass-dependent cooling flux to stellar irradiation in temperature-to-the-fourth space. [Cassini measurements of Jupiter’s energy budget](https://www.nature.com/articles/s41467-018-06107-2) support including an intrinsic heat source. New giant moons inherit stellar irradiation and the diluted thermal contribution of their actual primary. The particular cooling law remains a procedural approximation.

[NASA’s Saturn reference](https://science.nasa.gov/saturn/facts/) describes rings made of ice, rock and dust, with thin radial structure and divisions. The generator confines ring extents using a fluid Roche-limit estimate and a conservative fraction of the Hill region. Temperature controls an icy-to-rocky material proxy; warm rocky rings are less common. Seeded radial density bands and two gaps share one CPU/GPU field. [Tidal-disruption ring research](https://arxiv.org/abs/1609.02396) informs this formation interpretation; the generator does not simulate an actual disruption event.

[NASA’s Neptune reference](https://science.nasa.gov/neptune/neptune-facts/) motivates separating the muted blue-green appearance of volatile-rich envelopes from the warmer belts of large gas giants. The renderer uses stylized palettes within Infinite’s retained Hyperion presentation, rather than claiming spectral or photorealistic colour accuracy.

## Preservation boundary

The verified 0.34 checkpoint is `3bba48e`, with branch `checkpoint/infinite-0.34-before-planet-diversity` and a frozen executable. All 30 baseline core checks passed before edits.

- Every established primary keeps its old numerical ID, seed, orbit, physical radius, chemistry, geological process, terrain and history. Its moons also keep their existing addresses and descriptors.
- Four primaries are appended after `establishedPlanetCount`. Systems now contain 17–25 primaries, plus the existing stable moon generator’s additions around the new giants. The system coordinate unit stays unchanged; the physical envelope query includes the additions.
- `generationRecipeVersion=3`, population recipe 1, climate recipe 1, live weather recipe 2 and city recipe 1 remain intact. Additional appearance/envelope/ring feature streams have explicit independent keys.
- A frozen snapshot covers **897 pre-upgrade planet and moon descriptors**, plus representative existing globe geometry. The earlier terrain, surface-site, population and atomic preservation fixtures are retained.
- Gas envelopes expose no landable Surface children or surface settlements. Existing solid worlds retain the entire terrain-to-atomic hierarchy. Existing controls, navigation and saved-flight formats are unchanged.

The old surface-site snapshot now explicitly iterates its established population, so new empty gas-world entries are not confused with changed old sites. Atomic round-trip tests select solid worlds at the system step, while the new gas tests require envelopes to reject landable surface children.

## Rendering and resource use

Planet traits are read-only scene data. Existing terrain vertices, indices and material samples remain authoritative. The Hyperion and Hybrid shaders apply family-dependent substrate palettes and gas-deck detail; Original retains existing solid-world drawing, and draws the new giant meshes with generated broad belts.

Rings use a four-vertex plane per visible planet, analytic circular bounds and derivative-filtered optical bands. Their actual stellar direction provides illumination and the planet’s shadow; the same generated density field attenuates sunlight on the planet. They depth-test against opaque bodies without introducing a solid landing surface. The ring field is optical debris; individual chunks are not separately addressable asteroids. Ring geometry and its shadow can be disabled together for native negative controls.

There are no per-planet ring textures, new effect targets, temporal histories or dependencies. Existing generation workers, geometry caches and GPU uploads remain in use. New giant spheres refine from 16 × 32 to 64 × 128 segments with screen-space LOD; original solid-world geometry is unchanged. Rings remain within the existing world-depth presentation range.

## Validation and captures

All **69 checks passed** in the complete release run. After the final material, shared cloud-field and orbital-density refinements, all **32 core checks and both planet-gallery checks passed again**. The matched after-flight also completed the piloting, landing, contact-picking, shrinking and roll checks. [Complete release run](planet-diversity-0.35/release-tests.log), [final core checks](planet-diversity-0.35/final-core-tests.log), [final native checks](planet-diversity-0.35/final-native-tests.log).

The dedicated process test passes **346,303 assertions**. The 897-object established-world snapshot, earlier population/terrain/city preservation checks and atomic round trips all pass. [Preservation metadata](planet-diversity-0.35/preservation.json), [process counts](planet-diversity-0.35/process-counts.log).

| Real generated family | Native capture |
| --- | --- |
| Gas giant and rings | [View](planet-diversity-0.35/gas-1.png) |
| Ice giant | [View](planet-diversity-0.35/ice-giant-1.png) |
| Sub-Neptune | [View](planet-diversity-0.35/sub-neptune-1.png) |
| Hot giant | [View](planet-diversity-0.35/hot-giant.png) |
| Rocky | [View](planet-diversity-0.35/solid-0.png) |
| Iron-rich | [View](planet-diversity-0.35/solid-1.png) |
| Desert | [View](planet-diversity-0.35/solid-2.png) |
| Oceanic | [View](planet-diversity-0.35/solid-3.png) |
| Ice world | [View](planet-diversity-0.35/solid-4.png) |
| Hot lava/rock surface | [View](planet-diversity-0.35/solid-5.png) |

Clouds can obscure the underlying solid-world material in the atmospheric views. Thermal incandescence uses the existing geological height contours and surface temperature; it changes emission rather than terrain. The same broad giant belts and oval storms are generated in C++ for Original and refined in the GPU presentation.

The ring control changes **239,606 pixels**, with **0.064/255 RMS** error after a legal render-unit change. [Rings enabled](planet-diversity-0.35/gas-1.png), [disabled](planet-diversity-0.35/rings-off.png), [rebased](planet-diversity-0.35/rings-rebased.png), [measurements](planet-diversity-0.35/comparison.json). All three rendering modes retain matching planet descriptors and geometry. [Native proof](planet-diversity-0.35/native.log).

The dedicated diversity test checks deterministic identities, retained solid worlds, gas-envelope mass and irradiation, non-landable giants, mutual-Hill separation, ring confinement, temperature-dependent ring material and optical-density structure. A three-seed, 96-system sample contains 103 rocky, 9 iron-rich, 214 desert, 16 oceanic, 1,288 ice, 1 lava, 192 gas-giant, 96 ice-giant and 96 sub-Neptune primaries. Of the gas giants, 24 are hot; 315 envelopes have rings in this sample. These are generated gameplay counts rather than observational occurrence rates.

The native tour searches real generated worlds for all nine families and a hot giant. It compares the three giant families in Original, Hyperion and Hybrid from identical cameras, then compares enabled, disabled and rebased rings. The tour checks physical descriptors, submitted geometry and camera coordinates at every capture.

Metal is runtime-tested. Metal, GLSL and SPIR-V shaders compile; other platforms still need runtime validation.

## Measured before and after

The preserved 0.34 and final 0.35 executable visit the same original planet from the same camera, at 1440 × 900 with visual time fixed at 12 seconds. The new population remains present in 0.35. [Before](planet-diversity-0.35/before-orbit-hyperion.png), [after](planet-diversity-0.35/after-orbit-hyperion.png).

| Measurement | 0.34 | 0.35 |
| --- | ---: | ---: |
| Orbital Hyperion median frame interval | 15.61 ms | 12.51 ms |
| Orbital CPU median | 10.17 ms | 7.79 ms |
| Effect target storage | 21,600,000 B | 21,600,000 B |
| Material textures | 1,660,244 B | 1,660,244 B |
| Weather GPU fields | 87,384 B | 87,384 B |
| Orbital CPU cache | 3,582,472 B | 4,198,248 B |
| Full flight-loop median (contents differ) | 22.76 ms | 27.28 ms |
| Full flight-loop p95 (includes warm-up) | 44.19 ms | 46.05 ms |
| Maximum frame interval (includes warm-up) | 218.91 ms | 1483.23 ms |
| Full flight-loop peak RSS | 157.16 MiB | 147.02 MiB |

These are samples from serial runs on a shared Apple M1 MacBook Air (16 GiB, macOS 26.4.1). The orbital frame window excludes switching and screenshot overhead; the full-loop peak includes later stages and asynchronous generation. Added destinations can change the contents of later stages. Host load varies, so this does not establish a general speedup or isolate the cost of any one effect. [Raw measurements and executable hashes](planet-diversity-0.35/performance.json), [before trace](planet-diversity-0.35/before-frames.csv), [after trace](planet-diversity-0.35/after-frames.csv).

The signed **0.35.0 / build 35** macOS app passed package verification and resumed the saved flight with 17 primaries and 58 moons in the current system. [Package metadata](planet-diversity-0.35/package.json).
