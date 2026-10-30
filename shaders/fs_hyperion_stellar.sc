$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
uniform vec4 u_materialDomain;
uniform mat4 u_pattern;
#include "hyperion_star_types.sh"
// Hyperion solar-disc energy and filtered material noise, adapted to the
// existing stellar sphere. Temperature colours and the stellar radius are
// scene inputs; this is optical convection, not a new stellar simulation.
vec3 photosphere(vec3 starPoint,vec3 V,vec3 stellarColour,float phase,float seconds){
 float radius=max(u_materialDomain.y,1e-20);
 vec3 centre=starPoint.x>radius*4.?vec3(radius*9.,0.,0.):vec3(0.);
 vec3 n=normalize(starPoint-centre);
 vec3 N=normalize(mul(transpose(mat3(u_pattern)),n));
 float mu=clamp(dot(N,V),0.,1.);
 if(u_starType[0].x>.5)return typedPhotosphere(n,stellarColour,mu,seconds);
 vec3 drift=mod(vec3(seconds*.009,seconds*.005,0.),vec3(256.));
 vec3 q=n*58.+phase*vec3(13.,17.,23.)+drift;
 float cells=strataNoise(q,vec3(0.)),fine=strataNoise(q*2.+17.,vec3(0.));
 float granulation=smoothstep(.27,.70,cells*.76+fine*.24);
 float limb=.46+.54*pow(mu,.7);
 vec3 colour=mix(stellarColour,vec3(1.),.09);
 return colour*(1.65+granulation*1.55)*limb;
}
void main(){
 vec3 V=normalize(u_hyperion[2].xyz-v_position);
 vec3 colour=photosphere(v_pattern,V,v_color0.rgb,u_hyperion[0].z,u_hyperion[0].y);
 gl_FragColor=vec4(mix(v_color0.rgb,colour,u_hyperion[0].x),v_color0.a);
}
