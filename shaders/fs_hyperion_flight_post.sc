$input v_texcoord0
#include <bgfx_shader.sh>
#include "hyperion_pigment.sh"
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
SAMPLER2D(s_opaqueDepth,2);
uniform vec4 u_post[2]; // texel.xy, strength, unused; reserved
uniform vec4 u_atmosphere[4];
uniform mat4 u_atmosphereRotation;
// Hyperion atmoFragment: occupied ray span, exponential density, Rayleigh/Mie
// phases and sun optical depth. Distances are planet radii, converted to its
// source 9 km density scale; the existing opaque depth limits every ray.
vec2 sphereSpan(vec3 o,vec3 d,float r) {
    float b=dot(o,d),h=r*r-dot(cross(o,d),cross(o,d));
    if(h<0.)return vec2(-1.,-1.);h=sqrt(h);return vec2(-b-h,-b+h);
}
vec4 atmosphere(vec2 uv) {
    vec3 o=u_atmosphere[0].xyz,L=normalize(u_atmosphere[1].xyz);
    float H=u_atmosphere[1].w,outer=1.+H*4.444;
    vec2 ndc=uv*2.-1.;
    vec3 cameraRay=normalize(vec3(ndc.x*u_atmosphere[3].x,ndc.y*u_atmosphere[3].y*u_atmosphere[3].z,-1.));
    vec3 d=normalize(mul(u_atmosphereRotation,vec4(cameraRay,0.)).xyz);
    vec2 span=sphereSpan(o,d,outer);
    float start=max(0.,span.x),end=span.y;
    vec2 ground=sphereSpan(o,d,1.);if(ground.x>0.)end=min(end,ground.x);
    float depth=texture2D(s_opaqueDepth,uv).r;
    if(depth<.999999){float near=u_atmosphere[2].y,far=u_atmosphere[2].z;float z=near*far/(far-depth*(far-near));end=min(end,z/max(-cameraRay.z,.0001)/u_atmosphere[2].x);}
    if(end<=start || end-start<H*.00001)return vec4(0.,0.,0.,0.);
    float stepSize=(end-start)/8.,optical=0.;vec3 sum=vec3(0.,0.,0.);
    vec3 beta=vec3(.0058,.0135,.029)*u_atmosphere[0].w;
    float mu=dot(d,L),rayleigh=.05968*(1.+mu*mu),mie=.012*pow(max(.02,1.-.76*mu),-1.5);
    for(int i=0;i<8;i++){
        vec3 p=o+d*(start+(float(i)+.5)*stepSize);
        float density=exp(-max(0.,length(p)-1.)/H),kmStep=stepSize/H*9.;optical+=density*kmStep;
        vec2 sun=sphereSpan(p,L,outer),hit=sphereSpan(p,L,1.);
        float sunlight=hit.x>0.?0.:1.,sunOptical=0.;
        for(int j=0;j<2;j++){vec3 sp=p+L*sun.y*(float(j)+.5)/2.;sunOptical+=exp(-max(0.,length(sp)-1.)/H)*sun.y/H*4.5;}
        sum+=exp(-beta*(optical+sunOptical))*density*kmStep*sunlight;
    }
    vec3 scattering=sum*(beta*rayleigh+vec3(.0016,.0016,.0016)*mie)*7.;
    vec3 sampleDirection=normalize(o+d*(start+end)*.5);
    scattering*=mix(vec3(1.),.28+pigment(dot(sampleDirection,vec3(.3,.8,.4))*.7+u_atmosphere[2].w+mu*.12)*1.7,.88);
    float opacity=clamp(1.-exp(-dot(beta,vec3(.333,.333,.333))*optical),0.,.92);
    return vec4(scattering,opacity);
}
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
    // Hyperion psychedelic post: bright pigment retains its hue at the shoulder.
    c+=texture2D(s_bloom,uv).rgb*.82;
    vec2 fringe=(uv-.5)*texel*1.6*smoothstep(.15,.65,length(uv-.5));
    vec3 split=vec3(texture2D(s_scene,uv+fringe).r,original.g,texture2D(s_scene,uv-fringe).b);
    c+=max(vec3(0.),split-original)*.24;
    if(u_atmosphere[0].w>0.){vec4 air=atmosphere(uv);c=c*(1.-air.a)+air.rgb;}
    vec2 v=(uv-.5)*2.;c*=1.-.10*dot(v,v);
    float saturationLuma=luma(c);c=max(vec3(0.),mix(vec3(saturationLuma),c,1.19));
    c=max(c*1.30,vec3(0.,0.,0.));
    float peak=max(c.r,max(c.g,c.b)),mappedPeak=(peak*(2.51*peak+.03))/(peak*(2.43*peak+.59)+.14);
    vec3 film=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);
    c=clamp(mix(film,c/max(peak,.0001)*mappedPeak,.86),vec3(0.,0.,0.),vec3(1.,1.,1.));
    c=pow(c,vec3(1./2.2,1./2.2,1./2.2));
    gl_FragColor=vec4(mix(original,c,u_post[0].z),1.);
}
