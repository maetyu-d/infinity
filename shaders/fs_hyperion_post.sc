$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
uniform vec4 u_post[2]; // texel.xy, strength, unused; reserved
#include "hyperion_display.sh"
void main() {
    vec2 uv=v_texcoord0;
    vec3 original=texture2D(s_scene,uv).rgb,c=resolveScene(uv);
    c+=texture2D(s_bloom,uv).rgb*.58;
    c=displayTransform(c,uv);
    gl_FragColor=vec4(mix(original,c,u_post[0].z),1.);
}
