#include "infinity/Fractal.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
using namespace infinity::fractal;
static int checks=0;
static void check(bool b,const char* s) {++checks;if(!b)throw std::runtime_error(s);}
// Independent double-precision reference avoids finite-difference cancellation
// in the deeply transformed float noise coordinates.
static double referenceHeight(glm::dvec2 point,World w) {
    auto noise=[&](glm::dvec2 q,int octave){
        auto cell=glm::floor(q),t=q-cell,f=t*t*(glm::dvec2(3)-2.*t);
        int ix=int(cell.x),iz=int(cell.y);uint seed=w.seed+uint(octave)*917u;
        double a=fr_random(uint(ix)*73856093u^uint(iz)*19349663u^seed),b=fr_random(uint(ix+1)*73856093u^uint(iz)*19349663u^seed);
        double c=fr_random(uint(ix)*73856093u^uint(iz+1)*19349663u^seed),d=fr_random(uint(ix+1)*73856093u^uint(iz+1)*19349663u^seed);
        return glm::mix(glm::mix(a,b,f.x),glm::mix(c,d,f.x),f.y)*2-1;
    };
    auto octave=[](glm::dvec2 q,double frequency){return glm::dvec2(double(.8f)*q.x-double(.6f)*q.y,double(.6f)*q.x+double(.8f)*q.y)*frequency+glm::dvec2(double(7.13f),double(3.71f));};
    auto q=point/192.+glm::dvec2(double(3.11f),double(8.71f));double region=.5+.5*noise(q,0),t=std::clamp((region-double(.15f))/double(.70f),0.,1.);
    double gain=double(.25f)+double(.75f)*t*t*(3-2*t);
    q=octave(point/192.,1);double crest=1-std::abs(noise({q.x,q.y*double(.35f)},1));
    double relief=12*crest*crest,amplitude=double(1.65f);q=octave(q,2);
    for(int i=2;i<9;++i){relief+=amplitude*(1-std::abs(noise(q,i)));q=octave(q,2);amplitude*=double(.55f);}
    double band=region*3,level=std::floor(band),r=std::clamp((band-level-double(.15f))/double(.70f),0.,1.);
    return 12*(level+r*r*(3-2*r))+gain*relief;
}
int main() try {
    World world{.9f,42};
    for(int i=-80;i<80;++i) {
        float x=i*3.17f,z=i*9.71f,h=fr_height({x,z},world);
        check(std::isfinite(h),"fractal height is finite across signed coordinates");
        check(h==fr_height({x,z},world),"fractal generation repeats exactly");
        float center=fr_center(z,world.phase),ground=fr_height({center,z},world);
        auto hit=cast(world,{center,ground+2,z},{0,-1,0},4);
        check(bool(hit),"sphere tracing finds the fractal road");
        check(std::abs(hit->point.y-ground)<.01,"collision ray ends on the rendered fractal zero set");
        check(hit->normal.y>.7,"fractal road supports upward-facing contact normals");
        check(fr_environment(hit->point+hit->normal*.1f,world).distance>0,"contact normal points out of solid geometry");
    }
    check(fr_menger({0,0,0},{1,1,1},3)>0,"Menger geometry contains a real central hole");
    check(fr_menger({.98f,.98f,.98f},{1,1,1},3)<0,"recursive geometry retains solid corner regions");
    check(std::abs(fr_menger({0,0,0},{39,24.6f,3},3)-8.2f)<.001f,"anisotropic recursive openings retain their world-space distances");
    int detailed=0;float variation=0;
    for(int i=0;i<200;++i) {
        glm::vec2 p(i*17.91f-1300,i*31.13f-2200);
        float h=fr_height(p,world);variation+=std::abs(h-fr_height(p+glm::vec2(25,0),world));
        check(h>=0&&h<=fr_height_max(),"multifractal retains its analytic height bound");
        check(h==fr_ridged_height(p,world,9),"the course does not reshape the generated fractal");
        if(std::abs(h-fr_ridged_height(p,world,3))>.1f)++detailed;
        auto sample=fr_terrain_sample(p,world);
        check(std::abs(sample.height-h)<.00001f,"analytic-gradient evaluation retains the exact fractal height");
        check(glm::length(sample.gradient)<fr_terrain_slope(),"nine-octave terrain retains its tracing slope bound");
        glm::vec2 nearP(i*.731f+3.171f,i*.359f-5.312f);
        auto g=fr_terrain_sample(nearP,world).gradient,q=fr_octave(nearP/192.f,1);bool smooth=true;
        if(std::abs(fr_noise2({q.x,q.y*.35f},world.seed+917u))<.05f)smooth=false;
        q=fr_octave(q,2);
        for(int octave=2;octave<9;++octave) {
            if(std::abs(fr_noise2(q,world.seed+uint(octave)*917u))<.05f)smooth=false;
            q=glm::vec2(.8f*q.x-.6f*q.y,.6f*q.x+.8f*q.y)*2.f+glm::vec2(7.13f,3.71f);
        }
        if(smooth) {
            glm::dvec2 rp(nearP);double e=1e-5;
            glm::dvec2 finite((referenceHeight(rp+glm::dvec2(e,0),world)-referenceHeight(rp-glm::dvec2(e,0),world))/(2*e),
                (referenceHeight(rp+glm::dvec2(0,e),world)-referenceHeight(rp-glm::dvec2(0,e),world))/(2*e));
            check(glm::length(glm::dvec2(g)-finite)<.005,"analytic derivatives match independent double-precision reference");
        }
        auto cell=glm::floor(p/.75f)*.75f,f=(p-cell)/.75f;
        float cached=glm::mix(glm::mix(fr_height(cell,world),fr_height(cell+glm::vec2(.75f,0),world),f.x),
            glm::mix(fr_height(cell+glm::vec2(0,.75f),world),fr_height(cell+glm::vec2(.75f),world),f.x),f.y);
        check(h<=cached+fr_terrain_slope()*.75f*.707107f+.02f,"height-cache bound cannot skip real fractal geometry");
        auto bounds=fr_terrain_bounds(cell+glm::vec2(.375f),world,.75f);
        check(h<=bounds.x+.0001f,"interval cache contains the analytic terrain height");
        for(int corner=0;corner<4;++corner) {
            auto probe=cell+glm::vec2(corner&1?.75f:0,corner&2?.75f:0);
            check(fr_height(probe,world)<=bounds.x+.0001f,"interval cache contains all world-cell corners");
        }

        auto direction=glm::normalize(glm::vec3(std::sin(float(i)),.2f*std::cos(float(i)*.7f),std::cos(float(i))));
        float cellExit=1000;
        for(int axis=0;axis<2;++axis) {
            float velocity=axis==0?direction.x:direction.z;
            if(std::abs(velocity)>1e-6f)cellExit=std::min(cellExit,std::max(0.f,((velocity>0?cell[axis]+.75f:cell[axis])-p[axis])/velocity));
        }
        float raySlope=direction.y-glm::dot(sample.gradient,glm::vec2(direction.x,direction.z));
        float rayCurvature=bounds.y*(direction.x*direction.x+direction.z*direction.z);
        for(int j=1;j<=8;++j) {
            float t=cellExit*j/8;auto probe=glm::vec3(p.x,h+.1f,p.y)+direction*t;
            float lower=.1f+raySlope*t-.5f*rayCurvature*t*t;
            check(probe.y-fr_height({probe.x,probe.z},world)>=lower-.003f,"cell curvature bounds preserve all terrain before a quadratic step");
        }
        float clearance=.01f+.07f*(i%15),advance=fr_terrain_advance(clearance,sample.gradient,direction);
        glm::vec3 origin(p.x,h+clearance,p.y);
        for(int j=1;j<=8;++j) {
            auto probe=origin+direction*(advance*.9f*j/8);
            check(probe.y-fr_height({probe.x,probe.z},world)>-.002f,"quadratic ray advances remain outside the fractal surface");
        }
        auto point=glm::vec3(p.x,h+2,p.y),offset=glm::vec3(.23f,.12f,-.19f);
        check(std::abs(fr_environment(point,world).distance-fr_environment(point+offset,world).distance)<=glm::length(offset)+.001f,"terrain field obeys the conservative tracing bound");
    }
    check(detailed>190&&variation>50,"additional octaves change terrain geometry at driving scales");
    double lowRough=0,highRough=0,acrossVariation=0,alongVariation=0;int lowCount=0,highCount=0;
    float lowest=1e6f,highest=-1e6f;
    for(int z=-24;z<=24;++z)for(int x=-24;x<=24;++x){
        glm::vec2 p(x*137.f+31.17f,z*149.f+71.31f);float h=fr_raw_height(p,world),region=.5f+.5f*fr_noise2(p/192.f+glm::vec2(3.11f,8.71f),world.seed);
        lowest=std::min(lowest,h);highest=std::max(highest,h);
        double rough=std::abs(fr_raw_height(p+glm::vec2(6,0),world)+fr_raw_height(p-glm::vec2(6,0),world)-2*h);
        if(region<.35f){lowRough+=rough;++lowCount;}if(region>.65f){highRough+=rough;++highCount;}
        auto dune=[&](glm::vec2 q){float r=.5f+.5f*fr_noise2(q/192.f+glm::vec2(3.11f,8.71f),world.seed);return (fr_ridged_height(q,world,2)-fr_ridged_height(q,world,1))/fr_relief_gain(r);};
        acrossVariation+=std::abs(dune(p+glm::vec2(.8f,-.6f)*8.f)-dune(p));alongVariation+=std::abs(dune(p+glm::vec2(.6f,.8f)*8.f)-dune(p));
    }
    check(highest-lowest>40,"broad landforms span substantial elevation instead of uniform low ripples");
    check(lowCount>100&&highCount>100&&highRough/highCount>1.5*lowRough/lowCount,"regional fractal gain produces both smooth basins and rough high country");
    check(acrossVariation>alongVariation*1.6,"dune ridges are elongated in the generated geometry");
    std::cout<<"TERRAIN range="<<lowest<<","<<highest<<" lowRough="<<lowRough/lowCount<<" highRough="<<highRough/highCount<<" duneRatio="<<acrossVariation/alongVariation<<'\n';
    check(!cast(world,{0,fr_height_max()+40,0},{0,-1,0},32),"upper-space bounds cannot become a false collision plane");
    for(int side:{-1,1}) {
        auto post=fr_checkpoint_post(1,side,world);
        auto hit=cast(world,post+glm::vec3(0,1,-3),{0,0,1},5);
        check(bool(hit)&&hit->material==15,"functional checkpoint markers have shared collision geometry");
    }
    glm::vec3 eye(-40,fr_height({-40,-60},world)+14,-60),target(30,fr_height({30,60},world),60);
    auto forward=glm::normalize(target-eye),right=glm::normalize(glm::cross(glm::vec3(0,1,0),forward)),up=glm::cross(forward,right);
    auto direction=[&](float x,float y) {return glm::normalize(forward+right*x+up*y);};
    int coveredHits=0;
    for(int y=0;y<5;++y)for(int x=0;x<8;++x) {
        auto ray=direction(((x+.5f)/8*2-1)*.8f,((y+.5f)/5*2-1)*.5f);
        float t=.06f,slope=.5f/5*1.6f;
        for(int step=0;step<80&&t<180;++step) {
            float clearance=fr_environment(eye+ray*t,world).distance-t*slope;
            if(clearance<.004f)break;t+=std::max(.001f,clearance*.82f);
        }
        float start=std::max(.06f,t-.04f);
        for(int fy=0;fy<4;++fy)for(int fx=0;fx<4;++fx) {
            auto fine=direction(((x+(fx+.5f)/4)/8*2-1)*.8f,((y+(fy+.5f)/4)/5*2-1)*.5f);
            if(auto contact=cast(world,eye,fine,180)) {
                ++coveredHits;check(start<=contact->distance+.002f,"coarse empty-space cones preserve fine-ray terrain surfaces");
            }
        }
    }
    check(coveredHits>100,"cone validation exercises terrain surface rays");
    Vehicle car{};car.tyres={1,1,1,1};car.compression={.08f,.08f,.08f,.08f};car.attitude.z=car.attitude.w=1;
    auto samples=vehicleSamples(car);check(samples.size()>12,"box collision samples cover faces, edges and corners");
    for(auto p:samples) check(std::abs(fr_vehicle(glm::vec3(p),car,true,false).distance)<.002,"collision samples lie on the rendered car field");
    check(fr_vehicle_local({0,.8f,0},car,true,true).distance>.2f,"simplified cars have no driver, cage or cockpit geometry");
    for(int wheel=0;wheel<4;++wheel) {
        auto center=fr_wheel_center(car,wheel);
        check(fr_vehicle_local(center,car,true,true).distance<0,"each of the four wheel cylinders is solid");
        check(fr_vehicle_local(center,car,true,false).distance>.1f,"wheels are separate from the single box body");
        auto rim=center+glm::vec3(0,-fr_wheel_radius(car,wheel),0);
        check(std::abs(fr_vehicle_local(rim,car,true,true).distance)<.001f,"wheel geometry retains the physical tyre radius");
    }
    Vehicle rayCar=car;rayCar.placement={4,7,-3,.43f};rayCar.attitude.x=.08f;rayCar.attitude.y=-.12f;
    rayCar.steering={.16f,.20f,0,0};rayCar.rotation={.6f,.7f,.8f,.9f};rayCar.systems.y=.1f;
    int rayHits=0;
    for(int i=0;i<160;++i) {
        glm::vec3 origin=glm::vec3(rayCar.placement)+glm::vec3(std::sin(i*.83f)*5,1+std::cos(i*.41f)*3,std::cos(i*.83f)*5);
        glm::vec3 localTarget=fr_block_center(rayCar)+glm::vec3(std::sin(i*.31f)*.35f,0,std::cos(i*.27f)*.8f);
        glm::vec3 target=glm::vec3(rayCar.placement)+fr_rotate_y(fr_rotate_x(fr_rotate_z(localTarget,rayCar.attitude.y),rayCar.attitude.x),rayCar.placement.w);
        auto ray=glm::normalize(target-origin);auto fast=fr_vehicle_ray(origin,ray,rayCar,true,true,12);
        auto reference=castVehicle(rayCar,origin,ray,12);
        if(fast.distance<12) {
            ++rayHits;float residual=fr_vehicle(origin+ray*fast.distance,rayCar,true,true).distance;
            if(std::abs(residual)>.003)std::cerr<<"RAY i="<<i<<" residual="<<residual<<" distance="<<fast.distance<<" material="<<fast.material<<" local="<<fr_local(origin+ray*fast.distance,rayCar).x<<","<<fr_local(origin+ray*fast.distance,rayCar).y<<","<<fr_local(origin+ray*fast.distance,rayCar).z<<"\n";
            check(std::abs(residual)<.003,"component-level ray hits lie on the original vehicle field");
        }
        // The reference tracer has a loose hit tolerance; confirm entry into
        // solid geometry rather than treating a grazing near-miss as a hit.
        if(reference&&fr_vehicle(origin+ray*(reference->distance+.01f),rayCar,true,true).distance<-.0001f) {
            if(fast.distance>=12) {auto point=fr_local(reference->point,rayCar);std::cerr<<"RAY_MISS i="<<i<<" t="<<reference->distance<<" p="<<point.x<<","<<point.y<<","<<point.z<<"\n";}
            check(fast.distance<12,"component-level tracing preserves reference vehicle intersections");
            if(std::abs(fast.distance-reference->distance)>.04)std::cerr<<"RAY_MATCH i="<<i<<" fast="<<fast.distance<<" reference="<<reference->distance<<"\n";
            check(std::abs(fast.distance-reference->distance)<.04,"optimized vehicle tracing retains the first implicit surface");
        }
    }
    check(rayHits>100,"optimized vehicle validation exercises varied body and tyre surfaces");
    auto before=fr_vehicle_local({.30f,.11f,-1.1f},car,true,true).distance;
    car.dents={1,1,1,1};car.tyres.x=.1f;
    check(std::isfinite(fr_vehicle_local({.30f,.11f,-1.1f},car,true,true).distance),"damaged recursive vehicle remains finite");
    (void)before;
    std::cout<<"PASS "<<checks<<" shared fractal geometry/contact checks\n";return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
