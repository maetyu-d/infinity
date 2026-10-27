$input v_color0, v_position, v_pattern, v_sphere
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
#include "hyperion_pigment.sh"
void main(){
 float r=length(v_sphere.xy);if(r>1.)discard;
 vec3 colour=v_color0.rgb;
 if(v_color0.a>1.5){
  // The original generated gas/filament instances supply the positions and
  // density. Only their optical material is spectral; no new particles exist.
  float phase=u_hyperion[0].z;
  vec3 flow=ecstatic(v_pattern,phase,u_hyperion[0].y);
  float energy=max(colour.r,max(colour.g,colour.b));
  vec3 spectral=pigment(dot(v_pattern,vec3(.31,.73,.19))*.34+phase);
  colour=colour*.18+(spectral*.32+flow*.85)*energy;
  float glow=exp(-r*r*3.)*clamp((1.-r)*5.,0.,1.);
  gl_FragColor=vec4(colour*glow*1.7,1.);
 }else{
  // Hyperion skyFragment's compact point and bounded halo: keep real stellar
  // temperature colours instead of turning each emitter into a flat disc.
  float core=exp(-r*r*42.),halo=exp(-r*r*5.)*.13;
  float taper=clamp((1.-r)*5.,0.,1.);
  gl_FragColor=vec4(colour*(core*1.85+halo)*taper,1.);
 }
}
