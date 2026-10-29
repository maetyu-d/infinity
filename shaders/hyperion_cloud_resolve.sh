SAMPLER2D(s_cloudVolume,9);
#include "hyperion_cloud_ray.sh"
// Hyperion cloudResolveFragment: four depth-matched taps, with exact
// full-resolution occupied-ray rejection at foreground silhouettes.
vec4 resolveClouds(vec2 uv,out vec2 cloudClip){
 cloudClip=vec2(0.);
 if(u_weather[0].w<=0.||u_weather[3].w<.5)return vec4(0.,0.,0.,1.);
 vec3 o,d;vec4 span=cloudSpan(uv,o,d);
 if((span.y-span.x)+(span.w-span.z)<=0.)return vec4(0.,0.,0.,1.);
 vec2 texel=u_weather[3].xy,pixel=uv/texel-.5,base=floor(pixel),f=fract(pixel);
 float depth=texture2D(s_opaqueDepth,uv).r,total=0.;vec4 colour=vec4(0.);
 for(int y=0;y<2;y++)for(int x=0;x<2;x++){
  vec2 tap=(base+vec2(float(x),float(y))+.5)*texel;
  float other=texture2D(s_opaqueDepth,tap).r;
  float compatible=(depth==0.&&other==0.)?1.:smoothstep(.93,.99,min(depth,other)/max(max(depth,other),1e-20));
  float weight=(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y)*compatible;
  colour+=texture2D(s_cloudVolume,tap)*weight;total+=weight;
 }
 vec4 result=total>.0001?colour/total:vec4(0.,0.,0.,1.);
 // The final aerial perspective stops within an opaque bank. Integrating
 // the atmosphere all the way to the hidden ground would tint foreground
 // clouds with air behind them, especially on dense sunset atmospheres.
 cloudClip=vec2(span.x+(span.y-span.x)*.35,1.-result.a);
 return result;
}
