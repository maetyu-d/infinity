#define GLM_FORCE_INLINE
#include "infinity/Renderer.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/MajorLandforms.hpp"
#include <SDL3/SDL.h>
#include <oneapi/tbb/global_control.h>
#include <filesystem>
#include <iostream>
using namespace infinity;
int main(int argc,char** argv){try{
 if(argc<3)throw std::runtime_error("capture planet output");
 Universe u;const auto address=Address::parse(argv[1]);const auto planet=u.describe(address);
 if(!planet.geology||!planet.geology->landform)throw std::runtime_error("No natural landform at address");
 const auto& f=*planet.geology->landform;const auto descriptor=descriptorFingerprint(planet);
 const std::filesystem::path output=argv[2];std::filesystem::create_directories(output);
 oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism,4);
 SDL_SetAppMetadata("Infinity landform verification",nullptr,"org.infinity.universe.validation");
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
 auto* window=SDL_CreateWindow("Infinity / rare geological landforms",1440,900,0);if(!window)throw std::runtime_error(SDL_GetError());
 {Navigation nav(u);RegionCache cache(u);Renderer renderer(window,u,true);renderer.freezeVisualTime(12);renderer.setCockpitValidation(true);
 const double radius=std::pow(10.,planet.frame.logRadius);const auto target=f.centre*(radius+u.terrainHeight(planet,f.centre));
 for(unsigned scene=0;scene<2;++scene){
  const double w=f.widthMetres;const auto eye=target+f.centre*(scene?w*.32:w*1.6)+f.along*(scene?w*.85:w*2.1);
  nav.changeFrame(address);nav.setRenderScale(std::log10(w));nav.orientation=glm::quatLookAtRH(glm::normalize(target-eye),f.centre);
  nav.offset=eye/std::pow(10.,nav.logDistance)-nav.orientation*glm::dvec3(0,0,1);nav.selected.reset();
  const auto camera=nav.serialize();FlightInstruments instruments;instruments.hull=100;instruments.altitude=glm::length(eye)-radius-u.terrainHeight(planet,glm::normalize(eye));
  instruments.destination=std::string(majorLandformName(f.type));instruments.targetVisible=true;instruments.range=glm::length(target-eye);instruments.localTarget=glm::inverse(nav.orientation)*(target-eye);renderer.setCockpit(&instruments);
  unsigned stable=0;bool captured=false;FrameStats stats;std::vector<double> times;
  for(unsigned frame=0;frame<3500;++frame){SDL_Event e;while(SDL_PollEvent(&e)){}cache.beginFrame();stats=renderer.render(nav,cache,true,60,0,{});cache.finishFrame();
   const bool ready=frame>80&&!cache.stats().pending&&!cache.stats().queued&&!stats.surfacePending&&!stats.weatherPending&&!stats.lodTransitions&&!stats.uploadDeferred;
   stable=ready?stable+1:0;if(ready&&stable>5)times.push_back(stats.cpuMs);
   if(!captured&&stable>20){renderer.screenshot((output/(scene?"approach.ppm":"overview.ppm")).string());captured=true;}
   if(captured&&stable>45)break;if(frame==3499)throw std::runtime_error("Native terrain did not settle");
  }
  if(nav.serialize()!=camera||descriptorFingerprint(u.describe(address))!=descriptor)throw std::runtime_error("Rendering modified landform or camera");
  std::sort(times.begin(),times.end());std::cout<<"NATIVE "<<majorLandformName(f.type)<<" address="<<address.str()<<" scene="<<scene<<" cpu_median_ms="<<times[times.size()/2]<<" terrain_digest="<<renderer.terrainGeometryDigest()<<" surface_bytes="<<stats.surfaceBytes<<std::endl;
 }
 }SDL_DestroyWindow(window);SDL_Quit();std::cout<<"PASS native landform/approach, stable camera and deterministic geometry"<<std::endl;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
