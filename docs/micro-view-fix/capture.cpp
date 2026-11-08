#define GLM_FORCE_INLINE
#include "infinity/Renderer.hpp"
#include "infinity/Cockpit.hpp"
#include <SDL3/SDL.h>
#include <oneapi/tbb/global_control.h>
#include <filesystem>
#include <iostream>
#include <sstream>
using namespace infinity;
int main(int argc,char** argv){try{
 const std::filesystem::path out=argc>1?argv[1]:"/tmp/infinity-micro-isolation";std::filesystem::create_directories(out);
 const auto source=Address::parse("0,0,0/1:0/2:9/3:0/4:61516819435380/5:4/6:4611686018427387905");
 Universe u;const auto grain=u.describe(u.children(u.describe(source),1).front());const double radius=std::pow(10.,grain.frame.logRadius);
 std::cout<<"GRAIN "<<grain.address.str()<<" radius="<<radius<<" descriptor="<<descriptorFingerprint(grain)<<std::endl;
 std::cout<<"AXES "<<grain.crystal.axes.x<<" "<<grain.crystal.axes.y<<" "<<grain.crystal.axes.z<<" mineral="<<int(grain.environment.mineral)<<" sdf_eye="<<u.grainSdf(grain,{0,0,1.1})<<" sdf_origin="<<u.grainSdf(grain,{0,0,0})<<std::endl;
 if(argc>2 && std::string(argv[2])=="inspect"){const auto g=GeometryGenerator(u).generate(grain,3);glm::vec3 lo(1e10),hi(-1e10);double longest=0,residual=0;size_t invalid=0;
  for(const auto& v:g->solid.vertices){lo=glm::min(lo,glm::vec3(v.x,v.y,v.z));hi=glm::max(hi,glm::vec3(v.x,v.y,v.z));residual=std::max(residual,std::abs(u.grainSdf(grain,{v.x,v.y,v.z})));}
  for(size_t i=0;i<g->solid.indexCount();i+=3){glm::dvec3 p[3];for(unsigned k=0;k<3;++k){const auto& v=g->solid.vertices.at(g->solid.index(i+k));p[k]={v.x,v.y,v.z};}longest=std::max({longest,glm::length(p[0]-p[1]),glm::length(p[1]-p[2]),glm::length(p[2]-p[0])});}
  std::cout<<"MESH "<<g->solid.vertices.size()<<" triangles="<<g->solid.indexCount()/3<<" lo="<<lo.x<<","<<lo.y<<","<<lo.z<<" hi="<<hi.x<<","<<hi.y<<","<<hi.z<<" max_residual="<<residual<<" lod0="<<geometryFingerprint(*GeometryGenerator(u).generate(grain,0))<<" longest_edge="<<longest<<std::endl;return 0;}
 oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism,4);
 SDL_SetAppMetadata("Infinity micro inspection",nullptr,"org.infinity.universe.validation");if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
 auto* window=SDL_CreateWindow("Infinity / micro inspection",1440,900,0);if(!window)throw std::runtime_error(SDL_GetError());
 {Navigation nav(u);RegionCache cache(u);Renderer renderer(window,u,true);renderer.freezeVisualTime(140);renderer.setCockpitValidation(true);
 for(unsigned c=0;c<3;++c){
  const char* name=c==0?"full":c==1?"grain-only":"rock-only";
  renderer.setSpecimen(c==0?std::optional<Address>{}:std::optional<Address>(c==1?grain.address:source));
  std::ostringstream camera;camera.precision(17);camera<<grain.address.str()<<' '<<grain.frame.logRadius<<" 1 0 0 0 0 0 "<<(argc>2?std::stod(argv[2])-1.:.1);
  if(!nav.restore(camera.str()))throw std::runtime_error("Micro fixture camera rejected");
  FlightInstruments t;t.hull=100;t.shrinking=true;t.hullMetres=12*std::clamp(radius*.001,1e-12,.001);t.altitude=1.7;t.destination=grain.label();renderer.setCockpit(&t);
  unsigned stable=0;FrameStats stats;uint64_t prior=0;bool captured=false;
  for(unsigned frame=0;frame<2000;++frame){SDL_Event e;while(SDL_PollEvent(&e)){}cache.beginFrame();stats=renderer.render(nav,cache,true,60,0,{});cache.finishFrame();
   const bool ready=frame>60&&!cache.stats().pending&&!stats.lodTransitions&&!stats.uploadDeferred&&stats.triangles==prior;prior=stats.triangles;stable=ready?stable+1:0;
   if(stable>=20&&!captured){renderer.screenshot((out/(std::string(name)+".ppm")).string());captured=true;}
   if(captured&&stable>28)break;if(frame==1999)throw std::runtime_error("Micro scene did not settle");
  }
  std::cout<<"MICRO "<<name<<" triangles="<<stats.triangles<<" sections="<<stats.sections<<" lod="<<stats.lodTransitions<<" cpu="<<stats.cpuMs<<std::endl;
 }}SDL_DestroyWindow(window);SDL_Quit();
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
