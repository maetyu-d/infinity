#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/Cockpit.hpp"
#include <iostream>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
glm::dquat face(glm::dvec3 direction){direction=glm::normalize(direction);return glm::quatLookAtRH(direction,std::abs(direction.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0));}
glm::dvec3 land(const Universe& u,const Node& p){for(unsigned i=0;i<96;++i){const auto d=glm::normalize(glm::dvec3(std::sin(i*2.4)+.2,.7,std::cos(i*2.4)));
    const double h=u.terrainHeight(p,d),r=std::pow(10.,p.frame.logRadius);if(p.geology->liquidWater<=.1||h/r>=-.0008)return d*(r+h);}throw std::runtime_error("No fixture land");}
void layout(const ExpeditionPlan& source){for(bool expanded:{false,true}){auto plan=source;plan.details=expanded;
    if(!expanded)plan.firstOffer=plan.selected;
    for(double aspect:{1.,1.2,4./3,16./9,2.4}){
    const PlannerLayout screen(aspect,expanded,&plan);CockpitOverlay diagrams;drawPlannerDiagrams(diagrams,plan,screen);
    if(!expanded){const PlannerLayout detailed(aspect,true);
        check(screen.panel.w*screen.panel.h<detailed.panel.w*detailed.panel.h*.4,"Compact planner still obscures most of the expanded view");
        check(!screen.actions[0].overlaps(screen.details)&&screen.actions[1].w==0,"Compact view duplicates or overlaps its primary action");}
    check(diagrams.strokes.vertices.size()<12000&&diagrams.panels.vertices.size()<6000,"Planner diagrams exceed the bounded HUD budget");
    for(const auto* mesh:{&diagrams.panels,&diagrams.strokes})for(const auto& vertex:mesh->vertices){
        const bool bounds=std::isfinite(vertex.x)&&std::isfinite(vertex.y)&&vertex.x>=screen.panel.x-.01&&vertex.x<=screen.panel.x+screen.panel.w+.01&&vertex.y>=screen.panel.y&&vertex.y<=screen.panel.y+screen.panel.h;
        if(!bounds)std::cerr<<"Stage "<<unsigned(plan.stage)<<" expanded "<<expanded<<" vertex "<<vertex.x<<','<<vertex.y<<" panel "<<screen.panel.x<<','<<screen.panel.y<<','<<screen.panel.w<<','<<screen.panel.h<<'\n';
        check(bounds,"Planner vector geometry escapes the modal canvas");}
    FlightInstruments t;t.weapon="MINING PROBE / NOSE";t.crewFire="GUNNERS HOLD FIRE / NEUTRAL TRAFFIC NEARBY";
    for(bool probe:{false,true}){if(probe)t.phase="MINING PROBE CAMERA";
        const auto h=cockpitOverlay(t,aspect,"HYPERION","LONG TRANSACTION MESSAGE",{},{},false,1,nullptr,{1,0,0,0},nullptr,&plan);
        const auto conflicts=cockpitTextConflicts(h);
        if(!conflicts.empty()){const auto [a,b]=conflicts.front();std::cerr<<h.textRuns[a].value<<" / "<<h.textRuns[b].value<<'\n';}
        check(conflicts.empty(),"Planner text overlaps");
        for(const auto& run:h.textRuns)check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=900*aspect+.01&&run.bounds.y+run.bounds.h<=900.01,"Planner text escapes the canvas");
        check(std::any_of(h.textRuns.begin(),h.textRuns.end(),[](const auto& r){return r.value=="EXPEDITION PLANNER";}),"Modal planner is hidden by a camera or HUD layer");
        if(!expanded){unsigned count=0;for(const auto& run:h.textRuns)if(run.bounds.overlaps(screen.panel))++count;
            check(count<=22,"Compact planner has grown back into a dashboard");
            for(const auto& run:h.textRuns)check(run.value!="SHIP / TOOL STATIONS"&&run.value!="SCHEMATIC"&&run.value!="BULK MATERIALS / ESTIMATE","Detail-only telemetry leaks into the mission card");}
    }
}}}
}
int main(){try{
    Universe u;ShipCrew crew(600);
    PlannerChoice choice;PlannerInput in;in.open=true;in.crew=&crew;in.observer.seconds=100;
    auto loading=planCavernExpedition(u,nullptr,choice,in);check(loading.stage==PlanStage::Loading&&!loading.primaryEnabled,"Empty economy creates an actionable contract");layout(loading);
    const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9),home=system.child(Kind::Planet,0);
    const auto parent=u.describe(home);const auto entry=land(u,parent),up=glm::normalize(entry);
    auto net=std::make_shared<EconomicNetwork>();net->system=system;net->universeSeed=u.seed();net->fingerprint=600060;
    EconomicSite port;port.id={home,0};port.orbital=true;port.metres=entry+up*30000.;port.radius=100;port.capacityKg=2000000;port.initial.fill(10000);port.name="LONG-NAMED ORBITAL ASSAY AND EQUIPMENT HARBOUR";net->sites.push_back(port);
    Economy economy(net);EconomyPlayer player;ExplorationLedger ledger;ledger.offer(u,economy);
    uint64_t sequence=0;in.observer.reference=home;in.observer.position=port.metres;
    auto snapshot=[&]{EconomySnapshot s;s.network=net;s.sites=economy.sites();s.player=player;s.exploration=ledger.state();return s;};
    auto view=[&]{auto s=snapshot();return planCavernExpedition(u,&s,choice,in);};
    auto plan=view();check(plan.stage==PlanStage::Offer&&plan.primaryEnabled,"Funded cavern offer cannot be selected");
    check(plan.visual.known&&plan.visual.solid&&!plan.visual.hasBore,"Unvisited world invents a discovered excavation");
    double total=0;for(auto share:plan.visual.composition)total+=share;check(std::abs(total-1)<1e-9,"Bulk material bar does not normalize inherited composition");
    choice.contract=plan.contract;choice.tracking=true;
    const auto job=*std::find_if(ledger.state().contracts.begin(),ledger.state().contracts.end(),[&](const auto& j){return j.id==choice.contract;});
    const auto world=u.describe(job.target);const auto target=land(u,world),radial=glm::normalize(target);
    WeaponRay ray;ray.reference=job.target;ray.origin=target+radial*500.;ray.direction=-radial;ray.seconds=100;
    auto issue=[&](EconomicAction action,uint64_t id,unsigned mount=0,unsigned find=0){EconomicCommand c;c.action=action;c.sequence=++sequence;c.port=port.id;c.contract=id;c.weaponMount=mount;
        c.surveyAccount=choice.contract;
        c.observer=in.observer;c.workSeconds=8;c.serviceUnits=100;c.combatPresent=true;c.limitCents=100000000;
        if(action==EconomicAction::LaunchProbe){c.surveyTarget=job.target;c.probeRay=ray;c.probeValue=90;c.probeRate=.85;}
        c.cavernFind=find;return ledger.execute(u,economy,player,c);};
    const auto before=ledger.serialize(),crewBefore=crew.serialize();const auto identity=descriptorFingerprint(world);
    for(unsigned i=0;i<20;++i)view();
    check(ledger.serialize()==before&&crew.serialize()==crewBefore,"Opening a planner mutates the ledger or crew");
    in.pending=true;check(!view().primaryEnabled,"Planner accepts another action during a pending receipt");in.pending=false;
    auto poor=snapshot();poor.sites[0].cash=0;check(!planCavernExpedition(u,&poor,choice,in).primaryEnabled,"Planner promises an unfunded reward");
    check(issue(EconomicAction::AcceptSurvey,job.id).starts_with("SURVEY ACCEPTED"),"Cannot reserve planner contract");
    plan=view();check(plan.stage==PlanStage::Prepare&&plan.tracking,"Accepted contract does not show preparation");layout(plan);
    const auto purchaseStart=player.credits;
    check(summarizePlan(plan).blockers>0&&summarizePlan(plan).warning,"Compact preparation hides a required missing tool");
    check(issue(EconomicAction::FitProbeLauncher,0,0).starts_with("PROBE LAUNCHER FITTED"),"Cannot equip planned launcher");
    check(issue(EconomicAction::FitMiningHead,0,1).starts_with("MINING HEAD FITTED"),"Cannot equip planned mining head");
    const auto gearCost=purchaseStart-player.credits;
    check(view().checks.size()==7,"Preparation checks vanished");
    crew.setWeaponLevels(ledger.state().stationLoadout(player));crew.manWeapon(2,1);for(unsigned i=0;i<600;++i)crew.advance(.05);
    in.observer.reference=Address{};in.observer.position={0,0,0};
    plan=view();check(plan.stage==PlanStage::Travel,"Prepared expedition fails to advance to travel");layout(plan);
    check(summarizePlan(plan).step==2&&summarizePlan(plan).context.empty(),"Healthy travel fills the compact card with redundant status checks");
    auto receipt=plan;receipt.message="THE ASSAY PORT CANNOT ACCEPT THIS ORDER WHILE ITS WAREHOUSE IS FULL. YOUR CARGO AND RESERVED REWARD HAVE BEEN RETAINED; REVIEW THE ISSUER BEFORE RETRYING.";layout(receipt);
    check(PlannerLayout(16./9,false,&receipt).panel.h>PlannerLayout(16./9,false,&plan).panel.h,"A long command receipt does not receive its own space");
    check(plan.visual.bays[0].tool==PlannerTool::Probe&&plan.visual.bays[0].rounds==1&&plan.visual.bays[0].captain,"Ship diagram loses its fitted launch bay");
    check(plan.visual.bays[1].tool==PlannerTool::Miner&&plan.visual.bays[1].operatorReadiness>0,"Ship diagram loses its manned mining head");
    auto tiny=in;tiny.observer.size=.0005;const auto readySnapshot=snapshot();const auto tinyPlan=planCavernExpedition(u,&readySnapshot,choice,tiny);
    check(tinyPlan.stage==PlanStage::Prepare&&tinyPlan.primary==PlannerAction::Resume,"Tiny departure opens an irrelevant equipment service");
    auto packed=readySnapshot;packed.player.capacityKg=packed.player.massKg();
    check(planCavernExpedition(u,&packed,choice,in).equipmentPage==MarketPage::Goods,"Cargo blockage opens tool services instead of the goods hold");
    auto badBay=choice;badBay.bay=999;check(planCavernExpedition(u,&readySnapshot,badBay,in).bay<maxWeaponMounts,"Invalid transient bay indexes outside the ship");
    in.observer.reference=job.target;in.observer.position=ray.origin;
    check(view().stage==PlanStage::Drill,"Arrival fails to enable drilling objective");
    const auto rounds=ledger.state().probes.rounds;
    EconomicCommand bad;bad.action=EconomicAction::LaunchProbe;bad.sequence=++sequence;bad.surveyTarget=job.target;bad.observer=in.observer;bad.probeRay=ray;bad.serviceUnits=100;bad.probeValue=90;bad.probeRate=.1;
    check(ledger.execute(u,economy,player,bad).starts_with("PROBE FEED MUST"),"Invalid planned feed accepted");
    check(ledger.state().probes.rounds==rounds&&ledger.state().probes.holes.empty(),"Invalid launch preset spends ammunition or creates a bore");
    check(issue(EconomicAction::LaunchProbe,0,0).starts_with("PROBE LAUNCHED"),"Cannot launch planner preset");
    const auto b=ledger.state().probes.holes.back();choice.bore=b.id;
    check(probeOperation(b,b.started).target==90&&probeOperation(b,b.started).rate==.85,"Depth and feed were not committed atomically with the launch");
    in.observer.seconds=b.impactTime()+1;plan=view();check(plan.stage==PlanStage::Drill,"Active drill is not tracked");layout(plan);
    check(plan.visual.hasBore&&!plan.visual.chamberReady&&plan.visual.reached>0&&plan.visual.boreDepth==90,"Drill section does not follow the real operation");
    auto paused=snapshot();auto& pausedBore=paused.exploration.probes.holes.back();std::string pauseStatus;
    check(controlProbe(pausedBore,ProbeControl::Pause,0,b.impactTime()+.5,pauseStatus),"Cannot pause compact drill fixture");
    const auto pausedPlan=planCavernExpedition(u,&paused,choice,in);
    check(summarizePlan(pausedPlan).warning&&summarizePlan(pausedPlan).context.find("M / TIP")!=std::string::npos,"Compact card hides a paused drill or its actual depth and heat");
    in.observer.seconds=b.blastTime()+3;in.observer.position=b.entry+radial*25.;
    plan=view();check(plan.stage==PlanStage::Enter,"Spent ammunition prevents reuse of a completed cavern");layout(plan);
    check(plan.visual.chamberReady&&plan.visual.bays[0].rounds==0,"Diagram hides the completed cavern or refills spent ammunition");
    auto newChoice=choice;newChoice.newSite=true;
    const auto newSnapshot=snapshot();check(planCavernExpedition(u,&newSnapshot,newChoice,in).stage==PlanStage::Prepare,"New-site request reuses a completed hole");
    const auto find=cavernFinds(b)[0];const auto point=cavernFindPoint(b,find);
    in.observer.position=b.entry-radial*b.finalDepth();in.observer.attitude=face(point-in.observer.position);
    plan=view();check(plan.stage==PlanStage::Overview,"Cavern entry does not show an overview objective");layout(plan);
    check(issue(EconomicAction::ScanCavern,b.id).starts_with("CAVERN OVERVIEW"),"Cannot record planner overview");
    plan=view();check(plan.stage==PlanStage::Inspect,"Overview incorrectly completes a close assay");layout(plan);
    in.observer.size=.0005;in.observer.position=point+tangentFrame(radial)*find.inward*.5;in.observer.attitude=face(point-in.observer.position);
    check(issue(EconomicAction::ScanCavern,b.id).starts_with("CLOSE ASSAY"),"Cannot close-assay planner target");
    plan=view();check(plan.stage==PlanStage::Mark,"Close assay does not request a physical mark");layout(plan);
    check(plan.visual.deposits[0].grade==2&&plan.visual.shipLocal&&std::abs(plan.visual.shipMetres-.006)<1e-10,"Cavern diagram disagrees with the physical assay or ship size");
    check(issue(EconomicAction::MarkCavern,b.id).starts_with("CAVERN MATERIAL MARKED"),"Cannot mark planner target");
    plan=view();check(plan.stage==PlanStage::Regrow,"Tiny ship is directed to full-size extraction prematurely");layout(plan);
    check(plan.visual.deposits[0].marked,"Visual deposit omits the saved material mark");
    in.observer.size=1;in.observer.position=b.entry-radial*b.finalDepth();in.observer.attitude=face(point-in.observer.position);
    plan=view();check(plan.stage==PlanStage::Extract,"Regrowth does not advance to extraction");layout(plan);
    check(issue(EconomicAction::ExtractCavern,b.id,1).starts_with("CAVERN EXTRACTED"),"Cannot extract planner sample");
    plan=view();check(plan.stage==PlanStage::Return&&!plan.returnToPort&&plan.primary==PlannerAction::Resume,"Planner flies a loaded ship through a cavern roof");layout(plan);
    check(plan.visual.sampleKg==10&&plan.visual.deposits[0].minedKg>=10,"Sample gauge does not follow extraction and real cargo");
    check(summarizePlan(plan).context.starts_with("10 / 10 KG"),"Compact return omits the real sample cargo");
    const auto heldSnapshot=snapshot();auto nextSite=choice;nextSite.newSite=true;const auto heldPlan=planCavernExpedition(u,&heldSnapshot,nextSite,in);
    check(heldPlan.visual.hasBore&&!heldPlan.returnToPort,"Planning a new site hides the loaded cavern or requests flight through its roof");
    in.observer.position=b.entry+radial*40.;plan=view();check(plan.stage==PlanStage::Return&&plan.returnToPort,"Surface exit does not route back to issuer");
    auto sold=snapshot();constexpr std::array goods{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon};sold.player.cargo[size_t(goods[find.resource])]=0;
    in.observer.position=b.entry-radial*b.finalDepth();
    check(planCavernExpedition(u,&sold,choice,in).stage==PlanStage::Extract,"Sold sample is still shown as ready for delivery");
    check(planCavernExpedition(u,&sold,choice,in).visual.sampleKg==0,"Visual sample gauge retains sold material");
    auto stale=snapshot();stale.exploration.probes.holes.back().finds[0].revision=job.acceptedRevision;
    check(planCavernExpedition(u,&stale,choice,in).stage==PlanStage::Inspect,"A pre-acceptance scan counts as fresh progress");
    in.observer.reference=home;in.observer.position=port.metres;in.connectedIssuer=true;
    plan=view();check(plan.stage==PlanStage::Deliver&&plan.primaryEnabled,"Sample cannot be delivered at the issuing port");layout(plan);
    auto full=snapshot();auto fullNetwork=std::make_shared<EconomicNetwork>(*net);fullNetwork->sites[0].capacityKg=0;full.network=fullNetwork;
    check(!planCavernExpedition(u,&full,choice,in).primaryEnabled,"Planner promises delivery into a full port");
    check(issue(EconomicAction::ClaimSurvey,job.id).starts_with("CAVERN ASSAY COMPLETE"),"Planner sample handover failed");
    plan=view();check(plan.stage==PlanStage::Complete&&!plan.tracking,"Paid contract still requests delivery");layout(plan);
    check(plan.debrief&&plan.accounting.recorded&&plan.accounting.equipment==gearCost&&plan.accounting.paid==job.reward,"Debrief does not use actual purchase and payout receipts");
    check(plan.accounting.probesUsed==1&&plan.accounting.extractedKg>=10,"Debrief loses launched probes or recovered cargo");
    check(plan.accounting.netCash()==job.reward-gearCost,"Debrief invents cash or omits a cost");
    check(plan.nextContract&&plan.primary==PlannerAction::Next,"Paid expedition has no suitable next offer");
    const auto offered=ledger.state().contracts.size();ledger.offer(u,economy);ledger.offer(u,economy);
    check(ledger.state().contracts.size()==offered,"Follow-up offers duplicate on polling");
    auto nextChoice=choice;nextChoice.contract=plan.nextContract;const auto nextSnapshot=snapshot();
    check(planCavernExpedition(u,&nextSnapshot,nextChoice,in).stage==PlanStage::Offer,"Next action silently accepts a contract instead of showing its offer");
    PlannerChoice restored;check(restored.restore(choice.serialize())&&restored.serialize()==choice.serialize(),"Planner settings lost across restart");
    PlannerChoice legacy;check(legacy.restore("INFINITE_PLANNER 1 0 0 80 1 0 0 0")&&!legacy.details,"Old planner settings do not migrate to the minimal view");
    auto detailed=choice;detailed.details=true;PlannerChoice replayChoice;check(replayChoice.restore(detailed.serialize())&&replayChoice.details,"Details preference does not persist");
    const auto saved=restored.serialize();check(!restored.restore(saved+" trailing")&&restored.serialize()==saved,"Malformed settings damage a valid planner choice");
    ExplorationLedger loaded;check(loaded.restore(ledger.serialize(),u),"Planned expedition ledger does not restore");
    auto replay=snapshot();replay.exploration=loaded.state();check(planCavernExpedition(u,&replay,restored,in).stage==PlanStage::Complete,"Restored planner invents a second progress state");
    check(descriptorFingerprint(u.describe(job.target))==identity,"Planner changed the target world");
    std::cout<<"PASS: "<<checks<<" planner lifecycle, readiness, atomic presets, layout, navigation safety and restart checks\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
