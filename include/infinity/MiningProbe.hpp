#pragma once
#include "Geometry.hpp"
#include "Weapons.hpp"

namespace infinity {
class Navigation;
inline constexpr size_t maxBoreholes = 64;
inline constexpr unsigned maxProbeRounds = 6, boreSectors = 32;
enum class ProbeService : uint8_t { Fit, Reload, Remove };
enum class ProbePhase : uint8_t { Flight, Boring, Arming, Blast, Beacon, Paused, Retracting, Recovered };
enum class ProbeControl : uint8_t { Depth, Rate, Pause, Resume, Retract, Detonate };
inline constexpr unsigned maxProbeControls = 64, cavernFindCount = 3;
struct ProbeInstruction { double seconds = 0; ProbeControl control = ProbeControl::Pause; double value = 0; };
struct CavernRecipe {
    uint64_t seed = 0;
    std::array<double,4> fractions{};
    glm::dvec3 bedding{0,1,0}, fracture{1,0,0}; // planet frame; fields are continuous across excavations
    double sediment = 0, fractureStrength = 0, cooling = .5, temperature = 280;
    Mineral substrate = Mineral::Quartz;
};
struct CavernFindState {
    unsigned grade = 0, paidGrade = 0;
    uint64_t revision = 0, minedRevision = 0, minedKg = 0;
    bool marked = false;
};
struct Borehole {
    uint64_t id = 0;
    Address planet;
    unsigned mount = 0;
    glm::dvec3 launch{}, entry{}; // SI in the existing planet frame
    double started = 0, flightSeconds = 1, drillSeconds = 10;
    double depth = 80, chamberRadius = 18, shaftRadius = 4.5;
    unsigned geologyVersion = 0;
    CavernRecipe geology; // read-only derived metadata; reconstructed from the same planet on load
    std::vector<ProbeInstruction> controls;
    std::array<CavernFindState,cavernFindCount> finds;
    double impactTime() const { return started + flightSeconds; }
    double blastTime() const;
    double finalDepth() const;
    double roomRadius() const;
};
struct ProbeOperation {
    ProbePhase phase = ProbePhase::Flight;
    double depth = 0, reached = 0, target = 80, rate = 1;
    double work = 0, heat = 0, stopped = 0, blastAt = 1e30;
};
ProbeOperation probeOperation(const Borehole&, double seconds);
bool controlProbe(Borehole&, ProbeControl, double value, double seconds, std::string& status);
bool probeActive(const Borehole&, double seconds);
CavernRecipe cavernRecipe(const Universe&, const Borehole&);
enum class CavernFeature : uint8_t { Strata, Vein, Crystals };
struct CavernFind {
    unsigned index = 0, resource = 0;
    uint64_t id = 0;
    CavernFeature feature = CavernFeature::Strata;
    Mineral mineral = Mineral::Quartz;
    glm::dvec3 local{}, inward{0,1,0}; // bore tangent frame, in metres
    double radius = 2, concentration = 0;
};
struct CavernCrystal {
    glm::dvec3 centre{};
    glm::dquat orientation{1,0,0,0};
    double radius = .2, height = .8;
    Mineral mineral = Mineral::Quartz;
};
std::array<CavernFind,cavernFindCount> cavernFinds(const Borehole&);
struct CavernCrystals { std::array<CavernCrystal,7> items; size_t count = 0; };
CavernCrystals cavernCrystals(const Borehole&);
double cavernCrystalClearance(const CavernCrystal&, glm::dvec3 local);
Mesh cavernCrystalMesh(const CavernCrystal&);
glm::vec3 cavernWallColour(const Borehole&, glm::dvec3 local);
std::string cavernFindName(const CavernFind&);
glm::dvec3 cavernFindPoint(const Borehole&, const CavernFind&);
uint64_t probeGeometryKey(const Borehole&);
double probeBlastDamage(const Borehole&,glm::dvec3 planetMetres);
struct MiningProbeState {
    std::array<bool, maxWeaponMounts> launchers{};
    std::array<unsigned, maxWeaponMounts> rounds{};
    std::vector<Borehole> holes;
    size_t residentBytes() const;
};
struct ProbePose {
    ProbePhase phase = ProbePhase::Flight;
    glm::dvec3 position{}, forward{0, 0, -1};
    glm::dquat attitude{1, 0, 0, 0};
    double depth = 0, progress = 0, blast = 0;
};
struct ProbePlan {
    std::optional<Borehole> hole;
    std::string status;
};
ProbePlan planMiningProbe(const Universe&, const Address& planet, const WeaponRay&, unsigned mount,
                          uint64_t id);
ProbePose probePose(const Borehole&, double seconds);
void frameProbeCamera(Navigation& camera, const Universe&, const Borehole&, double seconds,
                       glm::dvec2 look = {});
std::string_view probePhaseName(ProbePhase);
const Borehole* latestProbe(const MiningProbeState&, const Address& system);
double boreClearance(const Borehole&, glm::dvec3 planetMetres, double seconds);
double boreRoomRadius(const Borehole&, glm::dvec3 chamberLocal);
double excavationClearance(const MiningProbeState&, const Address& planet, glm::dvec3, double seconds);
glm::dvec3 excavationNormal(const MiningProbeState&, const Address& planet, glm::dvec3, double seconds);
bool nearExcavation(const MiningProbeState&, const Address& planet, glm::dvec3, double seconds);
std::string serializeProbes(const MiningProbeState&);
bool restoreProbes(MiningProbeState&, std::string_view, const Universe&);
std::shared_ptr<Geometry> boreGeometry(const Universe&, const Borehole&, bool chamber,
                                      const std::atomic<bool>* cancel = nullptr);
std::shared_ptr<Geometry> excavatedTerrain(const Geometry&, std::span<const Borehole>,
                                          bool chamber, const std::atomic<bool>* cancel = nullptr);
} // namespace infinity
