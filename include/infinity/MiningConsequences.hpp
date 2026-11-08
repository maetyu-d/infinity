#pragma once
#include "Exploration.hpp"
#include "ProbePresentation.hpp"
namespace infinity {
struct MiningFaceWear {
    CavernFind face;
    double remaining = 1, worked = 0;
};
struct CavernWear {
    std::array<MiningFaceWear,cavernFindCount> faces;
    uint64_t key = 0, minedKg = 0;
    glm::vec3 shade(glm::dvec3,glm::vec3 original) const;
};
CavernWear cavernWear(const Universe&,const Borehole&,const ExplorationState&);
Mesh cavernTailings(const Borehole&,const CavernWear&);
ProbeMotes cavernMiningMotes(const Borehole&,const CavernFind&,double seconds);
ProbeMotes miningImpactMotes(glm::dvec3 origin,glm::dvec3 inward,glm::dvec3 down,uint64_t,Mineral,double seconds);
glm::vec3 prospectWearTint(const Universe&,const Node&,const ExplorationState&);
} // namespace infinity
