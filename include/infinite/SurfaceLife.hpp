#pragma once
#include "Activity.hpp"
#include <map>
#include <mutex>
#include <oneapi/tbb/task_group.h>

namespace infinite {
inline constexpr uint32_t climateRecipeVersion = 1, societyRecipeVersion = 1;
inline constexpr uint32_t cityRecipeVersion = 1;
inline constexpr uint32_t surfaceActivitySlot = 2000;
enum class WeatherType : uint8_t { Vacuum, Clear, Cloud, Rain, Snow, Dust };
struct PlanetClimate {
    uint64_t seed = 0;
    bool solid = false, temperate = false;
    double temperatureK = 0, pressureBar = 0, gravity = 0, moisture = 0;
    double daySeconds = 86400, scaleHeightMetres = 0, cloudHeightMetres = 0, windMetresPerSecond = 0;
    glm::dvec3 axis{0, 1, 0};
};
struct WeatherSample {
    WeatherType type = WeatherType::Vacuum;
    double temperatureK = 0, pressureBar = 0, cloud = 0, precipitation = 0, storm = 0;
    double visibilityMetres = 1e9;
    glm::dvec3 wind{}; // SI, in the existing planet's axes; tangent to its surface.
};
PlanetClimate planetClimate(const Node&);
WeatherSample sampleWeather(const PlanetClimate&, glm::dvec3 direction, double altitudeMetres,
                            double seconds);
std::string_view weatherName(WeatherType);
enum class SettlementKind : uint8_t { City, Colony, Mining, Research };
enum class SurfaceMesh : uint8_t {
    Block,
    Tower,
    Dome,
    Industry,
    Greenhouse,
    Utility,
    Foundation,
    Pad,
    Road,
    Resident,
    Suit,
    Courtyard,
    SteppedTower,
    RowHousing,
    Hall,
    Warehouse,
    HabitatCluster,
    Tree,
    Light,
    Bench,
    Plaza,
    Footway,
    Solar,
    Count
};
enum class CityPattern : uint8_t { Planned, Radial, Organic, Contour };
enum class BuildingUse : uint8_t { Housing, Commerce, Civic, Industry, Agriculture, Utilities, Park };
enum class StreetClass : uint8_t { Avenue, Local, Footway };
struct SurfaceBuilding {
    uint64_t id = 0;
    SurfaceMesh kind = SurfaceMesh::Block;
    glm::dvec3 position{}, dimensions{1}; // Metres, origin on the building's floor.
    glm::dquat orientation{1, 0, 0, 0};
    glm::vec3 tint{1};
    double foundationMetres = 1;
    unsigned inhabitants = 0;
    BuildingUse use = BuildingUse::Housing;
    uint64_t parcel = 0;
    uint16_t district = 0, frontage = UINT16_MAX;
    uint32_t pathBegin = 0;
    uint16_t pathCount = 0;
    unsigned jobs = 0;
};
struct SurfaceRoad {
    glm::dvec3 a{}, b{};
    double width = 7;
    StreetClass kind = StreetClass::Local;
    uint16_t street = UINT16_MAX;
    // Eight terrain-sampled strip vertices, relative to (a+b)/2 in planet axes.
    std::array<glm::vec3, 8> surface{};
};
struct CityJunction {
    uint64_t id = 0;
    glm::dvec2 local{};
    glm::dvec3 position{};
};
struct CityStreet {
    uint64_t id = 0;
    uint16_t a = 0, b = 0;
    StreetClass kind = StreetClass::Local;
    double width = 7;
    uint32_t roadBegin = 0, roadCount = 0;
};
struct CityDistrict {
    uint64_t id = 0;
    BuildingUse use = BuildingUse::Housing;
    glm::dvec2 centre{};
    double radius = 1, ageYears = 0;
};
struct CityParcel {
    uint64_t id = 0;
    std::array<glm::dvec2, 4> corners{};
    uint16_t frontage = UINT16_MAX, district = 0;
};
struct CityProp {
    SurfaceMesh kind = SurfaceMesh::Bench;
    glm::dvec3 position{}, dimensions{1};
    glm::dquat orientation{1, 0, 0, 0};
    glm::vec3 tint{1};
};
struct SurfaceSettlement {
    uint32_t slot = 0;
    uint64_t seed = 0;
    SettlementKind kind = SettlementKind::Research;
    std::string name;
    glm::dvec3 direction{}, landingPoint{}; // Landing point is the unmodified terrain.
    glm::dquat orientation{1, 0, 0, 0};
    double radius = 0;
    unsigned population = 0;
    bool sealed = true;
    std::vector<SurfaceBuilding> buildings;
    std::vector<SurfaceRoad> roads;
    // Public concourse, sampled from the real height field, reserved clear of buildings.
    std::array<glm::dvec3, 8> walkway{};
    uint32_t layoutRecipe = 0;
    uint64_t layoutSeed = 0;
    CityPattern pattern = CityPattern::Planned;
    double foundingYears = 0;
    std::vector<CityJunction> junctions;
    std::vector<CityStreet> streets;
    std::vector<CityDistrict> districts;
    std::vector<CityParcel> parcels;
    std::vector<CityProp> props;
    std::vector<glm::vec3> pedestrianPoints; // Metres relative to landingPoint, in town axes.
};
struct SurfaceWorld {
    Address planet;
    uint64_t seed = 0, descriptor = 0;
    double radiusMetres = 1;
    PlanetClimate climate;
    std::vector<SurfaceSettlement> settlements;
    size_t residentBytes() const;
};
struct SurfaceResident {
    uint64_t id = 0, homeBuilding = 0;
    glm::dvec3 position{};
    glm::dquat orientation{1, 0, 0, 0};
    bool outside = false, suited = true;
};
std::shared_ptr<SurfaceWorld> generateSurfaceLife(const Universe&, const Node&,
                                                  const std::atomic<bool>* cancel = nullptr);
SurfaceResident sampleResident(const SurfaceWorld&, const SurfaceSettlement&, unsigned id, double seconds);
ActivityBody surfaceContact(const SurfaceWorld&, const SurfaceSettlement&, double seconds);
Mesh surfaceMesh(SurfaceMesh, unsigned detail);
bool refineCity(const Universe&, const Node&, SurfaceSettlement&, const std::atomic<bool>* cancel = nullptr,
                std::optional<CityPattern> pattern = std::nullopt);
std::string_view cityPatternName(CityPattern);
uint64_t cityFingerprint(const SurfaceSettlement&);
// Swept sphere against conservative oriented building bounds. Returns earliest contact fraction.
std::optional<double> surfaceCollision(const SurfaceWorld&, glm::dvec3 fromMetres, glm::dvec3 toMetres,
                                       double shipRadius);
struct SurfaceCacheStats {
    size_t bytes = 0, pending = 0, entries = 0;
    uint64_t completed = 0, cancelled = 0, evicted = 0;
    double lastMs = 0;
    std::string error;
};
struct SurfaceLifeSummary {
    uint64_t population = 0;
    uint32_t ports = 0, openPorts = 0;
};
SurfaceLifeSummary summarizeSurfaceLife(const SurfaceWorld&);
// Read-only additive descriptors, generated off the render thread. Two jobs and a hard LRU budget.
class SurfaceLifeCache {
  public:
    explicit SurfaceLifeCache(const Universe&, size_t budget = 4 * 1024 * 1024);
    ~SurfaceLifeCache();
    void beginFrame();
    std::shared_ptr<const SurfaceWorld> request(const Node&);
    std::shared_ptr<const SurfaceWorld> find(const Address&) const;
    // Low-priority HUD survey uses the same two workers and authoritative
    // generator, retaining only a bounded compact result for distant worlds.
    void survey(const Node&);
    std::optional<SurfaceLifeSummary> summary(const Address&) const;
    void finishFrame();
    void wait();
    SurfaceCacheStats stats() const;

  private:
    struct Entry {
        std::shared_ptr<const SurfaceWorld> world;
        std::shared_ptr<std::atomic<bool>> cancel;
        uint64_t used = 0;
        bool surveyOnly = false;
    };
    const Universe& universe_;
    size_t budget_;
    uint64_t frame_ = 0;
    mutable std::mutex mutex_;
    std::map<Address, Entry> entries_;
    struct SurveyEntry {
        SurfaceLifeSummary result;
        uint64_t used = 0;
    };
    std::map<Address, SurveyEntry> summaries_; // At most 256 compact observations.
    std::shared_ptr<const SurfaceWorld> request(const Node&, bool surveyOnly);
    SurfaceCacheStats stats_;
    oneapi::tbb::task_group tasks_;
};
} // namespace infinite
