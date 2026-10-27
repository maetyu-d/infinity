$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
uniform vec4 u_post[2]; // texel.xy, strength, unused; reserved
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
void main() {
    vec2 uv=v_texcoord0,texel=u_post[0].xy;
    vec3 original=texture2D(s_scene,uv).rgb,c=original;
    vec3 a=texture2D(s_scene,uv+vec2(-1.,-1.)*texel).rgb,b=texture2D(s_scene,uv+vec2(1.,-1.)*texel).rgb;
    vec3 d=texture2D(s_scene,uv+vec2(-1.,1.)*texel).rgb,e=texture2D(s_scene,uv+vec2(1.,1.)*texel).rgb;
    float lo=min(luma(c),min(min(luma(a),luma(b)),min(luma(d),luma(e))));
    float hi=max(luma(c),max(max(luma(a),luma(b)),max(luma(d),luma(e))));
    if(hi-lo>max(.045,hi*.18)) {
        vec2 edge=vec2(-(luma(a)+luma(b)-luma(d)-luma(e)),luma(a)+luma(d)-luma(b)-luma(e));
        edge=clamp(edge/(min(abs(edge.x),abs(edge.y))+max(.012,(luma(a)+luma(b)+luma(d)+luma(e))*.03125)),vec2(-4.,-4.),vec2(4.,4.))*texel;
        c=mix(c,(texture2D(s_scene,uv+edge*.167).rgb+texture2D(s_scene,uv-edge*.167).rgb)*.5,.68);
    }
    vec3 neighbourhood=(a+b+d+e)*.25;
    float clarity=(1.-smoothstep(.12,.32,(hi-lo)/max(hi,.02)))*smoothstep(.015,.08,luma(c));
    c=max(vec3(0.,0.,0.),c+(c-neighbourhood)*clarity*.24);
    c+=texture2D(s_bloom,uv).rgb*.38;
    vec2 v=(uv-.5)*2.;c*=1.-.10*dot(v,v);
    c=max(c*1.30,vec3(0.,0.,0.));
    float peak=max(c.r,max(c.g,c.b)),mappedPeak=(peak*(2.51*peak+.03))/(peak*(2.43*peak+.59)+.14);
    vec3 film=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);
    c=clamp(mix(film,c/max(peak,.0001)*mappedPeak,.48),vec3(0.,0.,0.),vec3(1.,1.,1.));
    c=pow(c,vec3(1./2.2,1./2.2,1./2.2));
    gl_FragColor=vec4(mix(original,c,u_post[0].z),1.);
}
