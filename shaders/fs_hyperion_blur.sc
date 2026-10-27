$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
uniform vec4 u_post[2];
vec3 sampleLight(vec2 p) {vec3 c=texture2D(s_scene,p).rgb;return u_post[0].z>.5?max(c-.72,vec3(0.,0.,0.)):c;}
void main() {
    vec2 d=u_post[0].xy;
    vec3 c=sampleLight(v_texcoord0)*.227027;
    c+=(sampleLight(v_texcoord0+d*1.384615)+sampleLight(v_texcoord0-d*1.384615))*.316216;
    c+=(sampleLight(v_texcoord0+d*3.230769)+sampleLight(v_texcoord0-d*3.230769))*.070270;
    gl_FragColor=vec4(c,1.);
}
