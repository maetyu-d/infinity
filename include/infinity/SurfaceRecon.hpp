#pragma once
#include "Economy.hpp"
#include <oneapi/tbb/task_group.h>
#include <mutex>

namespace infinity {
inline constexpr unsigned reconGrid = 21;
struct ReconCell {
    glm::dvec3 metres{};
    float height = 0, slope = 0, concentration = 0;
    unsigned resource = 0;
    bool dry=true;
};
struct SurfaceMap {
    Address planet;
    Tile tile;
    glm::dvec3 centre{};
    glm::dquat axes{1,0,0,0};
    double radius = 4000;
    std::array<ReconCell,reconGrid*reconGrid> cells;
};
std::shared_ptr<const SurfaceMap> sampleSurfaceMap(const Universe&,const Node&,Tile,const std::atomic<bool>* cancel=nullptr);
glm::dvec2 reconPosition(const SurfaceMap&,glm::dvec3 planetMetres);
class SurfaceMapCache {
  public:
    ~SurfaceMapCache();
    std::shared_ptr<const SurfaceMap> request(const Universe&,const Node&,glm::dvec3 direction);
    void wait();
    size_t entries() const;
  private:
    struct Entry {Address planet;uint64_t tile=0,used=0;std::shared_ptr<const SurfaceMap> map;std::shared_ptr<std::atomic<bool>> cancel;};
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    oneapi::tbb::task_group tasks_;
    uint64_t serial_=0;
};
enum class ReconKind { Landing, Estimate, Assay, Entrance, Settlement, Incident };
struct ReconPoint {
    uint64_t id=0;
    ReconKind kind=ReconKind::Estimate;
    glm::dvec3 metres{};
    std::optional<Address> address;
    std::optional<ActivityId> site;
    uint64_t recovery=0;
    RecoveryPhase phase=RecoveryPhase::Open;
    unsigned grade=0,resource=0;
    bool actionable=false,waiting=false;
    std::string name,detail;
};
struct ReconWeather {
    glm::dvec2 point{};
    double severity=0;
};
struct ReconOrders {
    bool navigator=false,engineer=false;
    uint64_t target=0;
    double assessment=0,work=0;
};
struct SurfaceReconView {
    bool open=false,loading=false,navWatching=false,engineerWatching=false;
    std::shared_ptr<const SurfaceMap> map;
    glm::dvec3 eye{};
    glm::dvec2 ship{},heading{0,-1};
    std::vector<ReconPoint> points;
    std::array<ReconWeather,25> weather;
    uint64_t selected=0;
    double scanQuality=1,confidence=0,safeAltitude=250,workProgress=0;
    std::string status,navigator,engineer,action;
    const ReconPoint* target() const;
};
struct ReconInput {
    SurveyObserver observer;
    const SurfaceWorld* world=nullptr;
    const EconomySnapshot* economy=nullptr;
    const ShipCrew* crew=nullptr;
    SurfaceExposureState exposure;
    uint64_t selected=0;
    double iceLoad=0;
    bool open=false;
};
SurfaceReconView describeSurfaceRecon(const Universe&,std::shared_ptr<const SurfaceMap>,const ReconInput&,ReconOrders&,double dt);
void cycleReconPoint(uint64_t&,const SurfaceReconView&,int direction=1);
std::optional<uint64_t> pickReconPoint(const SurfaceReconView&,glm::dvec2 normalized);
bool fieldServiceAccess(const Universe&,const EconomicSite&,const SurveyObserver&,std::string& why);
std::string respondFieldIncident(const Universe&,Economy&,EconomyPlayer&,const EconomicCommand&);
} // namespace infinity
