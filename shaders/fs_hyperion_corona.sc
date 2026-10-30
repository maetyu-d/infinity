$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
#include "hyperion_pigment.sh"
uniform vec4 u_starType[2];
void main(){
 float r=length(v_sphere.xy);if(r>1.)discard;
 vec3 colour=v_color0.rgb;

  // The existing parent-star halo instance, with its inherited phase packed
  // in unused optical material bits. Hyperion disc/halo falloff and threads.
  float phase=(v_color0.a-4.)*4.,angle=atan2(v_sphere.y,v_sphere.x);
  float rays=thread(angle*31.+sin(angle*7.+phase*6.283)*2.-u_hyperion[0].y*.015,.10);
  float outer=max(0.,r-.38),taper=clamp((1.-r)*5.,0.,1.);
  float corona=exp(-outer*9.)*smoothstep(.32,.43,r)*taper;
  if(u_starType[0].x>.5){corona*=u_starType[0].w*2.;rays*=.4+u_starType[0].w;}
  vec3 spectral=pigment(phase+outer*.3+sin(angle)*.02);
  gl_FragColor=vec4(colour*exp(-r*r*48.)*.35+(colour*(.25+rays*.43)+spectral*rays*.035)*corona,1.);

}
