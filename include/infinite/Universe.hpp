#pragma once
#include "Core.hpp"
#include <atomic>
#include <memory>
#include <span>

namespace infinite {
inline constexpr uint32_t generationRecipeVersion = 3;
enum class Mineral : uint8_t { Quartz, Iron, Olivine, Ice, Graphite };
struct Material {
    Mineral mineral;
    std::string_view name, formula, lattice;
    glm::vec3 color;
    double density, cellMetres;
};
const Material& material(Mineral);
struct Composition {
    double silicate = .65, metal = .25, carbon = .08, ice = .02;
    void normalize();
};
enum class EventType { Accretion, Collision, Heating, Cooling, Erosion, Deposition, Crystallisation };
struct Event {
    EventType type;
    double ageGyr, intensity;
    uint64_t seed;
};
struct Environment {
    Composition chemistry;
    double temperature = 280, pressureGPa = .1, ageGyr = 4, peakTemperature = 1000;
    Mineral mineral = Mineral::Quartz;
    // Normalized process strengths; pressure and peakTemperature describe formation.
    double coolingRate = .5, waterActivity = .2, strain = .1, impact = .1;
};
struct WebSample {
    double sheet = 0, filament = 0, knot = 0, voidDistance = 0, density = 0;
};
enum class GalaxyType : uint8_t { Spiral, BarredSpiral, Elliptical, Irregular };
enum class Population : uint8_t { Disc, Arm, Bulge, Halo, Association };
struct GalaxyProcess {
    GalaxyType type = GalaxyType::Spiral;
    double bulgeFraction = .2, gasFraction = .2, discScale = .24, thickness = .025;
    double pitch = .3, axisRatio = .7, merger = 0;
    unsigned arms = 2;
};
enum class GeologyType : uint8_t { MobileLid, StagnantLid, IceShell };
struct Plate {
    glm::dvec3 centre, angularVelocity;
    double continental;
};
struct ImpactBasin {
    glm::dvec3 centre;
    double radius, survival;
};
struct PlanetProcess {
    uint64_t seed = 0;
    GeologyType type = GeologyType::StagnantLid;
    double logRadius = 6.8, activity = .5, liquidWater = 0, ice = 0, ageGyr = 4;
    std::array<Plate, 12> plates;
    std::array<ImpactBasin, 24> impacts;
    std::array<glm::dvec3, 5> hotspots;
};
struct GeologySample {
    double heightMetres = 0, convergence = 0, divergence = 0, shear = 0;
    double volcanism = 0, sediment = 0, erosion = 0, cratering = 0, continental = 0;
};
enum class RockType : uint8_t { Plutonic, Volcanic, Sedimentary, Metamorphic, Breccia, Metallic, Glacial };
struct RockProcess {
    RockType type = RockType::Plutonic;
    glm::dvec3 axes{1, .85, 1};
    glm::dquat fabric{1, 0, 0, 0};
    double porosity = 0, bedding = .2, weathering = 0, strain = .1, impact = .1, coolingRate = .5;
};
struct CrystalProcess {
    glm::dvec3 axes{1};
    double growth = .5, boundaryAngle = 0, dislocation = 0;
};
WebSample cosmicStructure(glm::dvec3, uint64_t seed);
GalaxyProcess galaxyProcess(uint64_t, const WebSample&, double ageGyr);
std::shared_ptr<const PlanetProcess> planetProcess(uint64_t, double logRadius, const Environment&);
GeologySample geologySample(const PlanetProcess&, glm::dvec3 direction);
RockProcess rockProcess(uint64_t, const Environment&, const GeologySample&);
CrystalProcess crystalProcess(uint64_t, const Environment&);
Mineral selectMineral(uint64_t, const Environment&);
double rockDensity(uint64_t, const RockProcess&, glm::dvec3);
class RockField {
  public:
    RockField(uint64_t, const RockProcess&);
    double operator()(glm::dvec3) const;

