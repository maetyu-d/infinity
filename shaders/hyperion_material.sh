// Hyperion worldFragment: psychedelic mineral/terrain treatment, BRDF,
// filtered veins and spectral Fresnel. All inputs are immutable scene data.
uniform vec4 u_hyperion[4];
uniform vec4 u_cavity;
uniform vec4 u_materialDomain;
uniform mat4 u_pattern;
uniform vec4 u_surface;
#if INFINITE_FLIGHT_RELIEF
uniform mat4 u_micro;
uniform mat4 u_meso;
uniform vec4 u_microParams;
#include "hyperion_material_detail.sh"
#endif
#include "hyperion_pigment.sh"
vec3 brdf(vec3 albedo,vec3 N,vec3 V,vec3 L,float rough,float metallic) {
 vec3 H=normalize(V+L);float nv=max(dot(N,V),.001),nl=max(dot(N,L),0.),nh=max(dot(N,H),0.),vh=max(dot(V,H),0.);
 float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1.)+1.;float D=a2/max(3.14159*denom*denom,.00001);
 float k=(rough+1.)*(rough+1.)/8.;float G=nv/(nv*(1.-k)+k)*nl/(nl*(1.-k)+k);
 vec3 F0=mix(vec3(.035),albedo,metallic),F=F0+(1.-F0)*pow(1.-vh,5.);
 return ((1.-F)*(1.-metallic)*albedo/3.14159+F*D*G/max(4.*nv*max(nl,.001),.001))*nl*3.8;
}
#include "hyperion_architecture.sh"
void main(){
 float strength=u_hyperion[0].x,seconds=u_hyperion[0].y,phase=u_hyperion[0].z,primitive=u_hyperion[0].w;
 if(primitive<-.5 && u_cavity.w>0. && length(v_position-u_cavity.xyz)<u_cavity.w)discard;
 vec3 base=v_color0.rgb;
 if(strength<=0.){gl_FragColor=vec4(base,v_color0.a);return;}
 if(primitive>1.5){gl_FragColor=vec4(base*(1.+strength*.28),v_color0.a);return;}
 vec3 V=normalize(u_hyperion[2].xyz-v_position),L=normalize(u_hyperion[3].xyz);
 vec3 N=normalize(cross(dFdx(v_position),dFdy(v_position)));
 if((primitive>.1 && primitive<1.5) || primitive< -1.5)N=normalize(v_sphere);
 // A field normal can lean behind the view while its coarse LOD triangle
 // remains visible. Reversing it would invert sun lighting into black seams.
 if(primitive< -1.5){float nv=dot(N,V);if(nv<.025)N=normalize(N+V*(.025-nv));}
 else if(dot(N,V)<0.)N=-N;
 if(u_materialDomain.x>3.5 && u_materialDomain.x<4.5){
  gl_FragColor=vec4(mix(base,architecture(base,N,V,L,v_pattern,v_sphere.y,phase,seconds),strength*.98),v_color0.a);return;
 }
 bool terrain=primitive< -1.5;
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
  if(primitive<.1 && primitive> -1.5)N=normalize(mix(radial,N,.08+.92*smoothstep(.004,.045,slope)));
 }
 float elevation=planet?.5+(length(p)-1.)*32.:dot(p,vec3(.31,.73,.19));
 float detail=sin(n.x*13.+sin(n.z*6.))*.13;
 vec3 mineral=pigment(elevation*3.7+phase+detail);
 vec3 tapestry=vec3(0.),flow=vec3(0.);
 if(!atom && !craft){tapestry=ecstatic(n,phase+.15,seconds);flow=silk(n,phase,seconds);}
 vec3 spectral=base*.30+mineral*.52+tapestry*.52;
 if(planet)spectral=base*.48+mineral*.22+tapestry*.14;
 vec3 albedo=mix(base,spectral,u_hyperion[1].z);
 if(atom || craft)albedo=base;
 else albedo*=planet?.76:.62; // Non-terrain meshes retain their original baked colour.
 if(terrain)albedo=base*.44+mineral*.035; // Streamed terrain supplies unlit reflectance.
 if(planet)albedo=mix(vec3(dot(albedo,vec3(.299,.587,.114)))*vec3(.86,.94,1.),albedo,.48);
 float rough=u_hyperion[1].x,microVein=0.,cavity=1.;
 vec3 microGlow=vec3(0.);
