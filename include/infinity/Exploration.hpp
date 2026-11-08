#pragma once
#include "Crew.hpp"
#include "Prospecting.hpp"
#include "MiningProbe.hpp"
#include "Weapons.hpp"
#include "SurfaceHazards.hpp"
#include <iosfwd>

namespace infinity {
class Economy;
class Navigation;
struct EconomyPlayer;
struct EconomicCommand;
struct EconomySnapshot;
struct MarketView;
inline constexpr size_t maxSurveyRecords = 1024, maxProspects = 1024, maxSurveyContracts = 128;
inline constexpr size_t maxSeamExpeditions = 128;
struct SurveyObserver {
    Address reference;
    glm::dvec3 position{}, velocity{}; // SI in reference axes
    glm::dquat attitude{1, 0, 0, 0};
    double size = 1, hull = 100, seconds = 0;
    bool field = false, landed = false;
};
struct SurveyRecord {
    Address address;
    uint64_t descriptor = 0;
    unsigned grade = 1;
    unsigned paidGrade = 0;
    uint64_t revision = 0;
    double seconds = 0;
    std::string name, formation, mineral, conditions, composition;
    std::array<double, 4> fractions{};
    std::optional<Address> prospect;
    ResourceStock resources{};
};
struct Prospect {
    Address address; // fixed level-7 surface patch; shared by all its finer scales
    ResourceStock extracted{};
    ResourceStock capacity{}; // frozen on first extraction; legacy depleted pools never refill
};
struct SeamExpedition {
    Address source; // actual Rock; all observations and marks must descend from it
    Address pool;   // existing finite, canonical surface reserve
    glm::dvec3 entryMetres{}; // safe return beacon in the planet frame, SI
    uint64_t beganRevision = 0, scannedRevision = 0;
    std::optional<Address> material;
    unsigned resource = 0;
    bool marked = false;
    uint64_t minedKg = 0;
};
enum class SurveyPhase : uint8_t { Open, Accepted, Complete, Cancelled };
struct SurveyAccounting {
    bool recorded = false;
    int64_t equipment = 0, ammunition = 0, repairs = 0, paid = 0;
    uint64_t lastCharge = 0, probesUsed = 0, extractedKg = 0;
    double departureHull = 100, returnHull = 100;
    int64_t netCash() const { return paid-equipment-ammunition-repairs; }
};
struct SurveyContract {
    uint64_t id = 0;
    ActivityId issuer;
    Address target;
    unsigned grade = 1;
    SurveyPhase phase = SurveyPhase::Open;
    int64_t reward = 0, escrow = 0;
    uint64_t acceptedRevision = 0;
    std::string name;
    bool cavern = false;
    uint64_t previous = 0;
    SurveyAccounting accounting;
    // A funded local shortage, using the existing cavern expedition workflow.
    unsigned supplyResource = 0;
    int64_t supplyKg = 0;
    uint64_t supplyMinedBaseline = 0;
    std::optional<Address> supplySite;
};
struct DebrisReserve {
    ActivityId id;
    ResourceStock extracted{};
};
struct ExplorationState {
    MiningProbeState probes;
    unsigned scannerLevel = 1, miningLevel = 0, miningMount = noWeaponMount;
    std::vector<SurveyRecord> journal;
    std::vector<Prospect> prospects;
    std::vector<SurveyContract> contracts;
    std::vector<SeamExpedition> seams; // most recently selected expedition is last
    std::vector<DebrisReserve> debris;
    uint64_t minedKg = 0, surveysDelivered = 0;
    int64_t surveyEarnings = 0;
    uint64_t revision = 0;
    std::array<unsigned, maxWeaponMounts> stationLoadout(const EconomyPlayer&) const;
};
struct CavernAssaySample { uint64_t bore = 0; unsigned find = 0, resource = 0; };
std::optional<CavernAssaySample> cavernAssaySample(const ExplorationState&, const SurveyContract&, const EconomyPlayer&);
bool supplyBoreMatches(const SurveyContract&, const Borehole&);
uint64_t surfaceSupplyMined(const ExplorationState&, const SurveyContract&);
uint64_t expeditionAccountFor(const ExplorationState&,uint64_t selected,const Address& currentSystem);
SurveyRecord surveyReading(const Universe&, const Address&, unsigned grade, double seconds);
std::optional<Address> surveySystem(const Address&);
std::optional<Address> prospectAddress(const Universe&, const Address&);
ResourceStock prospectResources(const Universe&, const Address&);
const SeamExpedition* activeSeam(const ExplorationState&, const Address& system);
const SeamExpedition* markedSeam(const ExplorationState&, const Address& target);
void selectSeamSource(Navigation&, const Address& source);
bool seamSurveyAccess(const Universe&, const SurveyObserver&, const SeamExpedition&, const Address&);
std::optional<glm::dvec3> seamEntryPoint(const Universe&, const SurveyObserver&, const Address& source);
std::string_view resourceName(unsigned);
bool surveyAccess(const Universe&, const SurveyObserver&, const Address&);
SurfaceExposure surveyExposure(const Universe&, const SurveyObserver&, std::optional<Address> target = {});
struct MiningAccess {
    bool allowed = false;
    Address deposit;
    WeaponRay ray;
    glm::dvec3 endpoint{};
    std::string status;
};
MiningAccess miningAccess(const Universe&, const SurveyObserver&, const Address&, unsigned mount,
                          std::span<const ActivityBody> nearby = {}, const SeamExpedition* seam = nullptr);
MiningAccess debrisMiningAccess(const Universe&, const SurveyObserver&, const ActivityId&);
class SurveyController {
  public:
    // Queue backpressure pauses work; releasing or losing access still cancels it.
    bool update(const Universe&, const SurveyObserver&, const std::optional<Address>&, unsigned grade,
                double crewQuality, double dt, bool held, bool submissionReady = true);
    void reset();
    double progress() const {
        return progress_;
    }
    const std::string& status() const {
        return status_;
    }

