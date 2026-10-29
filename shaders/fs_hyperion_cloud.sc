$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
#include "hyperion_pigment.sh"
#include "hyperion_noise.sh"
#include "hyperion_illumination.sh"
uniform vec4 u_hyperion[4];
uniform mat4 u_pattern;
// Hyperion cloudFragment's billow, forward scattering, optical extinction
// and density self-shadow. Existing climate samples supply the broad cover;
// fine optical structure cannot create clouds in an empty weather cell.
float cloudShape(vec3 p){return strataNoise(p,vec3(0.))*.67+strataNoise(p*2.1+7.1,vec3(0.))*.33;}
void main(){
 float cover=clamp(v_color0.a/.54,0.,1.);
 if(cover<.015)discard;
 vec3 n=normalize(v_pattern),L=normalize(mul(mat3(u_pattern),u_hyperion[3].xyz));
 vec3 q=n*540.+u_hyperion[0].z*vec3(17.,31.,13.);
 float footprint=max(length(dFdx(q)),length(dFdy(q)));
 float resolved=1.-smoothstep(.15,.75,footprint);
 // Broad banks remain resolved from orbit. Sub-kilometre optical grain is
 // faded before it can turn into isolated bright pixels on a distant globe.
 float shape=cloudShape(q*.1);
 shape+=(strataNoise(q,vec3(0.))-.5)*resolved*.13;
 float density=max(0.,cover*1.15+shape*.85-.62)*cover;
 float alpha=1.-exp(-density*3.1);
 if(alpha<.005)discard;
 // Hyperion cloudFragment's density-gradient lighting, powder scattering
 // and silver lining. The existing weather cover still owns every bank.
 float optical=0.;
 for(int i=1;i<=3;i++){
  float obscurer=cloudShape(q*.1+L*(float(i)*.55));
  optical+=max(0.,cover*1.15+obscurer*.85-.62)*cover;
 }
 float selfShadow=exp(-optical*1.8);
 vec3 billowNormal=materialBump(n,v_pattern,shape*.003,.62);
 float billowSun=max(0.,dot(billowNormal,L));
 float sun=dot(n,L),day=smoothstep(-.15,.20,sun);
 vec3 view=normalize(u_hyperion[2].xyz-v_position);
 float forward=pow(max(0.,dot(view,u_hyperion[3].xyz)),12.);
 float powder=1.-exp(-density*2.4*.28);
 float silver=pow(1.-clamp(density,0.,1.),2.)*forward;
 vec3 ambient=mix(vec3(.004,.009,.023),vec3(.055,.10,.19),day)*(.58+shape*.42);
 vec3 sunColour=mix(vec3(1.12,.29,.12),vec3(1.05,.96,.82),smoothstep(.02,.45,sun));
 vec3 sunlight=sunColour*day*selfShadow*(.38+billowSun*1.15+forward*1.7+powder*.50);
 vec3 color=(ambient+sunlight)*mix(vec3(.72,.81,.95),v_color0.rgb,.23);
 vec3 opal=pigment(u_hyperion[0].z+dot(n,vec3(.53,.71,.46))*.96+shape*.24+forward*.15);
 color*=mix(vec3(1.),.25+opal*1.35,.34);
 color+=sunColour*day*silver*.19*(.3+.7*selfShadow);
 color+=opal*pow(1.-abs(sun),3.)*day*.15;
 float strength=u_hyperion[0].x;
 gl_FragColor=vec4(mix(v_color0.rgb,color,strength),mix(v_color0.a,alpha,strength));
}
