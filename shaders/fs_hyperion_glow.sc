$input v_color0, v_position, v_pattern, v_sphere
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
#include "hyperion_pigment.sh"
void main() {
    // Analytic equivalent of Hyperion combatGlow's mipmapped radial sprite.
    float r=length(v_sphere.xy);
    if(r>1.)discard;
    float glow=exp(-r*r*5.)*clamp((1.-r)*5.,0.,1.);
    vec3 colour=v_color0.rgb;
    vec3 spectral=silk(v_pattern,u_hyperion[0].z,u_hyperion[0].y);
    colour*=mix(vec3(1.,1.,1.),vec3(.82,.82,.82)+spectral*.6,u_hyperion[0].x*.32);
    gl_FragColor=vec4(colour*glow*1.6,1.);
}
