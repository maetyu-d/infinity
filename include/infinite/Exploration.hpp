#pragma once
#include "Crew.hpp"
#include "Prospecting.hpp"
#include "Weapons.hpp"
#include <iosfwd>

namespace infinite {
class Economy;
struct EconomyPlayer;
struct EconomicCommand;
struct EconomySnapshot;
struct MarketView;
inline constexpr size_t maxSurveyRecords = 1024, maxProspects = 1024, maxSurveyContracts = 128;
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
enum class SurveyPhase : uint8_t { Open, Accepted, Complete, Cancelled };
struct SurveyContract {
    uint64_t id = 0;
    ActivityId issuer;
    Address target;
    unsigned grade = 1;
    SurveyPhase phase = SurveyPhase::Open;
    int64_t reward = 0, escrow = 0;
    uint64_t acceptedRevision = 0;
    std::string name;
};
struct ExplorationState {
    unsigned scannerLevel = 1, miningLevel = 0, miningMount = noWeaponMount;
    std::vector<SurveyRecord> journal;
    std::vector<Prospect> prospects;
    std::vector<SurveyContract> contracts;
    uint64_t minedKg = 0, surveysDelivered = 0;
    int64_t surveyEarnings = 0;
    uint64_t revision = 0;
    std::array<unsigned, maxWeaponMounts> stationLoadout(const EconomyPlayer&) const;
};
SurveyRecord surveyReading(const Universe&, const Address&, unsigned grade, double seconds);
std::optional<Address> surveySystem(const Address&);
std::optional<Address> prospectAddress(const Universe&, const Address&);
ResourceStock prospectResources(const Universe&, const Address&);
bool surveyAccess(const Universe&, const SurveyObserver&, const Address&);
struct MiningAccess {
    bool allowed = false;
    Address deposit;
    WeaponRay ray;
    glm::dvec3 endpoint{};
    std::string status;
};
MiningAccess miningAccess(const Universe&, const SurveyObserver&, const Address&, unsigned mount,
                          std::span<const ActivityBody> nearby = {});
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
    std::string serialize() const;
    bool restore(std::string_view, const Universe&);
    size_t residentBytes() const;

  private:
    friend class ExpeditionLedger;
    ExplorationState state_;
};
void explorationMarketView(MarketView&, const EconomySnapshot&, bool journal, size_t selected, unsigned mount,
                           size_t site = 0);
std::vector<size_t> explorationJobs(const EconomySnapshot&);
} // namespace infinite
