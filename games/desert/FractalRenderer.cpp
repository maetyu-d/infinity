#include "FractalRenderer.hpp"
#include "FractalSource.hpp"
#include "TerrainCache.hpp"
#include <bgfx/bgfx.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
#include <iostream>
#include <cstdlib>
namespace desert {
namespace fr=infinity::fractal;
namespace vectorStyle {
using namespace infinity::fractal;
using glm::smoothstep;
using infinity::fractal::atan2;
#include "VectorStyle.inl"
}
static bgfx::ShaderHandle shader(const std::string& source,bool fragment,int textures=0) {
    std::vector<uint8_t> bytes;auto put=[&](uint64_t n,int size) {for(int i=0;i<size;++i)bytes.push_back(uint8_t(n>>(8*i)));};
    bytes.insert(bytes.end(),{uint8_t(fragment?'F':'V'),'S','H',13});
    put(fragment?0x46524143:0,4);put(fragment?0:0x46524143,4);put(0,4);put(0,4);put(1+textures,2);
    auto uniform=[&](const std::string& name,int type,int number,int index) {put(name.size(),1);bytes.insert(bytes.end(),name.begin(),name.end());put(type,1);put(number,1);put(index,2);put(number,2);put(0,2);put(0,2);};
    uniform("u_fractal",fragment?18:2,160,0);if(textures)uniform("s_scene",16,1,0);if(textures>1)uniform("s_depth",16,1,1);if(textures>2)uniform("s_carDepth",16,1,2);if(textures>3)uniform("s_height",16,1,3);if(textures>4)uniform("s_coarse",16,1,4);if(textures>5)uniform("s_tracks",16,1,5);if(textures>6)uniform("s_trackMask",16,1,6);
    put(source.size(),4);bytes.insert(bytes.end(),source.begin(),source.end());put(0,1);put(0,1);put(2560,2);
    return bgfx::createShader(bgfx::copy(bytes.data(),uint32_t(bytes.size())));
}
static const char* prelude=R"MSL(
#include <metal_stdlib>
#define INFINITY_GPU 1
using namespace metal;
struct Global {float4 u_fractal[160];};
struct ViewRay {float3 eye,right,up,forward,ray;float tangent,aspect;bool gunner;};
ViewRay fr_view(float2 uv,constant Global& u) {
 bool split=u.u_fractal[89].z>.5,gunner=split&&uv.x>=.5;
 float x=split?uv.x*2.-(gunner?1.:0.):uv.x;
 ViewRay v;v.gunner=gunner;v.eye=u.u_fractal[gunner?85:1].xyz;
 v.right=u.u_fractal[gunner?86:2].xyz;v.up=u.u_fractal[gunner?87:3].xyz;v.forward=u.u_fractal[gunner?88:4].xyz;
 v.tangent=gunner?u.u_fractal[89].x:u.u_fractal[0].z;v.aspect=gunner?u.u_fractal[89].y:u.u_fractal[0].w;
 v.ray=normalize(v.forward+v.right*(x*2.-1.)*v.tangent*v.aspect+v.up*(1.-uv.y*2.)*v.tangent);return v;
}
)MSL";
static const char* vertex=R"MSL(
struct Input {float3 a_position [[attribute(0)]];};
struct Output {float4 position [[position]];float2 uv [[user(locn0)]];};
vertex Output xlatMtlMain(Input in [[stage_in]]) {Output out;out.position=float4(in.a_position,1.);out.uv=float2(in.a_position.x*.5+.5,.5-in.a_position.y*.5);return out;}
)MSL";
static const char* worldHelpers=R"MSL(
World fr_world(constant Global& u){return World{u.u_fractal[5].x,uint(u.u_fractal[5].y),&u.u_fractal[96],int(u.u_fractal[76].x)};}
)MSL";
static const char* fragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
Vehicle fr_car(constant Global& _mtl_u,int i) {int k=8+i*8;return Vehicle{_mtl_u.u_fractal[k],_mtl_u.u_fractal[k+1],_mtl_u.u_fractal[k+2],_mtl_u.u_fractal[k+3],_mtl_u.u_fractal[k+4],_mtl_u.u_fractal[k+5],_mtl_u.u_fractal[k+6],_mtl_u.u_fractal[k+7]};}
RutSample fr_tracks(float2 p,constant Global& _mtl_u,texture2d<float> tracks,texture2d<float> mask) {
 float4 window=_mtl_u.u_fractal[70];float2 grid=p/window.z,cell=floor(grid),f=grid-cell;
 RutSample result={0,float2(0),0};
 if(any(grid<window.xy+2.)||any(grid>window.xy+window.w-2.))return result;
 int size=int(window.w),tiles=size/32;
 uint2 tile=uint2(int2(floor(grid/32.))&int2(tiles-1));if(mask.read(tile).r<.5)return result;
 float4 wx=fr_rut_weights(f.x),wz=fr_rut_weights(f.y),dx=fr_rut_derivatives(f.x),dz=fr_rut_derivatives(f.y);
 int2 first=int2(cell)-1;float4 depths[4];
 for(int z=0;z<4;++z)depths[z]=float4(0);
 for(int z=0;z<4;++z)for(int x=0;x<4;++x) {
  float2 node=tracks.read(uint2((first+int2(x,z))&int2(size-1))).rg;
  depths[z][x]=node.x;result=fr_rut_accumulate(result,node,wx[x],wz[z],dx[x],dz[z]);
 }
 // A short LOD transition keeps the moving upload window continuous.
 float4 edges=float4(grid-window.xy,window.xy+window.w-grid)*window.z;
 float edge=min(min(edges.x,edges.y),min(edges.z,edges.w));
 float2 direction=edges.x==edge?float2(1,0):edges.y==edge?float2(0,1):edges.z==edge?float2(-1,0):float2(0,-1);
 float t=clamp((edge-.25)/4.,0.,1.),fade=t*t*(3.-2.*t);
 result.gradient=result.gradient*fade+result.height*direction*(6.*t*(1.-t)/4.);
 result.curvature=fr_rut_cell_curvature(depths[0],depths[1],depths[2],depths[3])+(t>0&&t<1?3.:0.);
 result.height*=fade;result.compaction*=fade;return result;
}
Field fr_scene(float3 p,constant Global& _mtl_u,texture2d<float> tracks,texture2d<float> mask) {
 if(p.y>fr_height_max()+7)return Field{(p.y-fr_height_max()-4)*.8,0};
 World w=fr_world(_mtl_u);
 return Field{(p.y-fr_height(p.xz,w))*fr_world_scale(w),0};
}
Field fr_cached(float3 p,constant Global& _mtl_u,texture2d<float> heights,sampler heightSampler,texture2d<float> tracks,texture2d<float> mask) {
 if(p.y>fr_height_max()+7)return Field{(p.y-fr_height_max()-4)*.8,0};
 float4 cache=_mtl_u.u_fractal[40];float2 q=p.xz/cache.z;
 float4 valid=_mtl_u.u_fractal[75];
 if(any(q<valid.xy+1.)||any(q>valid.zw-2.))return fr_scene(p,_mtl_u,tracks,mask);
 float upper=heights.sample(heightSampler,(floor(q)+.5)/cache.w,level(0)).r;
 World w=fr_world(_mtl_u);upper+=fr_feature_bounds((floor(q)+.5)*cache.z,cache.z*.708,w).x;
 return Field{(p.y-upper)*fr_world_scale(w),0};
}
float fr_cell_exit(float3 p,float3 ray,float cell) {
 float2 edge=(floor(p.xz/cell)+select(float2(0),float2(1),ray.xz>0.))*cell;
 float2 exits=(edge-p.xz)/select(float2(1e-10),ray.xz,abs(ray.xz)>1e-10);
 exits=select(float2(1e20),exits,abs(ray.xz)>1e-8);return max(0.,min(exits.x,exits.y));
}
float fr_empty_cell_advance(float3 p,float3 ray,float cell,float upper) {
 if(p.y<=upper)return -1.;
 float exit=fr_cell_exit(p,ray,cell),plane=ray.y<-.0000001?(p.y-upper)/(-ray.y):1e20;
 if(plane<exit)return plane;
 return exit+min(.001,max(0.,p.y+ray.y*exit-upper)*.24);
}
float fr_quadratic_cell_advance(float residual,float slope,float3 p,float3 ray,float cell,float curvature) {
 float m=max(.00001,curvature*dot(ray.xz,ray.xz)),root=sqrt(slope*slope+2.*m*residual);
 float advance=slope<0?2.*residual/(root-slope):(slope+root)/m,exit=fr_cell_exit(p,ray,cell);
 if(advance>=exit)return exit+min(.001,max(0.,residual+slope*exit-.5*m*exit*exit)*fr_rut_scale());
 return advance*.90;
}
float3 fr_normal(float3 p,constant Global& _mtl_u,float epsilon,texture2d<float> tracks,texture2d<float> mask) {
 float3 a=float3(1,-1,-1),b=float3(-1,-1,1),c=float3(-1,1,-1),d=float3(1,1,1);
 return normalize(a*fr_scene(p+a*epsilon,_mtl_u,tracks,mask).distance+b*fr_scene(p+b*epsilon,_mtl_u,tracks,mask).distance+c*fr_scene(p+c*epsilon,_mtl_u,tracks,mask).distance+d*fr_scene(p+d*epsilon,_mtl_u,tracks,mask).distance);
}
float3 fr_sky(float3 ray,float time,World w) {return float3(0);}
struct TraceOutput {float4 color [[color(0)]];float depth [[color(1)]];};
fragment TraceOutput xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],sampler s_sceneSampler [[sampler(0)]],texture2d<float> s_depth [[texture(1)]],sampler s_depthSampler [[sampler(1)]],texture2d<float> s_carDepth [[texture(2)]],sampler s_carDepthSampler [[sampler(2)]],texture2d<float> s_height [[texture(3)]],sampler s_heightSampler [[sampler(3)]],texture2d<float> s_coarse [[texture(4)]],sampler s_coarseSampler [[sampler(4)]],texture2d<float> s_tracks [[texture(5)]],texture2d<float> s_trackMask [[texture(6)]]) {
 float4 viewport=_mtl_u.u_fractal[0];ViewRay view=fr_view(in.position.xy/viewport.xy,_mtl_u);
 float3 ray=view.ray,eye=view.eye;World world=fr_world(_mtl_u);
 float maximum=_mtl_u.u_fractal[3].w,t=.06;
 if(view.gunner&&_mtl_u.u_fractal[94].x>.5)return TraceOutput{float4(0,0,0,-1),maximum};
 if(_mtl_u.u_fractal[7].x>.5) {
  if(_mtl_u.u_fractal[6].z>1.5&&_mtl_u.u_fractal[6].z<2.5)return TraceOutput{float4(0),maximum};
  // Empty-space cones cover a whole coarse pixel. Their stopping distances
  // conservatively initialize the fine rays, including silhouettes and holes.
  float slope=view.tangent/viewport.y*1.6;
  for(int i=0;i<80&&t<maximum;++i) {
   float clearance=fr_cached(eye+ray*t,_mtl_u,s_height,s_heightSampler,s_tracks,s_trackMask).distance-t*slope;
   if(clearance<.004)break;t+=max(.001,clearance*.82);
  }
  return TraceOutput{float4(0),min(maximum,max(.06,t-.02))};
 }
 float2 uv=in.position.xy/viewport.xy;
 float4 carColor=float4(0,0,0,-1);maximum=_mtl_u.u_fractal[3].w;
 if(_mtl_u.u_fractal[6].z>1.5&&_mtl_u.u_fractal[6].z<2.5)return TraceOutput{carColor,maximum};
 uint2 conePixel=min(uint2(uv*ceil(viewport.xy/8.)),uint2(ceil(viewport.xy/8.))-1);
 t=max(.06,s_scene.read(conePixel).r-.02);
 if(t>=maximum)return TraceOutput{carColor,maximum};
 Field hit={1,0};bool found=false;int iterations=0;float2 terrainGradient=float2(0);float terrainHeight=0;
 for(int i=0;i<2048;++i) {
  if(i>=int(_mtl_u.u_fractal[4].w))break;iterations=i;
  float3 point=eye+ray*t;
  if(point.y>fr_height_max()+7) {t+=(point.y-fr_height_max()-4)*.72;if(t>maximum)break;continue;}
  float4 cache=_mtl_u.u_fractal[40];float2 grid=point.xz/cache.z;
  float4 valid=_mtl_u.u_fractal[75];bool cached=all(grid>=valid.xy+1.)&&all(grid<=valid.zw-2.);
  float2 fine=float2(0,3),coarse=float2(0,3);
  float epsilon=.0007+t*view.tangent/viewport.y*.12;
  if(cached) {
   fine=s_height.sample(s_heightSampler,(floor(grid)+.5)/cache.w,level(0)).rg;
   coarse=s_coarse.sample(s_coarseSampler,(floor(grid/8.)+.5)/(cache.w/8.),level(0)).rg;
   fine+=fr_feature_bounds((floor(grid)+.5)*cache.z,cache.z*.708,world);
   coarse+=fr_feature_bounds((floor(grid/8.)+.5)*cache.z*8.,cache.z*8.*.708,world);
   float skip=max(fr_empty_cell_advance(point,ray,cache.z,fine.x),fr_empty_cell_advance(point,ray,cache.z*8.,coarse.x));
   if(skip>.0001) {t+=max(.0001,skip);if(t>maximum)break;continue;}
  }
  TerrainSample terrain=fr_terrain_sample(point.xz,world);terrainGradient=terrain.gradient;terrainHeight=terrain.height;
  float residual=point.y-terrain.height;hit=Field{residual*fr_world_scale(world),0};
  if(hit.distance<epsilon) {found=true;break;}
  float slope=ray.y-dot(terrain.gradient,ray.xz);
  float advance=cached?max(fr_quadratic_cell_advance(residual,slope,point,ray,cache.z,fine.y),
       fr_quadratic_cell_advance(residual,slope,point,ray,cache.z*8.,coarse.y)):
       fr_terrain_advance(residual,terrain.gradient,ray,world.featureCount>0?15.:0.)*.90;
  t+=max(.0001,advance);if(t>maximum)break;
 }
 float4 surface=float4(0,0,0,-1);
 if(found) {
  float3 p=eye+ray*t,n;
  if(hit.material==0)n=normalize(float3(-terrainGradient.x,1,-terrainGradient.y));
  else n=fr_normal(p,_mtl_u,max(.001,t*.00003),s_tracks,s_trackMask);
  surface=hit.material==0?float4(terrainGradient,(terrainHeight-eye.y)-dot(terrainGradient,ray.xz*t),-2.):float4(1,1,1,2);
  int debug=int(_mtl_u.u_fractal[6].y);
  if(debug==1)surface=float4(n*.5+.5,1);
  if(debug==2)surface=float4(mix(float3(.10,.72,.78),float3(1,.20,.06),float(iterations)/_mtl_u.u_fractal[4].w),1);
 }
 if(!found&&maximum<_mtl_u.u_fractal[3].w)return TraceOutput{carColor,maximum};
 return TraceOutput{surface,found?t:maximum};
}
)MSL";
// Trace cars first through their ray/sphere intervals. Their depth bounds the
// landscape pass, avoiding geometry hidden behind the block bodies and wheels.
static const char* vehicleFragment=R"MSL(
float fr_post_ray(float3 ro,float3 rd,float maximum) {
 float a=dot(rd.xz,rd.xz),b=dot(ro.xz,rd.xz),c=dot(ro.xz,ro.xz)-.14*.14;
 if(a>1e-10){float d=b*b-a*c;if(d>=0){float root=sqrt(d);
  for(int side=0;side<2;++side){float t=(-b+(side?root:-root))/a,y=ro.y+rd.y*t;
   if(t>=.06&&t<maximum&&y>=-fr_rut_limit()&&y<=3.2)maximum=t;}
 }}
 for(int cap=0;cap<2;++cap){float y=cap?3.2:-fr_rut_limit();float3 q=ro-float3(0,y,0);float b=dot(q,rd),d=b*b-dot(q,q)+.14*.14;
  if(d<0)continue;float root=sqrt(d);
  for(int side=0;side<2;++side){float t=-b+(side?root:-root),h=ro.y+rd.y*t;
   if(t>=.06&&t<maximum&&(cap?h>=y:h<=y))maximum=t;}
 }
 return maximum;
}
// Vectrex cables are one crisp beam, rather than solid tubes near the camera.
float2 fr_vector_segment(float3 eye,float3 ray,float3 a,float3 b,float tangent,float height,float maximum){
 float3 v=b-a,oa=eye-a,cr=cross(v,ray);float den=dot(cr,cr),u=0;
 if(den>1e-10)u=clamp(dot(cross(oa,ray),cr)/den,0.,1.);
 else u=dot(a-eye,ray)>.25?0.:1.;
 float t=dot(a+v*u-eye,ray);if(t<.25||t>=maximum)return float2(maximum,0);
 float pixel=max(.00015,2.*t*tangent/height),d=length(eye+ray*t-(a+v*u));
 float coverage=v_stroke(d,pixel*.72);return coverage>.001?float2(t,coverage):float2(maximum,0);
}
struct ObjectOutput {float4 color [[color(0)]];float range [[color(1)]];float depth [[depth(any)]];};
fragment ObjectOutput xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]]) {
 if(_mtl_u.u_fractal[6].z>.5&&_mtl_u.u_fractal[6].z<1.5)discard_fragment();
 float4 viewport=_mtl_u.u_fractal[0];float2 uv=in.position.xy/viewport.xy;
 ViewRay view=fr_view(uv,_mtl_u);float3 ray=view.ray,eye=view.eye,color=float3(1);float maximum=_mtl_u.u_fractal[3].w,nearest=maximum,tag=2.;
 int object=int(_mtl_u.u_fractal[74].x);
 if(object<4){Vehicle car=fr_car(_mtl_u,object);int part=int(_mtl_u.u_fractal[74].y);
  ImplicitHit hit=fr_vehicle_component_ray(eye,ray,car,part,maximum);
  nearest=hit.distance;tag=1.;
  if(nearest<maximum){float pixel=max(.001,2.*nearest*view.tangent/viewport.y);
   color=float3(v_vehicle(eye+ray*nearest,hit.normal,ray,car,part>0&&part<5?49+part:hit.material,pixel,view.right,view.up));
   if(int(_mtl_u.u_fractal[6].y)==1)color=hit.normal*.5+.5;}
 }else if(object<4+int(_mtl_u.u_fractal[41].y)*2)nearest=fr_post_ray(eye-_mtl_u.u_fractal[42+object-4].xyz,ray,maximum);
 else if(object<8+int(_mtl_u.u_fractal[41].y)*2){int k=77+(object-4-int(_mtl_u.u_fractal[41].y)*2)*2;
  float4 from=_mtl_u.u_fractal[k],to=_mtl_u.u_fractal[k+1];tag=1.;color=float3(1.);
  float3 previous=from.xyz;int segments=from.w>1.?3:1;
  for(int i=1;i<=segments;++i){float u=float(i)/float(segments);float3 next=mix(from.xyz,to.xyz,u)-float3(0,from.w>1.?4.*to.w*u*(1.-u):0.,0);
   float2 beam=fr_vector_segment(eye,ray,previous,next,view.tangent,viewport.y,nearest);if(beam.x<nearest){nearest=beam.x;color=float3(beam.y);}previous=next;}
 }else{
  float4 pickup=_mtl_u.u_fractal[91];float spin=_mtl_u.u_fractal[1].w*.65;
  float3 ro=fr_rotate_z(fr_rotate_y(eye-pickup.xyz,-spin),-.785398),rd=fr_rotate_z(fr_rotate_y(ray,-spin),-.785398);
  ImplicitHit hit=fr_ray_block(ro,rd,float3(0),float3(.45),maximum);nearest=hit.distance;tag=1.;
  if(nearest<maximum){float3 q=ro+rd*nearest,n=hit.normal;
   float pixel=max(.001,2.*nearest*view.tangent/viewport.y);
   float3 rt=fr_rotate_z(fr_rotate_y(view.right,-spin),-.785398),up=fr_rotate_z(fr_rotate_y(view.up,-spin),-.785398);
   float3 foot=v_footprint(n,rd,rt,up,pixel),gap=max(float3(.45)-abs(q),float3(0))/max(foot,float3(.0003));
   float edge=v_stroke(min(max(gap.x,gap.y),min(max(gap.x,gap.z),max(gap.y,gap.z))),.85);
   float2 face=abs(n.x)>.5?q.zy:abs(n.y)>.5?q.xz:q.xy;
   float icon=0.;if(max(abs(face.x),abs(face.y))<.30){
    if(pickup.w<1.5)icon=v_stroke(min(abs(face.x),abs(abs(face.x)-.18)),pixel*.85);
    else icon=max(v_stroke(face.y-(.22-abs(face.x)*1.6),pixel*.85),v_stroke(face.y+.24,pixel*.85));}
   color=float3(max(edge,icon));
  }
 }
 if(nearest>=maximum)discard_fragment();
 return ObjectOutput{float4(color,tag),nearest,nearest/maximum};
}
)MSL";
static const char* rutFragment=R"MSL(
struct TraceOutput {float4 color [[color(0)]];float depth [[color(1)]];};
fragment TraceOutput xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],texture2d<float> s_depth [[texture(1)]],texture2d<float> s_carDepth [[texture(2)]],texture2d<float> s_height [[texture(3)]],texture2d<float> s_coarse [[texture(4)]],texture2d<float> s_tracks [[texture(5)]],texture2d<float> s_trackMask [[texture(6)]]) {
 uint2 pixel=uint2(in.position.xy);float4 original=s_scene.read(pixel);float t=s_depth.read(pixel).r,maximum=_mtl_u.u_fractal[3].w;
 if(_mtl_u.u_fractal[94].x>.5&&in.position.x>=_mtl_u.u_fractal[0].x*.5)return TraceOutput{original,t};
 if(t>=maximum-.001||t>=_mtl_u.u_fractal[3].w||(_mtl_u.u_fractal[6].z>1.5&&_mtl_u.u_fractal[6].z<2.5))return TraceOutput{original,t};
 float4 viewport=_mtl_u.u_fractal[0];float2 uv=in.position.xy/viewport.xy;
 ViewRay view=fr_view(uv,_mtl_u);float3 ray=view.ray,eye=view.eye;
 RutSample rut=fr_tracks((eye+ray*t).xz,_mtl_u,s_tracks,s_trackMask);
 if(rut.height>=0)return TraceOutput{_mtl_u.u_fractal[6].z>2.5?float4(.0,.0,.10,1):original,t};

 World world=fr_world(_mtl_u);Field hit={1,0};float2 gradient=float2(0);bool found=false;int iterations=0;float terrainHeight=0;
 for(int i=0;i<512&&t<maximum;++i) {
  iterations=i;float3 point=eye+ray*t;TerrainSample terrain=fr_terrain_sample(point.xz,world);rut=fr_tracks(point.xz,_mtl_u,s_tracks,s_trackMask);
  terrain.height+=rut.height;terrain.gradient+=rut.gradient;gradient=terrain.gradient;terrainHeight=terrain.height;
  float residual=point.y-terrain.height,epsilon=.0007+t*view.tangent/viewport.y*.12;
  hit=Field{residual*(rut.height<0?fr_soil_scale(world):fr_world_scale(world)),0};
  if(hit.distance<epsilon){found=true;break;}
  float slope=ray.y-dot(terrain.gradient,ray.xz);
  float advance=rut.height<0?fr_quadratic_cell_advance(residual,slope,point,ray,fr_rut_cell(),3.+rut.curvature+(world.featureCount>0?15.:0.)):fr_terrain_advance(residual,terrain.gradient,ray,world.featureCount>0?15.:0.)*.90;
  t+=max(.0001,advance);
 }
 if(!found){if(maximum<_mtl_u.u_fractal[3].w)return TraceOutput{s_height.read(pixel),maximum};return TraceOutput{float4(0,0,0,-1),_mtl_u.u_fractal[3].w};}
 float3 n=hit.material==0?normalize(float3(-gradient.x,1,-gradient.y)):fr_normal(eye+ray*t,_mtl_u,.002,s_tracks,s_trackMask);
 float4 surface=hit.material==0?float4(gradient,(terrainHeight-eye.y)-dot(gradient,ray.xz*t),-2.-min(.32,-rut.height)):float4(1,1,1,2);
 if(_mtl_u.u_fractal[6].z>2.5)surface=float4(rut.compaction,-rut.height/fr_rut_limit(),.01,1);
 int debug=int(_mtl_u.u_fractal[6].y);if(debug==1)surface=float4(n*.5+.5,1);if(debug==2)surface=float4(mix(float3(.10,.72,.78),float3(1,.20,.06),float(iterations)/512.),1);
 return TraceOutput{surface,t};
}
)MSL";
static const char* heightFragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
fragment float2 xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]]) {
 float4 cache=_mtl_u.u_fractal[40];float2 pixel=floor(in.position.xy);
 float2 global=pixel+cache.w*ceil((cache.xy-pixel)/cache.w);
 World world=fr_world(_mtl_u);world.featureCount=0;
 return fr_terrain_bounds((global+.5)*cache.z,world,cache.z);
}
)MSL";
static const char* poolFragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
fragment float2 xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],sampler s_sceneSampler [[sampler(0)]]) {
 uint2 first=uint2(in.position.xy)*8;float2 bound=float2(-1e8,0);
 for(uint y=0;y<8;++y)for(uint x=0;x<8;++x)bound=max(bound,s_scene.read(first+uint2(x,y)).rg);
 return bound;
}
)MSL";
static const char* copyFragment=R"MSL(
struct Input {float4 position [[position]];float2 uv [[user(locn0)]];};
fragment float4 xlatMtlMain(Input in [[stage_in]],constant Global& _mtl_u [[buffer(0)]],texture2d<float> s_scene [[texture(0)]],sampler s_sceneSampler [[sampler(0)]],texture2d<float> s_depth [[texture(1)]],sampler s_depthSampler [[sampler(1)]],texture2d<float> s_carDepth [[texture(2)]],texture2d<float> s_height [[texture(3)]]) {
 // The close-range gunner display uses only crisp HUD symbols on black.
 if(_mtl_u.u_fractal[94].x>.5&&in.uv.x>=.5)return float4(0,0,0,1);
 ViewRay view=fr_view(in.uv,_mtl_u);int2 size=int2(_mtl_u.u_fractal[0].xy);int minX=view.gunner?size.x/2:0,maxX=_mtl_u.u_fractal[89].z>.5&&!view.gunner?size.x/2-1:size.x-1;float2 grid=in.uv*float2(size)-.5,f=fract(grid);int2 base=int2(floor(grid));
 float coverage=0;float4 surface=float4(0);float range=0;
 for(int y=0;y<2;++y)for(int x=0;x<2;++x){uint2 p=uint2(clamp(base+int2(x,y),int2(minX,0),int2(maxX,size.y-1)));float4 sample=s_scene.read(p);
  if(sample.a<-1.5){float weight=(x?f.x:1.-f.x)*(y?f.y:1.-f.y);surface+=sample*weight;range+=s_depth.read(p).r*weight;coverage+=weight;}
 }
 float t=_mtl_u.u_fractal[3].w;float3 color=float3(0);float lineCoverage=0;
 if(coverage>.001){surface/=coverage;range/=coverage;
  float3 ray=view.ray;
  float denominator=ray.y-dot(surface.xy,ray.xz),planeT=surface.z/(abs(denominator)>.00001?denominator:-.00001);
  t=planeT>.06&&abs(planeT-range)<max(.3,range*.12)?planeT:range;
  uint2 op=min(uint2(in.position.xy),uint2(s_height.get_width()-1,s_height.get_height()-1));
  float4 object=s_carDepth.read(op);if(coverage>.5&&s_height.read(op).r<t&&object.a>.5)return float4(object.rgb,object.a>1.5?0.:1.);
  float3 normal=normalize(float3(-surface.x,1,-surface.y)),point=view.eye+ray*t;
  float3 footprint=v_footprint(normal,ray,view.right,view.up,2.*t*view.tangent/_mtl_u.u_fractal[7].w);
  float depth=max(0.,-surface.a-2.);color=float3(v_terrain(point,footprint,depth,min(1.,depth*20.)));
 }
 float edgeWidth=max(.0001,fwidth(coverage));lineCoverage=smoothstep(.5-edgeWidth*.7,.5+edgeWidth*.7,coverage);
 if(coverage<.5)t=_mtl_u.u_fractal[3].w;
 float silhouette=(1.-smoothstep(0.,edgeWidth*1.1,abs(coverage-.5)))*.60;
 color=max(color*lineCoverage,float3(silhouette));
 uint2 objectPixel=min(uint2(in.position.xy),uint2(s_height.get_width()-1,s_height.get_height()-1));
 float objectRange=s_height.read(objectPixel).r;float4 object=s_carDepth.read(objectPixel);
 if(objectRange<t&&object.a>.5)return float4(object.rgb,object.a>1.5?0.:1.);
 if(_mtl_u.u_fractal[6].y>0||_mtl_u.u_fractal[6].z>2.5)return s_scene.read(uint2(clamp(int2(in.uv*float2(size)),int2(0),size-1)));
 return float4(color,1);
}
)MSL";