  private:
    std::optional<Address> target_;
    double progress_ = 0;
    bool submitted_ = false;
    std::string status_;
};
class MiningController {
  public:
    // Keep completed work available until the flight computer can accept it.
    bool update(const MiningAccess&, unsigned level, double readiness, double dt, bool held,
                bool submissionReady = true);
    double heat() const {
        return heat_;
    }
    double progress() const {
        return progress_;
    }
    const std::string& status() const {
        return status_;
    }
    void reset() {
        progress_ = 0;
        target_.reset();
    }

  private:
    std::optional<Address> target_;
    double progress_ = 0, heat_ = 0;
    bool overheated_ = false;
    std::string status_;
};
class ExplorationLedger {
  public:
    const ExplorationState& state() const {
        return state_;
    }
    void offer(const Universe&, Economy&);
    std::string execute(const Universe&, Economy&, EconomyPlayer&, const EconomicCommand&);
    void recordExpense(const EconomicCommand&,int64_t beforeCredits,const EconomyPlayer&);
    std::string serialize() const;
    bool restore(std::string_view, const Universe&);
    size_t residentBytes() const;

  private:
    friend class ExpeditionLedger;
    std::string executeOrder(const Universe&,Economy&,EconomyPlayer&,const EconomicCommand&);
    void offerFollowup(const Universe&,Economy&);
    void offerSurfaceSupply(const Universe&,Economy&);
    std::string executeProbe(const Universe&, Economy&, EconomyPlayer&, const EconomicCommand&);
    std::string executeCavern(const Universe&, Economy&, EconomyPlayer&, const EconomicCommand&);
    ExplorationState state_;
};
bool cavernSurveyAccess(const Universe&, const SurveyObserver&, const Borehole&, const CavernFind&, bool close);
MiningAccess cavernMiningAccess(const Universe&, const SurveyObserver&, const Borehole&, const CavernFind&,
                                unsigned mount, std::span<const ActivityBody> nearby = {});
void explorationMarketView(MarketView&, const EconomySnapshot&, bool journal, size_t selected, unsigned mount,
                           size_t site = 0);
std::vector<size_t> explorationJobs(const EconomySnapshot&);
} // namespace infinity
