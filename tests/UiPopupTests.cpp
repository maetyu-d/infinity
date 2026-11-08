#include "infinity/Cockpit.hpp"
#include "infinity/Economy.hpp"
#include <iostream>
#include <stdexcept>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void validate(const CockpitOverlay& h,double width,std::string_view mode){
    const auto conflicts=cockpitTextConflicts(h);
    if(!conflicts.empty()){
        const auto [a,b]=conflicts.front();
        const auto& x=h.textRuns[a];const auto& y=h.textRuns[b];
        std::cerr<<mode<<" / width "<<width<<" / "<<x.value<<" ("<<x.bounds.x<<','<<x.bounds.y<<") overlaps "
                 <<y.value<<" ("<<y.bounds.x<<','<<y.bounds.y<<")\n";
    }
    check(conflicts.empty(),"HUD text runs overlap");
    for(const auto& run:h.textRuns){
        check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=width+.01&&run.bounds.y+run.bounds.h<=900.01,
              "HUD text escapes its canvas");
        check(run.firstVertex+run.vertexCount<=h.lettering.vertices.size()&&run.firstIndex+run.indexCount<=h.lettering.indexCount(),
              "Modal occlusion corrupts a glyph range");
        check(std::all_of(h.lettering.indices.begin()+run.firstIndex,h.lettering.indices.begin()+run.firstIndex+run.indexCount,
              [&](auto i){return i>=run.firstVertex&&i<run.firstVertex+run.vertexCount;}),"Modal occlusion corrupts glyph indices");
    }
    const CockpitViewport viewport(int(std::lround(width)),900,{0,0,width,900});
    Mesh markers;
    for(const auto r:h.hitRegions){
        const auto p=viewport.toPixels({r.x+r.w*.5,r.y+r.h*.5});
        addLine(markers,{p.x-1,p.y,0},{p.x+1,p.y,0},{1,1,1});
    }
    const auto p=viewport.toPixels({5,450});
    addLine(markers,{p.x,p.y,0},{p.x,p.y+1,0},{1,1,1});
    maskCockpitMarkers(markers,viewport,h);
    check(markers.vertices.size()==2&&markers.indices.size()==2&&markers.indices[0]==0&&markers.indices[1]==1,
          "World markers cross a popup or modal panel, or unobstructed markers disappear");
    for(size_t i=0;i<h.popups.size();++i){
        for(size_t j=0;j<i;++j)check(!h.popups[i].bounds.overlaps(h.popups[j].bounds),"Popups overlap");
        for(const auto& r:h.worldLabels)check(!r.overlaps(h.popups[i].bounds),"World label overlaps popup");
    }
}
}
int main(){try{
    ShipCrew shipCrew;auto crew=shipCrew.view(0,CrewStation::Bridge);
    crew.members.resize(maxShipCrew);
    crew.summary="CREW EMERGENCY / ENGINE DAMAGE / RELIEF HELM MOVING";
    crew.alert="ENGINE ROOM FIRE / REPAIRS IN PROGRESS";crew.message="DAMAGE CONTROL ORDER CONFIRMED";
    crew.selectedSkill="BRIDGE 100 / NAVIGATION 100 / WEAPONS 100 / ENGINEERING 100";
    for(auto& member:crew.members)member.name="LIEUTENANT VERY LONG SURNAME";
    crew.tasks.resize(crew.members.size());for(auto& task:crew.tasks){task.status="REPAIRING";task.detail="MAINTAINING LIFE SUPPORT";}
    const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9);
    std::vector<CelestialContact> worlds(20);std::vector<ActivityContact> traffic(20);
    for(size_t i=0;i<worlds.size();++i){auto& c=worlds[i];c.address=system.child(Kind::Planet,i);c.label="INHABITED WORLD WITH A LONG NAME";
        c.localDirection=glm::normalize(glm::dvec3((int(i%5)-2)*.1,.1,-1));c.distanceMetres=10000+i;c.life=LifeDetection::Detected;c.population=9999999;}
    for(size_t i=0;i<traffic.size();++i){auto& c=traffic[i];c.body.id={worlds[0].address,uint32_t(i)};c.body.name="ORBITAL HARBOUR WITH A LONG NAME";
        c.body.kind=ActivityKind::Harbour;c.body.status="BERTHS OPEN";c.distance=2000+i;c.localDirection=worlds[i].localDirection;
        c.body.economicBrief="POWER CRITICAL / REQUESTING DELIVERIES";c.body.economicReport={"GOODS / MACHINERY / ELECTRONICS","FUNDED WAGES / AVAILABLE","DOCKING CREW ON DUTY"};}
    FlightInstruments original;original.weapon="MINING PROBE LAUNCHER";original.weaponStatus="READY / SIX PROBES";
    original.explorationActive=true;original.exploration={"MINING PROBE / NOSE","SIX ROUNDS / CTRL-P LAUNCH","SOLID TERRAIN / SAVED CAVERN","ALT-P CAMERA / CTRL-J ENTRANCE"};
    original.altitude=35;original.gear=true;original.descent=100;original.stoppingDistance=5000;original.growthBlocked=true;
    original.targetVisible=true;original.localTarget={0,0,-1};original.range=4000;
    original.crewFire="GUNNERS HOLD FIRE / TARGET HAS NEUTRAL SHIPS NEARBY";
    original.weather="HEAVY WEATHER / WIND 120M/S / VISIBILITY 500M";original.weatherDetail="DUST STORM / LIGHTNING";
    const std::string notice="TRANSACTION COMPLETE / MINING PROBE LAUNCHER FITTED / SIX ROUNDS AVAILABLE / NEW EQUIPMENT READY";
    for(double aspect:{1.,1.2,4./3,16./9,2.4}){
        for(bool expanded:{false,true})for(int scenario=0;scenario<11;++scenario){auto t=original;t.contactsExpanded=expanded;auto c=crew;MarketView market;const MarketView* m=nullptr;
            std::string mode=std::to_string(scenario);bool help=scenario==5;
            if(scenario==1){t.stationStatus="CAPTAIN AT PORT GUN / CREW RELIEF PILOT ACTIVE";t.playerGun=1;}
            if(scenario==2)c.flightOpen=true;
            if(scenario==3)c.open=true;
            if(scenario==4){market.open=market.connected=market.ready=true;market.page=MarketPage::Survey;market.site="SURFACE EQUIPMENT PORT";
                for(int j=0;j<16;++j)market.detail.push_back("SCANNER OR MINING PROBE / LONG EQUIPMENT DESCRIPTION");
                market.message="EQUIPMENT READY / TRANSACTION COMPLETE";market.summary="CARGO FULL / CREW READY";m=&market;}
            if(scenario==6){t.phase="MINING PROBE CAMERA";t.exploration={"BORING / CUTTER ACTIVE","DEPTH 80M / TARGET 80M","CUTTER 100 RPM / LOAD 80% / TIP HEAT 50%","ESC RETURNS TO SHIP / ALT-P CAMERA"};}
            if(scenario==7){t.shrinking=true;t.hullMetres=1e-11;t.logMetres=-11;}
            if(scenario==8||scenario==9){t.explorationActive=false;t.docking.active=true;t.docking.available=true;t.docking.range=25;
                t.docking.phase=scenario==8?DockingPhase::Glide:DockingPhase::Docked;t.docking.docked=scenario==9;}
            if(scenario==10){t.explorationActive=false;t.relativeMotion=true;t.properG=27;t.incomingFire=1;
                t.combatMission="INTERCEPT HOSTILE / DEFEND THE INHABITED COLONY";t.combatStatus="INCOMING FIRE / SHIELDS DOWN";}
            const auto h=cockpitOverlay(t,aspect,"HYPERION",notice,worlds,traffic,help,1,m,{1,0,0,0},&c);
            validate(h,900*aspect,mode);
            if(scenario==0||scenario==1||scenario==2){
                check(h.popups.size()==1&&h.popups.front().critical,"Critical warning and receipt must share one readable alert channel");
                check(std::any_of(h.textRuns.begin(),h.textRuns.end(),[](const auto& r){return r.value.starts_with("TRANSACTION");}),"Station status hides a new notification");
            }
        }
        for(auto page:{MarketPage::Goods,MarketPage::Routes,MarketPage::Industry,MarketPage::Jobs,MarketPage::Services,MarketPage::Outcomes,MarketPage::Weapons,MarketPage::Crew,MarketPage::Journal,MarketPage::Expeditions}){
            MarketView m;m.open=m.connected=m.ready=true;m.page=page;m.selected=2;m.message="ORDER COMPLETE";m.bottleneck="WAITING FOR PORT CREW";
            m.summary="AVAILABLE GOODS AND SERVICES";m.site="EQUIPMENT PORT";m.role="SURFACE COLONY";
            for(auto& r:m.rows)r="GOOD / STOCK 10000 / FREE 1000 / BUY 50000 / SELL 40000";
            for(int i=0;i<(page==MarketPage::Outcomes||page==MarketPage::Journal?11:page==MarketPage::Expeditions?16:80);++i)m.detail.push_back("ROW "+std::to_string(i)+" / SELECTED SERVICE OR REPORT / STORED RESULT");
            m.selected=14;m.plan="HAUL PLANNED / DELIVER TO ORBITAL HARBOUR";m.planTiming="ETA 1:20 / DEADLINE 5:00";
            validate(cockpitOverlay(original,aspect,"HYPERION",notice,worlds,traffic,false,1,&m),900*aspect,"market");
            if(page==MarketPage::Jobs||page==MarketPage::Industry||page==MarketPage::Services||page==MarketPage::Weapons||page==MarketPage::Routes){
                const auto h=cockpitOverlay(original,aspect,"HYPERION",notice,worlds,traffic,false,1,&m);
                const size_t selectedRow=m.selected*(page==MarketPage::Jobs||page==MarketPage::Routes?2:5);
                check(std::any_of(h.textRuns.begin(),h.textRuns.end(),[&](const auto& r){return r.value.starts_with("ROW "+std::to_string(selectedRow)+" /");}),"Long table hides the selected offer");
            }
        }
    }
    std::cout<<"PASS: "<<checks<<" popup, modal masking, text bounds and collision checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
