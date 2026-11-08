#include "infinity/Economy.hpp"
#include "infinity/CavernHazard.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Navigation.hpp"
#include "infinity/ProbePresentation.hpp"
#include "infinity/Streaming.hpp"
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
glm::dquat face(glm::dvec3 direction) {
    direction=glm::normalize(direction);
    return glm::quatLookAtRH(direction,std::abs(direction.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0));
}
glm::dvec3 land(const Universe& u,const Node& p) {
    for(unsigned i=0;i<96;++i){const auto d=glm::normalize(glm::dvec3(std::sin(i*2.4)+.2,.7,std::cos(i*2.4)));
        const double h=u.terrainHeight(p,d),r=std::pow(10.,p.frame.logRadius);
        if(p.geology->liquidWater<=.1||h/r>=-.0008)return d*(r+h);
    }
    throw std::runtime_error("No exposed land in fixture");
}
void checkIceHazard(const Universe& u) {
    std::optional<Borehole> icy;
    for (unsigned i = 0; i < 16 && !icy; ++i) {
        const auto system = u.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, i));
        for (const auto& address : u.children(system)) {
            const auto world = u.describe(address);
            if (!hasSolidSurface(world) || world.environment.temperature >= 250)
                continue;
            const auto entry = land(u, world);
            const auto up = glm::normalize(entry);
            WeaponRay ray;
            ray.reference = address;
            ray.origin = entry + up * 500.;
            ray.direction = -up;
            ray.seconds = 100;
            auto plan = planMiningProbe(u, address, ray, 0, 590060);
            if (plan.hole && iceFault(*plan.hole)) {
                icy = std::move(plan.hole);
                break;
            }
        }
    }
    check(bool(icy), "No inherited ice fault on generated solid worlds");
    const auto fault = *iceFault(*icy);
    auto warm = *icy;
    warm.geology.temperature = 280;
    check(!iceFault(warm), "Warm geology creates an ice hazard");
    warm = *icy;
    warm.geology.fractions[0] = 0;
    check(!iceFault(warm), "Ice hazard invents a missing mineral");
    MiningProbeState saved, loaded;
    saved.holes.push_back(*icy);
    check(restoreProbes(loaded, serializeProbes(saved), u), "Cannot restore a naturally icy borehole");
    const auto restored = iceFault(loaded.holes.front());
    check(restored && restored->face.id == fault.face.id && restored->weakness == fault.weakness,
          "Fault selection or weakness changes after reload");
    const auto cracks = iceCracks(*icy, fault);
    const auto reloadedCracks = iceCracks(loaded.holes.front(), *restored);
    for (size_t i = 0; i < cracks.size(); ++i) {
        check(cracks[i].from == reloadedCracks[i].from && cracks[i].to == reloadedCracks[i].to,
              "Fault endpoints change after reload");
        const auto radial = cracks[i].to + glm::dvec3(0, icy->finalDepth(), 0);
        check(glm::length(radial) < boreRoomRadius(*icy, radial), "Fracture rendered behind cavern wall");
    }
    IceStress stress;
    const auto eye = fault.face.local + fault.face.inward * 2.;
    double damage = 0;
    for (int i = 0; i < 120; ++i)
        damage += stress.update(icy->id, fault, eye, 0, 1, true, .05);
    check(damage == 0 && stress.warning(), "Ice did not warn during a safe initial cut");
    FlightInstruments cockpit;
    annotateIceStress(cockpit, stress, true, true);
    check(cockpit.exploration[2].starts_with("ENGINEER") && cockpit.exploration[3].starts_with("RELEASE CUTTER"),
          "Cutter warning omits a crew callout or actionable instruction");
    for (int i = 0; i < 60; ++i)
        stress.update(icy->id, fault, eye, 0, 1, false, .05);
    check(stress.load == 0 && !stress.falling(), "Releasing the cutter did not settle the ice");
    for (int i = 0; i < 120; ++i)
        stress.update(icy->id, fault, eye, .005, .001, false, .05);
    check(stress.load == 0, "Slow close assay stresses the ice");
    bool warned = false;
    int hits = 0;
    for (int i = 0; i < 120; ++i) {
        const double hit = stress.update(icy->id, fault, eye, .025, .001, false, .05);
        if (hit > 0) {
            check(warned && hit <= 2.4, "Ice fall damage was unannounced or unbounded");
            ++hits;
        }
        warned |= stress.warning();
    }
    check(hits == 1, "Icefall did not hit exactly once during the burst");
    check(iceFallMotes(*icy, fault, .55).count > 0 && iceFallMotes(*icy, fault, 2.4).count == 0,
          "Icefall debris is missing or never clears");
    for (int i = 0; i < 300; ++i)
        damage += stress.update(icy->id, fault, eye + fault.face.inward * 16., 0, 1, true, .05);
    check(damage == 0, "Icefall damaged a ship outside the fall zone");
}
}
int main(){try {
    Universe u;
    checkIceHazard(u);
    const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9);
    const auto home=u.describe(system.child(Kind::Planet,0));
    const auto entry=land(u,home),up=glm::normalize(entry);
    WeaponRay ray;ray.reference=home.address;ray.origin=entry+up*500.;ray.direction=-up;ray.seconds=100;
    const auto planned=planMiningProbe(u,home.address,ray,0,590059);
    check(bool(planned.hole),"No solid-world probe plan");
    const auto original=*planned.hole;
    const auto fingerprint=descriptorFingerprint(home);
    const auto finds=cavernFinds(original);
    const auto crystals=cavernCrystals(original);
    check(crystals.count==7,"Crystal pocket has no bounded physical clusters");
    const auto room=boreGeometry(u,original,true);
    check(room&&room->solid.indexCount()>15000&&room->residentBytes()<512*1024,"Cavern detail is missing or unbounded");
    check(geometryFingerprint(*room)==geometryFingerprint(*boreGeometry(u,original,true)),"Cavern geology changes on regeneration");
    auto other=original;other.id+=1;
    check(cavernWallColour(other,{1.3,-10,0})==cavernWallColour(original,{1.3,-10,0}),"Wall geology depends on ammunition identity");
    for(const auto& f:finds)check(original.geology.fractions[f.resource]>0,"Deposit invented a missing constituent");
    for(size_t i=0;i<crystals.count;++i){const auto& c=crystals.items[i];
        check(cavernCrystalClearance(c,c.centre)<0,"Crystal collision is hollow");
        const auto tip=c.centre+c.orientation*glm::dvec3(0,c.height,0);
        check(std::abs(cavernCrystalClearance(c,tip))<1e-8,"Crystal tip geometry and collision disagree");
        const auto point=original.entry+tangentFrame(up)*c.centre;
        check(boreClearance(original,point,original.blastTime()+2)<0,"Crystal cluster can be flown through");
    }
    for(auto mineral:{Mineral::Quartz,Mineral::Iron,Mineral::Olivine,Mineral::Ice,Mineral::Graphite}) {
        auto c=crystals.items[0];c.mineral=mineral;
        const auto mesh=cavernCrystalMesh(c);
        check(mesh.indexCount()>12,"Existing mineral habit produced no crystal mesh");
        for(const auto v:mesh.vertices)
            check(std::abs(cavernCrystalClearance(c,{v.x,v.y,v.z}))<1e-5,"Visible mineral facets disagree with the canonical crystal field");
    }
    auto controlled=original;std::string status;
    const double at=controlled.impactTime()+2;
    const auto historical=probePose(controlled,at-.1);
    check(controlProbe(controlled,ProbeControl::Pause,0,at,status),"Cannot pause active drill");
    const auto paused=probeOperation(controlled,at+10);
    check(paused.phase==ProbePhase::Paused&&std::abs(paused.depth-probePose(original,at).depth)<1e-8,"Pause advances the cutter");
    check(controlled.blastTime()>1e20&&probeActive(controlled,at+100),"Paused charge auto-detonates or permits another launch");
    check(probePose(controlled,at-.1).position==historical.position,"A later control rewrites past probe movement");
    check(controlProbe(controlled,ProbeControl::Depth,110,at+10,status),"Cannot choose a deeper target");
    check(controlProbe(controlled,ProbeControl::Rate,.65,at+10,status),"Cannot choose slower feed");
    check(controlProbe(controlled,ProbeControl::Resume,0,at+10,status),"Cannot resume cooled drill");
    check(glm::length(probePose(controlled,at+10).position-probePose(controlled,at+10-1e-6).position)<1e-5,"Resuming teleports the camera");
    check(std::abs(controlled.finalDepth()-110)<1e-8&&controlled.blastTime()<1e20,"Depth/rate change never reaches its new target");
    auto aborted=controlled;
    check(controlProbe(aborted,ProbeControl::Retract,0,at+11,status),"Cannot safely retract probe");
    check(probePose(aborted,at+100).phase==ProbePhase::Recovered&&!probeActive(aborted,at+100),"Aborted probe remains active");
    check(aborted.blastTime()>1e20&&boreClearance(aborted,entry-up*60.,at+100)<0,"Abort opens an unblasted chamber");
    auto early=original;
    const double earlyAt=early.impactTime()+early.drillSeconds*.6;
    check(controlProbe(early,ProbeControl::Detonate,0,earlyAt,status),"Cannot arm a sufficiently buried charge early");
    check(std::abs(early.finalDepth()-original.depth*.6)<1e-8&&std::abs(early.blastTime()-earlyAt-1.5)<1e-8,"Early blast uses planned depth or wrong fuse");
    check(std::abs(probeBlastDamage(early,entry-up*early.finalDepth())-160)<1e-8,"Early blast damage is centred at the old planned depth");
    check(probeBlastDamage(early,entry)==0&&probeBlastDamage(aborted,entry-up*aborted.finalDepth())==0,
          "Buried or disarmed charge damages an unrelated position");
    check(probeGeometryKey(early)!=probeGeometryKey(original),"New excavation depth reuses stale GPU geometry");
    RegionCache cache(u);std::shared_ptr<const Geometry> readyShaft,readyRoom;
    for(unsigned i=0;i<5&&(!readyShaft||!readyRoom);++i){cache.beginFrame();
        readyShaft=cache.requestBore(home,original,false);readyRoom=cache.requestBore(home,original,true);
        cache.finishFrame();cache.wait();}
    check(readyShaft&&readyRoom,"Original excavation never became ready");
    cache.beginFrame();
    check(cache.requestBore(home,early,false,early.finalDepth())==readyShaft,"A depth change hides the ready shaft while generating its replacement");
    check(!cache.requestBore(home,early,true),"A chamber at another physical depth was used as a fallback");
    cache.finishFrame();cache.wait();
    cache.beginFrame();const auto revised=cache.requestBore(home,early,true);cache.finishFrame();
    check(revised&&geometryFingerprint(*revised)!=geometryFingerprint(*readyRoom),"New chamber depth never replaces its cached predecessor");
    check(cache.stats().bytes<=32*1024*1024,"Controlled excavation bypasses the shared cache budget");
    auto tooEarly=original;
    check(!controlProbe(tooEarly,ProbeControl::Detonate,0,tooEarly.impactTime()+.01,status),"Surface charge bypassed minimum cover");
    auto angled=original;angled.launch+=tangentFrame(up)*glm::dvec3(150,0,0);
    check(controlProbe(angled,ProbeControl::Rate,.65,angled.started,status),"Angled drill control setup failed");
    check(glm::length(probePose(angled,angled.impactTime()-1e-6).position-probePose(angled,angled.impactTime()+1e-6).position)<.002,
          "Controlled probe camera jumps at an oblique surface impact");
    auto history=original;
    for(unsigned i=0;i<maxProbeControls-1;++i)
        check(controlProbe(history,ProbeControl::Rate,i%2?.65:.8,history.started+i*.001,status),"Bounded control history filled too early");
    check(!controlProbe(history,ProbeControl::Depth,120,history.impactTime()+1,status),"Non-terminal control consumed the reserved abort slot");
    check(controlProbe(history,ProbeControl::Retract,0,history.impactTime()+1,status),"Full command history leaves an active probe impossible to abort");
    auto hot=original;hot.drillSeconds=24;
    check(controlProbe(hot,ProbeControl::Depth,120,hot.started,status)&&controlProbe(hot,ProbeControl::Rate,1.5,hot.started,status),"Hot drill setup failed");
    check(probeOperation(hot,hot.impactTime()+14).phase==ProbePhase::Paused,"High feed never trips the thermal pause");
    MiningProbeState program;program.holes={controlled,early};program.holes[1].id+=2;
    MiningProbeState restored;
    check(restoreProbes(restored,serializeProbes(program),u)&&serializeProbes(restored)==serializeProbes(program),"Drill timeline did not survive restart");
    for(double time:{original.started,at-.1,at+8,at+11,controlled.blastTime()+2})
        check(probePose(restored.holes[0],time).position==probePose(controlled,time).position,"Restored control timeline changes the camera");

    auto net=std::make_shared<EconomicNetwork>();net->system=system;net->universeSeed=u.seed();net->fingerprint=590059;
    EconomicSite port;port.id={home.address,0};port.orbital=true;port.metres=entry+up*30000.;port.radius=100;
    port.capacityKg=2000000;port.initial.fill(10000);net->sites.push_back(port);
    Economy economy(net);EconomyPlayer player;ExplorationLedger ledger;ledger.offer(u,economy);
    const auto jobIt=std::find_if(ledger.state().contracts.begin(),ledger.state().contracts.end(),[](const auto& j){return j.cavern;});
    check(jobIt!=ledger.state().contracts.end()&&hasSolidSurface(u.describe(jobIt->target)),"Assay contract targets missing or gaseous terrain");
    const auto job=*jobIt;
    uint64_t sequence=0;
    SurveyObserver observer;observer.reference=home.address;observer.position=port.metres;observer.seconds=100;
    auto issue=[&](EconomicAction action,uint64_t id=0,unsigned find=0,unsigned mount=0,double work=8){
        EconomicCommand c;c.sequence=++sequence;c.action=action;c.port=port.id;c.contract=id;c.cavernFind=find;c.weaponMount=mount;
        c.observer=observer;c.limitCents=100000000;c.workSeconds=work;c.serviceUnits=100;c.combatPresent=true;
        if(action==EconomicAction::LaunchProbe){c.surveyTarget=job.target;c.probeRay=ray;}
        return ledger.execute(u,economy,player,c);
    };
    const auto money=economy.moneyTotal()+player.credits;
    check(issue(EconomicAction::AcceptSurvey,job.id).starts_with("SURVEY ACCEPTED"),"Assay reward could not enter escrow");
    check(issue(EconomicAction::FitProbeLauncher).starts_with("PROBE LAUNCHER FITTED"),"No probe equipment");
    check(issue(EconomicAction::FitMiningHead,0,0,1).starts_with("MINING HEAD FITTED"),"No extraction equipment");
    const auto targetPlanet=u.describe(job.target);const auto targetEntry=land(u,targetPlanet),targetUp=glm::normalize(targetEntry);
    ray.reference=job.target;ray.origin=targetEntry+targetUp*500.;ray.direction=-targetUp;ray.seconds=100;
    observer.reference=job.target;observer.position=ray.origin;
    check(issue(EconomicAction::LaunchProbe).starts_with("PROBE LAUNCHED"),"No expedition excavation");
    const auto hole=ledger.state().probes.holes.back();const auto feature=cavernFinds(hole)[0];
    const auto point=cavernFindPoint(hole,feature);
    observer.seconds=hole.blastTime()+3;observer.position=targetEntry-targetUp*hole.finalDepth();observer.attitude=face(point-observer.position);
    check(issue(EconomicAction::ScanCavern,hole.id).starts_with("CAVERN OVERVIEW"),"Cannot survey from within the real chamber");
    check(issue(EconomicAction::MarkCavern,hole.id).starts_with("SHRINK"),"Full-size overview was accepted as a close assay");
    observer.size=.0005;observer.position=point+tangentFrame(targetUp)*feature.inward*.5;observer.attitude=face(point-observer.position);
    check(issue(EconomicAction::ScanCavern,hole.id).starts_with("CLOSE ASSAY"),"Shrunken close assay did not register");
    check(issue(EconomicAction::MarkCavern,hole.id).starts_with("CAVERN MATERIAL MARKED"),"Close material could not be marked");
    check(!issue(EconomicAction::ExtractCavern,hole.id,0,1,6).starts_with("CAVERN EXTRACTED"),"Tiny ship extracted full-size cargo");
    observer.size=1;observer.position=targetEntry-targetUp*hole.finalDepth();observer.attitude=face(point-observer.position);
    const auto access=cavernMiningAccess(u,observer,hole,feature,1);
    if(!access.allowed)std::cerr<<access.status<<'\n';
    check(access.allowed,"Marked cavern material has no clear full-size extraction line");
    check(issue(EconomicAction::ExtractCavern,hole.id,0,1,6).starts_with("CAVERN EXTRACTED"),"Marked cavern extraction failed");
    check(ledger.state().minedKg==20&&ledger.state().probes.holes.back().finds[0].minedKg==20,"Extracted cargo and discovery records disagree");
    check(ledger.state().prospects.size()==1&&ledger.state().prospects[0].extracted[feature.resource]==20,"Cavern mining bypasses the shared finite reserve");
    const auto saved=ledger.serialize();ExplorationLedger loaded;
    check(loaded.restore(saved,u)&&loaded.serialize()==saved,"Discovery, mark, cargo progress or contract lost on restart");
    observer.reference=home.address;observer.position=port.metres;
    check(issue(EconomicAction::ClaimSurvey,job.id).starts_with("CAVERN ASSAY COMPLETE"),"Cannot deliver a new surveyed and mined sample");
    check(!issue(EconomicAction::ClaimSurvey,job.id).starts_with("CAVERN ASSAY COMPLETE"),"Assay reward can be claimed twice");
    check(economy.moneyTotal()+player.credits==money,"Probe services or assay escrow created/lost money");
    check(player.massKg()==10,"Assay did not hand exactly 10 kg of mined material to the port");
    std::ostringstream legacy;legacy<<"INFINITE_EXPLORATION 4 1 0 3 0 0 0 0\n0\n0\n0\n0\n"<<std::quoted("INFINITE_PROBES 1 0 0 0 0 0 0 0\n")<<'\n';
    check(loaded.restore(legacy.str(),u),"Version-four exploration save migration failed");
    check(descriptorFingerprint(u.describe(home.address))==fingerprint,"Cavern upgrade changed the planet descriptor");
    std::cout<<"PASS: "<<checks<<" cavern geology, crystal collision, drill control, shared depletion, sample delivery and restart checks\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
