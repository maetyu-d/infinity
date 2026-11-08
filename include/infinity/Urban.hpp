#pragma once
#include "SurfaceLife.hpp"

namespace infinity {
inline constexpr uint32_t urbanRecipeVersion=1;
inline constexpr unsigned urbanPlotsAcross=6, urbanPlotCount=36, urbanRoadSteps=32;
enum class UrbanTier : uint8_t { None, Regional, Metropolis };
struct UrbanGrowth {
    uint64_t seed=0;
    UrbanTier tier=UrbanTier::None;
    double maturity=0, water=0, materials=0, trade=0, energy=0, support=0;
    double radius=0;
    int halfCells=0;
};
// The rare decision uses canonical history and local endowments, never the
// mutable ledger, camera, cache pressure or generation order.
UrbanGrowth urbanGrowth(const Node&,const SurfaceSettlement&);
std::string_view urbanTierName(UrbanTier);
struct UrbanBlock {
    int16_t x=0,z=0;
    BuildingUse use=BuildingUse::Housing;
    float density=0,ground=0,tallest=0;
    uint64_t occupied=0;
    std::array<float,urbanPlotCount> floors{},foundations{};
    // Two shared street edges, sampled on both sides against the actual terrain.
    std::array<std::array<float,(urbanRoadSteps+1)*2>,2> roads{};
    uint32_t population=0,jobs=0;
};
struct UrbanRegion {
    UrbanGrowth growth;
    double planetRadius=1,cellWidth=1,maxElevation=0;
    uint64_t population=0;
    unsigned buildings=0,jobs=0,industry=0,agriculture=0,utilities=0,civic=0;
    std::vector<UrbanBlock> blocks;
    std::vector<int16_t> lookup;
    std::vector<SurfaceRoad> accessRoads;
    size_t residentBytes() const;
    const UrbanBlock* block(int x,int z) const;
};
std::shared_ptr<const UrbanRegion> generateUrbanRegion(const Universe&,const Node&,const SurfaceSettlement&,
    const UrbanGrowth&,std::span<const SurfaceSettlement> neighbours,const std::atomic<bool>* cancel=nullptr);
SurfaceBuilding urbanBuilding(const SurfaceSettlement&,const UrbanRegion&,const UrbanBlock&,unsigned plot);
glm::dvec3 urbanPoint(const SurfaceSettlement&,const UrbanRegion&,glm::dvec2 local,double height);
glm::dquat urbanOrientation(glm::dvec3 forward,glm::dvec3 up);
glm::dvec2 urbanLocal(const SurfaceSettlement&,const UrbanRegion&,glm::dvec3 point);
uint64_t urbanBlockId(const UrbanRegion&,const UrbanBlock&);
SurfaceRoad urbanRoad(const SurfaceSettlement&,const UrbanRegion&,const UrbanBlock&,unsigned edge,unsigned segment);
SurfaceResident urbanResident(const SurfaceSettlement&,const UrbanRegion&,const UrbanBlock&,unsigned plot,
    unsigned resident,double seconds);
std::optional<double> urbanCollision(const SurfaceSettlement&,const UrbanRegion&,glm::dvec3 from,glm::dvec3 to,double radius);
uint64_t urbanFingerprint(const SurfaceSettlement&);
uint64_t settlementPopulation(const SurfaceSettlement&);
double settlementExtent(const SurfaceSettlement&);
std::string settlementDisplayName(const SurfaceSettlement&);
} // namespace infinity
