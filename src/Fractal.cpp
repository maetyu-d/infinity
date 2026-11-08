#include "infinity/Fractal.hpp"
#include <cmath>
namespace infinity::fractal {
glm::vec3 gradient(World world,glm::vec3 p) {
    if(p.y>fr_height_max()+7)return {0,.8f,0};
    if(fr_environment(p,world).material==0) {
        auto terrain=fr_terrain_sample({p.x,p.z},world);
        float scale=world.tracks&&fr_cpu_rut(world.tracks,{p.x,p.z}).height<0?fr_rut_scale():fr_terrain_scale();
        return glm::vec3(-terrain.gradient.x,1,-terrain.gradient.y)*scale;
    }
    constexpr float e=.003f;glm::vec3 n;
    for(int i=0;i<3;++i) {auto a=p,b=p;a[i]+=e;b[i]-=e;n[i]=(fr_environment(a,world).distance-fr_environment(b,world).distance)/(2*e);}
    return n;
}
glm::vec3 normal(World world,glm::vec3 p) {auto n=gradient(world,p);return glm::length(n)>1e-5f?glm::normalize(n):glm::vec3(0,1,0);}
float signedDistance(World world,glm::vec3 p) {
    float d=fr_environment(p,world).distance;if(d>.08f)return d;
    return d/std::max(.08f,glm::length(gradient(world,p)));
}
std::optional<Contact> cast(World world,glm::vec3 origin,glm::vec3 direction,float maximum,float tolerance) {
    float t=0;direction=glm::normalize(direction);
    for(int i=0;i<256 && t<=maximum;++i) {
        auto p=origin+direction*t;auto field=fr_environment(p,world);
        if(field.distance<tolerance) return Contact{p,normal(world,p),t,field.material};
        t+=std::max(tolerance*.25f,field.distance*.85f);
    }
    return {};
}
std::optional<Contact> castVehicle(const Vehicle& car,glm::vec3 origin,glm::vec3 direction,float maximum,bool wheels) {
    float t=0;direction=glm::normalize(direction);
    for(int i=0;i<96&&t<maximum;++i) {
        auto p=origin+direction*t;auto field=fr_vehicle(p,car,true,wheels);
        if(field.distance<.0008f) {
            glm::vec3 n;constexpr float e=.001f;
            for(int j=0;j<3;++j) {auto a=p,b=p;a[j]+=e;b[j]-=e;n[j]=fr_vehicle(a,car,true,wheels).distance-fr_vehicle(b,car,true,wheels).distance;}
            return Contact{p,glm::normalize(n),t,field.material};
        }
        t+=std::max(.0002f,field.distance*.85f);
    }
    return {};
}
std::vector<glm::dvec3> vehicleSamples(const Vehicle& input) {
    // The body itself is a box, so face/edge/corner samples are exact points
    // on its collision and render surface rather than a separate proxy hull.
    auto center=fr_block_center(input),bounds=fr_block_bounds(input);
    std::vector<glm::dvec3> out;
    for(int x:{-1,0,1})for(int y:{-1,0,1})for(int z:{-2,-1,0,1,2}) {
        if(x==0&&y==0&&std::abs(z)<2)continue;
        out.emplace_back(center+bounds*glm::vec3(x,y,z*.5f));
    }
    return out;
}
}
