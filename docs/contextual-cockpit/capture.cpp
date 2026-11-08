#include "infinity/Renderer.hpp"
#include "infinity/Economy.hpp"
#include "infinity/Cockpit.hpp"
#ifdef HUD_AFTER
#include "infinity/CockpitContext.hpp"
#endif
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <oneapi/tbb/global_control.h>
using namespace infinity;
int main(int argc,char** argv){try{
 const std::filesystem::path output=argc>1?argv[1]:"/tmp/infinity-hud-before";
 std::filesystem::create_directories(output);std::ofstream metrics(output/"metrics.csv");metrics<<"case,text_runs,characters,panels_bytes,labels,popups\n";
 oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism,4);
 SDL_SetAppMetadata("Infinity cockpit verification",nullptr,"org.infinity.universe.validation");
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
 auto* window=SDL_CreateWindow("Infinity / cockpit verification",1440,900,0);if(!window)throw std::runtime_error(SDL_GetError());
 {Universe u;Navigation nav(u);RegionCache cache(u);FlightController flight(u,nav,cache);Pilot pilot(u,nav,flight);Renderer renderer(window,u,true);
 renderer.setCockpitValidation(true);renderer.freezeVisualTime(12);
 const auto planet=u.describe(Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0));
 const auto world=generateSurfaceLife(u,planet);const auto& city=world->settlements.front();const auto identity=descriptorFingerprint(planet),layout=cityFingerprint(city);
 nav.changeFrame(planet.address);nav.setRenderScale(3);nav.orientation=city.orientation*glm::angleAxis(-.12,glm::dvec3(1,0,0));
 const auto eye=city.landingPoint+city.orientation*glm::dvec3(140,170,350);nav.offset=eye/1000.-nav.orientation*glm::dvec3(0,0,1);nav.selected.reset();pilot.resetMotion();
 ActivityState activity;activity.seconds=12;activity.selected=ActivityId{planet.address,surfaceActivitySlot+city.slot};renderer.setActivity(&activity);
 ShipCrew crew(123);auto crewView=crew.view();renderer.setCrew(&crewView);
 std::vector<CelestialContact> contacts(20);std::vector<ActivityContact> traffic(20);
 for(unsigned i=0;i<20;++i){auto& c=contacts[i];c.address=planet.address.parent().child(Kind::Planet,i);c.label=i?"MOON "+std::to_string(30403+i):"PLANET 93612";
  c.life=LifeDetection::Detected;c.population=9800+i*73;c.moon=i>0;c.distanceMetres=120000.+i*300000.;c.localDirection=glm::normalize(glm::dvec3((int(i%5)-2)*.2,.1,-1));c.selected=i==0;
  auto& a=traffic[i];a.body.id={planet.address,i};a.body.name=i?"FREIGHTER "+std::to_string(1900+i):"COLONY 72066";a.body.kind=i?ActivityKind::Freighter:ActivityKind::SurfacePort;
  a.body.economicBrief="SHORT WATER";a.body.economicAlert=.6;a.body.economicReport={"POWER 63% / READINESS 0%","REPAIRS OFFLINE / CREW UNAVAILABLE","NEEDS 1280KG WATER"};
  a.distance=5000+i*2000;a.localDirection=c.localDirection;a.occluded=i%3==0;a.selected=i==0;
 }
 constexpr std::array names{"warp","approach","landing","docking","combat","surface","micro","catalogue"};
 for(unsigned scene=0;scene<names.size();++scene){FlightInstruments t;t.destination="MOUNTAIN 2690 / J NAVIGATE";t.targetVisible=true;t.localTarget={.2,.1,-1};t.range=2814000;
  t.speed=177000;t.driveSpeed=196000;t.throttle=1;t.hull=100;t.altitude=1630000;t.gear=true;t.massKg=12000;
  t.weapon="PULSE LASER I";t.weaponStatus="WEAPON INHIBITED";t.combatStatus="H TARGET HOSTILE / LOCAL SPACE CLEAR";t.crewFire="GUNNERS HOLD FIRE / 6 HOLD 7 DEF 8 FOCUS";
  t.systemFault="ENG 100 / RCS 100 / O2 100";t.localUp={0,1,0};t.flightSeconds=2346;t.travelMetres=1e8;
  if(scene==0){t.warp=WarpPhase::Manual;t.drive="MANUAL WARP";t.localTarget={.2,0,1};t.descent=-116000;t.drift=133000;}
  if(scene==1){t.navigating=true;t.range=18000;t.speed=450;t.altitude=12000;t.throttle=.45;t.gear=false;t.destination=city.name;t.drive="PLANETARY FLIGHT";}
  if(scene==2){t.range=200;t.speed=12;t.altitude=80;t.descent=2;t.drift=.5;t.landing.phase=LandingPhase::Align;t.landing.body=planet.address;t.safe=true;t.throttle=.1;}
  if(scene==3){t.docking.available=t.docking.active=true;t.docking.phase=DockingPhase::Glide;t.docking.range=80;t.docking.width=40;t.docking.closing=4;t.speed=4;t.altitude=-1;t.range=80;t.targetLabel="ORBITAL HARBOUR";}
  if(scene==4){t.incomingFire=.8;t.hostileContacts=3;t.weaponStatus="READY / SPACE FIRE";t.weaponHeat=66;t.speed=900;t.altitude=-1;t.gear=false;t.localMissile={.3,.1,-1};t.combatStatus="HOSTILES ATTACKING";}
  if(scene==5){t.explorationActive=true;t.exploration={"GEOLOGICAL EXTRACTION","EXTRACTING / 60% / HEAT 70%","MARKED ORE / FULL SIZE","CTRL-SPACE CUT / F12 SURVEY"};t.altitude=12;t.speed=0;t.throttle=0;t.range=25;}
  if(scene==6){t.shrinking=true;t.hullMetres=.0005;t.targetHullMetres=.0005;t.logMetres=-3;t.altitude=2;t.speed=.001;t.throttle=.2;t.drive="SHRUNK SHIP / MANOEUVRE";}
#ifdef HUD_AFTER
  t.contactsExpanded=scene==7;
#endif
  const auto hud=cockpitOverlay(t,16./9,"HYPERION",{},contacts,traffic,false,1,nullptr,{1,0,0,0},&crewView);
  size_t chars=0;for(const auto& r:hud.textRuns)chars+=r.value.size();metrics<<names[scene]<<','<<hud.textRuns.size()<<','<<chars<<','<<hud.panels.bytes()<<','<<hud.worldLabels.size()<<','<<hud.popups.size()<<'\n';metrics.flush();
  renderer.setCockpit(&t);unsigned stable=0;bool shot=false;FrameStats stats;
  for(unsigned i=0;i<2000;++i){SDL_Event e;while(SDL_PollEvent(&e)){}cache.beginFrame();stats=renderer.render(nav,cache,true,60,0,"");cache.finishFrame();
   if(i>80&&cache.stats().pending==0&&cache.stats().queued==0&&stats.surfacePending==0&&stats.weatherPending==0&&stats.lodTransitions==0)++stable;else stable=0;
   if(!shot&&stable>15){renderer.screenshot((output/(std::string(names[scene])+".ppm")).string());shot=true;stable=0;}
   else if(shot&&stable>6)break;
   if(i==1999)throw std::runtime_error("Scene did not settle");
  }
#ifdef HUD_AFTER
  const auto viewport=renderer.cockpitViewport();
  const auto actualWorlds=renderer.celestialContacts();const auto actualShips=renderer.activityContacts();
  const auto toggle=viewport.toPixels(scene==7?glm::dvec2(100,188):glm::dvec2(100,155));
  if(!renderer.contactsToggleHit(toggle.x,toggle.y))throw std::runtime_error("Visible Contacts heading cannot expand/collapse the catalogue");
  if(scene!=7){
   const auto visible=cockpitSummaryRows(actualWorlds,actualShips);
   for(size_t i=0;i<visible.size();++i){const auto row=visible[i];const auto point=viewport.toPixels({100,187+i*25.});
    if(row.activity){if(renderer.pickActivity(point.x,point.y)!=actualShips[row.index].body.id)throw std::runtime_error("Compact contact selects the wrong activity identity");}
    else if(renderer.pick(point.x,point.y)!=actualWorlds[row.index].address)throw std::runtime_error("Compact contact selects the wrong world identity");
   }
  }
  if(std::any_of(actualWorlds.begin(),actualWorlds.end(),hasSurfaceLife)){
   const auto badge=cockpitLifeBadge(viewport.aspect(),scene==7);const auto point=viewport.toPixels({badge.x+badge.w*.5,badge.y+badge.h*.5});
   if(renderer.pick(point.x,point.y)!=nextLivingWorld(actualWorlds,nav.selected))throw std::runtime_error("Life badge lost its canonical next-world selection");
  }
#endif
  if(descriptorFingerprint(u.describe(planet.address))!=identity||cityFingerprint(city)!=layout)throw std::runtime_error("HUD mutated the universe");
  std::cout<<"HUD_CAPTURE "<<names[scene]<<" digest="<<renderer.terrainGeometryDigest()<<" cpu_ms="<<stats.cpuMs<<"\n";
 }
 std::cout<<"PASS: eight native cockpit states / unchanged generated terrain and settlement\n";
 }SDL_DestroyWindow(window);SDL_Quit();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
