#include "infinity/ProbePresentation.hpp"
#include "infinity/CavernHazard.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/Navigation.hpp"
namespace infinity {
void annotateCavern(FlightInstruments& t,const Universe& u,const Navigation& nav,const Borehole& b,
                    unsigned index,std::string_view work) {
    index=std::min(index,cavernFindCount-1);
    const auto f=cavernFinds(b)[index];const auto& state=b.finds[index];
    const auto planet=u.describe(b.planet);
    const auto delta=u.pointRelative(b.planet,cavernFindPoint(b,f)/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance)-nav.cameraPosition();
    t.phase="CAVERN EXPLORATION";t.targetVisible=true;t.localTarget=glm::inverse(nav.orientation)*delta;
    t.range=glm::length(delta)*std::pow(10.,nav.logDistance);t.destination=cavernFindName(f);t.targetLabel=t.destination;
    t.weapon="CAVERN PROSPECTOR";t.explorationActive=true;
    t.weather="DEPOSIT / "+std::string(material(f.mineral).formula);
    t.weatherDetail="INHERITED GEOLOGY / SHARED FINITE RESERVE";t.weatherSeverity=0;
    const std::string next=state.grade<1 ? "HOLD F12 FOR OVERVIEW / Z SHRINK TO INSPECT"
        : state.grade<2 ? "SHRINK BELOW 12 MM / STOP NEAR THE FACE / F12"
        : !state.marked ? "CTRL-F12 MARK THIS MATERIAL"
        : t.shrinking ? "RETURN TO CHAMBER CENTRE / SHIFT-Z FULL SIZE"
                      : "CTRL-SPACE EXTRACT / FITTED MINING HEAD";
    t.exploration={std::to_string(index+1)+"/3 / "+cavernFindName(f),
        "ASSAY "+std::to_string(state.grade)+" / "+(state.marked?"MARKED":"UNMARKED")+" / MINED "+std::to_string(state.minedKg)+" KG",
        work.empty()?next:std::string(work),"SHIFT-F12 DEPOSIT / CTRL-J EXIT / F2 SURVEY JOBS"};
}
void annotateDrillCamera(FlightInstruments& t,const Borehole& b,double seconds,double density) {
    const auto sample=probePose(b,seconds);const auto operation=probeOperation(b,seconds);
    t.phase="MINING PROBE CAMERA";t.playerGun=noWeaponMount;t.gunAim={1,0,0,0};t.altitude=-sample.depth;t.explorationActive=true;
    t.exploration={std::string(probePhaseName(sample.phase))+" / CAMERA "+std::to_string(b.id%100000),
        "DEPTH "+std::to_string(int(sample.depth))+" M / TARGET "+std::to_string(int(operation.target))+" M / FEED "+std::to_string(int(operation.rate*100))+"%",
        probeToolReadout(probeVisual(b,seconds,density)),
        sample.phase==ProbePhase::Beacon ? "ESC SHIP / CTRL-J ENTRANCE / SHRINK TO ASSAY / SHIFT-F12 DEPOSIT"
            : "P PAUSE / [ ] DEPTH / - + FEED / R ABORT / ENTER CHARGE / ESC SHIP"};
}
void annotateIceStress(FlightInstruments& t, const IceStress& stress, bool cutting, bool engineer) {
    const bool falling = stress.falling();
    if (!falling && !stress.warning())
        return;
    t.weather = falling ? "ICE FALL" : "UNSTABLE ICE";
    t.weatherDetail = falling ? "LOOSE ICE / CLEAR THE FACE" : "FRACTURE LOAD " +
        std::to_string(int(stress.load * 100)) + "%";
    t.weatherSeverity = 1;
    t.exploration[2] = (engineer ? "ENGINEER / " : "") +
        std::string(falling ? "ICE FALL AT THE FACE" : "ICE FRACTURES SPREADING");
    t.exploration[3] = falling ? "BACK AWAY / LET ICE SETTLE"
        : cutting ? "RELEASE CUTTER / BACK AWAY" : "SLOW DOWN / BACK AWAY";
}
} // namespace infinity
