#include "infinity/CockpitContext.hpp"
#include "infinity/SurfaceRecon.hpp"
#include "infinity/OrbitalDebris.hpp"
#include <algorithm>
#include <cctype>

namespace infinity {
CockpitFocus cockpitFocus(const FlightInstruments& t){
    CockpitFocus f;f.warp=t.warp!=WarpPhase::Off;f.micro=t.shrinking;
    const double size=std::max(1e-12,t.hullMetres/12.);
    f.nearGround=t.altitude>=0&&t.altitude<2500*size;
    const bool passiveProspect=t.exploration[0].starts_with("GEOLOGICAL PROSPECT");
    const bool travellingJob=t.exploration[0].starts_with("COLONY MATERIAL EXPEDITION")&&!f.nearGround&&!t.landed;
    if(t.phase=="MINING PROBE CAMERA"){f.mode=CockpitMode::Probe;f.title="PROBE CAMERA";f.nextAction="ALT-P RETURN TO SHIP";}
    else if(f.warp){f.mode=CockpitMode::Travel;
        f.title=t.warp==WarpPhase::Manual?"MANUAL WARP":t.warp==WarpPhase::Route?"GUIDED WARP":"WARP EXIT";
        f.nextAction=t.warp==WarpPhase::Drop?"B TAKE HELM":"N DROP / B ABORT";}
    else if(t.docking.active||t.docking.docked||(t.docking.available&&!t.shrinking&&t.docking.range<3000)){
        f.mode=CockpitMode::Docking;f.title=t.docking.docked?"BERTH SECURE":"DOCKING";
        f.nextAction=t.docking.docked?"ENTER UNDOCK / F2 SERVICES":t.docking.active?"B TAKE HELM":"J DOCK / L CAPTURE";}
    else if((t.hostileContacts&&!t.shrinking)||t.incomingFire>.02||glm::length(t.localMissile)>.5||t.playerGun<maxWeaponMounts||t.weaponHeat>5){
        f.mode=CockpitMode::Combat;f.title=t.playerGun<maxWeaponMounts?"WEAPON STATION":"COMBAT";f.nextAction="SPACE FIRE / H HOSTILE";}
    else if(t.landing.phase!=LandingPhase::None&&(t.landing.phase!=LandingPhase::Landed||t.landing.settle>0)){
        f.mode=CockpitMode::Landing;f.title=t.landed?"TOUCHDOWN":"LANDING";f.nextAction=t.landed?"ENTER LAUNCH":"B TAKE HELM";}
    else if((t.explorationActive&&!passiveProspect&&!travellingJob)||t.phase=="CAVERN EXPLORATION"||t.phase=="SPECIMEN INSPECTION"||t.shrinking||
            (t.recon&&t.recon->open)||(t.landed&&t.landing.settle<=0)){
        f.mode=CockpitMode::Surface;f.title=t.phase=="CAVERN EXPLORATION"?"CAVERN EXPLORATION":t.phase=="SPECIMEN INSPECTION"?"SPECIMEN INSPECTION":t.shrinking?"MICRO FLIGHT":"SURFACE WORK";
        f.nextAction=t.explorationActive?"F12 SCAN / CTRL-SPACE MINE":t.shrinking?"SHIFT-Z RESTORE SIZE":t.landed?"ENTER LAUNCH / F5 MAP":"F5 MAP / F12 SCAN";}
    else if(t.landing.phase!=LandingPhase::None||(f.nearGround&&t.gear)){
        f.mode=CockpitMode::Landing;f.title=t.landed?"TOUCHDOWN":"LANDING";
        f.nextAction=t.landed?"ENTER LAUNCH":t.landing.phase==LandingPhase::Approach||t.landing.phase==LandingPhase::Align||t.landing.phase==LandingPhase::Flare?"B TAKE HELM":"L LAND / G GEAR";}
    else if(t.navigating||(t.altitude>=0&&t.altitude<50000)){
        f.mode=CockpitMode::Approach;f.title="APPROACH";f.nextAction=t.navigating?"B TAKE HELM":t.altitude>=0?"J APPROACH / L LAND":"J APPROACH";}
    else {f.title="FLIGHT";f.nextAction=t.targetVisible?"J APPROACH / CTRL-N WARP":"Y WORLDS / K TRAFFIC";}
    return f;
}
std::string cockpitDestination(const FlightInstruments& t){
    if(!t.targetVisible&&!t.navigating&&!t.warpDestination)return "NO DESTINATION";
    auto name=t.targetLabel.empty()?t.destination:t.targetLabel;
    for(const auto suffix:{" / J "," / F4 "," / J"," / F4"})if(const auto pos=name.find(suffix);pos!=std::string::npos)name.resize(pos);
    return name.empty()?"SELECTED DESTINATION":name;
}
std::vector<CockpitContactRow> cockpitSummaryRows(std::span<const CelestialContact> worlds,std::span<const ActivityContact> actors){
    struct Candidate {CockpitContactRow row;int priority;double range;};std::vector<Candidate> candidates;
    candidates.reserve(worlds.size()+actors.size());
    for(size_t i=0;i<actors.size();++i){const auto& a=actors[i];
        if(!a.selected&&(a.occluded||isOrbitalDebris(a.body.id)))continue;
        candidates.push_back({{true,i},a.selected?0:a.body.hostile?1:3,a.distance});}
    for(size_t i=0;i<worlds.size();++i){const auto& w=worlds[i];
        if(!w.selected&&(w.occluded||!hasSurfaceLife(w)))continue;
        candidates.push_back({{false,i},w.selected?0:2,w.distanceMetres});}
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.priority!=b.priority?a.priority<b.priority:a.range<b.range;});
    std::vector<CockpitContactRow> result;for(size_t i=0;i<std::min(size_t(3),candidates.size());++i)result.push_back(candidates[i].row);
    return result;
}
std::vector<CockpitAlert> cockpitAlerts(const FlightInstruments& t,const CrewView* crew,std::string_view notice){
    std::vector<CockpitAlert> alerts;
    const auto add=[&](std::string key,std::string title,std::string action,CockpitAlertLevel level,int priority,std::string detail={}){
        if(title.empty())return;
        if(std::any_of(alerts.begin(),alerts.end(),[&](const auto& a){return a.key==key||a.title==title||(!detail.empty()&&a.detail==detail);}))return;
        alerts.push_back({std::move(key),std::move(title),std::move(action),std::move(detail),level,priority});
    };
    const auto critical=CockpitAlertLevel::Critical,warning=CockpitAlertLevel::Warning,info=CockpitAlertLevel::Information;
    if(glm::length(t.localMissile)>.5)add("missile","INCOMING MISSILE","TURN CLEAR / EVADE",critical,100);
    if(t.hull<25)add("hull",t.hull<=0?"HULL LOST":"HULL CRITICAL / "+std::to_string(int(t.hull))+"%","F9 DAMAGE CONTROL / DISENGAGE",critical,96);
    if(t.incomingFire>.2)add("fire","TAKING FIRE","MANOEUVRE / F9 DAMAGE CONTROL",critical,95);
    const double size=std::max(1e-12,t.hullMetres/12.);
    const bool terrain=t.altitude>=0&&t.descent>2*size&&t.stoppingDistance>t.altitude&&
        (t.altitude<5000*size||t.altitude/std::max(1e-30,t.descent)<15);
    if(terrain)add("terrain","BRAKE / TERRAIN APPROACH","B RECOVER",critical,94);
    if(t.growthBlocked)add("growth","GROWTH BLOCKED","MOVE CLEAR BEFORE GROWING",critical,92);
    if(t.surfaceSeverity>=.15&&!t.surfaceWarning.empty())add("environment",t.surfaceWarning,t.surfaceAction+(t.surfaceLandingHold?" / SHIFT-L ACCEPT RISK":""),
        t.surfaceSeverity>=.85?critical:t.surfaceSeverity>=.6?warning:CockpitAlertLevel::Advisory,90);
    if(crew){
        for(size_t i=0;i<crew->rooms.size();++i){const auto& room=crew->rooms[i];
            const bool occupied=std::any_of(crew->members.begin(),crew->members.end(),[&](const auto& m){return m.room==CrewStation(i)&&m.health>0;});
            if(room.fire>25)add("crew-fire-"+std::to_string(i),"FIRE / "+std::string(crewStationName(CrewStation(i))),"F9 DAMAGE CONTROL",critical,88);
            else if(occupied&&room.oxygen<18)add("crew-air-"+std::to_string(i),"LOW OXYGEN / "+std::string(crewStationName(CrewStation(i))),"F9 LIFE SUPPORT",critical,87);
        }
        if(!std::any_of(alerts.begin(),alerts.end(),[](const auto& a){return a.key.starts_with("crew-fire-")||a.key.starts_with("crew-air-");})&&!crew->alert.empty()&&!crew->alert.starts_with("CREW STATIONS NORMAL")&&!crew->alert.starts_with("RELIEF HELM")&&!crew->alert.starts_with("HELM HANDOVER"))
            add("crew",crew->alert,"F9 CREW ORDERS",warning,55,crew->alert);
    }
    if(t.hostileContacts)add("hostiles",std::to_string(t.hostileContacts)+" HOSTILE CONTACTS","H SELECT HOSTILE",warning,60);
    if(t.weaponHeat>95&&cockpitFocus(t).mode==CockpitMode::Combat)add("weapon-heat","WEAPON OVERHEAT","RELEASE FIRE TO COOL",warning,65);
    if(t.thrustLimited)add("thrust","THRUST LIMITED","REDUCE LOAD / SEEK CLEARANCE",warning,58);
    if(!notice.empty()&&notice!=t.stationStatus){
        std::string title(notice);const auto split=title.find(" / ");if(split!=std::string::npos)title.resize(split);
        if(title.size()>64)title="FLIGHT COMPUTER MESSAGE";
        const bool failed=notice.find("FAILED")!=notice.npos||notice.find("CANNOT")!=notice.npos||notice.find("INSUFFICIENT")!=notice.npos;
        add("notice",std::move(title),failed?"REVIEW THE FLIGHT COMPUTER MESSAGE":"",failed?warning:info,failed?70:20,std::string(notice));
    }
    if(!t.arrivalComms.empty())add("radio","LOCAL COMMS",t.arrivalComms,info,15,t.arrivalComms);
    std::stable_sort(alerts.begin(),alerts.end(),[](const auto& a,const auto& b){return a.level!=b.level?a.level>b.level:a.priority>b.priority;});
    return alerts;
}
} // namespace infinity
