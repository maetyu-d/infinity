uniform vec4 u_planet[5];
uniform vec4 u_planetRings[2];
#include "hyperion_ring_density.sh"
vec3 giantClouds(vec3 n,float seconds){
 vec3 axis=normalize(u_planet[3].xyz);
 float latitude=dot(n,axis),phase=u_planet[0].y;
 float warp=sin(n.x*7.+phase*6.283)*.55+sin(n.z*11.+latitude*8.+seconds*.0004)*.28;
 vec3 east=normalize(cross(axis,vec3(.13,.71,.43))),north=cross(axis,east);
 float belt=latitude*u_planet[0].z*3.14159265+warp;
 float storm=0.,spiral=0.;
 for(int i=0;i<2;i++){
  float lat=(i==0?1.:-1.)*(.22+phase*.30),longitude=phase*6.283+float(i)*2.7;
  vec3 centre=axis*sin(lat)+(east*cos(longitude)+north*sin(longitude))*cos(lat);
  vec3 tangent=normalize(cross(axis,centre)),meridian=cross(centre,tangent);
  vec2 q=vec2(dot(n-centre,tangent)/.22,dot(n-centre,meridian)/.11);
  float core=exp(-dot(q,q)*1.6)*smoothstep(.1,.8,dot(n,centre));
  float arms=.5+.5*sin(atan2(q.y,q.x)*2.-log(dot(q,q)+.04)*4.);
  belt+=core*arms*4.;storm=max(storm,core);spiral=max(spiral,core*arms);
 }
 float belts=mix(.5,.5+.5*sin(belt),1.-smoothstep(.6,2.,fwidth(belt)));
 vec3 albedo=mix(u_planet[1].rgb,u_planet[2].rgb,mix(.5,belts,u_planet[3].w));
 albedo=mix(albedo,u_planet[1].rgb*.55+u_planet[2].rgb*spiral*.75,storm*.65);
 return albedo;
}
float planetRingSun(vec3 n,vec3 localLight){
 if(u_planetRings[0].z<=0.)return 1.;
 vec3 axis=u_planetRings[1].xyz;
 float denominator=dot(axis,localLight);
 if(abs(denominator)<.0001)return 1.;
 float t=-dot(n,axis)/denominator;
 float radius=length(n+localLight*t),footprint=max(.001,fwidth(radius));
 if(t<=0.)return 1.;
 float optical=ringDensity(u_planetRings[0],radius,footprint);
 return exp(-optical/max(.08,abs(denominator))*1.8);
}
