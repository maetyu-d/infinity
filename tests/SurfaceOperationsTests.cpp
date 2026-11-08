#include "infinity/SurfaceOperations.hpp"
#include "infinity/TerrainMaterial.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Economy.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
}
int main(){try{
    Universe u;const auto address=Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0);
    const auto planet=u.describe(address);const auto descriptor=descriptorFingerprint(planet);
    const auto geometry=GeometryGenerator(u).generate(planet,0);const auto shape=geometryFingerprint(*geometry);
    const auto stone=terrainMaterial(planet);
    for(const auto& v:stone.uniforms())for(int i=0;i<4;++i)check(std::isfinite(v[i]),"Material has nonfinite coefficients");
    check(stone.layerMetres>=2&&stone.layerMetres<=28&&stone.rockRoughness<stone.sedimentRoughness,
          "Rock and sediment have no distinct physical response");
    auto icy=planet;icy.environment.mineral=Mineral::Ice;icy.environment.temperature=160;
    const auto ice=terrainMaterial(icy);
    check(ice.frozen>.9&&ice.rockRoughness<stone.rockRoughness&&ice.sediment.b>ice.bedrock.b,
          "Dense ice and frost do not have distinct response");
    std::set<std::array<int,3>> colours;
    for(const auto kind:{Mineral::Quartz,Mineral::Iron,Mineral::Olivine,Mineral::Graphite}){
        auto n=planet;n.environment.mineral=kind;n.environment.temperature=280;
        const auto m=terrainMaterial(n);colours.insert({int(m.bedrock.r*1000),int(m.bedrock.g*1000),int(m.bedrock.b*1000)});
    }
    check(colours.size()==4,"Inherited minerals all look identical");
    WeatherSample calm;calm.temperatureK=280;calm.pressureBar=.8;calm.visibilityMetres=20000;
    const auto direction=glm::normalize(glm::dvec3(.3,1,.4));
    check(!surfaceConditions(planet,direction,0,calm).landingHold,"Calm flat site is closed to landing");
    auto wind=calm;wind.wind={70,0,0};wind.gustMetresPerSecond=72;
    auto conditions=surfaceConditions(planet,direction,0,wind);
    check(conditions.landingHold&&conditions.outdoorHold&&conditions.kind==SurfaceHazard::Gusts,
          "Dangerous surface wind does not hold landing and outside crews");
    auto vacuum=wind;vacuum.pressureBar=0;vacuum.wind={0,0,0};vacuum.gustMetresPerSecond=100;
    check(!surfaceConditions(planet,direction,0,vacuum).outdoorHold,"Vacuum acquired an atmospheric gust hazard");
    auto dust=calm;dust.type=WeatherType::Dust;dust.visibilityMetres=100;
    conditions=surfaceConditions(planet,direction,0,dust);
    check(conditions.kind==SurfaceHazard::Whiteout&&conditions.landingHold&&!conditions.action.empty(),
          "Dust hazard has no usable landing advice");
    auto storm=calm;storm.type=WeatherType::Thunderstorm;storm.storm=.9;storm.convection=.9;
    check(surfaceConditions(planet,direction,0,storm).landingHold,
          "Electrical storm is treated as safe in good visibility");
    conditions=surfaceConditions(planet,direction,.65,calm);
    check(conditions.landingHold&&conditions.kind==SurfaceHazard::LooseGround,"Steep scree has no landing hold");
    auto heat=calm;heat.temperatureK=900;
    check(surfaceConditions(planet,direction,0,heat).landingHold,"Assisted landing ignores extreme heat");
    auto gas=planet;gas.geology.reset();
    check(surfaceConditions(gas,direction,.9,wind).kind==SurfaceHazard::None&&terrainMaterial(gas).bedrock==glm::vec3(0),
          "Gas world acquired terrain material or a surface hazard");
    const auto world=generateSurfaceLife(u,planet);
    check(!world->settlements.empty(),"Surface work fixture has no retained settlement");
    const auto& city=world->settlements.front();const auto layout=cityFingerprint(city);
    auto network=std::make_shared<EconomicNetwork>();network->system=address.parent();
    EconomicSite site;site.id={address,surfaceActivitySlot+city.slot};site.name=city.name;network->sites.push_back(site);
    EconomySnapshot s;s.network=network;s.sites.resize(1);s.portSignals.resize(1);
    s.sites[0].bottleneck="OPERATING";s.portSignals[0].power=1;s.portSignals[0].readiness=1;
    const auto healthy=settlementWork(*world,city,calm,&s);
    check(healthy.synchronized&&healthy.activity==1&&!healthy.outdoorHold,"Healthy production never becomes visibly active");
    check(!settlementWork(*world,city,calm,nullptr).synchronized,"Missing ledger invents working production");
    auto stale=s;stale.catchingUp=true;
    check(!settlementWork(*world,city,calm,&stale).synchronized,"Catching-up ledger animates stale production");
    auto broken=s;broken.sites[0].outageUntil=10;
    const auto offline=settlementWork(*world,city,calm,&broken);
    check(offline.damaged&&offline.activity==0&&offline.status.find("OFFLINE")!=offline.status.npos,
          "Outage leaves the visible machinery working");
    const auto held=settlementWork(*world,city,wind,&s);
    check(held.outdoorHold&&held.activity==0,"Dangerous gusts leave the outside work running");
    auto unpowered=s;unpowered.portSignals[0].power=0;
    check(settlementWork(*world,city,calm,&unpowered).activity==0,"Unpowered equipment continues producing");
    const auto actors=sampleSurfaceWork(*world,city,healthy,20);
    const auto repeat=sampleSurfaceWork(*world,city,healthy,20);
    const auto later=sampleSurfaceWork(*world,city,healthy,25);
    check(!actors.empty()&&actors.size()<=28&&repeat.size()==actors.size(),"Work overlay is empty or unbounded");
    std::set<uint64_t> ids;bool moved=false;
    for(size_t i=0;i<actors.size();++i){const auto& a=actors[i];
        check(ids.insert(a.id).second,"Machinery duplicated an actor identity");
        check(a.id==repeat[i].id&&a.position==repeat[i].position&&a.orientation==repeat[i].orientation,
              "Surface work depends on request order or frame rate");
        check(a.id==later[i].id,"Animation replaced an existing work actor");
        moved|=a.position!=later[i].position||a.orientation!=later[i].orientation;
        check(std::isfinite(glm::length(a.position))&&std::isfinite(glm::length(a.orientation)),"Machinery pose is invalid");
        if(a.building){
            const auto b=std::find_if(city.buildings.begin(),city.buildings.end(),[&](const auto& b){return b.id==a.building;});
            check(b!=city.buildings.end(),"Machinery detached from its inherited building");
            for(const auto& v:surfaceMachineMesh(a.kind,2).vertices){
                const auto p=glm::inverse(b->orientation)*(a.position-b->position+
                    a.orientation*(glm::dvec3(v.x,v.y,v.z)*a.dimensions));
                check(std::abs(p.x)<=b->dimensions.x*.5+.001&&std::abs(p.z)<=b->dimensions.z*.5+.001&&
                      p.y<=b->dimensions.y*1.1+.001,"Roof machinery escapes the existing collision bounds");
            }
        }
    }
    check(moved,"Healthy work never moves");
    SurfaceWorkClock clock;clock.advance(1,20);clock.advance(1,21);
    auto heldWork=offline;heldWork.animationSeconds=clock.seconds;
    const auto parked=sampleSurfaceWork(*world,city,heldWork,21);
    clock.advance(0,25);heldWork.animationSeconds=clock.seconds;
    const auto parkedLater=sampleSurfaceWork(*world,city,heldWork,25);
    for(size_t i=0;i<parked.size();++i)check(parked[i].position==parkedLater[i].position&&
        parked[i].orientation==parkedLater[i].orientation,"Work stoppage teleported equipment");
    const auto stoppedTime=clock.seconds;clock.advance(1,26);
    check(clock.seconds==stoppedTime+1,"Work does not resume from its held phase");
    const auto stopped=sampleSurfaceWork(*world,city,offline,20),stoppedLater=sampleSurfaceWork(*world,city,offline,25);
    for(size_t i=0;i<stopped.size();++i)check(stopped[i].position==stoppedLater[i].position&&
        stopped[i].orientation==stoppedLater[i].orientation,"Offline equipment continues animating");
    for(unsigned role=0;role<unsigned(SurfaceMachine::Count);++role)for(unsigned lod=0;lod<3;++lod){
        const auto mesh=surfaceMachineMesh(SurfaceMachine(role),lod);
        check(!mesh.vertices.empty()&&mesh.indexCount()%3==0&&mesh.bytes()<200000,"Equipment mesh is invalid or oversized");
    }
    check(cityFingerprint(city)==layout&&descriptorFingerprint(u.describe(address))==descriptor&&
          geometryFingerprint(*geometry)==shape,"Presentation changed the retained city or planet");
    for(double aspect:{1.,1.2,4./3,16./9,2.4}){
        FlightInstruments t;t.altitude=25;t.gear=true;t.surfaceSeverity=.95;t.surfaceWarning="DANGEROUS CROSSWIND";
        t.surfaceAction="WAIT FOR WIND TO EASE / SEEK SHELTER";t.surfaceLandingHold=true;
        t.arrivalComms="PORT ARRIVAL / WORKING SURFACE CITY / SHIFT-I CONTACT 1/8";
        const auto hud=cockpitOverlay(t,aspect,"HYPERION");
        check(cockpitTextConflicts(hud).empty(),"Surface warning overlaps cockpit text");
        check(std::any_of(hud.popups.begin(),hud.popups.end(),[](const auto& p){return p.critical;}),
              "Hazard warning is not visibly prioritised");
    }
    std::cout<<checks<<" surface material, work and hazard checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
