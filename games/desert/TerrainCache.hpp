#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <vector>

namespace desert {
// Coordinates are global terrain cells; rectangles address the toroidal texture.
// Only the intersection of fully valid windows may be sampled while borders fill.
class TerrainCache {
public:
    using Rectangle=std::array<int,4>;
    struct Plan {Rectangle valid;std::vector<Rectangle> updates;bool reset;};
    explicit TerrainCache(int size=2048):size(size){}
    Plan advance(int x,int z,uint32_t nextSeed) {
        Plan out{};
        out.reset=!ready||seed!=nextSeed||std::abs(x-originX)>=size||std::abs(z-originZ)>=size;
        if(!out.reset){valid={std::max(valid[0],x),std::max(valid[1],z),std::min(valid[2],x+size),std::min(valid[3],z+size)};
            out.reset=valid[0]>=valid[2]||valid[1]>=valid[3];}
        auto strip=[&](int start,int length,bool vertical){
            int offset=(start%size+size)%size,first=std::min(length,size-offset);
            auto enqueue=[&](int begin,int count){for(int i=0;i<count;i+=8){int width=std::min(8,count-i);
                pending.push_back(vertical?Rectangle{begin+i,0,width,size}:Rectangle{0,begin+i,size,width});}};
            enqueue(offset,first);if(first<length)enqueue(0,length-first);
        };
        if(out.reset){pending.clear();out.updates.push_back({0,0,size,size});valid={x,z,x+size,z+size};}
        else {
            if(x!=originX)strip(x>originX?originX+size:x,std::abs(x-originX),true);
            if(z!=originZ)strip(z>originZ?originZ+size:z,std::abs(z-originZ),false);
            int remaining=0;for(const auto& r:pending)remaining+=r[2]*r[3];
            int budget=remaining>12*size?2:1,edge=std::max(1,size/8);
            while(!pending.empty()&&budget){auto& r=pending.front();bool vertical=r[2]<=8;
                int amount=std::min(vertical?r[3]:r[2],edge);
                out.updates.push_back(vertical?Rectangle{r[0],r[1],r[2],amount}:Rectangle{r[0],r[1],amount,r[3]});
                if(vertical){r[1]+=amount;r[3]-=amount;}else{r[0]+=amount;r[2]-=amount;}
                --budget;if(r[2]==0||r[3]==0)pending.pop_front();
            }
            if(pending.empty())valid={x,z,x+size,z+size};
        }
        originX=x;originZ=z;seed=nextSeed;ready=true;out.valid=valid;return out;
    }
private:
    int size,originX=0,originZ=0;uint32_t seed=0;bool ready=false;
    Rectangle valid{};std::deque<Rectangle> pending;
};
}
