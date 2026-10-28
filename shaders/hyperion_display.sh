#include "hyperion_tone.sh"
// Shared post treatment: HDR-safe edge filtering and hue-preserving display.
// No temporal history, so a rebase or extreme zoom cannot ghost old geometry.
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
float edgeLuma(vec3 c){return sqrt(max(luma(c),0.));}
vec3 resolveScene(vec2 uv){
 vec2 texel=u_post[0].xy;
 vec3 c=texture2D(s_scene,uv).rgb;
 vec3 a=texture2D(s_scene,uv+vec2(-1.,-1.)*texel).rgb,b=texture2D(s_scene,uv+vec2(1.,-1.)*texel).rgb;
 vec3 d=texture2D(s_scene,uv+vec2(-1.,1.)*texel).rgb,e=texture2D(s_scene,uv+vec2(1.,1.)*texel).rgb;
 float centre=edgeLuma(c),nw=edgeLuma(a),ne=edgeLuma(b),sw=edgeLuma(d),se=edgeLuma(e);
 float lo=min(centre,min(min(nw,ne),min(sw,se))),hi=max(centre,max(max(nw,ne),max(sw,se)));
 if(hi-lo>max(.035,hi*.14)){
  vec2 edge=vec2(-(nw+ne-sw-se),nw+sw-ne-se);
  edge=clamp(edge/(min(abs(edge.x),abs(edge.y))+max(.008,(nw+ne+sw+se)*.03125)),vec2(-6.),vec2(6.))*texel;
  vec3 narrow=(texture2D(s_scene,uv+edge*.167).rgb+texture2D(s_scene,uv-edge*.167).rgb)*.5;
  vec3 wide=narrow*.5+(texture2D(s_scene,uv+edge*.5).rgb+texture2D(s_scene,uv-edge*.5).rgb)*.25;
  float candidate=edgeLuma(wide);
  c=mix(c,candidate<lo||candidate>hi?narrow:wide,.88);
 }
 return c;
}
vec3 displayTransform(vec3 c,vec2 uv){
 vec2 v=(uv-.5)*2.;c*=1.-.10*dot(v,v);
 c=toneColour(c);
 // Stable sub-code-value dither prevents visible bands in dark nebulae and
 // atmosphere gradients. Pixel coordinates avoid frame-to-frame sparkle.
 float dither=fract(52.9829189*fract(dot(floor(uv/u_post[0].xy),vec2(.06711056,.00583715))))-.5;
 return clamp(c+dither/255.,vec3(0.),vec3(1.));
}
