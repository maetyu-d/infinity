$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
uniform vec4 u_post[2];
SAMPLER2D(s_opaqueDepth,2);
SAMPLER2D(s_cloudVolume,9);
#include "hyperion_stellar_optics.sh"
// Soft bright-pass preserves the emitter hue instead of clipping each colour
// channel at a different point. Dim materials do not acquire a glowing veil.
vec3 sampleLight(vec2 p) {
 vec3 c=texture2D(s_scene,p).rgb;
 if(u_post[0].z<.5)return c;
 float peak=max(c.r,max(c.g,c.b)),soft=clamp(peak-.75,0.,.5);
 soft=soft*soft*.5;
 return c*max(peak-1.,soft)/max(peak,.0001);
}
void main() {
    vec2 d=u_post[0].xy;
    vec3 c=sampleLight(v_texcoord0)*.227027;
    c+=(sampleLight(v_texcoord0+d*1.384615)+sampleLight(v_texcoord0-d*1.384615))*.316216;
    c+=(sampleLight(v_texcoord0+d*3.230769)+sampleLight(v_texcoord0-d*3.230769))*.070270;
    gl_FragColor=vec4(c+stellarLens(v_texcoord0)*2.,1.);
}
