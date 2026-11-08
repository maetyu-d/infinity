#include "infinity/SurfaceRecon.hpp"
#include "infinity/Cockpit.hpp"
#include <iostream>
#include <set>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
}
int main(){try{
    Universe u;const auto planet=u.describe(Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0));
    const auto world=generateSurfaceLife(u,planet);check(!world->settlements.empty(),"No procedural settlement");
    const auto cityIt=std::find_if(world->settlements.begin(),world->settlements.end(),[](const auto& city){return city.population>0;});
    check(cityIt!=world->settlements.end(),"No inhabited generated settlement");
    const auto& city=*cityIt;const auto identity=descriptorFingerprint(planet),layout=cityFingerprint(city);
    const auto direction=glm::normalize(city.landingPoint);const auto tile=tileAt(direction,14);
    const auto map=sampleSurfaceMap(u,planet,tile);check(map&&sizeof(SurfaceMap)*4<160*1024,"Map is missing or exceeds the bounded cache budget");
    const auto repeat=sampleSurfaceMap(u,planet,tile);
    for(size_t i=0;i<map->cells.size();++i){const auto& c=map->cells[i];
        check(std::isfinite(c.height)&&std::isfinite(c.slope)&&c.resource<4,"Invalid map sample");
        check(c.metres==repeat->cells[i].metres&&c.height==repeat->cells[i].height,"Map depends on sample order");
        check(std::abs(glm::length(c.metres)-std::pow(10.,planet.frame.logRadius)-u.terrainHeight(planet,glm::normalize(c.metres)))<1e-7,"Scanner terrain disagrees with the real surface");
    }
    std::atomic<bool> cancel{true};check(!sampleSurfaceMap(u,planet,tile,&cancel),"Cancelled sampling still publishes a map");
    auto gas=planet;gas.geology.reset();check(!sampleSurfaceMap(u,gas,tile),"Gas giant gained a landing surface");
    SurfaceMapCache cache;check(!cache.request(u,planet,direction),"Cold request blocks until generation completes");cache.wait();
    check(bool(cache.request(u,planet,direction)),"Completed map is unavailable");
    for(unsigned i=0;i<16;++i)cache.request(u,planet,glm::normalize(direction+map->axes*glm::dvec3(i*.004,0,0)));
    cache.wait();const auto end=glm::normalize(direction+map->axes*glm::dvec3(15*.004,0,0));
    const auto last=cache.request(u,planet,end);check(last&&encodeTile(last->tile)==encodeTile(tileAt(end,14))&&cache.entries()<=4,"Cancelled or evicted map replaced the latest request");
    auto net=std::make_shared<EconomicNetwork>();net->system=planet.address.parent();net->universeSeed=u.seed();net->fingerprint=0x5245434f4eULL;
    EconomicSite site;site.id={planet.address,surfaceActivitySlot+city.slot};site.seed=city.seed;site.name=city.name;
    site.metres=city.landingPoint;site.population=city.population;site.workers=100;site.power=100;site.sealed=true;site.temperature=280;
    site.capacityKg=2000000;site.initial.fill(10000);net->sites.push_back(site);
    Economy economy(net);economy.outage(site.id,100);economy.advanceTo(12,12);
    const auto found=std::find_if(economy.recoveries().begin(),economy.recoveries().end(),[](const auto& j){return j.kind==RecoveryKind::Maintenance;});
    check(found!=economy.recoveries().end(),"Actual plant outage did not create a funded incident");const auto job=*found;
    EconomySnapshot s;s.network=net;s.sites=economy.sites();s.tick=economy.tick();s.recoveries=economy.recoveries();
    SurveyObserver observer;observer.reference=planet.address;observer.position=site.metres+direction*3.;observer.seconds=60;observer.landed=true;
    ReconInput input;input.open=true;input.world=world.get();input.economy=&s;input.observer=observer;
    ReconOrders orders;auto view=describeSurfaceRecon(u,map,input,orders,.25);
    check(view.open&&!view.loading&&view.points.size()<=40,"Map view unavailable or unbounded");
    const auto incident=std::find_if(view.points.begin(),view.points.end(),[&](const auto& p){return p.recovery==job.id;});
    check(incident!=view.points.end()&&incident->site==site.id&&incident->metres==site.metres&&incident->actionable,"Incident invented a location or lost its real ledger link");
    const uint64_t incidentId=incident->id;
    auto loadingInput=input;loadingInput.selected=incidentId;
    const auto loading=describeSurfaceRecon(u,{},loadingInput,orders,.25);
    check(loading.loading&&loading.selected==incidentId,"Asynchronous map replacement discards the selected object");
    std::set<uint64_t> ids;for(const auto& p:view.points)check(ids.insert(p.id).second,"Scanner duplicates target identities");
    for(const auto& p:view.points)if(p.kind==ReconKind::Landing){const auto up=glm::normalize(p.metres);
        check(!surfaceCollision(*world,p.metres+up*24.,p.metres+up*3.,6.),"Suggested landing area intersects an existing building");}
    auto estimated=std::find_if(view.points.begin(),view.points.end(),[](const auto& p){return p.kind==ReconKind::Estimate;});
    check(estimated!=view.points.end()&&estimated->grade==0&&estimated->address,"Unsurveyed return is shown as confirmed");
    const auto estimateId=estimated->id;const auto pool=*estimated->address;
    auto record=surveyReading(u,pool,2,5);record.revision=1;s.exploration.journal.push_back(record);s.exploration.revision=1;
    const auto assayed=describeSurfaceRecon(u,map,input,orders,.25);
    check(std::any_of(assayed.points.begin(),assayed.points.end(),[&](const auto& p){return p.id==estimateId&&p.kind==ReconKind::Assay&&p.grade==2;}),"Recorded assay does not refine the same return");
    const auto frame=planet.address.child(Kind::Surface,encodeTile(tileAt(direction,7)));auto shifted=input;
    shifted.observer.reference=frame;shifted.observer.position=u.pointRelative(planet.address,observer.position/std::pow(10.,planet.frame.logRadius),frame,0);
    shifted.observer.attitude=u.relative(planet.address,frame,0).orientation*observer.attitude;
    ReconOrders shiftedOrders;const auto same=describeSurfaceRecon(u,map,shifted,shiftedOrders,.25);
    check(glm::length(same.ship-assayed.ship)<1e-5&&same.points.size()==assayed.points.size(),"Changing hierarchical reference changes the physical map");
    for(size_t i=0;i<same.points.size();++i)check(same.points[i].id==assayed.points[i].id,"Coordinate refinement changed scanner identities");
    uint64_t selected=incident->id;cycleReconPoint(selected,view);check(selected!=incident->id,"Target cycling does not advance");
    const auto hit=pickReconPoint(view,reconPosition(*map,incident->metres)/map->radius);
    check(hit&&*hit==incident->id,"Map picking does not use the same spatial coordinates");
    WeaponRay ray;ray.reference=planet.address;ray.origin=map->centre+glm::normalize(map->centre)*500.;ray.direction=-glm::normalize(map->centre);
    const auto drilled=planMiningProbe(u,planet.address,ray,0,0x5245434f4eULL);check(bool(drilled.hole),"Cannot plan an actual reconnaissance cavern fixture");
    s.exploration.probes.holes.push_back(*drilled.hole);
    auto excavated=input;excavated.observer.seconds=drilled.hole->blastTime()+2;
    const auto entrances=describeSurfaceRecon(u,map,excavated,orders,.25);
    check(std::any_of(entrances.points.begin(),entrances.points.end(),[&](const auto& p){return p.kind==ReconKind::Entrance&&p.metres==drilled.hole->entry;}),"Saved entrance does not share the actual excavation coordinates");
    EconomicFlight delayed;delayed.id={planet.address,53};delayed.destination=0;delayed.kg=40;delayed.weatherHold=true;delayed.phase=FreightPhase::Holding;
    s.flights.push_back(delayed);
    const auto held=describeSurfaceRecon(u,map,input,orders,.25);
    check(std::any_of(held.points.begin(),held.points.end(),[](const auto& p){return p.waiting&&!p.actionable;}),"Actual freight hold became a fictitious paid repair job");
    ShipCrew crew(0x5245434f4eULL);check(crew.members().size()>=2,"No crew fixture");
    for(size_t i=0;i<crew.members().size();++i)crew.order(i,CrewOrder::Rest,CrewStation::Bunks);
    crew.assign(0,CrewStation::Navigation);crew.assign(1,CrewStation::Engineering);
    for(unsigned i=0;i<600;++i)crew.advance(.05);
    input.crew=&crew;input.selected=incidentId;orders.navigator=true;orders.engineer=true;
    for(unsigned i=0;i<100;++i)view=describeSurfaceRecon(u,map,input,orders,.25);
    check(view.navWatching&&view.confidence>0&&orders.assessment==1&&view.safeAltitude>=250&&view.navigator.starts_with("NAV EST"),"Staffed navigation order never produces an assessment");
    input.exposure.heat=1.1;view=describeSurfaceRecon(u,map,input,orders,.25);
    check(view.engineerWatching&&view.engineer.starts_with("ENG EST / DAMAGE"),"Engineer ignores dangerous residual heat");
    input.iceLoad=.6;view=describeSurfaceRecon(u,map,input,orders,.25);
    check(view.engineer.find("ICE STRAIN")!=std::string::npos,"Engineer ignores live ice strain");
    crew.order(0,CrewOrder::Rest,CrewStation::Bunks);view=describeSurfaceRecon(u,map,input,orders,.25);
    check(!view.navWatching&&view.navigator.find("NEED A READY")!=std::string::npos,"Unstaffed navigator invents reliable forecasts");
    for(double aspect:{.7,1.,1.2,4./3,16./9,2.4}){
        FlightInstruments t;t.recon=std::make_shared<SurfaceReconView>(view);t.altitude=12;t.surfaceSeverity=.95;t.surfaceWarning="ELECTRICAL STORM / LIGHTNING RISK";
        t.surfaceAction="CLIMB CLEAR / SHELTER BEFORE DISCHARGE";t.surfaceLandingHold=true;
        const auto h=cockpitOverlay(t,aspect,"HYPERION","FIELD RESPONSE / PENDING SAVED RECEIPT");
        if(!cockpitTextConflicts(h).empty()){const auto [a,b]=cockpitTextConflicts(h).front();std::cerr<<h.textRuns[a].value<<" / "<<h.textRuns[b].value<<'\n';}
        check(cockpitTextConflicts(h).empty(),"Scanner text overlaps HUD or warnings");
        for(const auto& run:h.textRuns)check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=900*aspect+.01&&run.bounds.y+run.bounds.h<=900.01,"Scanner text escapes the viewport");
        check(std::any_of(h.popups.begin(),h.popups.end(),[](const auto& p){return p.critical;}),"Scanner hides a critical exposure warning");
    }
    EconomyPlayer player;uint64_t sequence=0;EconomicCommand command;command.action=EconomicAction::RespondFieldIncident;command.port=site.id;command.contract=job.id;
    command.workSeconds=8;command.serviceUnits=100;command.observer=observer;
    const auto issue=[&]{command.sequence=++sequence;return respondFieldIncident(u,economy,player,command);};
    const auto money=economy.moneyTotal()+player.credits;
    command.observer->position+=direction*100.;check(!issue().starts_with("RECOVERY ACCEPTED"),"Forged landed flag allows airborne field work");
    command.observer=observer;command.observer->field=true;check(!issue().starts_with("RECOVERY ACCEPTED"),"Warp field can service a site");
    command.observer=observer;check(issue().starts_with("RECOVERY ACCEPTED"),"Cannot accept the mapped incident at its actual site");
    for(size_t g=0;g<goodCount;++g)player.cargo[g]=job.required[g];
    command.workSeconds=0;check(!issue().starts_with("RECOVERY DELIVERED"),"Missing field work immediately transfers parts");
    command.workSeconds=8;const auto delivered=issue();check(delivered.starts_with("RECOVERY DELIVERED"),"Field response does not transfer required supplies");
    check(!issue().starts_with("RECOVERY DELIVERED"),"Field response can pay twice");
    const auto saved=economy.serialize();Economy replay(net);check(replay.restore(saved)&&replay.serialize()==saved,"Field incident progress does not survive restart");
    economy.advanceTo(30,30);
    const auto recovered=std::find_if(economy.recoveries().begin(),economy.recoveries().end(),[&](const auto& j){return j.id==job.id;});
    check(recovered!=economy.recoveries().end()&&recovered->phase==RecoveryPhase::Complete&&economy.sites()[0].outageUntil<=economy.tick(),"Field response never repairs the real plant");
    check(economy.moneyTotal()+player.credits==money,"Field response creates or loses money");
    s.sites=economy.sites();s.recoveries=economy.recoveries();s.tick=economy.tick();input.observer.seconds=economy.tick()*economyTickSeconds;
    view=describeSurfaceRecon(u,map,input,orders,.25);
    check(std::any_of(view.points.begin(),view.points.end(),[&](const auto& p){return p.id==incidentId&&p.phase==RecoveryPhase::Complete&&!p.actionable;}),"Incident marker is replaced instead of showing its real recovery");
    check(descriptorFingerprint(u.describe(planet.address))==identity&&cityFingerprint(city)==layout,"Reconnaissance changed retained terrain or settlement identity");
    std::cout<<"PASS: "<<checks<<" shared terrain, assays, crew orders, field recovery, cache and HUD checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
