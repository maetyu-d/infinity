#include "infinity/Renderer.hpp"
#include "infinity/Cockpit.hpp"
#ifdef URBAN_AFTER
#include "infinity/Urban.hpp"
#endif
#include <SDL3/SDL.h>
#include <oneapi/tbb/global_control.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <chrono>
using namespace infinity;
int main(int argc,char** argv){try{
 if(argc<5)throw std::runtime_error("Usage: capture address output slot radius");
 const auto address=Address::parse(argv[1]);const std::filesystem::path output=argv[2];const unsigned slot=unsigned(std::stoul(argv[3]));const double radius=std::stod(argv[4]);
 std::filesystem::create_directories(output);oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism,4);
 const auto start=std::chrono::steady_clock::now();Universe u;const auto planet=u.describe(address);const auto world=generateSurfaceLife(u,planet);
 const auto it=std::find_if(world->settlements.begin(),world->settlements.end(),[&](const auto& s){return s.slot==slot;});if(it==world->settlements.end())throw std::runtime_error("Missing city");const auto& city=*it;
 const auto core=cityFingerprint(city),descriptor=descriptorFingerprint(planet);std::cout<<"CITY address="<<address.str()<<" slot="<<slot<<" core="<<core<<" descriptor="<<descriptor<<" core_population="<<city.population<<" world_bytes="<<world->residentBytes()<<" generation_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
#ifdef URBAN_AFTER
 if(!city.urban)throw std::runtime_error("Natural fixture has no generated regional city");
 std::cout<<"URBAN buildings="<<city.urban->buildings<<" people="<<city.urban->population<<" radius="<<city.urban->growth.radius<<" bytes="<<city.urban->residentBytes()<<std::endl;
#endif
 SDL_SetAppMetadata("Infinity urban verification",nullptr,"org.infinity.universe.validation");if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
 auto* window=SDL_CreateWindow("Infinity / generated cities",1440,900,0);if(!window)throw std::runtime_error(SDL_GetError());
 {Navigation nav(u);RegionCache cache(u);Renderer renderer(window,u,true);ActivityState activity;activity.seconds=12;activity.selected=ActivityId{address,surfaceActivitySlot+slot};renderer.setActivity(&activity);renderer.freezeVisualTime(12);renderer.setCockpitValidation(true);
 constexpr std::array names{"overview","approach","street"};
 for(unsigned scene=0;scene<3;++scene){
  const glm::dvec2 at=scene?glm::dvec2(radius*.28,-radius*.12):glm::dvec2(0,0);
  const auto d=glm::normalize(city.direction+city.orientation*glm::dvec3(at.x,0,at.y)/world->radiusMetres);
  const auto target=d*(world->radiusMetres+u.terrainHeight(planet,d))+(scene==2?d*70.:glm::dvec3(0));
  const auto offset=scene==0?glm::dvec3(radius*.15,radius*.5,radius*.9):scene==1?glm::dvec3(1500,1100,2700):glm::dvec3(220,180,450);
  const auto eye=target+city.orientation*offset;const auto forward=glm::normalize(target-eye);
  nav.changeFrame(address);nav.setRenderScale(scene==2?3:4);nav.orientation=glm::quatLookAtRH(forward,glm::normalize(target));nav.offset=eye/std::pow(10.,nav.logDistance)-nav.orientation*glm::dvec3(0,0,1);nav.selected.reset();
  const auto camera=nav.serialize();FlightInstruments instruments;instruments.hull=100;instruments.altitude=glm::length(eye)-world->radiusMetres-u.terrainHeight(planet,glm::normalize(eye));instruments.targetVisible=true;instruments.destination=city.name;instruments.range=glm::length(eye-city.landingPoint);instruments.localTarget=glm::inverse(nav.orientation)*(city.landingPoint-eye);renderer.setCockpit(&instruments);
  unsigned stable=0;bool captured=false;FrameStats stats;std::vector<double> times;
  for(unsigned frame=0;frame<4000;++frame){SDL_Event event;while(SDL_PollEvent(&event)){}cache.beginFrame();stats=renderer.render(nav,cache,true,60,0,{});cache.finishFrame();
   const bool ready=frame>80&&!cache.stats().pending&&!cache.stats().queued&&!stats.surfacePending&&!stats.weatherPending&&!stats.lodTransitions&&!stats.uploadDeferred;
   stable=ready?stable+1:0;if(ready&&stable>5)times.push_back(stats.cpuMs);
   if(!captured&&stable>20){renderer.screenshot((output/(std::string(names[scene])+".ppm")).string());captured=true;}
   if(captured&&stable>45)break;if(frame==3999)throw std::runtime_error("City capture did not settle");}
  if(nav.serialize()!=camera||descriptorFingerprint(u.describe(address))!=descriptor||cityFingerprint(city)!=core)throw std::runtime_error("Urban rendering changed camera or core identity");
  std::sort(times.begin(),times.end());std::cout<<"URBAN_CAPTURE case="<<names[scene]<<" terrain_digest="<<renderer.terrainGeometryDigest()<<" cpu_median="<<times[times.size()/2]<<" surface_bytes="<<stats.surfaceBytes<<" visible_buildings="<<stats.surfaceBuildings<<" surface_gpu_bytes="<<stats.surfaceGpuBytes;
#ifdef URBAN_AFTER
  std::cout<<" urban_blocks="<<stats.urbanBlocks<<" urban_buildings="<<stats.urbanBuildings<<" urban_lights="<<stats.urbanLights<<" urban_residents="<<stats.urbanResidents;
#endif
  std::cout<<std::endl;
 }
 std::cout<<"PASS: same city core, planet, terrain camera and navigation; three native city approach views"<<std::endl;
 }SDL_DestroyWindow(window);SDL_Quit();return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<std::endl;return 1;}}
