#pragma once
#include "SurfaceLife.hpp"
#include <array>
#include <filesystem>
#include <memory>
namespace infinite {
inline constexpr unsigned economyRecipeVersion = 1;
inline constexpr double economyTickSeconds = 5; // Five economic minutes per five flight seconds.
enum class Good : uint8_t {
    Ice,
    Water,
    Oxygen,
    Ore,
    Silicate,
    Carbon,
    Alloy,
    Silicon,
    Polymers,
    Biomass,
    Food,
    Medicine,
    Electronics,
    Machinery,
    Propellant,
    Filters,
    Tools,
    HabitatParts,
    Instruments,
    Recyclate,
    Count
};
inline constexpr size_t goodCount = size_t(Good::Count);
using Stock = std::array<int64_t, goodCount>; // canonical kilograms; never render-scaled
struct GoodInfo {
    std::string_view name;
    int64_t baseCents, litresPerKg;
};
const GoodInfo& goodInfo(Good);
struct ProductionRecipe {
    std::string_view name;
    Stock inputs{}, outputs{};
    int64_t wasteKg = 0;
    unsigned workers = 1, power = 1;
};
std::span<const ProductionRecipe> productionRecipes();
struct EconomicSite {
    ActivityId id;
    std::string name, role;
    glm::dvec3 metres{}, systemMetres{}; // SI in id.home axes
    double radius = 1, temperature = 280, moisture = 0;
    PlanetClimate climate;
    uint64_t seed = 0, population = 0;
    bool sealed = false, orbital = false;
    unsigned workers = 1, power = 1;
    int64_t capacityKg = 10000;
    std::vector<unsigned> recipes;
    Stock initial{}, target{}, reserves{};
};
struct EconomicNetwork {
    Address system;
    uint64_t universeSeed = 0, fingerprint = 0;
    std::vector<EconomicSite> sites;
    std::vector<ActivityId> vessels;
    size_t residentBytes() const;
};
// Worker-only adapter: all geometry and society generators are read-only.
std::shared_ptr<const EconomicNetwork> generateEconomicNetwork(const Universe&, const Address&,
                                                               const std::atomic<bool>* cancel = nullptr);
enum class FacilityModule : uint8_t { Storage, Power, Automation, Count };
inline constexpr size_t facilityModuleCount = size_t(FacilityModule::Count);
struct FacilityRefit {
    std::string_view name, effect;
    Stock materials{};
    int64_t labourCents = 0;
    unsigned ticks = 0;
};
FacilityRefit facilityRefit(FacilityModule, unsigned level);
struct FacilityProject {
    FacilityModule module = FacilityModule::Storage;
    Stock delivered{};
    unsigned progress = 0;
    int64_t escrow = 0;
};
struct EconomicSiteState {
    Stock stock{}, reserved{}, extracted{}, produced{}, consumed{}, imported{}, exported{};
    int64_t cash = 0, householdCash = 0;
    unsigned condition = 1000, health = 1000;
    uint64_t outageUntil = 0;
    std::string bottleneck;
    std::array<unsigned, facilityModuleCount> modules{};
    std::optional<FacilityProject> project;
    int64_t dividends = 0, dividendsPaid = 0, operatingBaseline = 0;
};
inline int64_t economicCapacity(const EconomicSite& d, const EconomicSiteState& s) {
    return d.capacityKg + d.capacityKg * s.modules[size_t(FacilityModule::Storage)] / 4;
}
int64_t economicPower(const EconomicSite&, const EconomicSiteState&, uint64_t tick);
bool economicConstructionActive(const EconomicSite&, const EconomicSiteState&, uint64_t tick);
Stock facilityMaterials(const EconomicSiteState&); // installed and awaiting installation, not market stock
enum class FreightPhase : uint8_t { Idle, Reposition, Loading, Transit, Holding };
struct EconomicFreight {
    ActivityId id;
    unsigned location = 0, source = 0, destination = 0;
    FreightPhase phase = FreightPhase::Idle;
    Good good = Good::Ice;
    int64_t kg = 0; // reserved before pickup, onboard after pickup
    uint64_t startTick = 0, endTick = 0, trips = 0;
    int64_t payment = 0, fee = 0;
};
enum class ContractPhase : uint8_t { Open, Accepted, Complete, Expired };
struct EconomicContract {
    uint64_t id = 0, deadline = 0;
    unsigned site = 0;
    Good good = Good::Water;
    int64_t requested = 0, delivered = 0, unitCents = 0, escrow = 0;
    ContractPhase phase = ContractPhase::Open;
};
enum class HaulPhase : uint8_t { Open, Reserved, InTransit, Complete, Cancelled, Expired };
struct HaulContract {
    uint64_t id = 0, deadline = 0;
    unsigned source = 0, destination = 0;
    Good good = Good::Water;
    int64_t kg = 0, cost = 0, fee = 0, escrow = 0;
    HaulPhase phase = HaulPhase::Open;
};
struct PlayerHaul {
    Address system;
    uint64_t id = 0, deadline = 0;
    ActivityId source, destination;
    Good good = Good::Water;
    int64_t kg = 0;
    bool onboard = false;
};
struct EconomicEvent {
    uint64_t tick = 0;
    std::string text;
};
struct EconomyPlayer {
    int64_t credits = 250000, capacityKg = 4000, capacityLitres = 8000;
    Stock cargo{};
    uint64_t lastCommand = 0;
    std::optional<PlayerHaul> haul;
    uint64_t deliveries = 0, lateDeliveries = 0, returnedHauls = 0;
    int64_t freightEarnings = 0;
    int64_t massKg() const;
    int64_t volumeLitres() const;
    int64_t availableCargo(Good) const;
    int64_t sealedKg() const;
    std::string_view carrierRank() const;
    std::string serialize() const;
    bool restore(std::string_view);
};
struct EconomyQuote {
    int64_t buyCents = 0, sellCents = 0, available = 0;
};
enum class EconomicAction : uint8_t {
    Buy,
    Sell,
    Contract,
    Invest,
    CancelProject,
    CollectDividends,
    AcceptHaul,
    PickupHaul,
    DeliverHaul,
    CancelHaul
};
struct EconomicCommand {
    uint64_t sequence = 0;
    ActivityId port;
    EconomicAction action = EconomicAction::Buy;
    Good good = Good::Water;
    int64_t kg = 10, limitCents = 0;
    uint64_t contract = 0;
    FacilityModule module = FacilityModule::Storage;
};
int64_t economicPendingPayload(const EconomicCommand&, const EconomicSiteState&);
class Economy {
  public:
    explicit Economy(std::shared_ptr<const EconomicNetwork>);
    const EconomicNetwork& network() const {
        return *network_;
    }
    const std::vector<EconomicSiteState>& sites() const {
        return sites_;
    }
    const std::vector<EconomicFreight>& freight() const {
        return freight_;
    }
    const std::vector<EconomicContract>& contracts() const {
        return contracts_;
    }
    const std::vector<EconomicEvent>& events() const {
        return events_;
    }
    const std::vector<HaulContract>& hauls() const {
        return hauls_;
    }
    uint64_t tick() const {
        return tick_;
    }
    void advanceTo(uint64_t target, unsigned budget = 64);
    std::optional<size_t> siteIndex(const ActivityId&) const;
    EconomyQuote quote(size_t site, Good, int64_t kg) const;
    std::string execute(const EconomicCommand&, EconomyPlayer&);
    bool reconcilePlayer(EconomyPlayer&) const;
    void outage(const ActivityId&, uint64_t untilTick); // canonical external event, e.g. damaged plant
    int64_t moneyTotal() const;
    Stock inventoryTotal() const;
    Stock initialTotal() const;
    Stock externalGoodsDelta() const; // extracted + recipe output - inputs - final consumption
    std::string serialize() const;
    bool restore(std::string_view);
    size_t residentBytes() const;

