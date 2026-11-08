#include "infinity/SurfaceOperations.hpp"
#include <algorithm>

namespace infinity {
namespace {
void box(Mesh& m, glm::dvec3 centre, glm::dvec3 size, glm::vec3 colour) {
    const auto a=centre-size*.5,b=centre+size*.5;
    const std::array<glm::dvec3,8> p{{{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},
                                     {a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}}};
    for (const auto face : {std::array{0,3,2,1},std::array{4,5,6,7},std::array{0,4,7,3},
                            std::array{1,2,6,5},std::array{3,7,6,2},std::array{0,1,5,4}}) {
        const auto first=uint32_t(m.vertices.size());
        for (auto i:face)m.vertices.push_back({float(p[i].x),float(p[i].y),float(p[i].z),rgba(colour)});
        m.indices.insert(m.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    }
}
}
Mesh surfaceMachineMesh(SurfaceMachine kind, unsigned detail) {
    Mesh m;detail=std::min(2u,detail);
    const glm::vec3 metal(.65,.71,.74),dark(.08,.11,.14),glass(.12,.55,.67),cargo(.46,.56,.60);
    if (kind==SurfaceMachine::CargoRover) {
        box(m,{0,.65,0},{2.2,.65,4.8},metal);box(m,{0,1.35,-1.5},{1.9,1.4,1.5},metal);
        box(m,{0,1.58,-2.27},{1.5,.65,.04},glass);box(m,{0,1.3,.7},{1.8,.85,2.4},cargo);
        for(int side:{-1,1})for(double z:{-1.5,1.5})box(m,{side*1.13,.44,z},{.36,.8,.84},dark);
        if(detail){box(m,{0,2.1,-1.5},{.3,.15,.3},{1,.58,.08});
            for(int side:{-1,1})box(m,{side*.7,.8,-2.43},{.28,.18,.05},{.8,1.1,1.2});}
    } else if(kind==SurfaceMachine::CraneBase) {
        box(m,{0,.20,0},{1.1,.4,1.1},dark);
        box(m,{0,1.,0},{.28,1.6,.28},metal);
        if(detail)for(double y:{.6,1.1,1.6})box(m,{0,y,0},{.50,.07,.50},metal);
    } else if(kind==SurfaceMachine::CraneBoom) {
        box(m,{.55,0,0},{2.8,.18,.25},metal);box(m,{-1.,-.06,0},{.65,.42,.55},dark);
        box(m,{1.8,-.45,0},{.035,.8,.035},dark);box(m,{1.8,-.89,0},{.18,.10,.18},metal);
    } else if(kind==SurfaceMachine::Drill) {
        box(m,{0,.3,0},{1.5,.6,1.5},dark);box(m,{0,1.1,0},{.25,1.6,.25},metal);
        box(m,{0,.85,0},{.8,.42,.8},cargo);
        if(detail)for(double y:{.5,.9,1.3})box(m,{0,y,0},{.52,.08,.52},metal);
    }
    m.compact();return m;
}
} // namespace infinity
