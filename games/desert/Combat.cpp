#include "Combat.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
namespace desert {
namespace fr=infinity::fractal;using V=glm::dvec3;using P=glm::dvec2;
static std::array<Car*,4> cars(Race& r){return {&r.player,&r.rivals[0],&r.rivals[1],&r.rivals[2]};}
static std::array<const Car*,4> cars(const Race& r){return {&r.player,&r.rivals[0],&r.rivals[1],&r.rivals[2]};}
const char* weaponName(Weapon w){switch(w){case Weapon::RutStitcher:return "Rut stitcher";case Weapon::DuneJack:return "Dune jack";default:return "Tow harpoon";}}
fr::Vehicle armedVehicle(const Car& car){auto v=car.fieldVehicle();v.systems={car.turret.yaw,car.turret.pitch,car.turret.recoil,car.turret.flash};return v;}
V gunDirection(const Car& car){auto v=armedVehicle(car);return glm::normalize(V(fr::fr_world_vector(fr::fr_turret_direction(v),v)));}
V gunCamera(const Car& car){return car.position+car.orientation()*V(fr::fr_turret_pivot(armedVehicle(car)))+car.orientation()*V(0,.12,0)-gunDirection(car)*.25;}
V towMount(const Car& car){return car.position+car.orientation()*(V(fr::fr_turret_pivot(armedVehicle(car)))+V(0,.05,0));}
void aimTurret(Car& car,V target,double dt){
    auto delta=glm::conjugate(car.orientation())*(target-gunCamera(car));if(glm::length(delta)<.001)return;
    double yaw=std::atan2(delta.x,delta.z),pitch=std::atan2(delta.y,std::hypot(delta.x,delta.z));
    car.turret.yaw=std::remainder(car.turret.yaw+std::clamp(std::remainder(yaw-car.turret.yaw,2*std::numbers::pi),-2.4*dt,2.4*dt),2*std::numbers::pi);
    car.turret.pitch=std::clamp(car.turret.pitch+std::clamp(pitch-car.turret.pitch,-1.6*dt,1.6*dt),-.4,.70);
}
void Race::aimGunnerAt(V target,double dt){aimTurret(player,target,dt);}
static BulletHit traceGround(const Race& race,V origin,V direction,double range){
    auto world=race.course.field();double t=.001;
    for(int step=0;step<4096&&t<range;++step){auto p=origin+direction*t;auto field=fr::fr_environment(glm::vec3(p),world);
        if(field.distance<.0008f)return {p,-1,field.material,t};t+=std::max(.0002,double(field.distance)*.90);}
    if(t<range)return {origin+direction*t,-1,16,t};return {origin+direction*range,-1,-1,range};
}
BulletHit traceBullet(const Race& race,int owner,V origin,V direction,double range){
    if(glm::length(direction)<1e-9||range<=0)return {origin,-1,-1,0};direction=glm::normalize(direction);
    BulletHit hit{origin+direction*range,-1,-1,range};auto list=cars(race);
    for(int i=0;i<4;++i)if(i!=owner){auto candidate=fr::fr_visual_vehicle_ray(glm::vec3(origin),glm::vec3(direction),armedVehicle(*list[i]),true,true,float(hit.distance));
        if(candidate.distance<hit.distance)hit={origin+direction*double(candidate.distance),i,candidate.material,candidate.distance};}
    auto ground=traceGround(race,origin,direction,hit.distance);return ground.material>=0?ground:hit;
}
BulletHit weaponHit(const Race& race,int owner){auto car=cars(race)[owner];double range=car->turret.weapon==Weapon::TowHarpoon?58:80;
    auto aim=traceBullet(race,owner,gunCamera(*car),gunDirection(*car),range);auto muzzle=V(fr::fr_turret_muzzle(armedVehicle(*car)));auto direction=aim.point-muzzle;
    return traceBullet(race,owner,muzzle,glm::length(direction)<.01?gunDirection(*car):glm::normalize(direction),range);
}
CableView cableView(const Race& race,int owner){
    auto list=cars(race);const auto& car=*list[owner];const auto& c=car.turret.cable;CableView v;v.start=towMount(car);
    if(!c.active())return v;v.end=c.target==-2?c.groundAnchor:list[c.target]->position+list[c.target]->orientation()*c.localAnchor;
    double slack=std::max(0.,c.length-glm::length(v.end-v.start));auto mid=(v.start+v.end)*.5;
    v.sag=std::min({.6,slack*.18,std::max(0.,mid.y-race.course.height(mid.x,mid.z)-.12)});return v;
}
static V velocityAt(const Car& car,V point){return V(car.velocity.x,car.verticalSpeed,car.velocity.y)+glm::cross(car.orientation()*V(car.pitchRate,car.yawRate,car.rollRate),point-car.position);}
static double inverseMass(const Car& car,V point,V direction){using namespace infinity::buggy;V arm=glm::conjugate(car.orientation())*glm::cross(point-car.position,direction);return 1/mass+arm.x*arm.x/pitchInertia+arm.y*arm.y/yawInertia+arm.z*arm.z/rollInertia;}
void solveTowCable(Race& race,int owner,bool reel,double dt){
    if(dt<=0)return;auto list=cars(race);auto& car=*list[owner];auto& c=car.turret.cable;if(!c.active())return;
    c.age+=dt;c.obstructionClock-=dt;
    bool recovering=owner==0?race.recoveryDelay>0:race.rivalDelay[owner-1]>0;
    if(car.damage.wrecked()||recovering||c.age>10||(c.target>=0&&(c.target==owner||list[c.target]->damage.wrecked()||(c.target==0?race.recoveryDelay>0:race.rivalDelay[c.target-1]>0)))){c={};return;}
    auto v=cableView(race,owner);auto delta=v.end-v.start;double length=glm::length(delta);
    if(length<.05||length>60){c={};return;}auto n=delta/length;
    if(c.obstructionClock<=0){c.obstructionClock=.18;auto hit=traceGround(race,v.start,n,std::max(0.,length-.25));bool blocked=hit.material>=0;
        for(int j=0;j<4&&!blocked;++j)if(j!=owner&&j!=c.target){auto object=fr::fr_visual_vehicle_ray(glm::vec3(v.start),glm::vec3(n),armedVehicle(*list[j]),true,true,float(length-.25));blocked=object.distance<length-.251;}
        if(blocked){c={};car.turret.notice="Cable snagged";car.turret.pickupTime=1.4;return;}}
    c.length=std::clamp(c.length+(reel?-5.:5.)*dt,2.5,60.);double stretch=length-c.length;c.force=0;
    if(stretch<=0){c.overload=0;return;}
    auto* victim=c.target>=0?list[c.target]:nullptr;
    double speed=glm::dot((victim?velocityAt(*victim,v.end):V(0))-velocityAt(car,v.start),n);
    double inv=inverseMass(car,v.start,n)+(victim?inverseMass(*victim,v.end,n):0);
    // Implicit spring/damper solve, with equal and opposite point impulses.
    constexpr double spring=2400,damping=1700;
    double impulse=std::max(0.,(spring*stretch*dt+damping*speed*dt)/(1+(damping*dt+spring*dt*dt)*inv));
    double requested=impulse/dt;c.overload=requested>14500?c.overload+dt:std::max(0.,c.overload-dt*2);
    if(c.overload>.16){c={};car.turret.notice="Cable snapped";car.turret.pickupTime=1.5;return;}
    impulse=std::min(impulse,9000*dt);c.force=impulse/dt;car.contactImpulse3D(v.start,n,impulse);if(victim)victim->contactImpulse3D(v.end,-n,impulse);
}
Pickup coursePickup(const Course& course,int station){
    double s=45.+(station-1)*110.,lane=station<=2?0:station%3==0?-3.5:station%3==1?3.5:0;
    return {station,station%2?Weapon::RutStitcher:Weapon::DuneJack,course.point(s,lane)+V(0,.90,0)};
}
void Race::resetWeapons(){pickupViews.clear();pickupRespawn.clear();pickupClock=weaponTime=0;pickupsCollected=rutsFired=jacksRaised=cablesLatched=0;course.jacks.reserve(32);if(combatEnabled)updatePickups(.1);}
void Race::detachCables(int owner){for(auto* car:cars(*this))if(car==cars(*this)[owner]||car->turret.cable.target==owner)car->turret.cable={};}
void Race::selectWeapon(int owner,int direction){auto& gun=cars(*this)[owner]->turret;int current=int(gun.weapon);for(int i=0;i<3;++i){current=(current+(direction>=0?1:2))%3;if(current==0||gun.ammo[current]>0){gun.weapon=Weapon(current);gun.cable={};gun.cooldown=std::max(.18,gun.cooldown);break;}}}
void Race::updatePickups(double dt){
    pickupClock-=dt;if(pickupClock>0)return;pickupClock=.10;pickupViews.clear();std::set<int> ids;
    if(format()==RaceFormat::Circuit){for(int i=1;45+(i-1)*110<course.lapLength-20;++i)ids.insert(i);}
    else for(auto* car:cars(*this)){int near=std::max(1,int(std::floor((car->position.z-45)/110))+1);for(int i=std::max(1,near-2);i<=near+3;++i)if(format()!=RaceFormat::Stage||45+(i-1)*110<finishZ-20)ids.insert(i);}
    for(int id:ids){auto it=pickupRespawn.find(id);if(it!=pickupRespawn.end()&&it->second>weaponTime)continue;auto pickup=coursePickup(course,id);bool taken=false;
        for(auto* car:cars(*this)){int kind=int(pickup.weapon),capacity=kind==1?6:4;if(car->damage.wrecked()||car->turret.ammo[kind]>=capacity)continue;
            auto d=car->position-pickup.position;if(d.x*d.x+d.z*d.z>1.8*1.8||std::abs(d.y)>2.2)continue;
            auto& gun=car->turret;gun.ammo[kind]=std::min(capacity,gun.ammo[kind]+(kind==1?3:2));gun.weapon=pickup.weapon;gun.cable={};gun.cooldown=std::max(.2,gun.cooldown);gun.notice=std::string(weaponName(pickup.weapon))+" collected";gun.pickupTime=2.5;
            pickupRespawn[id]=weaponTime+18;++pickupsCollected;taken=true;break;
        }
        if(!taken)pickupViews.push_back(pickup);
    }
}
bool Race::addJack(P point,P forward){
    if(!std::isfinite(point.x+point.y+forward.x+forward.y)||glm::length(forward)<.01)return false;
    for(auto& f:course.jacks)if(glm::length(point-P(f.placement.x,f.placement.y))<7.2)return false;
    forward=glm::normalize(forward);if(course.jacks.size()==32)course.jacks.erase(course.jacks.begin());
    course.jacks.push_back({{point.x,point.y,forward.x,forward.y},{1.15,2.2,3.5,1.15}});++jacksRaised;return true;
}
P rutPathPoint(P center,P forward,int strand,double u){forward=glm::normalize(forward);P right(forward.y,-forward.x);return center+forward*(-4.5+9*u)+right*(strand*.9+.28*std::sin(u*9.));}
P jackFootprintPoint(P center,P forward,double angle,double radius){forward=glm::normalize(forward);P right(forward.y,-forward.x);return center+radius*(right*(2.2*std::cos(angle))+forward*(3.5*std::sin(angle)));}
void Race::stitchRuts(P point,P forward){
    if(!std::isfinite(point.x+point.y+forward.x+forward.y)||glm::length(forward)<.01)return;forward=glm::normalize(forward);P right(forward.y,-forward.x);
    for(int strand=-1;strand<=1;++strand){auto previous=rutPathPoint(point,forward,strand,0);
        for(int step=1;step<=24;++step){double t=step/24.;auto next=rutPathPoint(point,forward,strand,t);
            course.tracks.stamp(previous,next,glm::normalize(next-previous),.50,5000,4,2);previous=next;}}
    ++rutsFired;
}
void Race::stepWeapons(const Input& input,double dt,bool ai){
    if(!combatEnabled||dt<=0)return;weaponTime+=dt;for(auto& f:course.jacks)f.shape.x=std::min(f.shape.w,f.shape.x+float(dt*3.8));updatePickups(dt);auto list=cars(*this);
    for(int i=0;i<4;++i){auto& car=*list[i];auto& gun=car.turret;
        gun.cooldown=std::max(0.,gun.cooldown-dt);gun.recoil*=std::exp(-dt*24);gun.flash=std::max(0.,gun.flash-dt);gun.hitMarker=std::max(0.,gun.hitMarker-dt);gun.shotLife=std::max(0.,gun.shotLife-dt);gun.pickupTime=std::max(0.,gun.pickupTime-dt);
        if(i==0&&input.cut){gun.cable={};gun.cooldown=std::max(.35,gun.cooldown);}
        bool fire=i==0?input.fire&&!input.cut:false;int target=-1;
        if(i&&ai){double nearest=65;for(int j=0;j<4;++j)if(j!=i&&!list[j]->damage.wrecked()){double d=glm::length(list[j]->position-car.position);if(d<nearest&&(gun.weapon!=Weapon::TowHarpoon||d>7.)){nearest=d;target=j;}}
            if(target>=0){auto point=list[target]->position+list[target]->orientation()*V(0,.08,0);
                if(gun.weapon!=Weapon::TowHarpoon){point+=V(list[target]->velocity.x,0,list[target]->velocity.y)*.35;point.y=course.height(point.x,point.z);}
                aimTurret(car,point,dt);fire=glm::dot(glm::normalize(point-gunCamera(car)),gunDirection(car))>.997&&std::fmod(elapsed+i*.71,4.)<3.&&rivalDelay[i-1]<=0&&(gun.weapon!=Weapon::TowHarpoon||(std::abs(car.speed)>4&&std::abs(list[target]->speed)>3));}
            if(gun.cable.active()&&(gun.cable.age>2.5||glm::length(cableView(*this,i).end-towMount(car))<7.)){gun.cable={};gun.cooldown=std::max(2.5,gun.cooldown);}
        }
        solveTowCable(*this,i,fire&&gun.weapon==Weapon::TowHarpoon,dt);
        if(gun.weapon!=Weapon::TowHarpoon&&gun.ammo[int(gun.weapon)]==0)gun.weapon=Weapon::TowHarpoon;
        if(gun.cable.active()||!fire||gun.cooldown>0||car.damage.wrecked()||(i==0&&recoveryDelay>0)||(i&&rivalFinished[i-1]>0))continue;
        Weapon weapon=gun.weapon;double range=weapon==Weapon::TowHarpoon?58:80;
        auto aim=traceBullet(*this,i,gunCamera(car),gunDirection(car),range);auto muzzle=V(fr::fr_turret_muzzle(armedVehicle(car)));auto direction=aim.point-muzzle;
        direction=glm::length(direction)<.01?gunDirection(car):glm::normalize(direction);auto hit=weaponHit(*this,i);
        if(i&&weapon==Weapon::TowHarpoon&&hit.car!=target){gun.cooldown=.18;continue;}
        if(weapon!=Weapon::TowHarpoon){
            if((hit.car<0&&hit.material!=0)||glm::length(P(hit.point.x-car.position.x,hit.point.z-car.position.z))<3){gun.cooldown=.3;gun.notice="Aim at ground in range";gun.pickupTime=1.2;continue;}
            P ground(hit.point.x,hit.point.z),forward(direction.x,direction.z);if(glm::length(forward)<.01)forward={std::sin(car.yaw),std::cos(car.yaw)};
            bool done=weapon==Weapon::DuneJack?addJack(ground,forward):(stitchRuts(ground,forward),true);
            if(done&&weapon==Weapon::DuneJack)course.jacks.back().shape.x=.02f;
            if(!done){gun.cooldown=.3;gun.notice="Jack needs clear ground";gun.pickupTime=1.2;continue;}
            --gun.ammo[int(weapon)];gun.cooldown=weapon==Weapon::DuneJack?1.3:1.1;
            if(gun.ammo[int(weapon)]==0)gun.weapon=Weapon::TowHarpoon;
        }else{
            gun.cooldown=.7;
            if(hit.car>=0||hit.material==0||hit.material==15){auto& c=gun.cable;c.target=hit.car>=0?hit.car:-2;
                if(hit.car>=0)c.localAnchor=glm::conjugate(list[hit.car]->orientation())*(hit.point-list[hit.car]->position);else c.groundAnchor=hit.point;
                c.length=std::min(60.,glm::length(hit.point-towMount(car))+1.);++cablesLatched;gun.hitMarker=.2;
                if(hit.car>=0){auto& victim=*list[hit.car];victim.damage.chassis=std::max(.04,victim.damage.chassis-.002);victim.impactFlash=.12;victim.hitAge=0;++victim.impactSerial;}
            }
        }
        gun.shotStart=muzzle;gun.shotEnd=hit.point;gun.shotLife=.18;gun.lastTarget=hit.car;gun.lastShot=weapon;gun.recoil=1;gun.flash=.05;++gun.shots;car.contactImpulse3D(muzzle,-direction,3.);
    }
}
}