  private:
    uint64_t seed_;
    RockProcess process_;
    glm::dquat inverse_;
    std::array<glm::dvec3, 9> clasts_, pits_;
    std::array<double, 9> clastRadii_, pitRadii_;
};
double crystalDensity(Mineral, const CrystalProcess&, glm::dvec3);
glm::vec3 rockAlbedo(const RockProcess&, Mineral, glm::dvec3);
std::string_view processName(GalaxyType);
std::string_view processName(GeologyType);
std::string_view processName(RockType);
struct Orbit {
    double semiMajorMetres = 1, eccentricity = 0, phase = 0, inclination = 0, centralMassSolar = 1;
};
glm::dvec3 keplerPosition(const Orbit&, double seconds);
struct Node {
    Address address;
    uint64_t seed = 0;
    LocalFrame frame;
    Environment environment;
    std::array<Event, 6> history{};
    // Physical descriptors, rather than visual-only random parameters.
    double massSolar = 1, temperatureK = 5772, luminositySolar = 1, stellarRadiusMetres = 6.957e8;
    Orbit orbit;
    unsigned companions = 0;
    glm::dvec3 geographicDirection{0, 1, 0};
    glm::dquat terrainOrientation{1, 0, 0, 0};
    double planetLogRadius = 6.8;
    uint64_t planetSeed = 0;
    uint64_t hostRockSeed = 0;
    double hostRockImpact = 0, grainScaleToRock = 1;
    GalaxyProcess galaxy;
    Population population = Population::Disc;
    std::shared_ptr<const PlanetProcess> geology;
    RockProcess rock;
    CrystalProcess crystal;
    std::string label() const;
};
struct StarSample {
    glm::dvec3 position;
    glm::vec3 color;
    double mass;
    double ageGyr = 4, metallicity = .02;
    Population population = Population::Disc;
};
glm::vec3 stellarColor(double temperatureK);
uint64_t descriptorFingerprint(const Node&);
class Universe {
  public:
    explicit Universe(uint64_t seed = 0x494e46494e495445ULL);
    Node describe(const Address&) const;
    std::vector<Node> lineage(const Address&) const;
    std::vector<LocalFrame> frames(const Address&) const;
    std::vector<Address> children(const Node&, size_t limit = 128) const;
    StarSample starSample(const Node& galaxy, uint64_t id) const;
    glm::dvec3 gasSample(const Node& galaxy, uint64_t id) const;
    double cosmicDensity(glm::dvec3 p, uint64_t seed) const;
    double terrainHeight(const Node& planet, glm::dvec3 direction) const;
    glm::dvec3 terrainPoint(const Node& region, double x, double z) const;
    glm::dvec3 terrainDirection(const Node& region, double x, double z) const;
    glm::dvec3 terrainNormal(const Node&, glm::dvec3 direction) const;
    glm::vec3 terrainAlbedo(const Node&, glm::dvec3 direction) const;
    glm::vec3 terrainAlbedo(const Node&, glm::dvec3 direction, const GeologySample&) const;
    double rockSdf(const Node&, glm::dvec3 p) const;
    double grainSdf(const Node&, glm::dvec3 p) const;
    Pose relative(const Address&, const Address& anchor, double renderLog) const;
    glm::dvec3 pointRelative(const Address&, glm::dvec3 localPoint, const Address& anchor,
                             double renderLog) const;
    uint64_t seed() const {
        return seed_;
    }

  private:
    Node makeChild(const Node&, Segment) const;
    struct DescriptorCache;
    uint64_t seed_;
    std::shared_ptr<DescriptorCache> descriptors_;
};
struct Atom {
    glm::vec3 position;
    uint8_t element;
    float radius;
    uint64_t siteId;
};
struct Bond {
    uint32_t a, b;
};
struct Lattice {
    std::vector<Atom> atoms;
    std::vector<Bond> bonds;
    size_t vacancies = 0;
    std::vector<uint64_t> vacantSites;
};
enum class CrystalPhase : uint8_t { AlphaQuartz, BccIron, FccIron, Forsterite, IceIh, Graphite };
struct CrystalSite {
    glm::dvec3 fractional;
    uint8_t element;
};
struct CrystalCell {
    CrystalPhase phase;
    glm::dmat3 basisMetres;
    std::vector<CrystalSite> sites;
};
CrystalCell crystalCell(Mineral, const Environment&);
std::string_view processName(CrystalPhase);
double vacancyProbability(const Environment&, Mineral);
struct LatticeQuery {
    int minCell = -4, maxCell = 4;
};
uint64_t atomicSiteId(int x, int y, int z, unsigned motif);
Lattice generateLattice(const Node&, LatticeQuery = {}, const std::atomic<bool>* cancel = nullptr);
} // namespace infinite
