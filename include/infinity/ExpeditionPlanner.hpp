#pragma once
#include "Economy.hpp"

namespace infinity {
enum class PlanStage : uint8_t { Loading, Offer, Prepare, Travel, Drill, Enter, Overview, Inspect,
                                 Mark, Regrow, Extract, Return, Deliver, Complete, Cancelled };
struct PlannerChoice {
    uint64_t contract = 0, bore = 0;
    double depth = 80, feed = 1;
    unsigned bay = 0;
    bool tracking = false, newSite = false, details = false;
    std::string serialize() const;
    bool restore(std::string_view);
};
struct PlannerInput {
    SurveyObserver observer;
    const ShipCrew* crew = nullptr;
    unsigned captainMount = 0;
    bool open = false, pending = false, connectedIssuer = false;
};
struct PlannerOffer { uint64_t id = 0; std::string title; SurveyPhase phase = SurveyPhase::Open; };
struct PlannerCheck { std::string text; bool ready = false, required = true; };
struct PlannerStep { std::string text; bool done = false, active = false; };
enum class PlannerAction : uint8_t { Accept, Deliver, Course, Equipment, Crew, Resume, Next };
enum class PlannerTool : uint8_t { Empty, Gun, Probe, Miner };
struct PlannerBay {
    PlannerTool tool = PlannerTool::Empty;
    unsigned level = 0, rounds = 0;
    double operatorReadiness = 0;
    bool captain = false;
};
struct PlannerDeposit {
    glm::dvec2 metres{}; // x / depth in the saved bore's tangent section
    Mineral mineral = Mineral::Quartz;
    unsigned grade = 0;
    bool marked = false;
    uint64_t minedKg = 0;
};
struct PlannerVisuals {
    bool known = false, solid = false, hasBore = false, chamberReady = false, shipLocal = false;
    GeologyType geology = GeologyType::StagnantLid;
    glm::vec3 tint{.5f,.6f,.7f};
    std::array<double,4> composition{}; // ice, metal, silicate, carbon
    std::array<PlannerBay,maxWeaponMounts> bays;
    std::array<PlannerDeposit,cavernFindCount> deposits;
    double boreDepth = 80, reached = 0, chamberRadius = 18, heat = 0;
    double shipDepth = 0, shipLateral = 0, shipMetres = 12, sampleKg = 0;
    ProbePhase phase = ProbePhase::Flight;
};
struct ExpeditionPlan {
    bool open = false, tracking = false, busy = false, primaryEnabled = false, details = false;
    PlanStage stage = PlanStage::Loading;
    PlannerAction primary = PlannerAction::Resume;
    uint64_t contract = 0, bore = 0;
    unsigned find = 0, bay = 0, equipmentMount = 0;
    size_t selected = 0, firstOffer = 0, equipmentRow = 0;
    MarketPage equipmentPage = MarketPage::Survey;
    Address world;
    ActivityId issuer;
    std::optional<glm::dvec3> entrance;
    std::optional<glm::dvec3> worksite;
    int64_t requiredKg = 10;
    std::string environmentalWarning;
    bool returnToPort = false;
    double depth = 80, feed = 1, suggestedDepth = 80, suggestedFeed = 1;
    std::string title = "EXPEDITION PLANNER", issuerName, reward, geology, composition, conditions, forecastNote;
    std::string objective, instruction, primaryLabel = "RESUME FLIGHT", message, compact;
    std::vector<PlannerOffer> offers;
    std::vector<PlannerCheck> checks;
    std::array<PlannerStep,10> steps;
    PlannerVisuals visual;
    SurveyAccounting accounting;
    uint64_t nextContract = 0;
    bool debrief = false;
};
struct PlannerSummary {
    unsigned step = 0, blockers = 0;
    bool warning = false;
    std::string instruction,context;
};
PlannerSummary summarizePlan(const ExpeditionPlan&);
std::string_view planStageName(PlanStage);
ExpeditionPlan planCavernExpedition(const Universe&, const EconomySnapshot*, const PlannerChoice&, const PlannerInput&);
void cyclePlannerContract(PlannerChoice&, const ExpeditionPlan&, int direction);
void cyclePlannerBore(PlannerChoice&, const ExpeditionPlan&, const ExplorationState&);
} // namespace infinity
