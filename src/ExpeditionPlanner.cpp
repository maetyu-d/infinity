#include "infinity/SaveTag.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace infinity {
std::string PlannerChoice::serialize() const {
    std::ostringstream out;
    out<<std::setprecision(17)<<"INFINITY_PLANNER 2 "<<contract<<' '<<bore<<' '<<depth<<' '<<feed<<' '<<bay<<' '<<tracking<<' '<<newSite<<' '<<details;
    return out.str();
}
bool PlannerChoice::restore(std::string_view text) {
    PlannerChoice next;std::istringstream in{std::string(text)};std::string tag;unsigned version,tracked,fresh;
    if(!(in>>tag>>version>>next.contract>>next.bore>>next.depth>>next.feed>>next.bay>>tracked>>fresh)||
       !matchesSaveTag(tag, "INFINITY_PLANNER")||(version!=1&&version!=2)||!std::isfinite(next.depth)||next.depth<40||next.depth>120||
       !std::isfinite(next.feed)||next.feed<.35||next.feed>1.5||next.bay>=maxWeaponMounts||tracked>1||fresh>1)return false;
    if(version==2){unsigned expanded;if(!(in>>expanded)||expanded>1)return false;next.details=expanded;}
    in>>std::ws;if(!in.eof())return false;
    next.tracking=tracked;next.newSite=fresh;*this=next;return true;
}
std::string_view planStageName(PlanStage s) {
    constexpr std::array names{"LOADING","SELECT CONTRACT","PREPARE","TRAVEL","DRILL","ENTER CAVERN","OVERVIEW",
        "CLOSE ASSAY","MARK MATERIAL","REGROW","EXTRACT SAMPLE","RETURN TO PORT","DELIVER SAMPLE","COMPLETE","CANCELLED"};
    return names[size_t(s)];
}
PlannerSummary summarizePlan(const ExpeditionPlan& plan) {
    PlannerSummary s;s.instruction=plan.instruction;
    if(plan.stage!=PlanStage::Loading&&plan.stage!=PlanStage::Offer&&plan.stage!=PlanStage::Cancelled)
        for(unsigned i=0;i<plan.steps.size();++i)if(plan.steps[i].active||plan.steps[i].done)s.step=i+1;
    if(plan.stage==PlanStage::Offer&&s.instruction.starts_with(plan.reward+". "))s.instruction.erase(0,plan.reward.size()+2);
    if(plan.stage==PlanStage::Travel)s.instruction=plan.worksite?"Approach the marked resource region and choose exposed, gentle ground.":"Approach exposed solid terrain on this world.";
    if(plan.stage==PlanStage::Prepare||plan.stage==PlanStage::Extract){
        const PlannerCheck* first=nullptr;
        for(const auto& check:plan.checks)if(check.required&&!check.ready){if(!first)first=&check;++s.blockers;}
        if(first){s.context="NEED / "+first->text;
            if(s.blockers>1)s.context+=" / +"+std::to_string(s.blockers-1)+" IN DETAILS";
            s.warning=true;}
    }
    const auto& v=plan.visual;
    if(s.context.empty()&&plan.stage==PlanStage::Drill&&v.hasBore&&!v.chamberReady){
        s.context=std::string(probePhaseName(v.phase))+" / "+std::to_string(int(std::lround(v.reached)))+" / "+std::to_string(int(std::lround(v.boreDepth)))+" M / TIP "+std::to_string(int(std::lround(v.heat*100)))+"%";
        s.warning=v.phase==ProbePhase::Paused||v.heat>.65;
    }
    if(s.context.empty()&&(plan.stage==PlanStage::Extract||plan.stage==PlanStage::Return||plan.stage==PlanStage::Deliver))
        s.context=std::to_string(int(v.sampleKg))+" / "+std::to_string(plan.requiredKg)+" KG SAMPLE ABOARD";
    if(s.context.empty()&&!plan.environmentalWarning.empty()&&(plan.stage==PlanStage::Travel||plan.stage==PlanStage::Drill)){
        s.context=plan.environmentalWarning;s.warning=true;
    }
    if(plan.stage==PlanStage::Complete&&!plan.debrief)s.context="SAMPLE DELIVERED / REWARD PAID";
    return s;
}
namespace {
glm::dvec3 eyeIn(const Universe& u,const SurveyObserver& o,const Address& planet) {
    return o.reference==planet?o.position:u.pointRelative(o.reference,
        o.position/std::pow(10.,u.describe(o.reference).frame.logRadius),planet,0);
}
}
ExpeditionPlan planCavernExpedition(const Universe& u,const EconomySnapshot* s,const PlannerChoice& choice,const PlannerInput& input) {
    ExpeditionPlan v;v.open=input.open;v.depth=choice.depth;v.feed=choice.feed;v.bay=choice.bay;v.details=choice.details;
    v.busy=input.pending||(s&&s->catchingUp);
    const std::array stepNames{"PREPARE EQUIPMENT AND CREW","REACH THE CONTRACT WORLD","DRILL AND BLAST A CAVERN","ENTER THE EXCAVATION",
        "SCAN A DEPOSIT","SHRINK FOR A CLOSE ASSAY","MARK THE MATERIAL","REGROW IN CLEAR SPACE","EXTRACT A 10 KG SAMPLE","RETURN AND DELIVER TO ISSUER"};
    for(size_t i=0;i<v.steps.size();++i)v.steps[i].text=stepNames[i];
    if(!s||!s->network||s->sites.size()!=s->network->sites.size()){v.objective="LOCAL CONTRACTS LOADING";v.instruction="The flight computer is finding the local survey office.";return v;}
    const auto& x=s->exploration;
    for(const auto& c:x.contracts)if(c.cavern)v.offers.push_back({c.id,c.name,c.phase});
    if(v.offers.empty()){v.stage=PlanStage::Offer;v.objective="NO CAVERN ASSAY OFFERS HERE";v.instruction="Visit an inhabited system with survey offices.";return v;}
    auto selected=std::find_if(v.offers.begin(),v.offers.end(),[&](const auto& c){return c.id==choice.contract;});
    if(selected==v.offers.end()) {
        selected=std::find_if(v.offers.begin(),v.offers.end(),[](const auto& c){return c.phase==SurveyPhase::Accepted;});
        if(selected==v.offers.end()){
            const auto supply=std::find_if(x.contracts.begin(),x.contracts.end(),[](const auto& c){return c.supplyKg&&c.phase==SurveyPhase::Open;});
            if(supply!=x.contracts.end())selected=std::find_if(v.offers.begin(),v.offers.end(),[&](const auto& c){return c.id==supply->id;});
        }
        if(selected==v.offers.end())selected=std::find_if(v.offers.begin(),v.offers.end(),[](const auto& c){return c.phase==SurveyPhase::Open;});
        if(selected==v.offers.end())selected=v.offers.begin();
    }
    v.selected=size_t(selected-v.offers.begin());v.firstOffer=choice.details?(v.selected>=4?v.selected-3:0):v.selected;v.contract=selected->id;
    const auto& job=*std::find_if(x.contracts.begin(),x.contracts.end(),[&](const auto& c){return c.id==v.contract;});
    v.world=job.target;v.issuer=job.issuer;
    v.requiredKg=job.supplyKg?job.supplyKg:10;
    const auto amount=std::to_string(v.requiredKg)+" KG";
    v.tracking=job.phase==SurveyPhase::Accepted&&(choice.tracking||choice.contract==0);
    v.title=u.describe(v.world).label();v.reward=std::to_string(job.reward/100)+" CR / 10 KG ASSAY SAMPLE";
    if(job.supplyKg){v.title="LOCAL "+std::string(resourceName(job.supplyResource))+" SUPPLY";
        v.reward=std::to_string(job.reward/100)+" CR / "+amount+" "+std::string(resourceName(job.supplyResource));
        v.steps[8].text="MINE THE REQUESTED MATERIAL";
    }
    const auto p=u.describe(v.world);const bool solid=hasSolidSurface(p);
    v.geology=solid?std::string(processName(p.geology->type)):"NO SOLID LANDING SURFACE";
    auto chemistry=p.environment.chemistry;chemistry.normalize();
    auto& visual=v.visual;visual.known=true;visual.solid=solid;visual.shipMetres=input.observer.size*12;
    visual.tint=material(p.environment.mineral).color;visual.boreDepth=choice.depth;
    visual.geology=p.geology?p.geology->type:GeologyType::GasEnvelope;
    visual.composition={chemistry.ice,chemistry.metal,chemistry.silicate,chemistry.carbon};
    v.composition="BULK / ORE "+std::to_string(int(std::lround(chemistry.metal*100)))+"% / SILICATE "+std::to_string(int(std::lround(chemistry.silicate*100)))+"%";
    v.conditions=std::to_string(int(p.environment.temperature))+" K / "+std::string(material(p.environment.mineral).name);
    v.forecastNote="ESTIMATE / LOCAL SCAN REVEALS DEPOSITS";
    if(job.supplySite){const auto direction=u.describe(*job.supplySite).geographicDirection;
        v.worksite=direction*(std::pow(10.,p.frame.logRadius)+u.terrainHeight(p,direction));
        const auto weather=sampleWeather(planetClimate(p),direction,0,input.observer.seconds);
        const auto hazard=surfaceConditions(p,direction,0,weather);
        v.environmentalWarning=hazard.label;
        v.forecastNote=hazard.label.empty()?"LOCAL FORECAST CLEAR / ASSAY TO FIND THE REQUESTED MATERIAL":hazard.label+" / "+hazard.action;
    }
    v.suggestedDepth=solid?(p.geology->type==GeologyType::IceShell?60:p.geology->activity>.6?70:90):80;
    v.suggestedFeed=std::clamp(1.05-material(p.environment.mineral).density/20000.,.65,1.);
    const auto issuer=s->siteIndex(job.issuer);
    v.issuerName=issuer?s->network->sites[*issuer].name:"ISSUER WORLD / "+u.describe(job.issuer.home).label();
    auto stage=[&](PlanStage value,std::string objective,std::string instruction,PlannerAction action,std::string label,size_t index) {
        v.stage=value;v.objective=std::move(objective);v.instruction=std::move(instruction);v.primary=action;v.primaryLabel=std::move(label);
        v.primaryEnabled=!v.busy;for(size_t i=0;i<v.steps.size();++i){v.steps[i].done=i<index;v.steps[i].active=i==index;}
        v.compact="PLAN "+std::to_string(std::min(index+1,size_t(10)))+"/10 / "+std::string(planStageName(value))+" / F3 DETAILS";
    };
    const auto qualified=cavernAssaySample(x,job,s->player);
    const Borehole* bore=nullptr;double score=-1e30;
    for(const auto& b:x.probes.holes)if(supplyBoreMatches(job,b)&&(b.geologyVersion||probeActive(b,input.observer.seconds))) {
        const bool active=probeActive(b,input.observer.seconds);
        if(choice.newSite&&!active&&(!qualified||qualified->bore!=b.id))continue;
        double candidate=double(b.started)*1e-6;
        for(const auto& record:b.finds)candidate=std::max(candidate,double(record.grade*10+record.marked*20)+(record.revision>job.acceptedRevision?100:0)+(record.minedRevision>job.acceptedRevision?200:0));
        if(active)candidate+=500;
        if(b.id==choice.bore)candidate+=10000;
        if(qualified&&qualified->bore==b.id)candidate+=20000;
        if(candidate>score){score=candidate;bore=&b;}
    }
    bool inside=false,completed=false;glm::dvec3 eye{};
    if(bore){v.bore=bore->id;v.entrance=bore->entry;completed=input.observer.seconds>=bore->blastTime();
        eye=eyeIn(u,input.observer,bore->planet);
        inside=completed&&glm::length(eye-bore->entry)<bore->finalDepth()+bore->roomRadius()+30&&boreClearance(*bore,eye,input.observer.seconds)>0;
        double best=-1e30;
        for(const auto& f:cavernFinds(*bore)){if(job.supplyKg&&f.resource!=job.supplyResource)continue;const auto& r=bore->finds[f.index];
            const double rank=r.grade+(r.revision>job.acceptedRevision?10:0)+(r.marked?20:0)+(r.minedRevision>job.acceptedRevision?40:0);
            if(rank>best){best=rank;v.find=f.index;}}
        if(qualified&&qualified->bore==bore->id)v.find=qualified->find;
        const auto operation=probeOperation(*bore,input.observer.seconds);
        visual.hasBore=true;visual.chamberReady=completed;visual.boreDepth=completed?bore->finalDepth():operation.target;
        visual.reached=operation.reached;visual.chamberRadius=bore->roomRadius();visual.heat=operation.heat;visual.phase=operation.phase;
        const auto delta=eye-bore->entry;const auto local=glm::inverse(tangentFrame(glm::normalize(bore->entry)))*delta;
        visual.shipDepth=-local.y;visual.shipLateral=local.x;
        visual.shipLocal=std::abs(local.x)<150&&std::abs(local.z)<150&&local.y>-180&&local.y<60;
        constexpr std::array goods{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon};
        for(const auto& f:cavernFinds(*bore)){
            const auto& r=bore->finds[f.index];visual.deposits[f.index]={{f.local.x,-f.local.y},f.mineral,r.grade,r.marked,r.minedKg};
            if(r.marked&&r.grade>=job.grade&&r.revision>job.acceptedRevision&&r.minedRevision>job.acceptedRevision)
                if(!job.supplyKg||f.resource==job.supplyResource)visual.sampleKg=std::max(visual.sampleKg,double(std::min({v.requiredKg,int64_t(r.minedKg),s->player.availableCargo(goods[f.resource])})));
        }
    }
    const bool needProbe=!bore||(!completed&&!probeActive(*bore,input.observer.seconds));
    const auto bay=std::min(choice.bay,maxWeaponMounts-1);v.bay=bay;
    auto readiness=[&](unsigned mount){
        double ready=mount==input.captainMount?1.:input.crew?input.crew->gunnerReadiness(mount):0.;
        if(input.crew){ready*=input.crew->systemHealth(ShipSystem(unsigned(ShipSystem::NoseGun)+mount))/100.;
            const auto& room=input.crew->rooms()[size_t(CrewStation::Weapons)];
            if(room.fire>=25||room.oxygen<18||room.integrity<=0)ready=0;}
        return ready;
    };
    for(unsigned i=0;i<maxWeaponMounts;++i){auto& station=visual.bays[i];station.captain=i==input.captainMount;station.operatorReadiness=readiness(i);station.rounds=x.probes.rounds[i];
        if(x.probes.launchers[i]){station.tool=PlannerTool::Probe;station.level=1;}
        else if(x.miningLevel&&x.miningMount==i){station.tool=PlannerTool::Miner;station.level=x.miningLevel;}
        else if(const auto level=s->player.weaponAt(i)){station.tool=PlannerTool::Gun;station.level=level;}}
    const bool launcher=!needProbe||x.probes.launchers[bay],rounds=!needProbe||x.probes.rounds[bay]>0;
    const bool miner=x.miningLevel&&x.miningMount<maxWeaponMounts;
    const bool operators=(!needProbe||readiness(bay)>=.25)&&(!miner||readiness(x.miningMount)>0);
    const int64_t litres=job.supplyKg?job.supplyKg*goodInfo(std::array{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon}[job.supplyResource]).litresPerKg:20;
    const bool space=qualified||(s->player.capacityKg-s->player.massKg()>=v.requiredKg&&s->player.capacityLitres-s->player.volumeLitres()>=litres);
    const bool hull=input.observer.hull>0&&input.observer.size>=.99;
    v.checks={{"LAUNCHER / "+std::string(weaponMountName(bay)),launcher,true},{"PROBE AMMUNITION / "+std::to_string(x.probes.rounds[bay]),rounds,true},
        {miner?"MINER / "+std::string(weaponMountName(x.miningMount)):"MINING HEAD / NOT FITTED",miner,true},
        {"TOOL OPERATORS / F9 ASSIGN BAYS",operators,true},{"HOLD / "+amount+" AND "+std::to_string(litres)+" L FREE",space,true},
        {"FULL SIZE / OPERATIONAL HULL",hull,true},
        {"NAVIGATION / ENGINEERING CREW",input.crew&&input.crew->efficiency(CrewStation::Navigation)>=.2&&input.crew->efficiency(CrewStation::Engineering)>=.2,false}};
    const bool prepared=launcher&&rounds&&miner&&operators&&space&&hull;
    if(job.phase==SurveyPhase::Complete){visual.sampleKg=v.requiredKg;v.accounting=job.accounting;v.debrief=true;
        for(const auto& candidate:x.contracts)if(candidate.cavern&&candidate.phase==SurveyPhase::Open){
            if(!v.nextContract||candidate.previous==job.id)v.nextContract=candidate.id;
            if(candidate.previous==job.id)break;}
        stage(PlanStage::Complete,"SAMPLE DELIVERED / REWARD PAID",v.nextContract?"Assay delivered. Review the next funded expedition when ready.":"Assay delivered. Visit another survey office for a new expedition.",
              v.nextContract?PlannerAction::Next:PlannerAction::Resume,v.nextContract?"NEXT ASSAY":"RESUME FLIGHT",10);
        if(job.supplyKg){v.objective="SUPPLY DELIVERED / PLANT STOCK REPLENISHED";v.instruction="F2 Outcomes tracks real production after delivery. Weather, power or other missing inputs can still hold the plant.";}
        return v;}
    if(job.phase==SurveyPhase::Cancelled){stage(PlanStage::Cancelled,"CONTRACT CANCELLED","Reserved funds were returned to its issuer.",PlannerAction::Resume,"RESUME FLIGHT",0);return v;}
    const auto jobs=explorationJobs(*s);
    v.equipmentMount=bay;v.equipmentRow=3+jobs.size();
    if(launcher&&!rounds)v.equipmentRow+=1;
    if(launcher&&rounds&&!miner){v.equipmentRow=1;
        bool empty=false;for(unsigned i=0;i<maxWeaponMounts;++i)if(!x.probes.launchers[i]&&!s->player.weaponAt(i)){v.equipmentMount=i;empty=true;break;}
        if(!empty){v.equipmentPage=MarketPage::Weapons;v.equipmentRow=0;}}
    if(job.phase==SurveyPhase::Open){stage(PlanStage::Offer,job.supplyKg?"SUPPLY A REAL LOCAL SHORTAGE":"REVIEW AND ACCEPT THIS ASSAY",v.reward+
        (job.supplyKg?". Survey and mine fresh material in the generated region, then deliver it to this settlement.":". Inspect and mine a fresh sample after accepting."),PlannerAction::Accept,"ACCEPT CONTRACT",0);
        v.primaryEnabled=!v.busy&&issuer&&s->sites[*issuer].cash>=job.reward&&solid;
        if(!issuer)v.message="Return to the issuer's system to reserve this reward.";
        else if(s->sites[*issuer].cash<job.reward)v.message="Issuer needs funds / no reward has been reserved.";
        return v;}
    if(qualified){v.bore=qualified->bore;v.find=qualified->find;v.returnToPort=!inside;
        if(inside){stage(PlanStage::Return,"EXIT WITH YOUR ASSAY SAMPLE","Fly manually through the open shaft. Ctrl-J shows exit guidance; then return to the issuer.",PlannerAction::Resume,"RESUME / CTRL-J EXIT",9);return v;}
        if(input.connectedIssuer){stage(PlanStage::Deliver,"HAND OVER THE MINED MATERIAL","Transfer "+amount+" to the issuer and claim the reserved reward.",PlannerAction::Deliver,"DELIVER MATERIAL",9);
            const bool room=issuer&&std::accumulate(s->sites[*issuer].stock.begin(),s->sites[*issuer].stock.end(),int64_t(0))+v.requiredKg<=economicCapacity(s->network->sites[*issuer],s->sites[*issuer]);
            v.primaryEnabled=!v.busy&&room&&s->player.credits<=1000000000000000LL-job.escrow;
            if(!room)v.message="Port hold full / sample and reserved reward retained.";
        }else stage(PlanStage::Return,"RETURN WITH YOUR "+amount+" MATERIAL","Approach the issuing port, stop and dock or land. F4 sets the route.",PlannerAction::Course,"RETURN TO ISSUER",9);
        return v;}
    if(bore&&probeActive(*bore,input.observer.seconds)){const auto operation=probeOperation(*bore,input.observer.seconds);
        stage(PlanStage::Drill,std::string(probePhaseName(operation.phase)),"Alt-P opens the drill camera. Pause to cool, adjust the cut, or abort safely.",PlannerAction::Resume,"RESUME / ALT-P CAMERA",2);return v;}
    if(!inside&&(!prepared||!solid)){
        const auto action=!launcher||!rounds||!miner?PlannerAction::Equipment:!operators?PlannerAction::Crew:!space?PlannerAction::Equipment:PlannerAction::Resume;
        if(launcher&&rounds&&miner&&operators&&!space){v.equipmentPage=MarketPage::Goods;v.equipmentRow=0;}
        stage(PlanStage::Prepare,"PREPARE FOR THE EXPEDITION",!solid?"This target has no solid terrain.":!launcher?"Fit a probe launcher at the selected bay.":!rounds?"Reload probe ammunition at an equipment port.":!miner?"Fit a mining head in another empty bay.":!operators?"Assign a ready crew member to each auxiliary tool bay in F9.":!space?"Make room for "+amount+" before departure.":"Restore full size and an operational hull before departure.",
            action,action==PlannerAction::Crew?"PREPARE CREW":action==PlannerAction::Resume?"RESUME / RESTORE SHIP":!space&&launcher&&rounds&&miner?"OPEN CARGO MARKET":"OPEN EQUIPMENT",0);return v;}
    if(!completed){const auto local=eyeIn(u,input.observer,v.world);const double r=std::pow(10.,p.frame.logRadius),distance=glm::length(local);
        const bool region=!job.supplySite||(distance>1&&encodeTile(tileAt(glm::normalize(local),7))==job.supplySite->id());
        const bool nearby=distance>r*.9&&distance<r+1400000&&region;
        if(nearby)stage(PlanStage::Drill,bore?"DRILL ABORTED / CHOOSE CLEAR LAND":"FACE EXPOSED SOLID LAND","Ctrl-P launches with your planned depth and feed. Gas, water and occupied excavations are rejected.",PlannerAction::Resume,"RESUME / CTRL-P LAUNCH",2);
        else stage(PlanStage::Travel,"REACH "+v.title,job.supplyKg?"F4 targets the generated resource region. Check the forecast, approach and land on gentle exposed ground.":"F4 sets a course to the contract world. Approach exposed solid terrain.",PlannerAction::Course,job.supplyKg?"SET COURSE TO RESOURCE REGION":"SET COURSE TO WORLD",1);
        return v;}
    if(!inside){stage(PlanStage::Enter,"ENTER THE COMPLETED EXCAVATION","F4 approaches this saved entrance. Fly carefully through the open shaft.",PlannerAction::Course,"APPROACH ENTRANCE",3);return v;}
    const auto& record=bore->finds[v.find];const bool fresh=record.revision>job.acceptedRevision;
    if(!record.grade)stage(PlanStage::Overview,"SCAN AN EXPOSED DEPOSIT","Shift-F12 selects deposits. Face one and hold F12 for an overview.",PlannerAction::Resume,"RESUME / HOLD F12",4);
    else if(record.grade<job.grade||!fresh)stage(PlanStage::Inspect,"SHRINK FOR A FRESH CLOSE ASSAY","Shrink below 12 mm, stop near the selected face and hold F12. Magnification alone does not qualify.",PlannerAction::Resume,"RESUME / Z SHRINK",5);
    else if(!record.marked)stage(PlanStage::Mark,"MARK THE ASSAYED MATERIAL","While tiny and stopped near the face, press Ctrl-F12 to mark it.",PlannerAction::Resume,"RESUME / CTRL-F12 MARK",6);
    else if(input.observer.size<.99)stage(PlanStage::Regrow,"RETURN TO THE OPEN CHAMBER CENTRE","Move clear of rock and crystals, then Shift-Z restores full size. Tiny ships remain fragile.",PlannerAction::Resume,"RESUME / SHIFT-Z GROW",7);
    else stage(PlanStage::Extract,"EXTRACT FRESH "+amount+" MATERIAL",!miner?"Fit a mining head before extraction.":!operators?"Staff the mining head in F9.":"Stop at full size and hold Ctrl-Space with a clear cutter line. Keep "+amount+" aboard for delivery.",
        !miner?PlannerAction::Equipment:!operators?PlannerAction::Crew:PlannerAction::Resume,!miner?"OPEN EQUIPMENT":!operators?"PREPARE CREW":"RESUME / CTRL-SPACE",8);
    if(bore&&record.grade){const auto find=cavernFinds(*bore)[v.find];
        v.forecastNote="LOCAL ASSAY / "+cavernFindName(find);
        if(v.stage==PlanStage::Extract){const auto pool=bore->planet.child(Kind::Surface,encodeTile(tileAt(glm::normalize(bore->entry),7)));
            for(const auto& reserve:x.prospects)if(reserve.address==pool&&reserve.capacity[find.resource]==reserve.extracted[find.resource]){
                v.objective="MARKED MATERIAL IS DEPLETED";v.instruction="B selects another cavern. N plans a new site in another region; regional reserves are shared.";
                v.primary=PlannerAction::Resume;v.primaryLabel="RESUME / SELECT NEW SITE";}
        }
    }
    return v;
}
void cyclePlannerContract(PlannerChoice& choice,const ExpeditionPlan& v,int direction) {
    if(v.offers.empty())return;
    const auto i=(v.selected+v.offers.size()+(direction<0?v.offers.size()-1:1))%v.offers.size();
    choice.contract=v.offers[i].id;choice.bore=0;choice.newSite=false;choice.tracking=v.offers[i].phase==SurveyPhase::Accepted;
}
void cyclePlannerBore(PlannerChoice& choice,const ExpeditionPlan& v,const ExplorationState& s) {
    std::vector<uint64_t> ids;for(const auto& b:s.probes.holes)if(b.planet==v.world&&b.geologyVersion)ids.push_back(b.id);
    if(ids.empty())return;
    auto it=std::find(ids.begin(),ids.end(),v.bore);choice.bore=it==ids.end()||++it==ids.end()?ids.front():*it;choice.newSite=false;
}
} // namespace infinity
