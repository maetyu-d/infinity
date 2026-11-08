#include "infinity/ProbePresentation.hpp"
#include <iostream>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
}
int main(){try{
    Universe u;
    const auto a=Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0);
    const auto planet=u.describe(a);const auto up=glm::normalize(glm::dvec3(.37,.74,1));
    const auto entry=up*(std::pow(10.,planet.frame.logRadius)+u.terrainHeight(planet,up));
    WeaponRay ray;ray.reference=a;ray.origin=entry+up*500.;ray.direction=-up;ray.seconds=100;
    const auto plan=planMiningProbe(u,a,ray,0,580158);check(bool(plan.hole),"Missing presentation fixture");
    const auto b=*plan.hole;MiningProbeState state;state.holes.push_back(b);const auto save=serializeProbes(state);
    const auto base=boreGeometry(u,b,true);const auto hash=geometryFingerprint(*base);
    const auto boring=probeVisual(b,b.impactTime()+2,3300);
    const auto heavy=probeVisual(b,b.impactTime()+2,7000);
    check(boring.rpm>0&&boring.load>0&&boring.vibration>0,"Drilling has no machinery response");
    check(heavy.rpm<boring.rpm&&heavy.load>boring.load,"Inherited density has no effect on drill presentation");
    const auto edge=b.impactTime()+b.drillSeconds;
    const auto before=probeVisual(b,edge-.00001,3300),after=probeVisual(b,edge+.00001,3300);
    check(std::abs(std::sin(before.cutterAngle-after.cutterAngle))<.001,"Cutter snaps when arming starts");
    const auto armed=probeVisual(b,edge+1.3,3300);
    check(armed.rpm==0&&armed.retract==1&&armed.fuse>0&&armed.vibration==0,"Arming does not stop/retract/stabilize drill");
    check(probeToolReadout(armed).find("FUSE")!=std::string::npos,"Fuse countdown missing from telemetry");
    const auto blast=probeVisual(b,b.blastTime()+.02,3300),settled=probeVisual(b,b.blastTime()+2,3300);
    check(blast.flash>.7&&settled.flash==0&&settled.dust>0,"Flash and settling dust do not have distinct timing");
    const auto near=probeWallLight(b,boring,{1.3,-40,0},{0,-38,0},{0,-1,0},{.5,.5,.5},false);
    const auto far=probeWallLight(b,boring,{1.3,-40,0},{0,10,0},{0,-1,0},{.5,.5,.5},false);
    const auto away=probeWallLight(b,boring,{1.3,-40,0},{0,-38,0},{0,1,0},{.5,.5,.5},false);
    check(glm::length(near)>glm::length(far)*2,"Work light lacks physical distance falloff");
    check(glm::length(near)>glm::length(away)*2,"Work light illuminates behind its beam");
    const auto rig=probeRig(),again=probeRig();
    check(rig.housing.indexCount()>0&&rig.cutter.indexCount()>0&&rig.lamps.indexCount()>0,"Probe machinery missing");
    check(rig.housing.bytes()+rig.cutter.bytes()+rig.lamps.bytes()+rig.roller.bytes()+rig.drive.bytes()+rig.actuator.bytes()+rig.hotTeeth.bytes()<256*1024,"Probe rig exceeds bounded resident budget");
    check(rig.cutter.indexCount()==again.cutter.indexCount(),"Hardware construction is unstable");
    for(const auto* mesh:{&rig.housing,&rig.cutter,&rig.lamps,&rig.roller,&rig.drive,&rig.actuator,&rig.hotTeeth}){
        bool valid=true;for(const auto& vertex:mesh->vertices)valid&=std::isfinite(vertex.x+vertex.y+vertex.z);
        for(size_t i=0;i<mesh->indexCount();++i)valid&=mesh->index(i)<mesh->vertices.size();
        check(valid,"Non-finite or invalid probe hardware mesh");
    }
    double radialBound=0,lensDistance=1e9;bool rigid=true,pistonsConnected=true;
    for(unsigned step=0;step<=60;++step){
        auto v=probeVisual(b,edge+step*.025,3300);const auto mechanism=probeMechanism(v);
        rigid&=std::abs(glm::determinant(glm::dmat3(mechanism.head))-1)<1e-9;
        for(unsigned k=0;k<2;++k){const double x=k?.64:-.64;
            const auto end=mechanism.rods[k]*glm::dvec4(0,0,1,1);
            pistonsConnected&=glm::length(end-mechanism.carrier*glm::dvec4(x,0,.11,1))<1e-8;
        }
        auto bound=[&](const Mesh& mesh,const glm::dmat4& transform){
            for(const auto& vertex:mesh.vertices){const auto p=transform*glm::dvec4(vertex.x,vertex.y,vertex.z,1);
                radialBound=std::max(radialBound,std::hypot(p.x,p.y));}
        };
        bound(rig.cutter,mechanism.head);bound(rig.hotTeeth,mechanism.head);bound(rig.drive,mechanism.drive);
        for(const auto& roller:mechanism.rollers)bound(rig.roller,roller);
        // Saved bores can stop at any rotor phase, including on other mineral
        // types. Check the actual moving hardware through a full revolution.
        for(unsigned phase=0;phase<12;++phase){auto rotated=v;
            rotated.cutterAngle=phase*6.283185307179586/12;rotated.rollerAngle=-rotated.cutterAngle*2.3;
            const auto pose=probeMechanism(rotated);bound(rig.cutter,pose.head);bound(rig.hotTeeth,pose.head);
            for(const auto& roller:pose.rollers)bound(rig.roller,roller);
            for(const auto& vertex:rig.cutter.vertices){const auto p=pose.head*glm::dvec4(vertex.x,vertex.y,vertex.z,1);
                lensDistance=std::min(lensDistance,glm::length(glm::dvec3(p)));}
        }
    }
    check(rigid,"Retraction resizes or distorts the full-size cutter head");
    check(radialBound<1.30,"Hinged cutter intersects unexcavated shaft walls while stowing");
    check(pistonsConnected,"Hydraulic rods detach from their moving carrier");
    check(lensDistance>.15,"Retracting hardware passes through the camera lens");
    const auto startUp=probeVisual(b,b.impactTime(),3300),running=probeVisual(b,b.impactTime()+.4,3300);
    check(startUp.rpm==0&&running.rpm>0,"Cutter lacks a smooth start-up interval");
    check(probeVisual(b,edge+.1,3300).retract==0,"Head folds before its initial coast-down");
    check(probeVisual(b,edge+2,3300).heat<probeVisual(b,edge+.1,3300).heat,"Cutting tips never cool after stopping");
    for(double t:{b.started,b.impactTime()+2,edge+.8,b.blastTime()+.02,b.blastTime()+2,b.blastTime()+10}){
        const auto v=probeVisual(b,t,3300),replay=probeVisual(b,t,3300);
        const auto motes=probeMotes(b,v,t,{.4,.5,.6});const auto replayMotes=probeMotes(b,replay,t,{.4,.5,.6});
        check(v.cutterAngle==replay.cutterAngle&&motes.count==replayMotes.count,"Frame order changes presentation sample");
        check(motes.count<=128,"Unbounded transient particle count");
        if(t>b.blastTime()+9)check(motes.count==0,"Blast particles persist forever");
        for(size_t n=0;n<motes.count;++n){const auto& p=motes.items[n];
            check(p.position==replayMotes.items[n].position&&p.spark==replayMotes.items[n].spark&&std::isfinite(p.radius)&&p.radius>0&&p.opacity>=0&&p.opacity<=1,
                  "Particle sample is non-deterministic or invalid");
            if(v.blastAge<0)check(std::hypot(p.position.x,p.position.z)<1.25&&p.position.y+probePose(b,t).depth<1.2,
                                 "Drilling ejecta escape the shaft or cross the camera lens");
        }
    }
    check(serializeProbes(state)==save&&geometryFingerprint(*base)==hash,"Presentation altered excavation geometry or save state");
    const double done=b.blastTime()+2;
    const auto cavityEye=b.entry-up*b.depth;
    check(boreEnclosesView(b,done,cavityEye,-up,1.2,1.6,10,.005),"Covered cavern view retains exterior draws");
    check(!boreEnclosesView(b,done,cavityEye,up,1.2,1.6,10,.005),"Looking up the shaft hides the exterior");
    check(!boreEnclosesView(b,done,b.entry-up*3.,-up,1.2,1.6,10,.005),"Entrance proximity is not conservative");
    check(!boreEnclosesView(b,b.started,cavityEye,-up,1.2,1.6,10,.005),"Unexcavated rock culls the world");
    check(!boreEnclosesView(b,done,cavityEye,-up,1.2,1.6,10,20),"A clipped near plane culls the world");
    check(!boreEnclosesView(b,done,b.entry+up*10.,-up,1.2,1.6,10,.005),"Exterior flight culls the world");
    const auto basis=tangentFrame(up);bool conservative=true;
    for(unsigned step=0;step<=60;++step){const double angle=step*3.141592653589793/60;
        const auto forward=-up*std::cos(angle)+(basis*glm::dvec3(1,0,0))*std::sin(angle);
        if(!boreEnclosesView(b,done,cavityEye,forward,1.2,1.6,10,.005))continue;
        const auto camera=glm::inverse(glm::quatLookAtRH(forward,basis*glm::dvec3(0,0,-1)));
        for(unsigned k=0;k<64;++k){const double a=k*6.283185307179586/64;
            const auto ray=camera*(b.entry+basis*glm::dvec3(10*std::cos(a),0,10*std::sin(a))-cavityEye);
            if(ray.z<0&&std::abs(ray.x)<-ray.z*std::tan(.6)*1.6&&std::abs(ray.y)<-ray.z*std::tan(.6))conservative=false;
        }
    }
    check(conservative,"Portal culling hides an aperture sample inside the camera frustum");
    bool identical=true;
    for(double seconds:{b.impactTime()+2,b.blastTime()-.5,b.blastTime()+.08,b.blastTime()+2}){
        const auto v=probeVisual(b,seconds,3300);const bool complete=seconds>=b.blastTime();
        const auto light=probeLighting(b,v,{0,-b.depth+3,0},{.2,-1,.1},complete);
        for(const auto& vertex:base->solid.vertices){const glm::dvec3 point{vertex.x,vertex.y,vertex.z};
            identical&=rgba(light.shade(point,{.37,.52,.63}))==rgba(probeWallLight(b,v,point,light.eye,{.2,-1,.1},{.37,.52,.63},complete));
        }
    }
    check(identical,"Prepared work lighting changes the original vertex colours");
    auto v=probeVisual(b,done,3300);const auto light=probeLighting(b,v,{0,-70,0},{0,-1,0},true);
    v.heat=.9;v.dust=.3;v.rpm=300;
    check(light==probeLighting(b,v,{0,-70,0},{0,-1,0},true),"Non-light effects invalidate a settled light stream");
    check(!(light==probeLighting(b,v,{.001,-70,0},{0,-1,0},true)),"Moving the camera retains stale work lighting");
    check(!(light==probeLighting(b,v,{0,-70,0},{.1,-1,0},true)),"Turning the lamp retains stale work lighting");
    std::cout<<"PASS: "<<checks<<" probe machinery, local-light, fuse, dust and presentation isolation checks\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
