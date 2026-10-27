// Hyperion worldFragment: psychedelic mineral/terrain treatment, BRDF,
// filtered veins and spectral Fresnel. All inputs are immutable scene data.
uniform vec4 u_hyperion[4];
uniform vec4 u_cavity;
uniform vec4 u_materialDomain;
uniform mat4 u_pattern;
#if INFINITE_FLIGHT_RELIEF
uniform mat4 u_micro;
uniform vec4 u_microParams;
float grainHash(vec3 p){p=mod(p,vec3(64.));p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float grainNoise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(grainHash(i),grainHash(i+vec3(1.,0.,0.)),f.x),mix(grainHash(i+vec3(0.,1.,0.)),grainHash(i+vec3(1.,1.,0.)),f.x),f.y),mix(mix(grainHash(i+vec3(0.,0.,1.)),grainHash(i+vec3(1.,0.,1.)),f.x),mix(grainHash(i+vec3(0.,1.,1.)),grainHash(i+vec3(1.,1.,1.)),f.x),f.y),f.z);}
#endif
#include "hyperion_pigment.sh"
vec3 brdf(vec3 albedo,vec3 N,vec3 V,vec3 L,float rough,float metallic) {
 vec3 H=normalize(V+L);float nv=max(dot(N,V),.001),nl=max(dot(N,L),0.),nh=max(dot(N,H),0.),vh=max(dot(V,H),0.);
 float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1.)+1.;float D=a2/max(3.14159*denom*denom,.00001);
 float k=(rough+1.)*(rough+1.)/8.;float G=nv/(nv*(1.-k)+k)*nl/(nl*(1.-k)+k);
 vec3 F0=mix(vec3(.035),albedo,metallic),F=F0+(1.-F0)*pow(1.-vh,5.);
 return ((1.-F)*(1.-metallic)*albedo/3.14159+F*D*G/max(4.*nv*max(nl,.001),.001))*nl*3.8;
}
void main(){
 float strength=u_hyperion[0].x,seconds=u_hyperion[0].y,phase=u_hyperion[0].z,primitive=u_hyperion[0].w;
 if(primitive<-.5 && u_cavity.w>0. && length(v_position-u_cavity.xyz)<u_cavity.w)discard;
 vec3 base=v_color0.rgb;
 if(strength<=0.){gl_FragColor=vec4(base,v_color0.a);return;}
 if(primitive>1.5){gl_FragColor=vec4(base*(1.+strength*.28),v_color0.a);return;}
 vec3 V=normalize(u_hyperion[2].xyz-v_position),L=normalize(u_hyperion[3].xyz);
 vec3 N=normalize(cross(dFdx(v_position),dFdy(v_position)));
 if(primitive>.1 && primitive<1.5)N=normalize(v_sphere);
 if(dot(N,V)<0.)N=-N;
 bool planet=u_materialDomain.x>.5 && u_materialDomain.x<1.5;
 bool atom=primitive>.5 || (u_materialDomain.x>1.5 && u_materialDomain.x<2.5);
 bool craft=u_materialDomain.x>2.5;
 vec3 p=v_pattern,n=planet?normalize(p):p,radial=N;
 // Covector transform supplies a smooth broad sphere normal while retaining
 // steep geometric slopes. It changes lighting, never vertices or silhouettes.
 if(planet){
  radial=mul(transpose(mat3(u_pattern)),n);
  radial=normalize(radial/max(max(abs(radial.x),max(abs(radial.y),abs(radial.z))),1e-30));
  if(dot(radial,V)<0.)radial=-radial;
  float slope=1.-clamp(dot(radial,N),0.,1.);
  if(primitive<.1)N=normalize(mix(radial,N,.08+.92*smoothstep(.004,.045,slope)));
 }
 float elevation=planet?.5+(length(p)-1.)*32.:dot(p,vec3(.31,.73,.19));
 float detail=sin(n.x*13.+sin(n.z*6.))*.13;
 vec3 mineral=pigment(elevation*3.7+phase+detail);
 vec3 tapestry=vec3(0.),flow=vec3(0.);
 if(!atom && !craft){tapestry=ecstatic(n,phase+.15,seconds);flow=silk(n,phase,seconds);}
 vec3 spectral=base*.30+mineral*.52+tapestry*.52;
 vec3 albedo=mix(base,spectral,u_hyperion[1].z);
 if(atom || craft)albedo=base;
 else albedo*=.62; // Baked Infinite colours already include illumination.
 float rough=u_hyperion[1].x,microVein=0.,cavity=1.;
 vec3 microGlow=vec3(0.);
#if INFINITE_FLIGHT_RELIEF
 if(u_microParams.x>0. && primitive<.5){
  vec3 q=mul(u_micro,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
  float resolved=1.-smoothstep(.35,1.4,length(fwidth(q)));
  float grain=.5;if(resolved>.001)grain=mix(.5,grainNoise(q),resolved);
  float middleResolved=1.-smoothstep(.35,1.4,length(fwidth(q*4.)));
  float fineResolved=1.-smoothstep(.35,1.4,length(fwidth(q*16.)));
  float middle=.5,fine=.5;
  if(middleResolved>.001)middle=mix(.5,grainNoise(q*4.),middleResolved);
  if(fineResolved>.001)fine=mix(.5,grainNoise(q*16.),fineResolved);
  float height=((grain-.5)*.75+(middle-.5)*.16+(fine-.5)*.035)*u_microParams.x;
  vec3 dx=dFdx(v_position),dy=dFdy(v_position),a=cross(dy,N),b=cross(N,dx);float det=dot(dx,a);
  vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
  gradient*=min(1.,.65/max(length(gradient),.001));N=normalize(N-gradient);
  // Hyperion closeMaterial's filtered mineral body colour, seams and grazing
  // sheen. The existing modulo-64 material coordinates keep this stable across
  // floating-origin and scale changes; there is no texture-space geography.
  float strata=grain;
  if(resolved>.001)strata=mix(.5,grainNoise(q*2.),resolved);
  microVein=thread(strata*24.+grain*.8,.035)*resolved;
  vec3 closeColour=albedo*(.48+grain*.65)+pigment(phase+strata*.55+grain*.08)*.086+flow*.15;
  closeColour*=.86+middle*.18+fine*.10;
  cavity=1.-microVein*.24;
  closeColour*=cavity;
  albedo=mix(albedo,closeColour,resolved*.85);
  microGlow=pigment(phase+strata*.23+.42)*microVein*.035;
  rough=clamp(rough+(middle-.5)*middleResolved*.20,.18,.99);
 }
#endif
 vec3 ndx=dFdx(N),ndy=dFdy(N);float variance=min(.25,max(dot(ndx,ndx),dot(ndy,ndy))*.5);
 rough=pow(min(1.,pow(rough,4.)+variance),.25);
 float fresnel=pow(1.-max(0.,dot(N,V)),4.),sunDot=dot(N,L),day=smoothstep(-.10,.18,sunDot);
 // Hyperion worldFragment: directional light plus sky/ground irradiance.
 // The broad view-dependent reflection stays subordinate to surface relief.
 float skyFacing=planet?clamp(dot(N,radial)*.5+.5,0.,1.):.65;
 vec3 skyTint=vec3(.025,.044,.075)*(.18+.82*day);
 vec3 groundBounce=albedo*vec3(.045,.030,.025)*(.15+.85*day);
 vec3 lit=brdf(albedo,N,V,L,rough,u_hyperion[1].y)*cavity;
 lit+=albedo*mix(groundBounce,skyTint,skyFacing)*cavity;
 vec3 reflection=reflect(-V,N);
 vec3 reflectedPigment=pigment(dot(reflection,vec3(.19,.31,.23))+phase);
 lit+=mix(albedo,reflectedPigment,.35)*fresnel*(1.-rough)*(.035+.11*day)*cavity;
 if(atom){
  // Element hue and all bond/site identities stay legible. Only illumination,
  // a small grazing spectral sheen and the existing bloom affect the atoms.
  lit+=base*.10+mix(base,pigment(phase+fresnel*.35),.28)*fresnel*.24;
 }else if(craft){
  lit+=base*.11+pigment(phase+fresnel*.4)*fresnel*.30;
 }else{
  float ecPhase=elevation*176.+detail*2.+sin(n.x*29.+n.z*17.)*.24;
  float ecVeins=thread(ecPhase,.065),ecLace=thread(ecPhase*2.7+u_materialDomain.y*5.,.025);
  vec3 ecColour=pigment(elevation*6.3+phase+n.z*.18+seconds*.0008);
  lit+=ecColour*(ecVeins*.58+ecLace*.19)*u_hyperion[1].w*(.045+.955*day);
  lit+=tapestry*(.016+.075*fresnel)*(.12+.88*day)+flow*.012*day;
  lit+=pigment(n.z*.9+phase+fresnel*.35)*fresnel*.21*(.12+.88*day);
  lit+=microGlow+pigment(phase+fresnel*.4)*microVein*(.018+fresnel*.16);
 }
 float response=atom?.58:craft?.88:.96;
 gl_FragColor=vec4(mix(base,lit,strength*response),v_color0.a);
}
