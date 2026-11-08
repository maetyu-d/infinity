#include "Tactical.hpp"
#include <algorithm>
#include <cmath>
namespace desert {
using V=glm::dvec3;using P=glm::dvec2;namespace fr=infinity::fractal;
GunnerMap makeGunnerMap(const Car& car,double aspect){
    GunnerMap map;double cw=std::max(1280.,900*aspect),ch=cw/aspect;
    double scale=std::min(map.rect.z*cw,map.rect.w*ch)/60.;
    map.halfExtent={map.rect.z*cw/(2*scale),map.rect.w*ch/(2*scale)};
    map.center=P(car.position.x,car.position.z);return map;
}
P mapForward(const Car& car){auto f=car.orientation()*V(0,0,1);P p(f.x,f.z);return glm::length(p)>.01?glm::normalize(p):P(std::sin(car.yaw),std::cos(car.yaw));}
P mapCarPoint(const Car& car,V point){auto local=glm::conjugate(car.orientation())*(point-car.position);auto f=mapForward(car);P r(f.y,-f.x);return P(car.position.x,car.position.z)+r*local.x*2.1+f*local.z*2.1;}
GunnerTarget pickGunnerTarget(const Race& race,const GunnerMap& map,P cursor){
    P world=map.world(cursor);GunnerTarget target{{world.x,race.course.height(world.x,world.y),world.y},-1};
    double nearest=1e20;
    for(int i=0;i<3;++i){const auto& car=race.rivals[i];P f=mapForward(car),r(f.y,-f.x),d=world-P(car.position.x,car.position.z);P local(glm::dot(d,r),glm::dot(d,f));
        if(std::abs(local.x)>2.0||std::abs(local.y)>3.5||glm::length(d)>=nearest)continue;nearest=glm::length(d);
        auto v=car.fieldVehicle();auto b=fr::fr_block_bounds(v),c=fr::fr_block_center(v);
        V point=car.position+car.orientation()*V(std::clamp(local.x/2.1,double(c.x)-double(b.x)*.94,double(c.x)+double(b.x)*.94),c.y+b.y,std::clamp(local.y/2.1,double(c.z)-double(b.z)*.94,double(c.z)+double(b.z)*.94));
        target.car=i+1;
        if(race.player.turret.weapon!=Weapon::TowHarpoon)point.y=race.course.height(point.x,point.z);
        target.point=point;
    }return target;
}
GunnerAim evaluateGunnerAim(const Race& race,GunnerTarget target){
    const auto& car=race.player;const auto& gun=car.turret;GunnerAim out;out.target=target;out.origin=V(fr::fr_turret_muzzle(armedVehicle(car)));out.range=gun.weapon==Weapon::TowHarpoon?58:80;
    auto delta=target.point-out.origin;double distance=glm::length(delta);auto desired=distance>.001?delta/distance:gunDirection(car);
    out.actual=weaponHit(race,0);
    auto cameraDelta=target.point-gunCamera(car);out.aligned=glm::length(cameraDelta)>.001&&glm::dot(glm::normalize(cameraDelta),gunDirection(car))>.9994;
    if(car.damage.wrecked()||race.recoveryDelay>0){out.status=AimStatus::Disabled;return out;}
    if(distance>out.range){out.status=AimStatus::OutOfRange;return out;}
    if(gun.weapon!=Weapon::TowHarpoon&&glm::length(P(target.point.x-car.position.x,target.point.z-car.position.z))<3){out.status=AimStatus::TooClose;return out;}
    auto local=glm::conjugate(car.orientation())*(target.point-gunCamera(car));double pitch=std::atan2(local.y,std::hypot(local.x,local.z));
    if(pitch<-.405||pitch>.705){out.status=AimStatus::Elevation;return out;}
    auto hit=traceBullet(race,0,out.origin,desired,out.range);
    auto matches=[&](const BulletHit& candidate,double tolerance){
        if(gun.weapon==Weapon::TowHarpoon&&target.car>0)return candidate.car==target.car;
        bool surface=candidate.car>=0||candidate.material==0||(gun.weapon==Weapon::TowHarpoon&&candidate.material==15);
        return (target.car>0&&candidate.car==target.car)||(surface&&glm::length(P(candidate.point.x-target.point.x,candidate.point.z-target.point.z))<tolerance);
    };
    out.clear=matches(hit,1.0);
    if(!out.clear){out.status=AimStatus::Blocked;return out;}
    if(gun.weapon==Weapon::DuneJack)for(const auto& feature:race.course.jacks)if(glm::length(P(feature.placement.x,feature.placement.y)-P(hit.point.x,hit.point.z))<7.2){out.status=AimStatus::NoRoom;return out;}
    if(!out.aligned){out.status=AimStatus::Slewing;return out;}
    bool actualMatches=matches(out.actual,1.2);
    if(!actualMatches){out.status=AimStatus::Slewing;return out;}
    out.status=gun.cooldown>0?AimStatus::Reloading:AimStatus::Ready;return out;
}
const char* aimStatusText(AimStatus status){switch(status){
    case AimStatus::Ready:return "READY";case AimStatus::Slewing:return "TURRET TURNING";case AimStatus::Blocked:return "SHOT OBSTRUCTED";
    case AimStatus::OutOfRange:return "OUT OF RANGE";case AimStatus::TooClose:return "TOO CLOSE TO BUGGY";case AimStatus::Elevation:return "OUTSIDE TURRET ELEVATION";
    case AimStatus::NoRoom:return "JACK NEEDS CLEAR GROUND";case AimStatus::Reloading:return "RELOADING";default:return "LAUNCHER DISABLED";}}
}
