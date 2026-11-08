#include "infinity/Economy.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/SurfaceOperations.hpp"
#include <iostream>
#include <numeric>

using namespace infinity;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
glm::dquat face(glm::dvec3 d){d=glm::normalize(d);return glm::quatLookAtRH(d,std::abs(d.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0));}
void exposureChecks(const Universe& u,const Node& p){
    WeatherSample hot;hot.temperatureK=900;
    const auto heat=surfaceExposure(p,hot,10);
    SurfaceExposureState full,tiny;
    double fullDamage=0,tinyDamage=0;bool warning=false;
    for(unsigned i=0;i<1200;++i){
        const auto a=full.advance(heat,1,.05),b=tiny.advance(heat,.0005,.05);
        if(a>0||b>0)check(warning,"Exposure damaged an unwarned ship");
        if(i<160)check(a==0&&b==0,"No initial escape window for a tiny ship");
        warning|=tiny.heat>.25;fullDamage+=a;tinyDamage+=b;
    }
    check(fullDamage>0&&tinyDamage>fullDamage,"Shrinking does not increase environmental vulnerability");
    const auto saved=full;double coolDamage=0;
    for(unsigned i=0;i<1200;++i)coolDamage+=full.advance({},1,.05);
    check(full.heat<.05&&coolDamage<fullDamage,"Shelter fails to cool the hull");
    SurfaceExposureState split,whole;
    for(unsigned i=0;i<100;++i)whole.advance(heat,1,.1);
    for(unsigned i=0;i<200;++i)split.advance(heat,1,.05);
    check(std::abs(whole.heat-split.heat)<1e-10,"Thermal load depends on frame rate");
    check(full.advance(heat,1,-1)==0&&full.advance(heat,1,2)==0,"Invalid elapsed time applies damage");
    WeatherSample storm;storm.temperatureK=280;storm.pressureBar=1;storm.storm=1;storm.type=WeatherType::Thunderstorm;
    SurfaceExposureState charge;double hit=0;
    for(unsigned i=0;i<400;++i)hit+=charge.advance(surfaceExposure(p,storm,20),1,.05);
    check(hit==0&&charge.charge>.7,"Lightning has no useful advance warning");
    for(unsigned i=0;i<120;++i)hit+=charge.advance(surfaceExposure(p,storm,20),1,.05);
    check(hit>0&&hit<=1.8,"Storm produces no bounded discharge");
    const double before=charge.charge;charge.advance({},1,.5);
    check(charge.charge<before,"Electrical charge fails to dissipate outside weather");
    auto dust=storm;dust.storm=0;dust.type=WeatherType::Dust;dust.dust=.8;dust.visibilityMetres=200;
    check(surfaceExposure(p,dust,20).scanQuality<.5&&surfaceExposure(p,dust,30001).scanQuality==1,
          "Dust interferes in orbit or never interferes at the surface");
    check(surfaceExposure(p,hot,-10).heat==0,"Buried material retains outdoor heat forcing");
    auto gas=p;gas.geology.reset();check(surfaceExposure(gas,storm,0).scanQuality==1,"Gas giant acquired surface hazards");
    Navigation nav(u);nav.changeFrame(p.address);nav.setRenderScale(p.frame.logRadius);
    RegionCache cache(u);FlightController flight(u,nav,cache);Pilot pilot(u,nav,flight);
    // A valid exposed save must preserve loads and fail atomically on corruption.
    auto base=pilot.serialize();base.replace(0,16,"INFINITY_SHIP 8 ");
    base+=" - 1 0 "+std::to_string(saved.heat)+" .45";
    check(pilot.restore(base)&&pilot.ship().exposure.heat>.8&&pilot.ship().exposure.charge==.45,"Exposure save did not restore");
    const auto round=pilot.serialize();check(pilot.restore(round)&&pilot.serialize()==round,"Exposure save does not round trip");
    check(!pilot.restore(round+" junk")&&pilot.serialize()==round,"Malformed exposure save partially changed the ship");
    FlightInstruments t;t.environmentalHeat=1.05;t.electricalCharge=.8;t.surfaceSeverity=1;
    t.surfaceWarning="HULL HEAT 105%";t.surfaceAction="CLIMB / LEAVE HOT GROUND / COOL IN SHELTER";
    for(double aspect:{1.,1.2,4./3,16./9,2.4})check(cockpitTextConflicts(cockpitOverlay(t,aspect,"HYPERION")).empty(),"Hazard UI overlaps");
}
}
int main(){try{
    Universe u;const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9);
    const auto p=u.describe(system.child(Kind::Planet,0));const auto descriptor=descriptorFingerprint(p);
    exposureChecks(u,p);
    const auto world=generateSurfaceLife(u,p);check(!world->settlements.empty(),"No generated settlement");
    const auto& city=world->settlements.front();const auto layout=cityFingerprint(city);
    auto net=std::make_shared<EconomicNetwork>();net->system=system;net->universeSeed=u.seed();net->fingerprint=0x53555246414345ULL;
    EconomicSite port;port.id={p.address,surfaceActivitySlot+city.slot};port.seed=city.seed;port.name=city.name;
    port.metres=city.landingPoint;port.capacityKg=2000000;port.initial.fill(10000);port.initial[size_t(Good::Ore)]=0;
    port.target[size_t(Good::Ore)]=60;port.initial[size_t(Good::Alloy)]=40;port.target[size_t(Good::Alloy)]=100;
    port.recipes={5};port.power=100;port.workers=100;port.temperature=280;port.sealed=true;
    net->sites.push_back(port);
    Economy economy(net);EconomyPlayer player;ExplorationLedger ledger;ledger.offer(u,economy);
    const auto found=std::find_if(ledger.state().contracts.begin(),ledger.state().contracts.end(),[](const auto& j){return j.supplyKg>0;});
    check(found!=ledger.state().contracts.end(),"Actual recipe shortage does not produce a local mining mission");
    const auto job=*found;check(job.target==p.address&&job.supplySite&&job.supplyResource==1&&job.supplyKg==40,"Supply offer ignores its local shortage");
    const auto count=ledger.state().contracts.size();ledger.offer(u,economy);ledger.offer(u,economy);
    check(ledger.state().contracts.size()==count,"Polling duplicates funded supply offers");
    auto balanced=std::make_shared<EconomicNetwork>(*net);balanced->sites[0].initial[size_t(Good::Ore)]=10000;
    Economy healthyEconomy(balanced);ExplorationLedger quiet;quiet.offer(u,healthyEconomy);
    check(std::none_of(quiet.state().contracts.begin(),quiet.state().contracts.end(),[](const auto& j){return j.supplyKg>0;}),"Balanced settlement invents a shortage mission");
    auto snapshot=[&]{EconomySnapshot s;s.network=net;s.sites=economy.sites();s.player=player;s.exploration=ledger.state();s.tick=economy.tick();s.portSignals=economicPortSignals(s);return s;};
    WeatherSample calm;calm.temperatureK=280;calm.pressureBar=1;calm.visibilityMetres=20000;
    const auto pre=snapshot();const auto idle=settlementWork(*world,city,calm,&pre);
    check(idle.orderCount==0&&idle.machineryActivity==0,"Missing raw stock invents working production");
    auto cargo=pre;EconomicFlight freight;freight.id={p.address,53};freight.kg=35;freight.good=Good::Ore;freight.phase=FreightPhase::Loading;
    cargo.flights.push_back(freight);cargo.sites[0].bottleneck="MISSING ORE";
    const auto loading=settlementWork(*world,city,calm,&cargo);
    check(loading.orderCount==1&&loading.orders[0].duty==SurfaceDuty::LoadFreight&&loading.orders[0].kg==35&&
        loading.activity>0&&loading.machineryActivity==0,"Raw-material shortage incorrectly halts real freight loading");
    const auto transport=sampleSurfaceWork(*world,city,loading,25);
    check(std::any_of(transport.begin(),transport.end(),[](const auto& a){return a.kind==SurfaceMachine::CargoRover&&a.order.kg==35;}),"No rover represents the real freight order");
    SurveyObserver observer;observer.reference=p.address;observer.position=port.metres;observer.seconds=100;observer.landed=true;
    PlannerChoice choice;choice.contract=job.id;choice.tracking=true;
    auto plan=[&]{const auto s=snapshot();PlannerInput in;in.observer=observer;in.open=true;in.connectedIssuer=observer.landed;
        return planCavernExpedition(u,&s,choice,in);};
    check(plan().worksite&&plan().requiredKg==40,"Planner hides the generated extraction region or requested mass");
    WeaponRay ray;uint64_t sequence=0;
    auto issue=[&](EconomicAction action,uint64_t id=0,unsigned find=0,unsigned mount=0){
        EconomicCommand c;c.sequence=++sequence;c.action=action;c.port=port.id;c.contract=id;c.cavernFind=find;c.weaponMount=mount;
        c.surveyAccount=job.id;c.observer=observer;c.limitCents=100000000;c.workSeconds=8;c.serviceUnits=100;c.combatPresent=true;
        if(action==EconomicAction::LaunchProbe){c.surveyTarget=job.target;c.probeRay=ray;}
        return ledger.execute(u,economy,player,c);
    };
    const auto money=economy.moneyTotal()+player.credits;
    check(issue(EconomicAction::AcceptSurvey,job.id).starts_with("SURVEY ACCEPTED"),"Cannot accept local supply");
    check(issue(EconomicAction::FitProbeLauncher).starts_with("PROBE LAUNCHER FITTED"),"No launcher");
    check(issue(EconomicAction::FitMiningHead,0,0,1).starts_with("MINING HEAD FITTED"),"No mining head");
    const auto direction=u.describe(*job.supplySite).geographicDirection;
    const auto entry=direction*(std::pow(10.,p.frame.logRadius)+u.terrainHeight(p,direction));
    ray.reference=p.address;ray.origin=entry+direction*500.;ray.direction=-direction;ray.seconds=100;
    observer.position=ray.origin;observer.landed=false;
    check(issue(EconomicAction::LaunchProbe).starts_with("PROBE LAUNCHED"),"Cannot drill procedural supply region");
    const auto bore=ledger.state().probes.holes.back();choice.bore=bore.id;
    check(supplyBoreMatches(job,bore),"Real resource-region bore does not match its job");
    const auto finds=cavernFinds(bore);const auto f=std::find_if(finds.begin(),finds.end(),[&](const auto& a){return a.resource==job.supplyResource;});
    check(f!=finds.end(),"Generated region has no required mineral");
    const auto point=cavernFindPoint(bore,*f);
    observer.seconds=bore.blastTime()+3;observer.position=entry-direction*bore.finalDepth();observer.attitude=face(point-observer.position);
    check(issue(EconomicAction::ScanCavern,bore.id,f->index).starts_with("CAVERN OVERVIEW"),"No overview");
    check(!cavernAssaySample(ledger.state(),job,player),"Overview qualifies without close inspection or mining");
    observer.size=.0005;observer.position=point+tangentFrame(direction)*f->inward*.5;observer.attitude=face(point-observer.position);
    check(issue(EconomicAction::ScanCavern,bore.id,f->index).starts_with("CLOSE ASSAY"),"Cannot shrink and inspect");
    check(issue(EconomicAction::MarkCavern,bore.id,f->index).starts_with("CAVERN MATERIAL MARKED"),"Cannot mark local ore");
    check(!issue(EconomicAction::ExtractCavern,bore.id,f->index,1).starts_with("CAVERN EXTRACTED"),"Tiny ship carries industrial ore");
    observer.size=1;observer.position=entry-direction*bore.finalDepth();observer.attitude=face(point-observer.position);
    for(unsigned i=0;i<2;++i)check(issue(EconomicAction::ExtractCavern,bore.id,f->index,1).starts_with("CAVERN EXTRACTED"),"Full-size cut fails");
    check(player.availableCargo(Good::Ore)==40,"Cuts do not yield the requested finite material");
    const auto saved=ledger.serialize();ExplorationLedger loaded;
    check(saved.starts_with("INFINITY_EXPLORATION 9 ")&&loaded.restore(saved,u)&&loaded.serialize()==saved,"Supply mission fails save/reload");
    auto old=job;old.supplyMinedBaseline=40;old.acceptedRevision=0;
    check(!cavernAssaySample(ledger.state(),old,player),"Historical extraction can satisfy a newly accepted supply job");
    observer.position=port.metres;observer.landed=true;
    check(plan().stage==PlanStage::Deliver,"Planner fails to request delivery after mining");
    const auto beforeStock=economy.sites()[0].stock[size_t(Good::Ore)];
    check(issue(EconomicAction::ClaimSurvey,job.id).starts_with("SURFACE SUPPLY COMPLETE"),"No settlement delivery");
    check(player.availableCargo(Good::Ore)==0&&economy.sites()[0].stock[size_t(Good::Ore)]==beforeStock+40,"Delivery creates or loses material");
    check(!issue(EconomicAction::ClaimSurvey,job.id).starts_with("SURFACE SUPPLY COMPLETE"),"Reward claimed twice");
    check(economy.moneyTotal()+player.credits==money,"Mission creates or loses money");
    const auto delivered=snapshot();const auto working=settlementWork(*world,city,calm,&delivered);
    check(working.orderCount>0&&working.orders[0].duty==SurfaceDuty::Production,"Delivered ore does not enable actual production activity");
    auto weather=calm;weather.type=WeatherType::Thunderstorm;weather.storm=.95;
    const auto held=settlementWork(*world,city,weather,&delivered);
    check(held.activity==0&&held.outdoorHold&&held.orderCount==working.orderCount,"Weather discards real work instead of holding it");
    economy.advanceTo(1,1);
    check(economy.sites()[0].produced[size_t(Good::Alloy)]>0,"Delivery never resumes the real refinery recipe");
    check(plan().stage==PlanStage::Complete&&plan().instruction.find("other missing inputs")!=std::string::npos,"Debrief promises a fake unconditional restart");
    check(descriptorFingerprint(u.describe(p.address))==descriptor&&cityFingerprint(city)==layout,"Environmental gameplay changed retained planet or city structures");
    std::cout<<"PASS: "<<checks<<" exposure, shortage, drill-shrink-mark-mine-deliver, activity and persistence checks\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
