#include "infinity/Economy.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/Streaming.hpp"
#include <iostream>
#include <numeric>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
double area(const Geometry& g){double a=0;for(size_t i=0;i<g.solid.indexCount();i+=3)
    a+=glm::length(glm::cross(g.precisePositions[g.solid.index(i+1)]-g.precisePositions[g.solid.index(i)],
                             g.precisePositions[g.solid.index(i+2)]-g.precisePositions[g.solid.index(i)]))*.5;return a;}
}
int main(){try{
    Universe u;
    const auto a=Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0);
    const auto p=u.describe(a);const auto fingerprint=descriptorFingerprint(p);
    const auto up=glm::normalize(glm::dvec3(.37,.74,1));const auto basis=tangentFrame(up);
    const auto entry=up*(std::pow(10.,p.frame.logRadius)+u.terrainHeight(p,up));
    WeaponRay ray;ray.reference=a;ray.origin=entry+up*500.;ray.direction=-up;ray.seconds=100;
    const auto plan=planMiningProbe(u,a,ray,1,580058);
    check(bool(plan.hole),"Solid land rejected probe trajectory");const auto b=*plan.hole;
    check(glm::length(b.entry-entry)<.001,"Probe missed actual terrain height");
    check(probePose(b,b.started).phase==ProbePhase::Flight,"Probe did not start in flight");
    const auto boring=probePose(b,b.impactTime()+b.drillSeconds*.5);
    check(boring.phase==ProbePhase::Boring&&std::abs(boring.depth-b.depth*.5)<1e-8,"Boring depth not continuous");
    check(glm::length(boring.attitude*glm::dvec3(0,0,-1)-boring.forward)<1e-8,"First-person camera detached from probe heading");
    check(probePose(b,b.impactTime()+b.drillSeconds+.5).phase==ProbePhase::Arming,"Missing fuse phase");
    check(probePose(b,b.blastTime()+.1).blast>.5,"Buried explosion missing");
    check(probePose(b,b.blastTime()+1).phase==ProbePhase::Beacon,"Protected camera did not remain as a survey beacon");
    Navigation parked(u),camera(u);const auto parkedBefore=parked.serialize();
    for(double time:{b.started,b.impactTime()-.1,b.impactTime()+2,b.blastTime()-.5,b.blastTime()+.1,b.blastTime()+3}){
        frameProbeCamera(camera,u,b,time);
        const auto physical=u.pointRelative(camera.anchor,camera.cameraPosition()*
            std::pow(10.,camera.logDistance-u.describe(camera.anchor).frame.logRadius),a,0);
        check(glm::length(physical-probePose(b,time).position)<.0001,"Probe camera lost physical position during automatic magnification");
        check(parked.serialize()==parkedBefore,"Remote camera altered parked ship navigation");
    }
    auto miss=ray;miss.direction=up;check(!planMiningProbe(u,a,miss,1,2).hole,"Away-facing shot created a cavern");
    for(const auto& planet:u.children(u.describe(a.parent()))){const auto node=u.describe(planet);
        if(!hasSolidSurface(node)){check(!planMiningProbe(u,planet,ray,1,2).hole,"Gas world accepted excavation");break;}}
    const double done=b.blastTime()+2;
    check(boreClearance(b,entry-up*20.,b.impactTime()+.1)<0,"Unbored material vanished early");
    check(boreClearance(b,entry-up*20.,done)>3,"Bore did not connect the surface to the chamber");
    for(int y=-40;y<=20;++y)
        check(std::max(double(y),boreClearance(b,entry+up*double(y),done))>2.5,
              "Air/shaft intersection created a false ceiling or a full-size entry barrier");
    check(boreClearance(b,entry-up*b.depth,done)>10,"Explosion did not create a room");
    check(boreClearance(b,entry-up*b.depth+basis*glm::dvec3(40,0,0),done)<0,"Cavern extended outside its walls");
    MiningProbeState physical;physical.holes.push_back(b);
    auto open=boreGeometry(u,b,true);auto tunnel=boreGeometry(u,b,false);
    check(open&&tunnel&&open->solid.indexCount()>tunnel->solid.indexCount(),"No distinct bore and cavern meshes");
    const auto replay=boreGeometry(u,b,true);
    check(geometryFingerprint(*open)==geometryFingerprint(*replay),"Excavation geometry not deterministic");
    std::atomic<bool> cancel{true};check(!boreGeometry(u,b,true,&cancel),"Cancelled cavern generated a mesh");
    auto terrain=terrainGeometry(u,p,tileAt(up,16),0);const auto terrainHash=geometryFingerprint(*terrain);
    auto cut=excavatedTerrain(*terrain,std::span(&b,1),true);
    check(cut&&area(*cut)<area(*terrain)&&area(*cut)>area(*terrain)*.2,"Surface opening did not subtract actual triangles");
    const auto squareMetres=(area(*terrain)-area(*cut))*std::pow(10.,2*terrain->logUnits);
    check(squareMetres>20&&squareMetres<200,"Terrain aperture was cut in the wrong coordinate units");
    check(geometryFingerprint(*terrain)==terrainHash,"Excavation mutated canonical terrain geometry");
    MiningProbeState restored;
    check(restoreProbes(restored,serializeProbes(physical),u)&&serializeProbes(restored)==serializeProbes(physical),"Probe/cavern save failed round trip");
    auto saved=serializeProbes(restored);check(!restoreProbes(restored,saved+"x",u)&&serializeProbes(restored)==saved,"Malformed save damaged valid excavations");
    Navigation nav(u);nav.changeFrame(a);nav.setRenderScale(1);nav.offset=(entry-up*b.depth)/10.-glm::dvec3(0,0,1);
    RegionCache cache(u);FlightController flight(u,nav,cache);Pilot pilot(u,nav,flight);
    auto immutable=std::make_shared<MiningProbeState>(physical);pilot.setExcavations(immutable,done);
    const auto before=nav.cameraPosition()*10.;pilot.update({},.05);
    check(glm::length(nav.cameraPosition()*std::pow(10.,nav.logDistance)-before)<1,"Ship teleported out of the cavern to the original ground");
    check(pilot.ship().hull>99&&pilot.instruments().altitude>2,"Empty cavern collided as solid planet");
    pilot.setVelocity(basis*glm::dvec3(80,0,0));
    for(unsigned k=0;k<80;++k)pilot.update({},.02);
    const auto after=u.pointRelative(nav.anchor,nav.cameraPosition()*std::pow(10.,nav.logDistance-u.describe(nav.anchor).frame.logRadius),a,0);
    check(excavationClearance(physical,a,after,done)>0,"Ship tunnelled through a cavern wall");
    std::shared_ptr<const Geometry> asyncBore,asyncRoom,asyncCut;
    for(unsigned k=0;k<5&&(!asyncBore||!asyncRoom||!asyncCut);++k){cache.beginFrame();
        asyncBore=cache.requestBore(p,b,false);asyncRoom=cache.requestBore(p,b,true);
        asyncCut=cache.requestExcavatedTerrain(terrain,{b});cache.finishFrame();cache.wait();}
    check(asyncBore&&asyncRoom&&asyncCut,"Asynchronous excavation meshes never became ready");
    check(asyncRoom->viewKey==1&&geometryFingerprint(*asyncRoom)==geometryFingerprint(*open),"Cached cavern differs from saved excavation");
    check(cache.stats().bytes<=32*1024*1024,"Excavation bypassed the shared cache budget");
    auto net=std::make_shared<EconomicNetwork>();net->system=a.parent();net->universeSeed=u.seed();net->fingerprint=580058;
    EconomicSite port;port.id={a,0};port.orbital=true;port.radius=100;port.metres=entry+up*30000.;port.capacityKg=2000000;port.initial.fill(10000);net->sites.push_back(port);
    Economy e(net);EconomyPlayer player;ExplorationLedger ledger;uint64_t sequence=0;
    SurveyObserver o;o.reference=a;o.position=port.metres;o.seconds=100;
    auto issue=[&](EconomicAction action,unsigned mount=1){EconomicCommand c;c.sequence=++sequence;c.action=action;c.port=port.id;c.weaponMount=mount;c.observer=o;
        if(action==EconomicAction::LaunchProbe){c.surveyTarget=a;c.probeRay=ray;c.serviceUnits=100;}
        else{EconomySnapshot s;s.network=net;s.sites=e.sites();s.tick=e.tick();s.player=player;s.exploration=ledger.state();
            const auto service=action==EconomicAction::FitProbeLauncher?ProbeService::Fit:action==EconomicAction::ReloadProbe?ProbeService::Reload:ProbeService::Remove;
            const auto q=economicProbeQuote(s,0,mount,service);c.limitCents=service==ProbeService::Remove?q.sellCents:q.totalCents();}
        return ledger.execute(u,e,player,c);};
    const auto money=e.moneyTotal()+player.credits;
    check(issue(EconomicAction::FitProbeLauncher).starts_with("PROBE LAUNCHER FITTED"),"Empty bay could not accept launcher");
    check(player.weaponAt(0)==1&&player.weaponAt(1)==0&&ledger.state().probes.rounds[1]==1,"Refit affected another gun or omitted initial ammunition");
    check(e.moneyTotal()+player.credits==money,"Probe installation did not conserve money");
    check(issue(EconomicAction::ReloadProbe).starts_with("PROBES RELOADED")&&ledger.state().probes.rounds[1]==4,"Reload not backed by port parts");
    check(issue(EconomicAction::FitProbeLauncher,0).starts_with("PROBE LAUNCHER FITTED")&&player.weaponAt(0)==0,"Gun turret could not be traded for a probe launcher");
    check(e.moneyTotal()+player.credits==money,"Turret trade-in did not conserve money");
    o.position=ray.origin;
    check(issue(EconomicAction::LaunchProbe).starts_with("PROBE LAUNCHED"),"Equipped probe did not launch");
    check(ledger.state().probes.rounds[1]==3&&ledger.state().probes.holes.size()==1,"Launch did not atomically consume one probe");
    check(issue(EconomicAction::LaunchProbe).starts_with("PROBE ALREADY ACTIVE")&&ledger.state().probes.rounds[1]==3,"Repeated launch duplicated charge");
    o.seconds=200;ray.seconds=200;
    check(issue(EconomicAction::LaunchProbe).starts_with("EXISTING EXCAVATION")&&ledger.state().probes.rounds[1]==3,
          "Overlapping blast produced intersecting collision and visible walls or consumed a rejected probe");
    ExplorationLedger load;const auto whole=ledger.serialize();
    check(load.restore(whole,u)&&load.serialize()==whole,"Equipment and in-flight bore did not survive restart");
    o.position=port.metres;o.seconds=200;
    check(issue(EconomicAction::SellProbeLauncher).starts_with("PROBE LAUNCHER SOLD"),"Launcher could not be removed");
    check(ledger.state().probes.holes.size()==1&&!ledger.state().probes.launchers[1],"Selling a bay erased the excavation");
    check(e.moneyTotal()+player.credits==money,"Probe service transactions created or lost money");
    check(load.restore("INFINITE_EXPLORATION 3 1 0 3 0 0 0 0\n0\n0\n0\n0\n",u)&&load.state().probes.holes.empty(),"Version-three save migration failed");
    check(descriptorFingerprint(u.describe(a))==fingerprint,"Mining probe changed planet identity or seed");
    std::cout<<"PASS: "<<checks<<" mining-probe refit, flight, boring, camera, physical cavern, finite ammunition and restart checks\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
