#include "TerrainCache.hpp"
#include <array>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
int main() try {
    constexpr int n=32;desert::TerrainCache planner(n);
    struct Cell{int x=0,z=0;uint32_t seed=0;};std::array<Cell,n*n> cells{};
    uint32_t random=2791,seed=1;int x=0,z=0,checks=0;
    auto roll=[&](){random=random*1664525u+1013904223u;return random;};
    auto wrap=[](int v){return (v%n+n)%n;};
    for(int frame=0;frame<6000;++frame){
        if(frame%997==0)++seed;
        if(frame%173==0){x+=int(roll()%137)-68;z+=int(roll()%137)-68;}
        else{x+=int(roll()%5)-2;z+=int(roll()%5)-2;}
        int ox=int(std::floor(double(x)/8))*8,oz=int(std::floor(double(z)/8))*8;
        auto plan=planner.advance(ox,oz,seed);int work=0;
        for(auto r:plan.updates){if(r[0]<0||r[1]<0||r[0]+r[2]>n||r[1]+r[3]>n)throw std::runtime_error("update outside texture");
            work+=r[2]*r[3];
            for(int pz=r[1];pz<r[1]+r[3];++pz)for(int px=r[0];px<r[0]+r[2];++px)
                cells[pz*n+px]={ox+wrap(px-ox),oz+wrap(pz-oz),seed};
        }
        if(!plan.reset&&work>2*n)throw std::runtime_error("cache budget exceeded");
        for(int gz=plan.valid[1];gz<plan.valid[3];++gz)for(int gx=plan.valid[0];gx<plan.valid[2];++gx){
            auto c=cells[wrap(gz)*n+wrap(gx)];++checks;
            if(c.x!=gx||c.z!=gz||c.seed!=seed)throw std::runtime_error("stale cell exposed as valid");
        }
        // Every coarse 8x8 bound trusted by the marcher must also be complete.
        for(int v:plan.valid)if(v%8)throw std::runtime_error("coarse bound exposed partially");
    }
    std::cout<<"PASS "<<checks<<" cache-cell checks across motion, reversals, wrapping, teleports and seed changes\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
