#include "Combat.hpp"
#include "Tactical.hpp"
#include <iostream>
#include <stdexcept>
#include <numbers>
using namespace desert;
namespace fr=infinity::fractal;
using V=glm::dvec3;
static int checks=0;
static void check(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
static void airborne(Race& r){r.combatEnabled=true;r.state=State::Racing;r.player=Car{};r.player.position={0,100,0};for(int i=0;i<3;++i){r.rivals[i]=Car{};r.rivals[i].position={1000.+100*i,100,0};}}
int main()try{
    Race race(20261101);airborne(race);race.rivals[0].position={0,100,20};Input trigger;trigger.fire=true;
    for(int i=0;i<100;++i)race.aimGunnerAt(race.rivals[0].position+V(0,.08,0),1./60.);
    auto h=traceBullet(race,0,gunCamera(race.player),gunDirection(race.player));
    check(h.car==1,"independent gun sight hits an opposing vehicle");
    check(h.distance>15&&h.distance<25,"vehicle hit is at the actual primitive surface");
    race.rivals[1].position={0,100,10};h=traceBullet(race,0,gunCamera(race.player),gunDirection(race.player));
    check(h.car==2,"nearer vehicle occludes the selected target");race.rivals[1].position={1100,100,0};
    check(race.player.turret.weapon==Weapon::TowHarpoon&&race.player.turret.ammo[1]==0&&race.player.turret.ammo[2]==0,"harpoon is the unlimited starting weapon");
    double condition=race.rivals[0].damage.chassis;
    race.stepWeapons(trigger,1./120.,false);
    check(race.player.turret.shots==1&&race.player.turret.cable.target==1,"harpoon latches onto an opposing vehicle");
    check(race.rivals[0].damage.chassis<condition&&race.player.turret.hitMarker>0,"harpoon impact provides damage and confirmed hit feedback");
    auto& cable=race.player.turret.cable;double paid=cable.length;
    race.stepWeapons(trigger,1./120.,false);check(cable.length<paid&&race.player.turret.shots==1,"holding fire reels without repeatedly shooting");
    paid=cable.length;race.stepWeapons({},1./120.,false);check(cable.length>paid,"releasing fire pays out slack");
    Input cut=trigger;cut.cut=true;race.stepWeapons(cut,1./120.,false);check(!cable.active()&&race.player.turret.shots==1,"right click cuts without instantly reattaching");
    cable.target=1;cable.localAnchor={.7,.08,0};cable.length=glm::length(cableView(race,0).end-towMount(race.player))-1;
    auto momentum=[&](){V p(0);for(auto* car:std::array<Car*,2>{&race.player,&race.rivals[0]})p+=V(car->velocity.x,car->verticalSpeed,car->velocity.y)*infinity::buggy::mass;return p;};
    auto beforeMomentum=momentum();solveTowCable(race,0,true,1./120.);
    check(glm::length(momentum()-beforeMomentum)<1e-8,"tow force conserves combined linear momentum");
    check(cable.force>0&&cable.force<=9000&&std::abs(race.rivals[0].yawRate)>0,"tow acts at the hook point and applies bounded angular force");
    cable.length=50;auto beforeSpeed=race.player.velocity;solveTowCable(race,0,false,1./120.);check(cable.force==0&&race.player.velocity==beforeSpeed,"slack cable applies no pulling force");
    race.rivals[0].position.z=70;solveTowCable(race,0,true,1./120.);check(!cable.active(),"cable breaks beyond its maximum range");race.rivals[0].position.z=20;
    auto shots=race.player.turret.shots;race.pause();race.step(trigger,1./120.);check(race.player.turret.shots==shots,"pause cannot fire");
    race.state=State::Countdown;race.countdown=2;race.step(trigger,1./120.);check(race.player.turret.shots==shots,"countdown cannot fire");
    race.state=State::Racing;race.player.turret.cooldown=0;race.player.damage.engine=0;race.stepWeapons(trigger,1./120.,false);check(race.player.turret.shots==shots,"disabled car cannot shoot");
    race.player.damage.engine=1;race.recoveryDelay=2;race.stepWeapons(trigger,1./120.,false);check(race.player.turret.shots==shots,"recovery cannot shoot");race.recoveryDelay=0;
    // A downward ray uses the mathematical field, including newly stamped ruts.
    V top(67,80,73);h=traceBullet(race,0,top,V(0,-1,0),100);double initialY=h.point.y;
    check(h.car==-1&&std::abs(initialY-race.course.height(top.x,top.z))<.02,"bullets stop on the real fractal heightfield");
    for(int i=0;i<60;++i)race.course.tracks.stamp({67,72},{67,74},{0,1},.5,3000,1,1);
    h=traceBullet(race,0,top,V(0,-1,0),100);
    check(h.point.y<initialY-.02&&std::abs(h.point.y-race.course.height(top.x,top.z))<.02,"bullet collision follows deformed terrain");
    race.rivals[0].position={67,initialY-6,73};h=traceBullet(race,0,top,V(0,-1,0),100);check(h.car==-1,"terrain occludes vehicles beneath it");
    V post(race.course.markers.front());h=traceBullet(race,0,post+V(0,8,0),V(0,-1,0),10);
    check(h.material==15&&h.distance>4.6&&h.distance<4.8,"checkpoint posts block weapon rays at their actual capsule caps");
    // Pickups are physical, limited-charge items with a shared respawn timer.
    Race items(20261101);items.combatEnabled=true;items.start();items.state=State::Racing;
    auto first=coursePickup(items.course,1);items.player.position=first.position;items.updatePickups(.2);
    check(items.player.turret.ammo[1]==3&&items.player.turret.weapon==Weapon::RutStitcher,"driving through a rut pickup grants and equips three charges");
    items.updatePickups(.2);check(items.player.turret.ammo[1]==3,"collected pickup cannot be taken twice during cooldown");
    items.weaponTime=18.1;items.updatePickups(.2);check(items.player.turret.ammo[1]==6,"pickup respawns on a later pass and respects inventory capacity");
    auto second=coursePickup(items.course,2);items.player.position=second.position;items.updatePickups(.2);
    check(items.player.turret.ammo[2]==2&&items.player.turret.weapon==Weapon::DuneJack,"dune jack is a separate two-charge pickup");
    items.selectWeapon(0,1);check(items.player.turret.weapon==Weapon::TowHarpoon,"mouse wheel can return to the basic weapon");
    items.selectWeapon(0,1);check(items.player.turret.weapon==Weapon::RutStitcher,"mouse wheel selects a stored pickup");
    items.player=Car{};items.player.position={67,items.course.height(67,73)+2,73};items.player.turret.ammo[1]=1;items.player.turret.weapon=Weapon::RutStitcher;
    V ground(67,items.course.height(67,86),86);for(int i=0;i<60;++i)items.aimGunnerAt(ground,1./60.);items.stepWeapons(trigger,1./120.,false);
    check(items.rutsFired==1&&items.player.turret.ammo[1]==0&&items.player.turret.weapon==Weapon::TowHarpoon,"firing the last rut charge returns to the unlimited harpoon");
    double deepest=0;auto impact=items.player.turret.shotEnd;for(int z=-20;z<=20;++z)for(int x=-8;x<=8;++x)deepest=std::min(deepest,double(items.course.tracks.sample({impact.x+x*.25,impact.z+z*.25}).height));
    check(deepest<-.20&&deepest>=-.321,"rut stitcher cuts deep physical grooves in the shared terrain");
    Race raised(20261101);raised.combatEnabled=true;raised.state=State::Racing;double base=raised.course.height(67,73);
    check(raised.addJack({67,73},{0,1}),"dune jack creates a terrain formation");
    check(std::abs(raised.course.height(67,73)-base-1.15)<.0001,"dune jack raises the collision surface by its actual height");
    check(!raised.addJack({68,73},{0,1}),"overlapping jacks cannot stack into unbounded terrain");
    for(int z=-7;z<=7;++z)for(int x=-5;x<=5;++x){glm::vec2 point(67+x*.35,73+z*.35);auto sample=fr::fr_terrain_sample(point,raised.course.field());float eps=.02;
        float dx=(fr::fr_height(point+glm::vec2(eps,0),raised.course.field())-fr::fr_height(point-glm::vec2(eps,0),raised.course.field()))/(2*eps);
        float dz=(fr::fr_height(point+glm::vec2(0,eps),raised.course.field())-fr::fr_height(point-glm::vec2(0,eps),raised.course.field()))/(2*eps);
        check(glm::length(sample.gradient-glm::vec2(dx,dz))<.08,"raised-terrain normals agree with the mathematical surface");
        auto bounds=fr::fr_terrain_bounds(point,raised.course.field(),.75);check(sample.height<=bounds.x+.001,"terrain cache bounds include dune jacks");
    }
    h=traceBullet(raised,0,{67,80,73},{0,-1,0},100);check(std::abs(h.point.y-raised.course.height(67,73))<.02,"weapon ray hits the raised mathematical terrain");
    raised.player.position={67,raised.course.height(67,60)+2,60};raised.player.turret.weapon=Weapon::DuneJack;raised.player.turret.ammo[2]=1;
    ground={67,raised.course.height(67,88),88};for(int i=0;i<60;++i)raised.aimGunnerAt(ground,1./60.);raised.stepWeapons(trigger,1./120.,false);
    check(raised.jacksRaised==2&&raised.player.turret.ammo[2]==0&&raised.player.turret.weapon==Weapon::TowHarpoon,"dune pickup consumes a charge and restores the basic weapon");
    raised.stitchRuts({67,73},{0,1});h=traceBullet(raised,0,{67,80,73},{0,-1,0},100);
    check(std::abs(h.point.y-raised.course.height(67,73))<.02,"weapon collision follows ruts cut into a raised dune jack");
    Race endlessItems(20261101,RaceFormat::Endless);endlessItems.combatEnabled=true;endlessItems.state=State::Racing;auto farPickup=coursePickup(endlessItems.course,92);endlessItems.player.position=farPickup.position;endlessItems.updatePickups(.2);
    check(endlessItems.player.turret.ammo[2]==2&&endlessItems.pickupRespawn.contains(92),"endless routes generate collectible weapons beyond the old stage boundary");
    auto count=raised.course.jacks.size();raised.recover();check(raised.course.jacks.size()==count,"recovery retains weapon-deformed terrain");
    raised.start();check(raised.course.jacks.empty()&&raised.course.tracks.tileCount()==0&&raised.player.turret.weapon==Weapon::TowHarpoon,"new race clears terrain weapons and restores starting equipment");
    // Raised ground changes suspension support and vehicle motion, not just pixels.
    Race jump(20261101);auto where=jump.course.point(35);double heading=jump.course.heading(35);jump.addJack({where.x,where.z},{std::sin(heading),std::cos(heading)});
    Course clean(20261101,RaceFormat::Circuit);Car plain,over;plain.reset(clean,20);over=plain;plain.velocity=over.velocity={std::sin(plain.yaw)*12,std::cos(plain.yaw)*12};plain.speed=over.speed=12;double movement=0;
    for(int i=0;i<240;++i){plain.step(clean,{.5,0,false,false},1./120.);over.step(jump.course,{.5,0,false,false},1./120.);movement=std::max(movement,std::abs(over.position.y-plain.position.y));}
    check(movement>.10,"dune jack noticeably changes the vehicle's physical trajectory");
    Race growing(20261101);growing.combatEnabled=true;growing.state=State::Racing;growing.player.reset(growing.course,65);auto under=growing.player.position;growing.addJack({under.x,under.z},{0,1});growing.course.jacks.back().shape.x=.02;
    for(int i=0;i<120;++i){growing.player.step(growing.course,{0,0,true,false},1./120.);growing.stepWeapons({},1./120.,false);}
    check(std::isfinite(growing.player.position.y)&&growing.player.position.y>under.y+.4,"a growing dune jack physically lifts a car without trapping it below terrain");
    Race snag(20261101);snag.combatEnabled=true;snag.state=State::Racing;V gate(snag.course.markers.front());snag.player=Car{};snag.rivals[0]=Car{};
    snag.player.position=gate+V(-6,1.2,.98);snag.rivals[0].position=gate+V(6,1.96,0);snag.player.turret.cable.target=1;snag.player.turret.cable.length=11;
    solveTowCable(snag,0,true,1./120.);check(!snag.player.turret.cable.active(),"terrain and checkpoint posts sever an obstructed cable");
    airborne(snag);snag.player.turret.cable.target=1;snag.player.turret.cable.length=20;snag.rivals[0].turret.cable.target=0;snag.rivals[0].turret.cable.length=20;
    snag.recover();check(!snag.player.turret.cable.active()&&!snag.rivals[0].turret.cable.active(),"recovery releases every cable attached to the recovered car");
    // Geometry queries and turret transforms agree across car attitudes and aim.
    for(int i=0;i<80;++i){Car c;c.position={5,100,7};c.yaw=i*.23;c.pitch=.2*std::sin(i);c.roll=.3*std::cos(i);c.turret.yaw=i*.31;c.turret.pitch=-.15+.8*(i%17)/16.;
        auto v=armedVehicle(c);auto direction=gunDirection(c);auto q=c.orientation()*glm::angleAxis(c.turret.yaw,V(0,1,0))*glm::angleAxis(-c.turret.pitch,V(1,0,0));
        check(glm::length(direction-q*V(0,0,1))<5e-6,"gunner camera and physical barrel have the same orientation");
        auto muzzle=V(fr::fr_turret_muzzle(v));auto hit=fr::fr_visual_vehicle_ray(glm::vec3(muzzle+direction*2.),glm::vec3(-direction),v,true,true,10);
        check(hit.material==9&&hit.distance>1.9&&hit.distance<2.1,"barrel remains hittable through arbitrary car and gun rotations");
        check(std::abs(fr::fr_visual_vehicle(glm::vec3(muzzle+direction*(2.-hit.distance)),v,true,true).distance)<.0001,"exact ray and rendered turret field meet at the same surface");
        check(std::abs(glm::length(hit.normal)-1)<1e-5,"turret ray normals remain unit length");
        for(int sample=0;sample<12;++sample){V from=c.position+V(std::sin(i+sample*.7)*5,1.3+std::cos(sample)*2,std::cos(i*.3+sample)*5);
            V toward=glm::normalize(c.position+V(0,.3,0)-from);auto whole=fr::fr_visual_vehicle_ray(glm::vec3(from),glm::vec3(toward),v,true,true,20);
            fr::ImplicitHit parts{20,{},0};for(int component=0;component<8;++component)parts=fr::fr_nearest_hit(parts,fr::fr_vehicle_component_ray(glm::vec3(from),glm::vec3(toward),v,component,parts.distance));
            check(std::abs(parts.distance-whole.distance)<.0001,"native component rendering preserves the whole-car ray surface");
            if(parts.distance<20)check(glm::length(parts.normal-whole.normal)<.001f,"native component rendering preserves surface normals");
        }
    }
    auto cap=fr::fr_ray_capsule({0,0,-5},{0,0,1},{0,0,0},{0,0,3},.1,20);
    check(std::abs(cap.distance-4.9)<.001,"tracer capsule handles an axial ray");
    cap=fr::fr_ray_capsule({-5,0,1},{1,0,0},{0,0,0},{0,0,3},.1,20);check(std::abs(cap.distance-4.9)<.001,"tracer capsule handles a side ray");
    for(int i=0;i<100;++i){double t=5+i*.3,x=.06+i*.001;V rd=glm::normalize(V((.018-x)/t,0,1));double expected=(x-.018)/(-rd.x);
        auto hit=fr::fr_ray_capsule({float(x),0,-.5f},glm::vec3(rd),{0,0,0},{0,0,280},.018,300);
        check(std::abs(hit.distance-expected)<.002,"long tracers remain thin and precise at near-axial gunner angles");
    }
    airborne(race);race.rivals[0].position={0,100,20};race.player.speed=race.rivals[0].speed=10;race.elapsed=0;
    for(int i=0;i<360;++i){race.elapsed+=1./120.;race.stepWeapons({},1./120.);}
    check(race.rivals[0].turret.shots>0,"rival turret can acquire and fire at a visible car");
    airborne(race);race.player.position={67,race.course.height(67,73)-10,73};race.rivals[0].position={67,race.course.height(67,93)-10,93};race.player.speed=race.rivals[0].speed=10;
    for(int i=0;i<360;++i){race.elapsed+=1./120.;race.stepWeapons({},1./120.);}
    check(race.rivals[0].turret.shots==0,"rival cannot fire through occluding fractal terrain");
    auto before=race.player.yaw;race.aimGunnerAt(race.player.position+V(30,2,0),1.);check(race.player.yaw==before&&race.player.turret.yaw>1,"gunner can turn without steering the driver");
    // Map input must select the same physical surface that the launcher can hit.
    Race tactical(20261101);airborne(tactical);tactical.rivals[0].position={0,100,25};
    for(double aspect:{.8,1.25,1.6,2.4}){auto map=makeGunnerMap(tactical.player,aspect);
        for(int i=0;i<31;++i){glm::dvec2 uv(.02+i*.031,.13+i*.023);check(glm::length(map.project(map.world(uv))-uv)<1e-12,"map projection and cursor agree across window shapes");}
        double cw=std::max(1280.,900*aspect),ch=cw/aspect;
        check(std::abs(map.rect.z*cw/(2*map.halfExtent.x)-map.rect.w*ch/(2*map.halfExtent.y))<1e-10,"map metres have equal horizontal and vertical scale");
    }
    auto map=makeGunnerMap(tactical.player,1.6);auto initialCenter=map.center;tactical.player.yaw=1.4;tactical.player.roll=.4;tactical.player.pitch=-.2;
    check(glm::length(makeGunnerMap(tactical.player,1.6).center-initialCenter)<1e-10,"north-up chart ignores chassis rotation and roll");tactical.player=Car{};tactical.player.position={0,100,0};
    for(double yaw:{0.,.8,1.9,3.1}){auto& target=tactical.rivals[0];target.yaw=yaw;auto f=mapForward(target);glm::dvec2 right(f.y,-f.x),center(target.position.x,target.position.z);
        auto chosen=pickGunnerTarget(tactical,map,map.project(center+right*1.2+f*1.8));
        check(chosen.car==1,"rotated enlarged car icon selects the opposing car");
        auto local=glm::conjugate(target.orientation())*(chosen.point-target.position);auto bounds=fr::fr_block_bounds(armedVehicle(target)),c=fr::fr_block_center(armedVehicle(target));
        check(std::abs(local.x-1.2/2.1)<1e-6&&std::abs(local.z-1.8/2.1)<1e-6,"cursor selects an off-centre physical attachment point");
        check(std::abs(local.y-c.y-bounds.y)<1e-6,"selected attachment is on the physical roof surface");
        auto enlarged=mapCarPoint(target,chosen.point);check(glm::length(enlarged-(center+right*1.2+f*1.8))<1e-6,"attachment marker returns to the enlarged car symbol");
    }
    tactical.rivals[0].yaw=0;auto target=pickGunnerTarget(tactical,map,map.project({0,25}));
    tactical.player.turret.yaw=2.;auto moving=evaluateGunnerAim(tactical,target);check(!moving.canFire()&&moving.status==AimStatus::Slewing,"map cannot launch while barrel is turning");
    for(int i=0;i<120;++i)aimTurret(tactical.player,target.point,1./60.);
    auto ready=evaluateGunnerAim(tactical,target);check(ready.canFire()&&ready.actual.car==1,"map can fire after the physical turret reaches a clear car");
    auto expected=ready.actual;Input shoot;shoot.fire=true;tactical.stepWeapons(shoot,1./120.,false);
    check(tactical.player.turret.cable.target==1&&glm::length(tactical.player.turret.shotEnd-expected.point)<1e-7,"map hit preview and actual harpoon agree exactly");
    tactical.player.turret.cable={};tactical.player.turret.cooldown=.3;check(evaluateGunnerAim(tactical,target).status==AimStatus::Reloading,"map reports weapon cooldown");tactical.player.turret.cooldown=0;
    tactical.rivals[1].position={0,100,12};check(evaluateGunnerAim(tactical,target).status==AimStatus::Blocked,"nearer car blocks map targeting through it");tactical.rivals[1].position={1000,100,0};
    check(evaluateGunnerAim(tactical,{{0,100,90},-1}).status==AimStatus::OutOfRange,"map rejects targets beyond physical weapon range");
    check(evaluateGunnerAim(tactical,{{0,200,5},-1}).status==AimStatus::OutOfRange,"range is a three dimensional distance");
    check(evaluateGunnerAim(tactical,{{0,130,5},-1}).status==AimStatus::Elevation,"map respects turret elevation limits");
    tactical.player.turret.weapon=Weapon::DuneJack;check(evaluateGunnerAim(tactical,{{0,99,1},-1}).status==AimStatus::TooClose,"terrain preview respects the buggy exclusion radius");
    glm::dvec2 mapGround(10,15);auto cursor=map.project(mapGround);double beforeHeight=pickGunnerTarget(tactical,map,cursor).point.y;
    check(tactical.addJack(mapGround,{0,1}),"map terrain fixture creates a physical mound");
    double mapRaised=pickGunnerTarget(tactical,map,cursor).point.y;check(mapRaised>beforeHeight+1.1,"map cursor samples raised terrain rather than old height cache");
    tactical.stitchRuts(mapGround,{0,1});check(pickGunnerTarget(tactical,map,cursor).point.y<mapRaised-.02,"map cursor also samples actual rut depth");
    Race groundMap(20261101);airborne(groundMap);groundMap.player.position={67,groundMap.course.height(67,73)+2,73};
    groundMap.player.turret.weapon=Weapon::RutStitcher;groundMap.player.turret.ammo[1]=2;
    GunnerTarget groundTarget{{67,groundMap.course.height(67,86),86},-1};
    for(int i=0;i<120;++i)aimTurret(groundMap.player,groundTarget.point,1./60.);
    auto groundReady=evaluateGunnerAim(groundMap,groundTarget);check(groundReady.canFire(),"map can deploy a terrain tool onto clear ground");
    // A car symbol can cover ground without its solid body covering that exact ray.
    groundTarget.car=1;check(evaluateGunnerAim(groundMap,groundTarget).canFire(),"terrain targeting accepts clear ground beneath a selected symbol");
    auto groundImpact=weaponHit(groundMap,0);groundMap.stepWeapons(shoot,1./120.,false);
    check(groundMap.rutsFired==1&&glm::length(groundMap.player.turret.shotEnd-groundImpact.point)<1e-7,"terrain preview uses the actual physical launch endpoint");
    std::cout<<"PASS "<<checks<<" weapon, pickup, terrain and turret geometry checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
