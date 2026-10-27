# Structural process models — application 0.3.0 / recipe 3

Historical model validation for application 0.3.0. Recipe 3 is retained in version 0.4; current representation and performance evidence are in [PERFORMANCE.md](PERFORMANCE.md).

The generator now carries process descriptors through the hierarchy. Random counters select initial conditions and individual sites; each scale applies its own structural rules to those conditions. Geometry LOD, query size, cache state and worker order do not enter the hierarchical seed recipe.

## Implemented processes

| Scale | Generating process | Inherited constraints and visible consequences |
|---|---|---|
| Cosmic web | A two-scale, adhesion-inspired cellular collapse skeleton. Near-equal arrival distances create sheets; triple and four-way junctions create filaments and knots. Smaller structure is conditioned on coarse collapse. | Galaxy placement samples this same density. Void interiors remain sparse; cluster environments affect the galaxy merger/morphology proxy. No independent noise cloud replaces the skeleton. |
| Galaxies | Exponential surface-density discs; logarithmic arms and bars; truncated Hernquist bulges/haloes; quenched elliptical spheroids; irregular associations with shared centres. | Four morphology families, different gas budgets and spatial thicknesses. Old bulge/halo populations and young associations have distinct age distributions. A continuous broken power-law mass function is truncated by main-sequence survival age. Addressed systems inherit exactly the sampled mass, age, position and colour. |
| Planetary geology | Rigid spherical plate kinematics using Euler poles, with continuous pairwise boundary weights. Relative velocity generates convergence, extension and shear. Hotspot shields, impact bowls/rims/ejecta, resurfacing, incision and deposition contribute separate fields. | Size, retained heat, age, hydration and ice select mobile-lid, stagnant-lid or ice-shell regimes. Water enables the analytic erosion/deposition proxy. Active resurfacing suppresses old craters. Height, colour, normal and local sections sample the same planet descriptor. |
| Rocks | Seven origins: jointed plutonic blocks, vesicular volcanic material, bedded sediment, folded/foliated metamorphic material, impact breccia, metallic fragments and glacial ice. | Cooling, water, burial/formation pressure, strain, impacts and chemical reservoirs select the origin. The density field contains the corresponding joints, pores, layers, clasts or surface pits. Material bands use that same fabric. |
| Mineral grains | Composition-weighted endmember selection and crystal-habit growth. Quartz prisms and terminations, cubic iron facets, orthorhombic olivine, ice plates and graphite flakes. | No mineral is selected from an absent reservoir. Ice selection requires low temperature and pressure. Slow cooling gives larger grains; strain affects growth axes, boundaries and dislocations. Every grain is clipped to its actual host rock, and the atomic specimen is chosen inside that grain. |
| Atomic lattices | Fractional crystallographic sites, temperature-dependent iron phase, thermal/elastic lattice deformation, site-keyed static displacement, vacancies and substitution. Local bins find chemically allowed bonds. | Six phase representations: alpha quartz; BCC and FCC iron; 28-site forsterite with inherited Fe/Mg substitution; ice-Ih oxygen framework; AB graphite. Vacancy formation, retained impact damage and annealing depend on material and formation history. Query expansion preserves existing sites and bonds. |

The galaxy orientation and morphology are descriptors of the addressed galaxy. Galaxy gas uses its disc/arm/association model; the old generic central dust cloud is removed. Low-mass stars dominate the stellar sample, so the resulting image is not artificially populated with short-lived blue stars in ancient components.

The chemistry fractions represent condensed-solid reservoirs rather than stellar hydrogen/helium abundance. The inner planetary orbit follows an irradiation-driven refractory condensation bound; system reference radii enclose the resulting catalogue. Formation peak temperatures cannot fall below the current temperature. Solid selection rejects unsupported hot phases. Irradiation also changes volatile retention, and differentiation reweights inherited reservoirs without introducing an absent species. Below the planet, rock, grain and lattice retain that chemical budget. Mineral selection represents available endmembers and inclusions; it does not claim a fully mass-conserving thermochemical equilibrium calculation.

