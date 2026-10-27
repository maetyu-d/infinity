$input v_color0, v_position, v_pattern, v_sphere
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
uniform vec4 u_cavity;
uniform mat4 u_micro;
uniform vec4 u_microParams;
// Hyperion minorBakeFragment's filtered value grain and minorFragment's
// derivative relief normal. This is material response only, with no displacement.
float grainHash(vec3 p){p=mod(p,vec3(64.,64.,64.));p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float grainNoise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(grainHash(i),grainHash(i+vec3(1.,0.,0.)),f.x),mix(grainHash(i+vec3(0.,1.,0.)),grainHash(i+vec3(1.,1.,0.)),f.x),f.y),mix(mix(grainHash(i+vec3(0.,0.,1.)),grainHash(i+vec3(1.,0.,1.)),f.x),mix(grainHash(i+vec3(0.,1.,1.)),grainHash(i+vec3(1.,1.,1.)),f.x),f.y),f.z);}
// [strength,time,phase,primitive], [roughness,metal,pigment,fibre], eye, light
#include "hyperion_pigment.sh"
// Hyperion flight worldFragment brdf(), constants and highlight response.
vec3 brdf(vec3 albedo,vec3 N,vec3 V,vec3 L,float rough,float metallic) {
    vec3 H=normalize(V+L);float nv=max(dot(N,V),.001),nl=max(dot(N,L),0.),nh=max(dot(N,H),0.),vh=max(dot(V,H),0.);
    float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1.)+1.;float D=a2/max(3.14159*denom*denom,.00001);
    float k=(rough+1.)*(rough+1.)/8.;float G=nv/(nv*(1.-k)+k)*nl/(nl*(1.-k)+k);
    vec3 F0=mix(vec3(.035,.035,.035),albedo,metallic),F=F0+(1.-F0)*pow(1.-vh,5.);
    return ((1.-F)*(1.-metallic)*albedo/3.14159+F*D*G/max(4.*nv*max(nl,.001),.001))*nl*3.8;
}
void main() {
    float strength=u_hyperion[0].x,phase=u_hyperion[0].z,primitive=u_hyperion[0].w;
    // The universe's existing microscopic inspection cavity, now clipped per
    // fragment so a streamed terrain tile cannot expose a rectangular edge.
    if(primitive<-.5 && u_cavity.w>0. && length(v_position-u_cavity.xyz)<u_cavity.w)discard;
    vec3 base=v_color0.rgb;
    if(strength<=0.) { gl_FragColor=vec4(base,v_color0.a);return; }
    if(primitive>1.5) { // Bond/orbit lines retain the authoritative graph.
        gl_FragColor=vec4(base*(1.+strength*.16),v_color0.a);return;
    }
    vec3 V=normalize(u_hyperion[2].xyz-v_position);
    vec3 N=normalize(cross(dFdx(v_position),dFdy(v_position)));
    if(primitive>.5)N=normalize(v_sphere);
    if(dot(N,V)<0.)N=-N;
    vec3 L=normalize(u_hyperion[3].xyz);
    float fresnel=pow(1.-max(0.,dot(N,V)),4.);
    // Object-space colour fields: no vertex displacement or replacement geography.
    vec3 p=v_pattern;
    vec3 flow=silk(p,phase,u_hyperion[0].y);
    float height=dot(p,vec3(.31,.73,.19));
    vec3 mineral=pigment(height*3.7+phase+sin(p.x*13.+sin(p.z*6.))*.13);
    float amount=u_hyperion[1].z;
    vec3 albedo=mix(base,base*.42+mineral*.27+flow*.28,amount*strength);
    float rough=u_hyperion[1].x;
    if(u_microParams.x>0. && primitive<.5){
        vec3 q=mul(u_micro,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
        float resolved=1.-smoothstep(.35,1.4,length(fwidth(q)));
        float grain=.5;if(resolved>.001)grain=mix(.5,grainNoise(q),resolved);
        float h=(grain-.5)*u_microParams.x;
        vec3 dx=dFdx(v_position),dy=dFdy(v_position),a=cross(dy,N),b=cross(N,dx);float det=dot(dx,a);
        vec3 gradient=sign(det)*(dFdx(h)*a+dFdy(h)*b)/max(abs(det),1e-20);
        gradient*=min(1.,.65/max(length(gradient),.001));N=normalize(N-gradient);
        albedo*=.80+grain*.40;
        rough=clamp(rough+(grain-.5)*resolved*.12,.18,.99);
    }
    vec3 ndx=dFdx(N),ndy=dFdy(N);
    float variance=min(.25,max(dot(ndx,ndx),dot(ndy,ndy))*.5);
    rough=pow(min(1.,pow(rough,4.)+variance),.25);
    vec3 lit=brdf(albedo,N,V,L,rough,u_hyperion[1].y)+albedo*vec3(.10,.15,.21);
    lit+=pigment(phase+fresnel*.4)*fresnel*.11;
    float trace=thread(height*420.,.015);
    lit+=pigment(height*3.+phase)*trace*u_hyperion[1].w*max(0.,dot(N,L));
    // Existing vertex colours contain baked diffuse shading. Blend in the new
    // light response instead of discarding that information or recolouring sites.
    float lightMix=primitive>.5?.22:.68;
    vec3 c=mix(base,lit,strength*lightMix);
    gl_FragColor=vec4(c,v_color0.a);
}