struct FractalRenderer::Impl {
 bgfx::TextureHandle presentTexture=BGFX_INVALID_HANDLE,presentCoefficients=BGFX_INVALID_HANDLE;bgfx::FrameBufferHandle presentTarget=BGFX_INVALID_HANDLE;bgfx::ProgramHandle presentProgram=BGFX_INVALID_HANDLE;
 bgfx::TextureHandle displayTexture=BGFX_INVALID_HANDLE;bgfx::FrameBufferHandle displayTarget=BGFX_INVALID_HANDLE;
 bgfx::ProgramHandle displayProgram=BGFX_INVALID_HANDLE,phosphorProgram=BGFX_INVALID_HANDLE;int displayWidth=0,displayHeight=0,crtWidth=0,crtHeight=0;
 bgfx::TextureHandle phosphorTexture[2]={BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE};
 bgfx::FrameBufferHandle phosphorTarget[2]={BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE};
 bool lastTactical=false,crtEffects=true,phosphorValid=false,phosphorReset=true;int phosphorIndex=0,lastHiddenHead=-2;
 std::chrono::steady_clock::time_point displayFrame{};double displayTime=0;
 glm::vec2 beamJitter{};
 bgfx::ProgramHandle rutProgram=BGFX_INVALID_HANDLE;bgfx::TextureHandle compositeTexture=BGFX_INVALID_HANDLE,compositeDepth=BGFX_INVALID_HANDLE;bgfx::FrameBufferHandle compositeTarget=BGFX_INVALID_HANDLE;
 bgfx::ProgramHandle poolProgram=BGFX_INVALID_HANDLE,heightProgram=BGFX_INVALID_HANDLE,trace=BGFX_INVALID_HANDLE,vehicles=BGFX_INVALID_HANDLE,copy=BGFX_INVALID_HANDLE;
 bgfx::UniformHandle trackSampler=BGFX_INVALID_HANDLE,trackMaskSampler=BGFX_INVALID_HANDLE;
 bgfx::TextureHandle trackTexture=BGFX_INVALID_HANDLE,trackMaskTexture=BGFX_INVALID_HANDLE;
 uint64_t lastTrackRevision=0;uint32_t trackSeed=0;bool trackWindowValid=false;int trackMinX=0,trackMinZ=0;
 static constexpr int trackSize=2048,trackTiles=trackSize/fr::Deformation::tileSize;
 struct TrackSlot {int x=0,z=0;uint64_t revision=0;};std::array<TrackSlot,trackTiles*trackTiles> trackSlots{};
 bgfx::UniformHandle coarseSampler=BGFX_INVALID_HANDLE,heightSampler=BGFX_INVALID_HANDLE,uniform=BGFX_INVALID_HANDLE,depthSampler=BGFX_INVALID_HANDLE,carDepthSampler=BGFX_INVALID_HANDLE,sampler=BGFX_INVALID_HANDLE;
 bgfx::VertexBufferHandle quad=BGFX_INVALID_HANDLE;bgfx::IndexBufferHandle indices=BGFX_INVALID_HANDLE;
 bgfx::TextureHandle texture=BGFX_INVALID_HANDLE,worldColor=BGFX_INVALID_HANDLE,worldDepth=BGFX_INVALID_HANDLE,objectDepth=BGFX_INVALID_HANDLE,coneColor=BGFX_INVALID_HANDLE,coneDepth=BGFX_INVALID_HANDLE,finalDepth=BGFX_INVALID_HANDLE,heightTexture=BGFX_INVALID_HANDLE,coarseTexture=BGFX_INVALID_HANDLE;bgfx::FrameBufferHandle framebuffer=BGFX_INVALID_HANDLE,worldTarget=BGFX_INVALID_HANDLE,coneTarget=BGFX_INVALID_HANDLE,heightTarget=BGFX_INVALID_HANDLE,coarseTarget=BGFX_INVALID_HANDLE;
 std::array<glm::vec4,160> data{};std::vector<uint32_t> pixels;int width=0,height=0,targetWidth=0,targetHeight=0;
 static constexpr int cacheSize=2048;static constexpr float cacheCell=.75f;
 int profile=0;TerrainCache terrainCache;
 std::vector<std::array<int,4>> updateCache(fr::World world,glm::dvec3 eye) {
  if(!bgfx::isValid(heightTexture)) {
   heightTexture=bgfx::createTexture2D(cacheSize,cacheSize,false,1,bgfx::TextureFormat::RG32F,BGFX_TEXTURE_RT|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
   heightTarget=bgfx::createFrameBuffer(1,&heightTexture,false);
   coarseTexture=bgfx::createTexture2D(cacheSize/8,cacheSize/8,false,1,bgfx::TextureFormat::RG32F,BGFX_TEXTURE_RT|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
   coarseTarget=bgfx::createFrameBuffer(1,&coarseTexture,false);
  }
  int x=int(std::floor((eye.x/cacheCell-cacheSize/2)/8))*8,z=int(std::floor((eye.z/cacheCell-cacheSize/2)/8))*8;
  auto plan=terrainCache.advance(x,z,world.seed);data[40]={x,z,cacheCell,cacheSize};
  data[75]={plan.valid[0],plan.valid[1],plan.valid[2],plan.valid[3]};return plan.updates;
 }
 void updateTracks(fr::World world,glm::dvec3 eye) {
  uint64_t revision=world.tracks?world.tracks->revision():0;
  int minX=int(std::floor(eye.x/(fr::fr_rut_cell()*fr::Deformation::tileSize)))-trackTiles/2;
  int minZ=int(std::floor(eye.z/(fr::fr_rut_cell()*fr::Deformation::tileSize)))-trackTiles/2;
  data[70]={minX*fr::Deformation::tileSize,minZ*fr::Deformation::tileSize,fr::fr_rut_cell(),trackSize};
  bool newSeed=trackWindowValid&&trackSeed!=world.seed;
  if(!bgfx::isValid(trackTexture)) {
   std::vector<glm::vec2> zero(trackSize*trackSize,glm::vec2(0));
   trackTexture=bgfx::createTexture2D(trackSize,trackSize,false,1,bgfx::TextureFormat::RG32F,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
   bgfx::updateTexture2D(trackTexture,0,0,0,0,trackSize,trackSize,bgfx::copy(zero.data(),uint32_t(zero.size()*sizeof(glm::vec2))));
   trackMaskTexture=bgfx::createTexture2D(trackTiles,trackTiles,false,1,bgfx::TextureFormat::R8,BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
  }
  if(trackWindowValid&&!newSeed&&revision==lastTrackRevision&&minX==trackMinX&&minZ==trackMinZ)return;
  std::array<glm::vec2,fr::Deformation::tileSize*fr::Deformation::tileSize> nodes{};
  std::array<uint8_t,trackTiles*trackTiles> mask{};
  std::array<uint8_t,(trackTiles+2)*(trackTiles+2)> occupied{};
  for(int z=minZ;z<minZ+trackTiles;++z)for(int x=minX;x<minX+trackTiles;++x) {
   int sx=x&(trackTiles-1),sz=z&(trackTiles-1);auto& slot=trackSlots[sz*trackTiles+sx];
   uint64_t current=world.tracks?world.tracks->tileRevision(x,z):0;
   if((slot.x!=x||slot.z!=z||slot.revision!=current||newSeed)&&(slot.revision||current)) {
    if(world.tracks)world.tracks->copyTile(x,z,nodes);else nodes.fill(glm::vec2(0));
    bgfx::updateTexture2D(trackTexture,0,0,sx*fr::Deformation::tileSize,sz*fr::Deformation::tileSize,fr::Deformation::tileSize,fr::Deformation::tileSize,bgfx::copy(nodes.data(),uint32_t(sizeof(nodes))));
   }
   slot={x,z,current};
   occupied[(z-minZ+1)*(trackTiles+2)+x-minX+1]=current?255:0;
  }
  if(world.tracks)for(int z=-1;z<=trackTiles;++z)for(int x=-1;x<=trackTiles;++x)
   if(x==-1||z==-1||x==trackTiles||z==trackTiles)occupied[(z+1)*(trackTiles+2)+x+1]=world.tracks->occupied(minX+x,minZ+z)?255:0;
  for(int z=0;z<trackTiles;++z)for(int x=0;x<trackTiles;++x){
   uint8_t any=0;for(int dz=0;dz<3;++dz)for(int dx=0;dx<3;++dx)any|=occupied[(z+dz)*(trackTiles+2)+x+dx];
   mask[((z+minZ)&(trackTiles-1))*trackTiles+((x+minX)&(trackTiles-1))]=any;
  }
  bgfx::updateTexture2D(trackMaskTexture,0,0,0,0,trackTiles,trackTiles,bgfx::copy(mask.data(),uint32_t(mask.size())));
  trackMinX=minX;trackMinZ=minZ;trackSeed=world.seed;trackWindowValid=true;
 }
 float renderScale=.4375f;int adjustFrames=0;
 glm::dvec3 lastEye{};glm::dquat lastOrientation{1,0,0,0};
 std::array<fr::Vehicle,4> lastCars{};fr::World lastWorld{};
 std::array<double,24> workSamples{};
 int stillFrames=0,tile=0,lastDebug=-1;bool refining=false;double lastFov=0,lastAspect=0;
 void target(int w,int h) {
  width=w;height=h;
  if(targetWidth==w&&targetHeight==h&&bgfx::isValid(framebuffer))return;
  if(bgfx::isValid(compositeTarget))bgfx::destroy(compositeTarget);if(bgfx::isValid(compositeTexture))bgfx::destroy(compositeTexture);if(bgfx::isValid(compositeDepth))bgfx::destroy(compositeDepth);
  if(bgfx::isValid(framebuffer))bgfx::destroy(framebuffer);if(bgfx::isValid(texture))bgfx::destroy(texture);
  if(bgfx::isValid(coneTarget))bgfx::destroy(coneTarget);
  if(bgfx::isValid(coneColor))bgfx::destroy(coneColor);if(bgfx::isValid(coneDepth))bgfx::destroy(coneDepth);
  const uint64_t flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
  coneColor=bgfx::createTexture2D((w+7)/8,(h+7)/8,false,1,bgfx::TextureFormat::RGBA8,flags);
  coneDepth=bgfx::createTexture2D((w+7)/8,(h+7)/8,false,1,bgfx::TextureFormat::R32F,flags);
  bgfx::TextureHandle coneAttachments[]={coneColor,coneDepth};coneTarget=bgfx::createFrameBuffer(2,coneAttachments,false);
  if(bgfx::isValid(finalDepth))bgfx::destroy(finalDepth);
  finalDepth=bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::R32F,flags);
  targetWidth=w;targetHeight=h;texture=bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
  compositeTexture=bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA32F,BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);compositeDepth=bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::R32F,flags);bgfx::TextureHandle compositeAttachments[]={compositeTexture,compositeDepth};compositeTarget=bgfx::createFrameBuffer(2,compositeAttachments,false);
  bgfx::TextureHandle finalAttachments[]={texture,finalDepth};framebuffer=bgfx::createFrameBuffer(2,finalAttachments,false);
 }
};
#include "VectorDisplay.inl"
FractalRenderer::FractalRenderer():p(std::make_unique<Impl>()) {
 if(bgfx::getRendererType()!=bgfx::RendererType::Metal)return;
 if(const char* mode=std::getenv("DUNE_RENDER_PROFILE"))p->profile=std::strcmp(mode,"terrain")==0?1:std::strcmp(mode,"vehicles")==0?2:std::strcmp(mode,"tracks")==0?3:0;
 const float emptyObject[]={0,0,0,-1},emptyRange[]={750,0,0,1};bgfx::setPaletteColor(0,emptyObject);bgfx::setPaletteColor(1,emptyRange);
 p->uniform=bgfx::createUniform("u_fractal",bgfx::UniformType::Vec4,160);p->sampler=bgfx::createUniform("s_scene",bgfx::UniformType::Sampler);p->depthSampler=bgfx::createUniform("s_depth",bgfx::UniformType::Sampler);p->carDepthSampler=bgfx::createUniform("s_carDepth",bgfx::UniformType::Sampler);
 p->trackSampler=bgfx::createUniform("s_tracks",bgfx::UniformType::Sampler);p->trackMaskSampler=bgfx::createUniform("s_trackMask",bgfx::UniformType::Sampler);
 p->coarseSampler=bgfx::createUniform("s_coarse",bgfx::UniformType::Sampler);
 p->poolProgram=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+poolFragment,true,1),true);
 p->heightSampler=bgfx::createUniform("s_height",bgfx::UniformType::Sampler);
 p->heightProgram=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+heightFragment,true),true);
 bgfx::setViewMode(5,bgfx::ViewMode::Sequential);bgfx::setViewMode(6,bgfx::ViewMode::Sequential);
 p->trace=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+vectorStyleSource+fragment,true,7),true);
 std::string helpers(fragment);helpers.resize(helpers.find("struct TraceOutput"));
 p->rutProgram=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+vectorStyleSource+helpers+rutFragment,true,7),true);
 p->vehicles=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+vectorStyleSource+helpers+vehicleFragment,true),true);
 p->copy=bgfx::createProgram(shader(std::string(prelude)+vertex,false),shader(std::string(prelude)+fractalFieldSource+worldHelpers+vectorStyleSource+copyFragment,true,4),true);
 bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).end();
 const float vertices[]={-1,-1,0,1,-1,0,1,1,0,-1,1,0};const uint16_t indices[]={0,1,2,0,2,3};
 p->quad=bgfx::createVertexBuffer(bgfx::copy(vertices,sizeof(vertices)),layout);p->indices=bgfx::createIndexBuffer(bgfx::copy(indices,sizeof(indices)));
}
FractalRenderer::~FractalRenderer() {
 if(bgfx::isValid(p->presentTarget))bgfx::destroy(p->presentTarget);if(bgfx::isValid(p->presentTexture))bgfx::destroy(p->presentTexture);if(bgfx::isValid(p->presentCoefficients))bgfx::destroy(p->presentCoefficients);if(bgfx::isValid(p->presentProgram))bgfx::destroy(p->presentProgram);
 for(int i=0;i<2;++i){if(bgfx::isValid(p->phosphorTarget[i]))bgfx::destroy(p->phosphorTarget[i]);if(bgfx::isValid(p->phosphorTexture[i]))bgfx::destroy(p->phosphorTexture[i]);}
 if(bgfx::isValid(p->phosphorProgram))bgfx::destroy(p->phosphorProgram);
 if(bgfx::isValid(p->displayTarget))bgfx::destroy(p->displayTarget);if(bgfx::isValid(p->displayTexture))bgfx::destroy(p->displayTexture);if(bgfx::isValid(p->displayProgram))bgfx::destroy(p->displayProgram);
 if(bgfx::isValid(p->rutProgram))bgfx::destroy(p->rutProgram);if(bgfx::isValid(p->compositeTarget))bgfx::destroy(p->compositeTarget);if(bgfx::isValid(p->compositeTexture))bgfx::destroy(p->compositeTexture);if(bgfx::isValid(p->compositeDepth))bgfx::destroy(p->compositeDepth);
 std::cout<<"VECTOR_IMAGE width="<<p->displayWidth<<" height="<<p->displayHeight<<" glow_width="<<p->crtWidth<<" glow_height="<<p->crtHeight<<'\n';
 std::cout<<"FRACTAL_IMAGE width="<<p->width<<" height="<<p->height<<" mode="<<(p->refining?"still-refinement":"moving")<<'\n';
 if(bgfx::isValid(p->trackTexture))bgfx::destroy(p->trackTexture);if(bgfx::isValid(p->trackMaskTexture))bgfx::destroy(p->trackMaskTexture);
 if(bgfx::isValid(p->trackSampler))bgfx::destroy(p->trackSampler);if(bgfx::isValid(p->trackMaskSampler))bgfx::destroy(p->trackMaskSampler);
 if(bgfx::isValid(p->coarseTarget))bgfx::destroy(p->coarseTarget);if(bgfx::isValid(p->coarseTexture))bgfx::destroy(p->coarseTexture);
 if(bgfx::isValid(p->coarseSampler))bgfx::destroy(p->coarseSampler);if(bgfx::isValid(p->poolProgram))bgfx::destroy(p->poolProgram);
 if(bgfx::isValid(p->heightTarget))bgfx::destroy(p->heightTarget);if(bgfx::isValid(p->heightTexture))bgfx::destroy(p->heightTexture);
 if(bgfx::isValid(p->heightProgram))bgfx::destroy(p->heightProgram);if(bgfx::isValid(p->heightSampler))bgfx::destroy(p->heightSampler);
 if(bgfx::isValid(p->framebuffer))bgfx::destroy(p->framebuffer);if(bgfx::isValid(p->texture))bgfx::destroy(p->texture);
 if(bgfx::isValid(p->finalDepth))bgfx::destroy(p->finalDepth);
 if(bgfx::isValid(p->carDepthSampler))bgfx::destroy(p->carDepthSampler);
 if(bgfx::isValid(p->coneTarget))bgfx::destroy(p->coneTarget);
 if(bgfx::isValid(p->coneColor))bgfx::destroy(p->coneColor);if(bgfx::isValid(p->coneDepth))bgfx::destroy(p->coneDepth);
 if(bgfx::isValid(p->worldTarget))bgfx::destroy(p->worldTarget);
 if(bgfx::isValid(p->worldColor))bgfx::destroy(p->worldColor);if(bgfx::isValid(p->worldDepth))bgfx::destroy(p->worldDepth);if(bgfx::isValid(p->objectDepth))bgfx::destroy(p->objectDepth);
 if(bgfx::isValid(p->vehicles))bgfx::destroy(p->vehicles);
 if(bgfx::isValid(p->depthSampler))bgfx::destroy(p->depthSampler);
 if(bgfx::isValid(p->trace))bgfx::destroy(p->trace);if(bgfx::isValid(p->copy))bgfx::destroy(p->copy);
 if(bgfx::isValid(p->uniform))bgfx::destroy(p->uniform);if(bgfx::isValid(p->sampler))bgfx::destroy(p->sampler);
 if(bgfx::isValid(p->quad))bgfx::destroy(p->quad);if(bgfx::isValid(p->indices))bgfx::destroy(p->indices);
}
bool FractalRenderer::available() const {return bgfx::isValid(p->heightProgram)&&bgfx::isValid(p->trace)&&bgfx::isValid(p->rutProgram)&&bgfx::isValid(p->vehicles)&&bgfx::isValid(p->copy);}
void FractalRenderer::draw(const infinity::ellipsoid::Camera& camera,fr::World world,const std::array<fr::Vehicle,4>& cars,int width,int height,double time,bool native,bool quality,int debug,int hiddenHead,double pixelDensity,double frameWorkSeconds,const infinity::ellipsoid::Camera* gunner,const std::array<glm::vec4,8>& shots,const std::vector<glm::vec4>& pickups,const GunnerMap* tactical) {
 native=native&&available();
 if(p->lastTactical!=(tactical!=nullptr))p->phosphorReset=true;p->lastTactical=tactical!=nullptr;
 auto geometryCars=cars;for(auto& car:geometryCars) {car.systems.w=0;car.attitude.w=0;} // Wheel rotation changes vector spokes.
 uint64_t trackRevision=world.tracks?world.tracks->revision():0;
 bool moved=gunner||trackRevision!=p->lastTrackRevision||camera.verticalFov!=p->lastFov||camera.aspect!=p->lastAspect||p->lastDebug!=debug||glm::length(camera.eye-p->lastEye)>.001||std::abs(glm::dot(camera.orientation,p->lastOrientation))<.99999999||
  std::memcmp(geometryCars.data(),p->lastCars.data(),sizeof(geometryCars))!=0||world.phase!=p->lastWorld.phase||world.seed!=p->lastWorld.seed||world.route!=p->lastWorld.route;
 if(world.seed!=p->lastWorld.seed||glm::length(camera.eye-p->lastEye)>12.||std::abs(glm::dot(camera.orientation,p->lastOrientation))<.80||debug!=p->lastDebug||hiddenHead!=p->lastHiddenHead)p->phosphorReset=true;
 p->lastHiddenHead=hiddenHead;
 p->stillFrames=moved?0:p->stillFrames+1;
 bool refine=native&&quality&&p->stillFrames>12;
 p->lastFov=camera.verticalFov;p->lastAspect=camera.aspect;p->lastEye=camera.eye;p->lastOrientation=camera.orientation;p->lastCars=geometryCars;p->lastWorld=world;p->lastDebug=debug;
 int baseWidth=int(width/std::max(1.,pixelDensity));
 float minimumScale=std::min(1.f,(gunner?384.f:320.f)/std::max(1,baseWidth));p->renderScale=std::max(minimumScale,p->renderScale);
 if(native&&!refine&&!p->refining) {
  p->workSamples[p->adjustFrames++]=std::clamp(frameWorkSeconds,.001,.12);
  if(p->adjustFrames==int(p->workSamples.size())){
   auto samples=p->workSamples;std::sort(samples.begin(),samples.end());double budget=samples[21];
   if(budget>.0138){float desired=p->renderScale*std::sqrt(.0122/budget);
    p->renderScale=std::clamp(std::floor(std::max(desired,p->renderScale-.0625f)*32)/32.f,minimumScale,1.f);
   }else if(budget<.0095)p->renderScale=std::min(1.f,p->renderScale+.03125f);
   p->adjustFrames=0;
  }
 }
 int w=native?(quality?int(baseWidth*p->renderScale):std::max(240,int(baseWidth*p->renderScale*.75))):quality?320:160;
 w=std::max(16,w/16*16);int h=int(double(w)*height/width);
 bool fill=p->width!=w||p->height!=h;
 p->refining=refine;int tile=refine&&!fill?(p->tile++%8):-1;
 if(native){p->target(baseWidth,int(double(baseWidth)*height/width));p->width=w;p->height=h;}else {p->width=w;p->height=h;p->pixels.resize(w*h);}
 p->data[7].w=height;p->data[0]={w,h,std::tan(camera.verticalFov*.5),camera.aspect};p->data[1]=glm::vec4(camera.eye,time);
 p->data[2]=glm::vec4(camera.orientation*glm::dvec3(1,0,0),.06);p->data[3]=glm::vec4(camera.orientation*glm::dvec3(0,1,0),750);
 p->data[4]=glm::vec4(camera.orientation*glm::dvec3(0,0,1),quality?2048:512);
 p->data[5]={world.phase,float(world.seed),0,0};p->data[6]={float(hiddenHead),float(debug),float(p->profile),0};
 p->data[89]={0,0,gunner?1:0,0};
 if(gunner){p->data[85]=glm::vec4(gunner->eye,0);p->data[86]=glm::vec4(gunner->orientation*glm::dvec3(1,0,0),0);p->data[87]=glm::vec4(gunner->orientation*glm::dvec3(0,1,0),0);p->data[88]=glm::vec4(gunner->orientation*glm::dvec3(0,0,1),0);p->data[89].x=std::tan(gunner->verticalFov*.5);p->data[89].y=gunner->aspect;}
 p->data[94]={tactical?1:0,0,0,0};
 p->data[76].x=std::min(32,world.featureCount);
 for(int i=0;i<int(p->data[76].x);++i){p->data[96+i*2]=world.features[i].placement;p->data[97+i*2]=world.features[i].shape;}
 for(int i=0;i<8;++i)p->data[77+i]=shots[i];
 for(int i=0;i<4;++i)std::memcpy(p->data.data()+8+i*8,&cars[i],sizeof(fr::Vehicle));
 if(native) {
  p->updateTracks(world,camera.eye);p->lastTrackRevision=trackRevision;
  auto updates=p->updateCache(world,camera.eye);
  if(world.route==1&&world.markerCache){p->data[41]={0,world.markerCount,1,0};auto radii=fr::fr_circuit_radii(world.seed);p->data[71]={radii.x,radii.y,0,0};for(int i=0;i<world.markerCount*2;++i)p->data[42+i]=world.markerCache[i];}
  else {int first=std::max(1,int(std::floor((camera.eye.z-800)/180))),last=int(std::ceil((camera.eye.z+800)/180));if(world.markerCount>0)last=std::min(world.markerCount,last);
   p->data[41]={first,std::max(0,last-first+1),0,0};for(int gate=first;gate<=last;++gate)for(int side=0;side<2;++side)p->data[42+(gate-first)*2+side]=glm::vec4(fr::fr_checkpoint_post(gate,side?1:-1,world),0);
  }
  const bgfx::ViewId order[]={5,6,1,4,0,7,2,3,8,9,10};bgfx::setViewOrder(0,11,order);
  if(!updates.empty()) {
   bgfx::setViewFrameBuffer(5,p->heightTarget);bgfx::setViewRect(5,0,0,p->cacheSize,p->cacheSize);bgfx::setViewClear(5,BGFX_CLEAR_NONE);
   for(auto rectangle:updates) {bgfx::setScissor(rectangle[0],rectangle[1],rectangle[2],rectangle[3]);bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_R|BGFX_STATE_WRITE_G);bgfx::submit(5,p->heightProgram);}
  }
  if(!updates.empty()) {
   bgfx::setViewFrameBuffer(6,p->coarseTarget);bgfx::setViewRect(6,0,0,p->cacheSize/8,p->cacheSize/8);bgfx::setViewClear(6,BGFX_CLEAR_NONE);
   for(auto rectangle:updates) {bgfx::setScissor(rectangle[0]/8,rectangle[1]/8,(rectangle[0]+rectangle[2]+7)/8-rectangle[0]/8,(rectangle[1]+rectangle[3]+7)/8-rectangle[1]/8);bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setTexture(0,p->sampler,p->heightTexture);bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_R|BGFX_STATE_WRITE_G);bgfx::submit(6,p->poolProgram);}
  }
  bgfx::setViewFrameBuffer(1,p->worldTarget);bgfx::setViewRect(1,0,0,width,height);
  bgfx::setViewClear(1,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,1.f,0,0,1);bgfx::touch(1);
  for(int pane=0;pane<(gunner?2:1);++pane){if(pane&&tactical)continue;const auto& objectCamera=pane?*gunner:camera;int paneWidth=gunner?width/2:width,paneX=pane*width/2;
  auto bounds=[&](glm::dvec3 center,double radius){
   auto q=glm::conjugate(objectCamera.orientation)*(center-objectCamera.eye);std::array<int,4> rectangle{0,0,paneWidth,height};
   if(q.z+radius<=.06)return std::array<int,4>{0,0,0,0};
   if(q.z-radius>.06){double tangent=std::tan(objectCamera.verticalFov*.5),loX=1e20,hiX=-1e20,loY=1e20,hiY=-1e20;
    for(int z:{-1,1})for(int v:{-1,1}){double d=q.z+z*radius;double x=(q.x+v*radius)/(d*tangent*objectCamera.aspect),y=(q.y+v*radius)/(d*tangent);
     loX=std::min(loX,x);hiX=std::max(hiX,x);loY=std::min(loY,y);hiY=std::max(hiY,y);}
    int x=std::clamp(int(std::floor((loX*.5+.5)*paneWidth))-2,0,paneWidth),y=std::clamp(int(std::floor((.5-hiY*.5)*height))-2,0,height);
    rectangle={x,y,std::clamp(int(std::ceil((hiX*.5+.5)*paneWidth))+2,0,paneWidth)-x,std::clamp(int(std::ceil((.5-loY*.5)*height))+2,0,height)-y};
   }return rectangle;
  };
  auto cableBounds=[&](glm::dvec3 a,glm::dvec3 b,double sag,bool cable){
   double loX=1e20,hiX=-1e20,loY=1e20,hiY=-1e20,tangent=std::tan(objectCamera.verticalFov*.5);bool visible=false;
   auto project=[&](glm::dvec3 q){if(q.z<.06)return;visible=true;double x=q.x/(q.z*tangent*objectCamera.aspect),y=q.y/(q.z*tangent);loX=std::min(loX,x);hiX=std::max(hiX,x);loY=std::min(loY,y);hiY=std::max(hiY,y);};
   auto edge=[&](glm::dvec3 a,glm::dvec3 b){project(a);project(b);if((a.z<.06)!=(b.z<.06)){auto q=a+(b-a)*((.06-a.z)/(b.z-a.z));q.z=.06;project(q);}};
   glm::dvec3 previous=glm::conjugate(objectCamera.orientation)*(a-objectCamera.eye);int count=cable?3:1;double radius=cable?.009:.012;
   for(int segment=1;segment<=count;++segment){double u=double(segment)/count;auto point=glm::mix(a,b,u)-glm::dvec3(0,cable?4*sag*u*(1-u):0,0);auto next=glm::conjugate(objectCamera.orientation)*(point-objectCamera.eye);
    std::array<glm::dvec3,8> start,end;for(int i=0;i<8;++i){glm::dvec3 d=radius*glm::dvec3(i&1?1:-1,i&2?1:-1,i&4?1:-1);start[i]=previous+d;end[i]=next+d;edge(start[i],end[i]);}
    for(int i=0;i<8;++i)for(int bit:{1,2,4})if((i^bit)>i){edge(start[i],start[i^bit]);edge(end[i],end[i^bit]);}previous=next;
   }
   if(!visible)return std::array<int,4>{0,0,0,0};
   int x=std::clamp(int(std::floor((loX*.5+.5)*paneWidth))-2,0,paneWidth),y=std::clamp(int(std::floor((.5-hiY*.5)*height))-2,0,height);
   return std::array<int,4>{x,y,std::clamp(int(std::ceil((hiX*.5+.5)*paneWidth))+2,0,paneWidth)-x,std::clamp(int(std::ceil((.5-loY*.5)*height))+2,0,height)-y};
  };
  auto vehicleBounds=[&](const fr::Vehicle& car,int part){
   double loX=1e20,hiX=-1e20,loY=1e20,hiY=-1e20,tangent=std::tan(objectCamera.verticalFov*.5);bool visible=false;
   auto project=[&](glm::dvec3 q){if(q.z<.06)return;visible=true;double x=q.x/(q.z*tangent*objectCamera.aspect),y=q.y/(q.z*tangent);
    loX=std::min(loX,x);hiX=std::max(hiX,x);loY=std::min(loY,y);hiY=std::max(hiY,y);};
   {
    glm::vec3 center=part>=5?fr::fr_turret_center(car,part-5):part?fr::fr_wheel_center(car,part-1):fr::fr_block_center(car);
    glm::vec3 half=part>=5?fr::fr_turret_bounds(part-5):part?glm::vec3(.20f,fr::fr_wheel_radius(car,part-1),fr::fr_wheel_radius(car,part-1)):fr::fr_block_bounds(car);
    std::array<glm::dvec3,8> corners;
    for(int i=0;i<8;++i){glm::vec3 q=half*glm::vec3(i&1?1:-1,i&2?1:-1,i&4?1:-1);
     if(part>=5)q=fr::fr_turret_turn(q,car,part-5,false);
     if(part&&part<=2)q=fr::fr_rotate_y(q,car.steering[part-1]);q+=center;
     q=fr::fr_rotate_y(fr::fr_rotate_x(fr::fr_rotate_z(q,car.attitude.y),car.attitude.x),car.placement.w)+glm::vec3(car.placement);
     corners[i]=glm::conjugate(objectCamera.orientation)*(glm::dvec3(q)-objectCamera.eye);project(corners[i]);
    }
    for(int i=0;i<8;++i)for(int bit=1;bit<8;bit*=2){int j=i^bit;if(j<i)continue;auto a=corners[i],b=corners[j];
     if((a.z<.06)!=(b.z<.06)){auto q=a+(b-a)*((.06-a.z)/(b.z-a.z));q.z=.06;project(q);}}
   }
   if(!visible)return std::array<int,4>{0,0,0,0};
   int x=std::clamp(int(std::floor((loX*.5+.5)*paneWidth))-2,0,paneWidth),y=std::clamp(int(std::floor((.5-hiY*.5)*height))-2,0,height);
   return std::array<int,4>{x,y,std::clamp(int(std::ceil((hiX*.5+.5)*paneWidth))+2,0,paneWidth)-x,std::clamp(int(std::ceil((.5-loY*.5)*height))+2,0,height)-y};
  };
  p->data[0].x=width;p->data[0].y=height;
  for(int object=0;object<8+int(p->data[41].y)*2+int(pickups.size());++object){
   int tracer=object-4-int(p->data[41].y)*2;
   for(int part=0;part<(object<4?8:1);++part){
    if(object==0&&part>=5&&(pane||hiddenHead==0))continue;
    std::array<int,4> rectangle;
    if(object<4)rectangle=vehicleBounds(cars[object],part);
    else if(tracer<0)rectangle=bounds(glm::dvec3(p->data[42+object-4])+glm::dvec3(0,1.44,0),1.91);
    else if(tracer<4){if(shots[tracer*2].w<=0)continue;glm::dvec3 a(shots[tracer*2]),b(shots[tracer*2+1]);rectangle=cableBounds(a,b,shots[tracer*2+1].w,shots[tracer*2].w>1);}
    else {p->data[91]=pickups[tracer-4];rectangle=bounds(glm::dvec3(pickups[tracer-4]),.80);}
    if(rectangle[2]<=0||rectangle[3]<=0)continue;rectangle[0]+=paneX;
    p->data[74]={object,part,0,0};bgfx::setScissor(rectangle[0],rectangle[1],rectangle[2],rectangle[3]);
    bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS);bgfx::submit(1,p->vehicles);
   }
  }
  }
  p->data[0].x=w;p->data[0].y=h;
  if(!refine||fill) {
  int cw=(w+7)/8,ch=(h+7)/8;
  p->data[0].x=cw;p->data[0].y=ch;p->data[7].x=1;
  bgfx::setViewFrameBuffer(4,p->coneTarget);bgfx::setViewRect(4,0,0,cw,ch);bgfx::setViewClear(4,BGFX_CLEAR_NONE);
  bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setTexture(0,p->sampler,p->texture);
  bgfx::setTexture(1,p->depthSampler,p->worldColor);bgfx::setTexture(2,p->carDepthSampler,p->worldDepth);bgfx::setTexture(3,p->heightSampler,p->heightTexture);bgfx::setTexture(4,p->coarseSampler,p->coarseTexture);bgfx::setTexture(5,p->trackSampler,p->trackTexture);bgfx::setTexture(6,p->trackMaskSampler,p->trackMaskTexture);
  bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(4,p->trace);
  }
  p->data[0].x=w;p->data[0].y=h;p->data[7].x=0;
  bgfx::setViewFrameBuffer(0,p->framebuffer);bgfx::setViewRect(0,0,0,w,h);bgfx::setViewClear(0,BGFX_CLEAR_NONE);
  if(tile>=0)bgfx::setScissor(tile*w/8,0,(tile+1)*w/8-tile*w/8,h);
  bgfx::setTexture(0,p->sampler,p->coneDepth);bgfx::setTexture(1,p->depthSampler,p->worldColor);bgfx::setTexture(2,p->carDepthSampler,p->worldDepth);bgfx::setTexture(3,p->heightSampler,p->heightTexture);bgfx::setTexture(4,p->coarseSampler,p->coarseTexture);bgfx::setTexture(5,p->trackSampler,p->trackTexture);bgfx::setTexture(6,p->trackMaskSampler,p->trackMaskTexture);
  bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(0,p->trace);
  bgfx::setViewFrameBuffer(7,p->compositeTarget);bgfx::setViewRect(7,0,0,w,h);bgfx::setViewClear(7,BGFX_CLEAR_NONE);
  if(tile>=0)bgfx::setScissor(tile*w/8,0,(tile+1)*w/8-tile*w/8,h);
  bgfx::setTexture(0,p->sampler,p->texture);bgfx::setTexture(1,p->depthSampler,p->finalDepth);bgfx::setTexture(2,p->carDepthSampler,p->worldColor);bgfx::setTexture(3,p->heightSampler,p->worldColor);bgfx::setTexture(4,p->coarseSampler,p->worldDepth);bgfx::setTexture(5,p->trackSampler,p->trackTexture);bgfx::setTexture(6,p->trackMaskSampler,p->trackMaskTexture);
  bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(7,p->rutProgram);
  bgfx::setViewFrameBuffer(2,p->displayTarget);bgfx::setViewRect(2,0,0,width,height);bgfx::setViewClear(2,BGFX_CLEAR_NONE);
  bgfx::setTexture(0,p->sampler,p->compositeTexture);bgfx::setTexture(1,p->depthSampler,p->compositeDepth,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);bgfx::setTexture(2,p->carDepthSampler,p->worldColor);bgfx::setTexture(3,p->heightSampler,p->worldDepth);bgfx::setUniform(p->uniform,p->data.data(),160);bgfx::setVertexBuffer(0,p->quad);bgfx::setIndexBuffer(p->indices);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(2,p->copy);return;
 }
 bgfx::setViewFrameBuffer(0,p->displayTarget);
 bool gunnerPixel=false;auto scene=[&](glm::vec3 point) {auto hit=fr::fr_environment(point,world);for(int i=0;i<4;++i)if(glm::length(point-glm::vec3(cars[i].placement))<3) {auto object=fr::fr_visual_vehicle(point,cars[i],i!=hiddenHead,true,!(i==0&&(gunnerPixel||hiddenHead==0)));if(object.distance<hit.distance) {if(object.material==5)object.material=20+i;hit=object;}}return hit;};
 for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
  if(tactical&&gunner&&x>=w/2){p->pixels[y*w+x]=0xff000000;continue;}
  gunnerPixel=gunner&&x>=w/2;const auto& currentCamera=gunner&&x>=w/2?*gunner:camera;auto ray=glm::normalize(glm::vec3(currentCamera.ray((gunner?x%(w/2):x)+.5,y+.5,gunner?w/2:w,h)));glm::vec3 eye(currentCamera.eye),color(0);float t=.06f;bool gate=false;
  for(int i=0;i<128&&t<400;++i) {
   auto point=eye+ray*t;auto hit=scene(point);if(hit.distance<.002f+t*.0005f) {
    glm::vec3 n;for(int k=0;k<3;++k){auto u=point,v=point;u[k]+=.003f;v[k]-=.003f;n[k]=scene(u).distance-scene(v).distance;}n=glm::normalize(n);
    float pixel=std::max(.001f,2*t*float(std::tan(currentCamera.verticalFov*.5))/h),beam=1;
    if(hit.material==0){auto footprint=vectorStyle::v_footprint(n,ray,glm::vec3(currentCamera.orientation*glm::dvec3(1,0,0)),glm::vec3(currentCamera.orientation*glm::dvec3(0,1,0)),pixel);auto rut=world.tracks?fr::fr_cpu_rut(world.tracks,{point.x,point.z}):fr::RutSample{};point.y=fr::fr_terrain_sample({point.x,point.z},world).height+rut.height;beam=vectorStyle::v_terrain(point,footprint,-rut.height,rut.compaction);}
    else if(hit.material!=15){int closest=0;float distance=1e8f;for(int i=0;i<4;++i){float d=std::abs(fr::fr_visual_vehicle(point,cars[i],true,true).distance);if(d<distance){distance=d;closest=i;}}beam=vectorStyle::v_vehicle(point,n,ray,cars[closest],hit.material,pixel,glm::vec3(currentCamera.orientation*glm::dvec3(1,0,0)),glm::vec3(currentCamera.orientation*glm::dvec3(0,1,0)));}
    color=debug==1?n*.5f+.5f:glm::vec3(beam);gate=hit.material==15;break;
   }
   t+=std::max(.001f,hit.distance*.86f);
  }
  color=glm::clamp(color*255.f,glm::vec3(0),glm::vec3(255));p->pixels[y*w+x]=uint32_t(color.x)|(uint32_t(color.y)<<8)|(uint32_t(color.z)<<16)|(gate?0:0xff000000);
 }
}
const std::vector<uint32_t>& FractalRenderer::pixels() const {return p->pixels;}
int FractalRenderer::frameWidth() const {return p->width;}int FractalRenderer::frameHeight() const {return p->height;}
}