#if INFINITE_FLIGHT_RELIEF
 // Hyperion surface_detail.hpp: smooth strata/deposits bridge planetary
 // colour to the separately filtered 16 m close-material texture.
 if(terrain && u_microParams.z>0. && u_microParams.y>.5){
  vec3 q=mul(u_meso,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
  vec3 offset=phase*vec3(31.,17.,47.);
  float broad=strataNoise(q*.0625+offset);
  float warp=strataNoise(q*.25+offset+11.);
  float middle=strataNoise(q+offset+warp*2.);
  float grain=strataNoise(q*4.+offset+31.);
  float strata=broad*.50+warp*.30+middle*.20;
  float slope=1.-clamp(dot(N,radial),0.,1.);
  float dust=smoothstep(.30,.76,warp)*(1.-smoothstep(.08,.5,slope));
  float layer=q.y*(.25*6.283185)+middle*2.+warp*4.;
  float bedding=mix(.5,.5+.5*sin(layer),1.-smoothstep(.6,2.,fwidth(layer)));
  float vein=thread(middle*24.+warp*.8,.035)*(1.-smoothstep(.15,.9,fwidth(middle*24.)));
  vec3 deposits=albedo*(.48+strata*.92+grain*.22+bedding*.10);
  deposits=mix(deposits,deposits*.75+vec3(.055,.043,.032),dust*.30);
  deposits+=pigment(phase+strata*.55+grain*.08)*.055;
  deposits=mix(deposits,deposits*.65+pigment(phase+middle*.18)*.11,vein*.32);
  albedo=deposits;
  rough=clamp(rough+(grain-.5)*.16+dust*.10,.22,.98);
  float height=((middle-.5)*.65+(grain-.5)*.16)*u_microParams.z;
  vec3 dx=dFdx(v_position),dy=dFdy(v_position),a=cross(dy,N),b=cross(N,dx);float det=dot(dx,a);
  vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
  gradient*=min(1.,.28/max(length(gradient),.001));N=normalize(N-gradient);
  microGlow=pigment(phase+middle*.18)*vein*.008;
 }
 if(u_microParams.x>0. && primitive<.5 && !atom && !craft){
  vec3 q=mul(u_micro,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
  float footprint=max(length(dFdx(q)),length(dFdy(q))),resolved=1.-smoothstep(.12,.75,footprint);
  vec3 materialN=mul(mat3(u_micro),N);
  materialN=normalize(materialN/max(max(abs(materialN.x),max(abs(materialN.y),abs(materialN.z))),1e-30));
  vec3 qdx=dFdx(q),qdy=dFdy(q);
  vec4 field=vec4(.5,0.,.5,.5);
  if(resolved>.001 && u_microParams.y>.5)field=closeField(q+phase*vec3(31.,17.,47.),materialN,qdx,qdy,planet?.5:.125);
  float stone=field.r,crack=field.g,deposit=field.b,grit=field.a;
  float originalRough=rough;
  float ice=1.-step(.1,abs(u_surface.x-3.));
  float frost=smoothstep(.42,.72,deposit+grit*.13)*ice*(1.-smoothstep(245.,278.,u_surface.z));
  float seam=crack*(1.-frost*.45)*(.62+.38*clamp(u_materialDomain.y,0.,1.));
  float height=((stone-.5)*.75-seam*(.38+ice*.30)+(grit-.5)*.06)*u_microParams.x*u_microParams.y*resolved;
  vec3 dx=dFdx(v_position),dy=dFdy(v_position),a=cross(dy,N),b=cross(N,dx);float det=dot(dx,a);
  vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
  gradient*=min(1.,.65/max(length(gradient),.001));N=normalize(N-gradient);
  // Mineral-specific response follows inherited mineral/chemistry values.
  // Baked fractures affect illumination only; the generator owns cavities.
  vec3 body=albedo*(.63+stone*.55+grit*.16);
  body+=pigment(phase+deposit*.23)*.035+flow*.045;
  if(ice>.5){
   body=mix(body,body*.65+vec3(.10,.20,.23),frost*.58);
   body=mix(body,body*.25+vec3(.004,.018,.045),seam*.72);
   rough=mix(.25+grit*.17,.58,frost*.78);
  }else{
   float dust=smoothstep(.45,.73,deposit)*(1.-crack);
   body=mix(body,body*.79+albedo*.14,dust*.40);
   rough=clamp(rough+(grit-.5)*.18+crack*.10,.18,.99);
  }
  rough=mix(originalRough,rough,resolved*u_microParams.y);
  microVein=seam*resolved*u_microParams.y;
  cavity=1.-microVein*mix(.22,.38,ice);
  albedo=mix(albedo,body*cavity,resolved*.85*u_microParams.y);
  microGlow+=pigment(phase+deposit*.23+.42)*microVein*.035;

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
  lit+=ecColour*(ecVeins*.58+ecLace*.19)*u_hyperion[1].w*(.045+.955*day)*(terrain?.025:1.);
  lit+=(tapestry*(.016+.075*fresnel)*(.12+.88*day)+flow*.012*day)*(terrain?.12:1.);
  lit+=pigment(n.z*.9+phase+fresnel*.35)*fresnel*(terrain?.035:.21)*(.12+.88*day);
  lit+=microGlow+pigment(phase+fresnel*.4)*microVein*(.018+fresnel*.16);
 }
 float response=atom?.58:craft?.88:.96;
 gl_FragColor=vec4(mix(base,lit,strength*response),v_color0.a);
}
