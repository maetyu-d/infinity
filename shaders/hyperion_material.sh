// Hyperion worldFragment: psychedelic mineral/terrain treatment, BRDF,
// filtered veins and spectral Fresnel. All inputs are immutable scene data.
uniform vec4 u_hyperion[4];
uniform vec4 u_cavity;
uniform vec4 u_materialDomain;
#include "hyperion_planets.sh"
uniform mat4 u_pattern;
uniform vec4 u_surface;
uniform vec4 u_environment;
uniform vec4 u_landingContact[2]; // ground point/radius and tangent up/strength, camera-relative
uniform vec4 u_weatherVisual[3];
#if INFINITE_FLIGHT_RELIEF
uniform mat4 u_micro;
uniform mat4 u_meso;
uniform vec4 u_microParams;
#include "hyperion_material_detail.sh"
#endif
#include "hyperion_noise.sh"
#include "hyperion_pigment.sh"
#include "hyperion_tone.sh"
vec3 brdf(vec3 albedo,vec3 N,vec3 V,vec3 L,float rough,float metallic) {
 vec3 H=normalize(V+L);float nv=max(dot(N,V),.001),nl=max(dot(N,L),0.),nh=max(dot(N,H),0.),vh=max(dot(V,H),0.);
 float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1.)+1.;float D=a2/max(3.14159*denom*denom,.00001);
 float k=(rough+1.)*(rough+1.)/8.;float G=nv/(nv*(1.-k)+k)*nl/(nl*(1.-k)+k);
 vec3 F0=mix(vec3(.035),albedo,metallic),F=F0+(1.-F0)*pow(1.-vh,5.);
 return ((1.-F)*(1.-metallic)*albedo/3.14159+F*D*G/max(4.*nv*max(nl,.001),.001))*nl*3.8;
}
// Hyperion lighting.hpp receiver-plane PCF, evaluated on actual geometry.
// Depth-buffer reconstruction cannot retain grazing-angle surface precision.
SAMPLER2D(s_sunShadow,4);
uniform mat4 u_sunFromWorld;
uniform vec4 u_sunShadowParams;
float surfaceSun(vec3 position,float nl){
 if(u_sunShadowParams.x<.5)return 1.;
 vec3 q=mul(u_sunFromWorld,vec4(position,1.)).xyz;
 if(q.z<=0.||q.z>=1.||min(q.x,q.y)<=.002||max(q.x,q.y)>=.998)return 1.;
 vec3 dx=dFdx(q),dy=dFdy(q);float det=dx.x*dy.y-dx.y*dy.x;
 vec2 gradient=abs(det)>1e-16?vec2(dx.z*dy.y-dy.z*dx.y,dy.z*dx.x-dx.z*dy.x)/det:vec2(0.);
 float texel=u_sunShadowParams.y;
 vec2 pixel=q.xy/texel-.5,base=(floor(pixel)+.5)*texel,f=fract(pixel);
 float receiver=q.z-max(.00006,.00016*(1.-clamp(nl,0.,1.)));
 float a=step(receiver+dot(gradient,base-q.xy),texture2D(s_sunShadow,base).r);
 float b=step(receiver+dot(gradient,base+vec2(texel,0.)-q.xy),texture2D(s_sunShadow,base+vec2(texel,0.)).r);
 float c=step(receiver+dot(gradient,base+vec2(0.,texel)-q.xy),texture2D(s_sunShadow,base+vec2(0.,texel)).r);
 float e=step(receiver+dot(gradient,base+vec2(texel,texel)-q.xy),texture2D(s_sunShadow,base+vec2(texel,texel)).r);
 float visibility=mix(mix(a,b,f.x),mix(c,e,f.x),f.y);
 float edge=smoothstep(.015,.085,min(min(q.x,q.y),min(1.-q.x,1.-q.y)));
 return mix(1.,visibility,edge);
}
#include "hyperion_illumination.sh"
#include "hyperion_weather.sh"
#include "hyperion_architecture.sh"
void main(){
 float strength=u_hyperion[0].x,seconds=u_hyperion[0].y,phase=u_hyperion[0].z,primitive=u_hyperion[0].w;
 if(primitive<-.5 && u_cavity.w>0. && length(v_position-u_cavity.xyz)<u_cavity.w)discard;
 vec3 base=v_color0.rgb;
 if(strength<=0.){gl_FragColor=vec4(base,v_color0.a);return;}
 if(primitive>1.5){
  float bond=step(1.5,u_materialDomain.x)*(1.-step(2.5,u_materialDomain.x));
  gl_FragColor=vec4(base*(1.+strength*(.50+bond*1.8)),v_color0.a);return;
 }
 vec3 V=normalize(u_hyperion[2].xyz-v_position),L=normalize(u_hyperion[3].xyz);
 vec3 N=normalize(cross(dFdx(v_position),dFdy(v_position)));
 if((primitive>.1 && primitive<1.5) || primitive< -1.5)N=normalize(v_sphere);
 // A field normal can lean behind the view while its coarse LOD triangle
 // remains visible. Reversing it would invert sun lighting into black seams.
 if(primitive< -1.5){float nv=dot(N,V);if(nv<.025)N=normalize(N+V*(.025-nv));}
 else if(dot(N,V)<0.)N=-N;
 if(u_materialDomain.x>3.5 && u_materialDomain.x<4.5){
  gl_FragColor=vec4(mix(base,architecture(base,N,V,L,v_pattern,v_sphere,phase,seconds,surfaceSun(v_position-u_hyperion[2].xyz,dot(N,L))*weatherSun(v_position,u_hyperion[2].xyz,L),v_position),strength*.98),v_color0.a);return;
 }
 bool terrain=primitive< -1.5;
 bool planet=u_materialDomain.x>.5 && u_materialDomain.x<1.5;
 bool atom=primitive>.5 || (u_materialDomain.x>1.5 && u_materialDomain.x<2.5);
 bool craft=u_materialDomain.x>2.5;
 bool cockpit=u_materialDomain.x>4.5;
 bool gas=planet && u_environment.z>.5;
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
 // Solid globe colours contain Geometry.cpp's fixed authoring light.
 // Remove its broad spherical component before the live stellar BRDF. The
 // retained geometry and albedo variation still supply relief and geology.
 if(planet && !terrain && !gas)
  base/=max(.25,.19+.81*max(0.,dot(n,normalize(vec3(-.5,.8,1.)))));
 float elevation=planet?.5+v_elevation*32.:dot(p,vec3(.31,.73,.19));
 float detail=sin(n.x*13.+sin(n.z*6.))*.13;
 vec3 mineral=pigment(elevation*3.7+phase+detail);
 vec3 tapestry=vec3(0.),flow=vec3(0.);
 if(!atom && !craft && !terrain){tapestry=ecstatic(n,phase+.15,seconds);flow=silk(n,phase,seconds);}
 vec3 spectral=base*.30+mineral*.52+tapestry*.52;
 if(planet)spectral=base*.48+mineral*.22+tapestry*.14;
 vec3 albedo=mix(base,spectral,u_hyperion[1].z);
 if(atom || craft)albedo=base;
 if(cockpit)albedo=base*.09;
 else albedo*=planet?.46:.62; // Non-terrain meshes retain their original baked colour.
 if(terrain)albedo=base*.38+mineral*.018; // Streamed terrain supplies unlit reflectance.
 if(planet)albedo=mix(vec3(dot(albedo,vec3(.299,.587,.114)))*vec3(.86,.94,1.),albedo,.82);
 if(planet && !gas && u_planet[0].x>.5){
  float elevationTone=clamp(.45+v_elevation*50.,0.,1.);
  vec3 substrate=mix(u_planet[1].rgb,u_planet[2].rgb,elevationTone);
  albedo=mix(albedo,substrate*.35,.46);
 }
 if(gas){
  albedo=giantClouds(n,seconds)*.48+tapestry*.025;
 }
 // Geological contrast follows the actual authored height and slopes at
 // every LOD. Reuse the inherited mineral pigment instead of evaluating a
 // second regional noise field for every terrain fragment.
 if(planet && !gas){
  float exposed=smoothstep(-.0004,.0008,v_elevation);
  float steep=1.-clamp(dot(N,radial),0.,1.);
  albedo*=.68+exposed*.46;
  albedo=mix(albedo,albedo*.68+mineral*.055,smoothstep(.08,.40,steep)*.32);
 }
 // Mineral identity and the existing geological lowland/temperature rule
 // control coating; no liquid plane, height field or new biome is generated.
 float ice=gas?0.:max(1.-step(.1,abs(u_surface.x-3.)),planet?1.-step(.1,abs(u_environment.w-2.)):0.);
 float liquid=planet && !gas?u_surface.y*smoothstep(235.,275.,u_surface.z)*(1.-smoothstep(340.,410.,u_surface.z)):0.;
 float wet=liquid*(1.-smoothstep(-.00088,-.00072,v_elevation));
 // The same forecast that draws the rain wets nearby retained terrain.
 // No stored water, geometry, roughness texture or procedural seed changes.
#if INFINITE_FLIGHT_RELIEF
 if(terrain && u_weatherVisual[0].w>.5 && u_weatherVisual[2].z>u_weatherVisual[2].w+.01 && u_microParams.x>0.){
  float rangeMetres=length(v_position-u_hyperion[2].xyz)*.36/u_microParams.x;
  float rain=max(0.,u_weatherVisual[2].z-u_weatherVisual[2].w);
  rain*=smoothstep(271.,280.,u_surface.z)*(1.-smoothstep(365.,390.,u_surface.z));
  wet=max(wet,rain*.85*(1.-smoothstep(5000.,15000.,rangeMetres)));
 }
#endif
 // Hyperion glacial colour/roughness response, constrained by the actual
 // exposed heights, slopes, temperature and ice identity of this world.
 float glacialFrost=0.;
 if(planet && ice>.5){
  float slope=1.-clamp(dot(N,radial),0.,1.);
  glacialFrost=smoothstep(-.0004,.0012,v_elevation)*(1.-smoothstep(.10,.40,slope))*(1.-smoothstep(250.,280.,u_surface.z));
  float layers=v_elevation*18000.+n.y*7.;
  float band=mix(.5,.5+.5*sin(layers),1.-smoothstep(.6,2.,fwidth(layers)));
  vec3 denseIce=albedo*vec3(.32,.56,.72)+vec3(.004,.022,.046)+mineral*.022;
  vec3 snow=albedo*.62+vec3(.16,.23,.27);
  albedo=mix(denseIce*(.83+band*.22),snow,glacialFrost*.84);
 }
 float rough=atom?.24:craft?u_hyperion[1].x:u_hyperion[1].x*.88,microVein=0.,cavity=1.;
 vec3 microGlow=vec3(0.),coatHue=mineral;
#if INFINITE_FLIGHT_RELIEF
 // Hyperion surface_detail.hpp: smooth strata/deposits bridge planetary
 // colour to the separately filtered 16 m close-material texture.
 if(terrain && u_microParams.z>0. && u_microParams.y>.5){
  vec3 q=mul(u_meso,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
  vec3 origin=mul(transpose(u_meso),vec4(0.,0.,0.,1.)).xyz;
  vec3 offset=phase*vec3(31.,17.,47.);
  float broad=strataNoise(q*.0625+offset,origin*.0625);
  float warp=strataNoise(q*.25+offset+11.,origin*.25);
  float middle=strataNoise(q+offset+warp*2.,origin);
  float grain=strataNoise(q*4.+offset+31.,origin*4.);
  float strata=broad*.50+warp*.30+middle*.20;
  float slope=1.-clamp(dot(N,radial),0.,1.);
  float dust=smoothstep(.30,.76,warp)*(1.-smoothstep(.08,.5,slope));
  float layer=q.y*(.25*6.283185)+middle*2.+warp*4.;
  float bedding=mix(.5,.5+.5*sin(layer),1.-smoothstep(.6,2.,fwidth(layer)));
  float vein=thread(middle*24.+warp*.8,.035)*(1.-smoothstep(.15,.9,fwidth(middle*24.)));
  vec3 deposits=albedo*(.24+strata*1.45+grain*.34+bedding*.12);
  deposits=mix(deposits,deposits*.75+vec3(.055,.043,.032),dust*.30);
  coatHue=pigment(phase+strata*.85+grain*.16);
  // Hyperion's geological pigment sits in the reflectance. Light and shadow
  // still describe the existing relief; this is not an emissive overlay.
  // Pigment absorbs the broad white substrate instead of adding another
  // pastel light term. The retained metre-anchored strata select the hue.
  vec3 dryPigment=deposits*(.32+coatHue*1.25)+coatHue*.065;
  vec3 frozenPigment=deposits*vec3(.57,.83,1.05)+coatHue*.075;
  deposits=mix(deposits,mix(dryPigment,frozenPigment,ice),.62);
  deposits=mix(deposits,deposits*.88+coatHue*.028,.20);
  deposits+=coatHue*.003;
  deposits=mix(deposits,deposits*.65+pigment(phase+middle*.18)*.11,vein*.32);
  albedo=deposits;
  rough=clamp(rough+(grain-.5)*.16+dust*.10,.22,.98);
  float height=((middle-.5)*1.8+(grain-.5)*.35)*u_microParams.z;
  vec3 dx=dFdx(v_position),dy=dFdy(v_position),a=cross(dy,N),b=cross(N,dx);float det=dot(dx,a);
  vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
  gradient*=min(1.,.42/max(length(gradient),.001));N=normalize(N-gradient);
  cavity*=mix(.60,1.,smoothstep(.28,.58,middle+grain*.08));
  float mineralIce=ice;
  float quartz=1.-step(.1,abs(u_surface.x));
  float hot=smoothstep(800.,1350.,u_surface.z)*smoothstep(.35,.80,u_materialDomain.z);
  vec3 seamColour=mix(coatHue,mix(vec3(1.6,.13,.015),vec3(1.9,.55,.06),middle),hot);
  microGlow=seamColour*vein*(.016+mineralIce*.028+quartz*.022+hot*.36);
 }
 if(u_microParams.x>0. && primitive<.5 && !atom && !craft){
  vec3 q=mul(u_micro,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
  float footprint=max(length(dFdx(q)),length(dFdy(q)));
  // Below float interpolation precision this inherited physical tile is locally
  // constant. Do not amplify quantisation into false normal relief at nano zoom.
  float resolved=(1.-smoothstep(.12,.75,footprint))*smoothstep(.00002,.0002,footprint);
  vec3 materialN=mul(mat3(u_micro),N);
  materialN=normalize(materialN/max(max(abs(materialN.x),max(abs(materialN.y),abs(materialN.z))),1e-30));
  vec3 qdx=dFdx(q),qdy=dFdy(q);
  vec4 field=vec4(.5,0.,.5,.5);
  if(resolved>.001 && u_microParams.y>.5)field=closeField(q+phase*vec3(31.,17.,47.),materialN,qdx,qdy,planet?.5:.125);
  float stone=field.r,crack=field.g,deposit=field.b,grit=field.a;
  float originalRough=rough;
  float frost=smoothstep(.42,.72,deposit+grit*.13)*ice*(1.-smoothstep(245.,278.,u_surface.z));
  float seam=crack*(1.-frost*.45)*(.62+.38*clamp(u_materialDomain.y,0.,1.));
  float height=((stone-.5)*1.1-seam*(.55+ice*.35)+(grit-.5)*.09)*u_microParams.x*u_microParams.y*resolved;
  vec3 dx=dFdx(v_position),dy=dFdy(v_position),a=cross(dy,N),b=cross(N,dx);float det=dot(dx,a);
  vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
  gradient*=min(1.,.65/max(length(gradient),.001));N=normalize(N-gradient);
  // Mineral-specific response follows inherited mineral/chemistry values.
  // Baked fractures affect illumination only; the generator owns cavities.
  vec3 body=albedo*(.63+stone*.55+grit*.16);
  body+=pigment(phase+deposit*.23)*.035+flow*.045;
  if(ice>.5){
   body=mix(body,body*.57+vec3(.17,.28,.33),frost*.65);
   body=mix(body,body*.18+vec3(.003,.010,.034),seam*.82);
   rough=mix(.19+grit*.16,.68,frost*.82);
  }else{
   float dust=smoothstep(.45,.73,deposit)*(1.-crack);
   body=mix(body,body*.79+albedo*.14,dust*.40);
   rough=clamp(rough+(grit-.5)*.18+crack*.10,.18,.99);
  }
  rough=mix(originalRough,rough,resolved*u_microParams.y);
  microVein=seam*resolved*u_microParams.y;
  cavity*=1.-microVein*mix(.38,.50,ice);
  albedo=mix(albedo,body*cavity,resolved*.85*u_microParams.y);
  microGlow+=pigment(phase+deposit*.23+.42)*microVein*.035;

 }
#endif
#if INFINITE_FLIGHT_RELIEF
 // Hyperion water.hpp: filtered, metre-anchored ripple normals on the
 // existing warm wet coating. Integer frequencies preserve the 64-unit
 // material-origin period; no sea plane or landing surface is added.
 if(planet && wet>.02 && ice<.5 && u_microParams.x>0.){
  vec3 q=mul(u_micro,vec4(v_position-u_hyperion[2].xyz,1.)).xyz;
  vec3 a=vec3(31.,0.,17.)*.0981747704,b=vec3(-69.,0.,19.)*.0981747704,c=vec3(113.,0.,-67.)*.0981747704;
  vec3 wave=vec3(dot(q,a)-seconds*.94+phase*2.7,dot(q,b)-seconds*1.41+phase*4.1,dot(q,c)-seconds*2.37+phase*7.);
  vec3 footprint=abs(vec3(dot(dFdx(q),a),dot(dFdx(q),b),dot(dFdx(q),c)))+abs(vec3(dot(dFdy(q),a),dot(dFdy(q),b),dot(dFdy(q),c)));
  vec3 resolved=1.-smoothstep(vec3(.45),vec3(1.65),footprint);
  float height=dot(sin(wave),resolved*vec3(.12,.035,.012))*u_microParams.x;
  N=materialBump(N,v_position,height*wet,.38);
  rough=mix(rough,.13+dot(1.-resolved,vec3(.028,.020,.012)),wet*.8);
 }
#endif
 // Hyperion's wet lowland response and glacial depth tint. These shade
 // the retained triangles; they never flatten terrain or change collision.
 if(planet){
  albedo=mix(albedo,albedo*.47+vec3(.003,.018,.025),wet*.72);
  rough=mix(rough,.19,wet*.78);
 }
 if(planet && ice>.5)rough=mix(rough,mix(.20,.76,glacialFrost),.65);
 // Dust and weathered sediment are rough dielectric coatings. The actual
 // slopes expose the inherited bedrock; liquid and ice keep their own BRDF.
 float regolith=0.;
 if(planet && !gas){
  float steep=1.-clamp(dot(N,radial),0.,1.);
  regolith=(1.-ice)*(1.-wet)*(1.-smoothstep(.10,.38,steep));
  rough=max(rough,mix(.52,.82,regolith)*(1.-wet)*(1.-ice));
 }
 vec3 ndx=dFdx(N),ndy=dFdy(N);float variance=min(.25,max(dot(ndx,ndx),dot(ndy,ndy))*.5);
 rough=pow(min(1.,pow(rough,4.)+variance),.25);
 // Hyperion worldFragment's pixel-angle roughness floor suppresses distant
 // glints while close ice/mineral highlights remain crisp.
 if(planet){float pixelAngle=max(length(dFdx(n)),length(dFdy(n)));rough=max(rough,mix(.13,.48,smoothstep(.002,.025,pixelAngle)));}
 float fresnel=pow(1.-max(0.,dot(N,V)),4.),sunDot=dot(N,L),day=smoothstep(-.10,.18,sunDot);
 // Hyperion worldFragment: directional light plus sky/ground irradiance.
 // The broad view-dependent reflection stays subordinate to surface relief.
 vec3 up=planet?radial:normalize(L+vec3(.15,.4,.2));
 float skyDay=planet?smoothstep(-.16,.10,dot(radial,L)):1.;
 float air=planet?clamp(u_environment.x*.7,0.,1.):.15;
 float visibility=cockpit?1.:surfaceSun(v_position-u_hyperion[2].xyz,sunDot);
 if(planet){
  visibility*=weatherSun(v_position,u_hyperion[2].xyz,L);
  vec3 localLight=normalize(u_planet[4].xyz+vec3(1e-20));
  visibility*=planetRingSun(n,localLight);
 }
 // Atmospheric sunset light changes only illumination, with the retained
 // stellar direction and pressure. Airless worlds keep their neutral sun.
 vec3 sunlight=mix(vec3(1.18,.40,.16),vec3(1.,.96,.88),smoothstep(-.02,.30,dot(up,L)));
 sunlight=mix(vec3(1.),sunlight,air*.80);
 float metallic=atom?.32:u_hyperion[1].y;
 metallic*=1.-regolith*.90;
 vec3 lit=brdf(albedo,N,V,L,rough,metallic)*cavity*visibility*sunlight;
 lit+=diffuseEnvironment(N,up,albedo,skyDay,air)*cavity;
 // Directional environment reflection makes rough rock, ice, polished ore and
 // atomic sites respond differently to the same sun and broad sky.
 vec3 reflected=reflectedEnvironment(reflect(-V,N),up,skyDay,phase,rough,planet?air:1.);
 vec3 f0=mix(vec3(.035),albedo,metallic);
 vec3 reflectance=environmentReflectance(f0,fresnel,rough);
 lit+=reflected*reflectance*(1.-rough*.65)*cavity*(1.+wet*1.4+ice*.38);
 if(planet){
  // Low-energy transmission tint at glacial grazing angles; the dark
  // fractures and opaque silhouettes continue to come from the material.
  lit+=albedo*vec3(.12,.36,.45)*ice*pow(1.-max(0.,dot(N,V)),2.)*day*.22;
  lit+=coatHue*fresnel*(ice*.055+wet*.07)*day;
  // Hyperion worldFragment's filtered luminous shores. Coverage follows the
  // existing wet-lowland elevation rule; this coats the retained triangles.
  // No water plane, coastline geometry or collision surface is introduced.
  float shoreWidth=max(fwidth(v_elevation)*1.5,.000012);
  float shore=1.-smoothstep(shoreWidth,shoreWidth*3.,abs(v_elevation+.00080));
  float shoreResolved=1.-smoothstep(.00010,.00060,fwidth(v_elevation));
  vec3 foam=mix(vec3(.24,.34,.31),coatHue*.35+.08,.22);
  lit+=foam*shore*shoreResolved*liquid*(1.-ice)*(.06+.36*day)*visibility;
 }
 // Hyperion worldFragment's gear contact shadow. The CPU supplies the actual
 // retained terrain point and physical hull size; this changes colour only.
 if(terrain && u_landingContact[1].w>0.){
  vec3 delta=v_position-u_hyperion[2].xyz-u_landingContact[0].xyz;
  delta-=u_landingContact[1].xyz*dot(delta,u_landingContact[1].xyz);
  float contact=exp(-dot(delta,delta)/max(u_landingContact[0].w*u_landingContact[0].w,1e-24));
  lit*=1.-contact*u_landingContact[1].w;
 }
 lit+=albedo*pow(max(0.,dot(V,L)),4.)*day*visibility*(terrain?.045:.012)*cavity;
 if(atom){
  // Element hue and all bond/site identities stay legible. Only illumination,
  // a small grazing spectral sheen and the existing bloom affect the atoms.
  lit+=base*.075+mix(base,pigment(phase+fresnel*.35),.22)*fresnel*.26;
  lit+=mix(base,vec3(1.),.35)*pow(max(0.,dot(N,normalize(V+L))),48.)*.24;
 }else if(craft){
  lit+=base*(cockpit?.015:.14)+pigment(phase+fresnel*.4)*fresnel*(cockpit?.16:.38);
 }else{
  float ecPhase=elevation*176.+detail*2.+sin(n.x*29.+n.z*17.)*.24;
  float ecVeins=thread(ecPhase,.065),ecLace=thread(ecPhase*2.7+u_materialDomain.y*5.,.025);
  vec3 ecColour=pigment(elevation*6.3+phase+n.z*.18+seconds*.0008);
  lit+=ecColour*(ecVeins*.58+ecLace*.19)*u_hyperion[1].w*(.045+.955*day)*(terrain?.025:gas?.12:1.);
  lit+=(tapestry*(.016+.075*fresnel)*(.12+.88*day)+flow*.012*day)*(terrain?.12:gas?.20:1.);
  lit+=mix(pigment(n.z*.9+phase+fresnel*.35),coatHue,terrain?.35:0.)*fresnel*(terrain?.025:gas?.07:.15)*(.18+.82*day);
  if(planet && !terrain){
   float air=gas?1.:clamp(u_environment.x*.7,0.,1.);
   vec3 limb=mix(vec3(.10,.42,.85),pigment(phase+fresnel*.28)*.8,.32);
   lit+=limb*pow(fresnel,.8)*air*(.08+.45*smoothstep(-.15,.25,dot(radial,L)));
  }
  lit+=microGlow+pigment(phase+fresnel*.4)*microVein*(.018+fresnel*.16);
 }
 // Incandescence follows the retained surface temperature and geological
 // height contours. It never changes the solid world's terrain or deposits.
 if(planet && u_planet[0].x>5.5 && u_planet[0].x<6.5){
  float contour=v_elevation*4000.+dot(n,vec3(3.,5.,2.));
  float footprint=max(.025,fwidth(contour));
  float fissures=1.-smoothstep(.06,.06+footprint,abs(sin(contour)));
  float thermal=clamp(pow(max(0.,u_surface.z)/1400.,4.),0.,4.);
  vec3 heat=mix(vec3(1.5,.055,.006),vec3(2.1,.65,.045),smoothstep(1150.,1800.,u_surface.z));
  lit=lit*.12+heat*thermal*(.08+fissures*.40);
 }
 // The existing cutaway remains visible behind the specimen, with lower
 // reflected illumination so its facets do not overwhelm the inspected sites.
 if(u_surface.w>.5)lit*=.22;
 float response=atom?.91:cockpit?.98:craft?.88:.96;
 vec3 colour=mix(base,lit,strength*response);
 if(cockpit)colour=toneColour(colour);
 gl_FragColor=vec4(colour,v_color0.a);
}