Formation pressure, peak temperature, cooling rate, water activity, strain and collision damage are explicit inputs. Event-summary intensities derive from them. The atomic node retains its grain's event history instead of rolling a separate set of events. Immutable planet descriptors and a bounded 1,024-object, thread-safe descriptor memo keep repeated navigation queries inexpensive. Geometry generation remains asynchronous.

## Atomic structure checks

The ideal periodic cells are tested separately from finite specimen clipping, thermal displacement, substitution and vacancies:

| Phase | Sites in conventional cell | Tested local coordination |
|---|---:|---|
| Alpha quartz | 3 Si + 6 O | Four oxygen neighbours around each Si |
| BCC iron | 2 Fe | Eight nearest neighbours |
| FCC iron | 4 Fe | Twelve nearest neighbours |
| Forsterite | 8 Mg + 4 Si + 16 O | Four oxygen neighbours around each Si |
| Ice Ih | 4 oxygen sites | Four nearest oxygen neighbours |
| Graphite | 4 C | Three in-plane neighbours; no artificial interlayer covalent edges |

The former seven-site olivine schematic is replaced by the orthorhombic 28-site structure. Fe/Mg substitution uses the inherited metal reservoir and an independent site seed. Quartz's fractional-site origin convention and bond topology are corrected. The model retains an oxygen-only ice framework; it does not invent hydrogen coordinates or solve proton disorder.

## Validation

The final desktop build passed **9/9 CTest cases** in **32.08 seconds**:

- **66,204 process checks:** a sampled web contains sheets, filaments, knots and voids; cell boundaries remain continuous; an ensemble includes all four galaxy families; stellar ages and survival masses agree; luminous-star orbits respect the solid condensation limit; unsupported hot solid phases are rejected; plate-velocity reversal exchanges convergence and divergence; dry/frozen bodies have no liquid erosion; all seven rock origins produce distinct density signatures; growth responds to cooling; crystal stoichiometry and coordination are checked; defects respond to damage/annealing; inherited descriptors and parallel reverse-order regeneration agree.
- **251 foundation checks:** existing addressing, finite transforms, deterministic geometry, orbit, cache, persistence and cancellation coverage, now including graphite.
- **61,289 continuity checks:** full non-default galaxy-to-lattice-to-galaxy journeys across three universe seeds, all canonical LODs, shared terrain fields, host-constrained matter, saved navigation, cache eviction and query-stable sites/vacancies/bonds for all five minerals.
- Separate persistence writer/reader fixtures, including a restart at atomic scale followed by the complete return journey.
- Real **1920×1080 Metal** tour: eight objects, seven revisits, fifteen settled captures. Submitted geometry is checked against independently regenerated descriptors and every LOD from a fresh descriptor cache. Digest: `16684174391868210590`.
- Framebuffer validation: **99,528 bright carbon pixels** in the central image region. The graphite parent section cannot reach the test's brightness threshold. The checker was extended because the earlier oxygen/iron-only colour test did not recognize carbon atoms.

The natural default branch is an icy world, a glacial host and a carbon-bearing graphite inclusion, illustrating that the inclusion refines the inherited mixed chemistry. This selection is not hard-coded for the tour.

The measured proof tour had median **16.68 ms**, P95 **17.52 ms**, and peak resident memory approximately **146.4 MiB**. This includes all-LOD proof workers and lossless captures; it is a local measurement, not a universal frame-rate guarantee. Generation benchmarks record requested detail, with invariant levels canonicalized by the generator.

Evidence: [complete test results](diversity-tests.log), [individual test output](diversity-test-details.log), [native addresses and proof](native-diversity.log), [frame timings](diversity-frame-profile.csv), [generation timings](benchmark-v0.3.csv), [benchmark memory](benchmark-memory-v0.3.txt), [pixel check](diversity-pixel-check.txt).

Captures: [galaxy](diversity-captures/galaxy.png), [icy planet](diversity-captures/planet.png), [rock](diversity-captures/rock.png), [grain](diversity-captures/grain.png), [graphite lattice](diversity-captures/lattice.png).

## Compatibility and reproduction

