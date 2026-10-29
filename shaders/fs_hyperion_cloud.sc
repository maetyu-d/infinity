$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
#include "hyperion_pigment.sh"
uniform vec4 u_hyperion[4];
uniform mat4 u_pattern;
// Hyperion cloudFragment's billow, forward scattering, optical extinction
// and density self-shadow. Existing climate samples supply the broad cover;
// fine optical structure cannot create clouds in an empty weather cell.
float cloudHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float cloudNoise(vec3 p){
 vec3 c=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 return mix(mix(mix(cloudHash(c),cloudHash(c+vec3(1.,0.,0.)),f.x),mix(cloudHash(c+vec3(0.,1.,0.)),cloudHash(c+vec3(1.,1.,0.)),f.x),f.y),mix(mix(cloudHash(c+vec3(0.,0.,1.)),cloudHash(c+vec3(1.,0.,1.)),f.x),mix(cloudHash(c+vec3(0.,1.,1.)),cloudHash(c+vec3(1.,1.,1.)),f.x),f.y),f.z);
}
float billow(vec3 p){return cloudNoise(p)*.70+cloudNoise(p*2.03+7.1)*.30;}
void main(){
 float cover=clamp(v_color0.a/.54,0.,1.);
 if(cover<.015)discard;
 vec3 n=normalize(v_pattern),L=normalize(mul(mat3(u_pattern),u_hyperion[3].xyz));
 vec3 q=n*540.+u_hyperion[0].z*vec3(17.,31.,13.);
 float footprint=max(length(dFdx(q)),length(dFdy(q)));
 float resolved=1.-smoothstep(.15,.75,footprint);
 // Broad banks remain resolved from orbit. Sub-kilometre optical grain is
 // faded before it can turn into isolated bright pixels on a distant globe.
 float shape=cloudNoise(q*.1)*.67+cloudNoise(q*.21+7.1)*.33;
 shape+=(cloudNoise(q)-.5)*resolved*.13;
 float density=max(0.,cover*1.15+shape*.85-.62)*cover;
 float alpha=1.-exp(-density*2.4);
 if(alpha<.005)discard;
 // Hyperion cloudFragment's density-gradient lighting, powder scattering
 // and silver lining. The existing weather cover still owns every bank.
 float obscurer=cloudNoise(q*.1+L*.8);
 float lightBillow=cloudNoise(q*.21+7.1+L*.65);
 float gradient=clamp(.5+(shape-(obscurer*.67+lightBillow*.33))*2.4,0.,1.);
 float selfShadow=exp(-max(0.,cover*.9+obscurer*.7-.6)*2.2)*(.22+.78*gradient);
 float sun=dot(n,L),day=smoothstep(-.15,.20,sun);
 vec3 view=normalize(u_hyperion[2].xyz-v_position);
 float forward=pow(max(0.,dot(view,u_hyperion[3].xyz)),12.);
 float powder=1.-exp(-density*2.4*.28);
 float silver=pow(1.-clamp(density,0.,1.),2.)*forward;
 vec3 ambient=mix(vec3(.012,.023,.050),vec3(.16,.25,.39),day)*(.58+shape*.42);
 vec3 sunColour=mix(vec3(1.12,.29,.12),vec3(1.05,.96,.82),smoothstep(.02,.45,sun));
 vec3 sunlight=sunColour*day*selfShadow*(.60+max(0.,sun)*.8+forward*1.7+powder*.34);
 vec3 color=(ambient+sunlight)*mix(vec3(.72,.81,.95),v_color0.rgb,.23);
 vec3 opal=pigment(u_hyperion[0].z+dot(n,vec3(.53,.71,.46))*.96+shape*.24+forward*.15);
 color*=mix(vec3(1.),.25+opal*1.35,.46);
 color+=sunColour*day*silver*.19*(.3+.7*selfShadow);
 color+=opal*pow(1.-abs(sun),3.)*day*.15;
 float strength=u_hyperion[0].x;
 gl_FragColor=vec4(mix(v_color0.rgb,color,strength),mix(v_color0.a,alpha,strength));
}
