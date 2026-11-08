#pragma once
#include "Combat.hpp"
namespace desert {
struct GunnerMap {
    glm::dvec2 center{},halfExtent{};
    glm::dvec4 rect{.5,0,.5,1};
    glm::dvec2 world(glm::dvec2 cursor)const{return center+glm::dvec2((cursor.x-.5)*2*halfExtent.x,(.5-cursor.y)*2*halfExtent.y);}
    glm::dvec2 project(glm::dvec2 p)const{auto d=p-center;return {.5+d.x/(2*halfExtent.x),.5-d.y/(2*halfExtent.y)};}
    bool contains(glm::dvec2 uv,double margin=0)const{return uv.x>=margin&&uv.x<=1-margin&&uv.y>=margin&&uv.y<=1-margin;}
};
GunnerMap makeGunnerMap(const Car&,double aspect);
glm::dvec2 mapForward(const Car&);
glm::dvec2 mapCarPoint(const Car&,glm::dvec3 worldPoint);
struct GunnerTarget {glm::dvec3 point{};int car=-1;};
enum class AimStatus {Ready,Slewing,Blocked,OutOfRange,TooClose,Elevation,NoRoom,Reloading,Disabled};
struct GunnerAim {
    GunnerTarget target;
    BulletHit actual;
    glm::dvec3 origin{};
    double range=58;
    AimStatus status=AimStatus::Slewing;
    bool clear=false,aligned=false;
    bool canFire()const{return status==AimStatus::Ready;}
};
GunnerTarget pickGunnerTarget(const Race&,const GunnerMap&,glm::dvec2 cursor);
GunnerAim evaluateGunnerAim(const Race&,GunnerTarget);
const char* aimStatusText(AimStatus);
}
