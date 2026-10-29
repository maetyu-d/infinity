$input v_texcoord0
#include <bgfx_shader.sh>
#include "hyperion_pigment.sh"
SAMPLER2D(s_opaqueDepth,2);
#include "hyperion_optical_ray.sh"
#include "hyperion_polar.sh"
void main(){gl_FragColor=vec4(polarEmission(v_texcoord0),1.);}
