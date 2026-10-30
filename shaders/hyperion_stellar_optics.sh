uniform vec4 u_stellar[3];
uniform mat4 u_starFromEye;
#include "hyperion_star_types.sh"
// Hyperion's source-depth test over the actual solar disc. Bearings, radius,
// temperature and eclipse area come from immutable scene descriptions.
float stellarVisibility(bool includeCloud){
 float open=0.,attenuation=0.;
 for(int i=0;i<5;i++){
  vec2 offset=vec2(0.);
  if(i==1)offset.x=1.;if(i==2)offset.x=-1.;if(i==3)offset.y=1.;if(i==4)offset.y=-1.;
  vec2 tap=u_stellar[0].xy+offset*vec2(1./u_stellar[2].x,1.)*u_stellar[0].z*.60;
  float visible=step(texture2D(s_opaqueDepth,tap).r,u_stellar[1].w*1.03+1e-10);
  float cloud=includeCloud && u_stellar[2].w>.5?texture2D(s_cloudVolume,tap).a:1.;
  open+=visible*.2;attenuation+=visible*cloud*.2;
 }
 return min(open,u_stellar[0].w)*attenuation/max(open,.0001);
}
// Sharp source coverage stays at full resolution, with exact foreground
// rejection. The expensive visibility samples run only within the disc.
vec3 stellarDisc(vec2 uv){
 if(u_stellar[0].w<=0.)return vec3(0.);
 float radius=max(u_stellar[0].z,u_stellar[2].z*.35);
 vec2 q=(uv-u_stellar[0].xy)*vec2(u_stellar[2].x,1.);
 float aa=clamp(u_stellar[2].z/radius,.001,.8),squared=dot(q,q)/(radius*radius);
 if(squared>(1.+aa)*(1.+aa))return vec3(0.);
 if(texture2D(s_opaqueDepth,uv).r>u_stellar[1].w*1.03+1e-10)return vec3(0.);
 float r=sqrt(squared),disc=1.-smoothstep(1.-aa,1.+aa,r),limb=.46+.54*sqrt(max(0.,1.-min(1.,squared)));
 if(u_starType[0].x>.5){
  // A resolved physical mesh already supplies this photosphere. The analytic
  // disc fills precision/LOD gaps; adding both would wash out convection.
  if(texture2D(s_opaqueDepth,uv).r>u_stellar[1].w*.70)return vec3(0.);
  vec2 xy=q/radius;xy.y*=u_starFromEye[3].w;
  vec3 n=mul(mat3(u_starFromEye),vec3(xy,sqrt(max(0.,1.-min(1.,squared)))));
  return typedPhotosphere(n,u_stellar[1].rgb,sqrt(max(0.,1.-min(1.,squared))),u_starType[1].w)*disc*stellarVisibility(false)*u_stellar[2].y;
 }
 return u_stellar[1].rgb*disc*limb*1.2*stellarVisibility(false)*u_stellar[2].y;
}
// Actual Hyperion postFragment streak and ghost, evaluated in the existing
// third-resolution bloom pass. Analytic box filtering keeps thin streaks
// stable between low-resolution pixels without a new texture or history.
vec3 stellarLens(vec2 uv){
 if(u_stellar[0].w<=0.)return vec3(0.);
 float visibility=stellarVisibility(true);if(visibility<=.0001)return vec3(0.);
 vec2 sun=uv-u_stellar[0].xy,q=sun*vec2(u_stellar[2].x,1.);
 float radius=max(u_stellar[0].z,u_stellar[2].z*.35),r=length(q)/radius;
 float aureole=exp(-r*r*.18)*.045+exp(-r*.8)*.015;
 float halfWidth=u_stellar[2].z*1.5,a=sun.y-halfWidth,b=sun.y+halfWidth;
 float integral=sign(b)*(1.-exp(-abs(b)*950.))-sign(a)*(1.-exp(-abs(a)*950.));
 float streak=integral/max(1900.*halfWidth,.0001)*exp(-abs(sun.x)*3.5);
 vec2 ghost=(uv-(.5-(u_stellar[0].xy-.5)*.38))*vec2(u_stellar[2].x,1.);
 float halo=exp(-abs(length(ghost)-.13)*190.);
 vec3 colour=u_stellar[1].rgb,flare=mix(colour,vec3(1.,.60,.22),.20)*.30;
 if(u_starType[0].x>.5)aureole*=u_starType[0].w;
 return (colour*aureole+flare*(streak*.14+halo*.03))*visibility*u_stellar[2].y;
}