**Recipe 3 preserves canonical addresses, integer child IDs and the original hierarchical seed hash.** Eight legacy path seeds remain pinned in tests. It intentionally changes positions, environments, geometry, material choices, lattice motifs and defect arrangements produced from those seeds. Recipe-2 mesh bytes are not compatibility fixtures for recipe 3. Transient geometry caches rebuild. Discovery notes remain attached to their addresses, and the SQLite sparse-storage schema is unchanged. Use `--fresh` or R to start with a reset camera when inspecting the new layouts.

Process-specific streams use named feature domains. Atomic vacancies, thermal displacement and substitution use exact unit-cell/motif identities, independently of enumeration bounds. Structural descriptors participate in the identity fingerprint. Fresh-world tests and the native proof do not depend on memoized descriptors.

```sh
cmake -S . -B build -DINFINITE_VIEWER=ON -DINFINITE_GPU_TESTS=ON
cmake --build build --target infinite infinite_tests infinite_continuity_tests infinite_process_tests infinite_benchmark -j 4
ctest --test-dir build --output-on-failure
./build/infinite_benchmark > build/generation.csv
```

The GPU fixtures require an active desktop. Set `INFINITE_GPU_TESTS=OFF` for the seven headless cases; `INFINITE_VIEWER=OFF` also avoids building the renderer. On this machine, `ctest` is `/Users/user/.local/opt/cmake/CMake.app/Contents/bin/ctest`.

## Scientific basis and approximation boundaries

These are analytic generating models informed by physical mechanisms. Their coefficients are illustrative, not fitted simulation outputs. The collapse skeleton does not integrate gravity, Euler-pole kinematics does not integrate mantle convection, and the incision/deposition expression is not a hydrological solver. The mineral library is a restricted endmember model rather than a phase-equilibrium solver. Crystal facets illustrate material habits; their surface energies are not computed. Static lattice displacement and defect fields are not molecular dynamics.

Primary references used to constrain the models:

- [NASA: Mapping the Cosmic Web](https://science.nasa.gov/mission/hubble/science/science-highlights/mapping-the-cosmic-web/) describes the growth of density variations into sheets, filaments and clusters. The particular cellular approximation and coefficients here are implementation choices.
- [NASA: Galaxy Types](https://science.nasa.gov/universe/galaxies/types/) distinguishes discs, arms, bulges, haloes, young arm populations and gas-poor ellipticals. [NASA-hosted IMF review](https://ned.ipac.caltech.edu/level5/March18/Hopkins/Hopkins2.html) discusses broken power-law mass distributions; the bounded inverse sampler here is an illustrative population model.
- [USGS: Plate Motions](https://pubs.usgs.gov/gip/dynamic/understanding.html) describes convergent, divergent and transform boundaries. [USGS: Collecting Rocks](https://pubs.usgs.gov/gip/collect1/collectgip.html) connects rock origin, cooling and crystal size. These inform the process rules rather than validate their numerical coefficients.
- [AFLOW: Alpha Quartz](https://www.aflow.org/prototype-encyclopedia/A2B_hP9_152_c_a-001/), [AFLOW: Hexagonal Graphite](https://aflow.org/p/A_hP4_194_bc-001/A_hP4_194_bc-001.pdf) and [AFLOW: Forsterite](https://www.aflow.org/prototype-encyclopedia/V88M/) supply prototype symmetry and Wyckoff construction. Forsterite parameters are rounded educational values transformed into the standard Pnma setting; they are not a refined structure at the generated environment.
- [Chinese Academy of Sciences: Iron Lattice Dynamics](https://english.iop.cas.cn/rh/rp/201601/t20160105_158450.html) identifies the BCC/FCC/BCC transitions used by the phase selector. [Morrison et al., ice-Ih calculations](https://www.tcm.phy.cam.ac.uk/~mcp1/publications/PDF/morrison1.pdf) describes the tetrahedral oxygen framework. [Vacancy calculations in BCC iron](https://pmc.ncbi.nlm.nih.gov/articles/PMC7287698/) supports the approximate Fe vacancy energy; other formation energies are illustrative effective parameters.

Remaining engineering limits include finite sparse object queries, unstitched terrain edges, bounded lattice windows, no persistent matter editing, and unverified Windows/Linux GPU execution. Cross-compiler/libm floating-point identity remains unproven. The earlier camera/identity continuity guarantees continue to be exercised within this build.