  private:
    std::shared_ptr<const EconomicNetwork> network_;
    std::vector<EconomicSiteState> sites_;
    std::vector<EconomicFreight> freight_;
    std::vector<EconomicContract> contracts_;
    std::vector<EconomicEvent> events_;
    std::vector<HaulContract> hauls_;
    uint64_t nextHaul_ = 1;
    uint64_t tick_ = 0, nextContract_ = 1;
    int64_t treasury_ = 100000000, carrierCash_ = 10000000;
    int64_t endowmentMoney_ = 0, externalMoneyDelta_ = 0;
    void step();
    void event(std::string);
    void production(size_t);
    void logistics();
    void offers();
    void development();
    std::string invest(const EconomicCommand&, EconomyPlayer&, size_t);
    void haulOffers();
    std::string haulCommand(const EconomicCommand&, EconomyPlayer&);
    uint64_t travelTicks(size_t, size_t) const;
    int64_t unitPrice(size_t, Good, int64_t available, bool buy) const;
};
struct EconomicFlight {
    ActivityId id;
    glm::dvec3 from{}, to{}, sourceCentre{}, destinationCentre{}; // SI in system reference
    double sourceRadius = 1, destinationRadius = 1, stellarRadius = 1;
    double startSeconds = 0, endSeconds = 1;
    Good good = Good::Water;
    int64_t kg = 0;
    FreightPhase phase = FreightPhase::Idle;
};
struct EconomySnapshot {
    std::shared_ptr<const EconomicNetwork> network;
    std::vector<EconomicSiteState> sites;
    std::vector<EconomicFlight> flights;
    std::vector<EconomicContract> contracts;
    std::vector<EconomicEvent> events;
    std::vector<HaulContract> hauls;
    EconomyPlayer player;
    uint64_t tick = 0;
    bool catchingUp = false;
    std::string message;
    struct TradeRoute {
        unsigned source = 0, destination = 0;
        Good good = Good::Water;
        int64_t kg = 0, costCents = 0, revenueCents = 0;
        double distanceMetres = 0;
    };
    std::vector<TradeRoute> routes;
    struct Job {
        bool haul = false;
        uint64_t id = 0;
        ActivityId target;
        EconomicAction action = EconomicAction::Contract;
        Good good = Good::Water;
        int64_t kg = 0;
        std::string title, route;
    };
    std::vector<Job> jobs;
    std::optional<size_t> siteIndex(const ActivityId&) const;
    EconomyQuote quote(size_t, Good, int64_t kg) const;
    std::optional<ActivityId> contractDestination() const;
};
// Bounded worker-only search. Quotes include both spreads and marginal stock changes.
std::vector<EconomySnapshot::TradeRoute> economicTradeRoutes(const EconomySnapshot&);
std::vector<EconomySnapshot::Job> economicJobs(const EconomySnapshot&);
ActivityBody sampleEconomicFreight(const Universe&, const EconomicNetwork&, const EconomicFlight&,
                                   double seconds);
// One cancellable worker owns the ledger and SQLite. Main/render consume immutable snapshots.
class EconomyRuntime {
  public:
    EconomyRuntime(const Universe&, const std::filesystem::path& data);
    ~EconomyRuntime();
    void request(const Address& system, double seconds);
    bool command(const EconomicCommand&);
    std::shared_ptr<const EconomySnapshot> snapshot() const;
    void flush();
    size_t residentBytes() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Pilot;
class Navigation;
bool economicTradeAccess(const Universe&, const Navigation&, const Pilot&, const EconomicSite&);
enum class MarketPage : uint8_t { Goods, Routes, Industry, Jobs };
struct MarketView {
    bool open = false, connected = false, ready = false;
    std::string site, role, message, bottleneck, contract;
    std::array<std::string, goodCount> rows;
    size_t selected = 1;
    int64_t quantity = 10, credits = 0, cargoKg = 0, cargoLitres = 0;
    uint64_t tick = 0;
    MarketPage page = MarketPage::Goods;
    std::vector<std::string> detail;
    std::string summary;
    std::string activeJob;
};
MarketView economicMarketView(const EconomySnapshot*, const std::optional<ActivityId>&, size_t selected,
                              int64_t quantity, bool connected, MarketPage page = MarketPage::Goods);
} // namespace infinite
