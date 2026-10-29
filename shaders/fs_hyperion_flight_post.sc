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
// phases and sun optical depth. Radii convert to actual kilometres; retained
// climate supplies density and scale height. Opaque depth limits each ray.
vec2 sphereSpan(vec3 o,vec3 d,float r) {
    float b=dot(o,d),h=r*r-dot(cross(o,d),cross(o,d));
    if(h<0.)return vec2(-1.,-1.);h=sqrt(h);return vec2(-b-h,-b+h);
}
vec3 atmosphere(vec2 uv, out vec3 transmission) {
    transmission=vec3(1.);
    vec3 o=u_atmosphere[0].xyz,L=normalize(u_atmosphere[1].xyz);
    float H=u_atmosphere[1].w,outer=1.+H*4.444;
    vec2 ndc=uv*2.-1.;
    vec3 cameraRay=normalize(vec3(ndc.x*u_atmosphere[3].x,ndc.y*u_atmosphere[3].y*u_atmosphere[3].z,-1.));
    vec3 d=normalize(mul(u_atmosphereRotation,vec4(cameraRay,0.)).xyz);
    vec2 span=sphereSpan(o,d,outer);
    float start=max(0.,span.x),end=span.y;
    vec2 ground=sphereSpan(o,d,1.);if(ground.x>0.)end=min(end,ground.x);
    float depth=texture2D(s_opaqueDepth,uv).r;
    if(depth>0.){float near=u_atmosphere[2].y,far=u_atmosphere[2].z;float z=near*far/(near+depth*(far-near));end=min(end,z/max(-cameraRay.z,.0001)/u_atmosphere[2].x);}
    if(end<=start || end-start<H*.00001)return vec3(0.);
    float stepSize=(end-start)/12.,optical=0.;vec3 sum=vec3(0.,0.,0.);
    vec3 beta=vec3(.0058,.0135,.029)*u_atmosphere[0].w;
    float mu=dot(d,L),rayleigh=.05968*(1.+mu*mu),mie=.012*pow(max(.02,1.-.76*mu),-1.5);
    for(int i=0;i<12;i++){
        vec3 p=o+d*(start+(float(i)+.5)*stepSize);
        float density=exp(-max(0.,length(p)-1.)/H),kmStep=stepSize*u_atmosphere[3].w;optical+=density*kmStep;
        vec2 sun=sphereSpan(p,L,outer),hit=sphereSpan(p,L,1.);
        float sunlight=hit.x>0.?0.:1.,sunOptical=0.;
        for(int j=0;j<4;j++){vec3 sp=p+L*sun.y*(float(j)+.5)/4.;sunOptical+=exp(-max(0.,length(sp)-1.)/H)*sun.y*u_atmosphere[3].w*.25;}
        sum+=exp(-beta*(optical+sunOptical))*density*kmStep*sunlight;
    }
    vec3 scattering=sum*(beta*rayleigh+vec3(.0016,.0016,.0016)*mie)*7.;
    vec3 sampleDirection=normalize(o+d*(start+end)*.5);
    // Hyperion maximum-look aerial palette, localized to optical scattering.
    float horizon=exp(-abs(dot(d,normalize(o)))*7.);
    float dusk=1.-smoothstep(.03,.32,abs(dot(normalize(o),L)));
    vec3 prism=pigment(dot(sampleDirection,vec3(.3,.8,.4))*.45+u_atmosphere[2].w+mu*.08);
    scattering*=mix(vec3(1.),.38+prism*1.45,.10+horizon*.20+dusk*.22);
    transmission=exp(-beta*optical);
    return scattering;
}
#include "hyperion_display.sh"
void main() {
    vec2 uv=v_texcoord0;
    vec3 original=texture2D(s_scene,uv).rgb,c=resolveScene(uv);
    c+=texture2D(s_bloom,uv).rgb*.50;
    if(u_atmosphere[0].w>0.){vec3 transmission;vec3 air=atmosphere(uv,transmission);c=c*transmission+air;}
    c=displayTransform(c,uv);
    gl_FragColor=vec4(mix(original,c,u_post[0].z),1.);
}
