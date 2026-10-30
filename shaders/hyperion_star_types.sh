uniform vec4 u_starType[2]; // family+1, convection scale, spots, wind; phase, spin Hz, K, active
#include "hyperion_noise.sh"
// Coordinates are stellar-local unit normals. Derivative-filtered convection
// keeps the same features under zoom, rotation and a change of render units.
vec3 typedPhotosphere(vec3 n,vec3 colour,float mu,float seconds){
 float kind=u_starType[0].x,phase=u_starType[1].x;
 float spin=kind>7.5&&kind<8.5?0.:mod(seconds*u_starType[1].y,1.)*6.283;
 float cs=cos(spin),sn=sin(spin);n.xz=mat2(cs,-sn,sn,cs)*n.xz;
 vec3 p=n*u_starType[0].y+phase*vec3(17.,31.,11.);
 float cell=strataNoise(p,vec3(0.)),fine=strataNoise(p*2.03+9.,vec3(0.));
 float convection=smoothstep(.22,.78,cell*.8+fine*.2);
 float spots=1.-smoothstep(.25,.40,strataNoise(n*7.+phase*31.,vec3(0.)));
 float value=.20+convection*1.3-spots*u_starType[0].z*.5;
 if(kind>6.5&&kind<7.5)value=.96+convection*.06; // compact cooling white dwarf
 if(kind>7.5&&kind<8.5){ // exposure-averaged magnetic caps; never strobe
  float cap=pow(abs(dot(n,normalize(vec3(.35,.86,.37)))),18.);
  value=.64+cap*.7;
 }
 if(kind>8.5&&kind<9.5){
  float belt=sin(n.y*24.+(cell-.5)*3.+phase*6.283);
  value=.42+convection*.45+belt*.17;
  colour=mix(colour,colour*vec3(.62,.28,.13),smoothstep(.1,.8,belt)*.6);
 }
 float limb=.42+.58*pow(clamp(mu,0.,1.),.65);
 return colour*max(.06,value)*limb;
}
