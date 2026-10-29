// Source: Hyperion flight/architecture.hpp, structureLight and its glass,
// ceramic coating, room lighting, prismatic paint and grazing-film branches.
// Only material presentation; local metre coordinates never follow the camera.
float structureHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
// Hyperion architecture.hpp::windowInterior, adapted from model UVs to
// Infinite's existing metre-grid facades. The room is optical presentation
// behind an existing glass surface; no building or simulation is generated.
vec3 windowInterior(vec3 V,vec3 N,vec2 grid,vec3 position,vec3 accent,float identity,float working){
 vec2 du=dFdx(grid),dv=dFdy(grid);float det=du.x*dv.y-du.y*dv.x;
 if(abs(det)<1e-12)return accent*(.015+working*.09);
 vec3 px=dFdx(position)*u_materialDomain.z,py=dFdy(position)*u_materialDomain.z;
 vec3 tangent=(px*dv.y-py*du.y)/det,bitangent=(py*du.x-px*dv.x)/det;
 vec3 ray=vec3(-dot(V,normalize(tangent))/max(length(tangent),.05),-dot(V,normalize(bitangent))/max(length(bitangent),.05),-max(dot(V,N),.08)/2.8);
 vec3 safeRay=vec3(ray.x<0.?-1.:1.,ray.y<0.?-1.:1.,-1.)*max(abs(ray),vec3(.00001));
 vec3 origin=vec3(clamp(fract(grid),vec2(0.),vec2(1.))-.5,-.001);
 vec3 wall=vec3(safeRay.x>0.?.5:-.5,safeRay.y>0.?.5:-.5,-1.);
 vec3 travel=(wall-origin)/safeRay;float t=min(travel.x,min(travel.y,travel.z));vec3 hit=origin+safeRay*t;
 float back=step(travel.z,min(travel.x,travel.y)+.0001),floorFace=step(travel.y,min(travel.x,travel.z)+.0001)*step(safeRay.y,0.);
 vec3 room=mix(vec3(.20,.15,.10),accent*.25+vec3(.08,.10,.14),identity);
 room*=mix(.30,.64,back)*mix(1.,.55,floorFace);
 float details=1.-smoothstep(.08,.35,max(length(du),length(dv)));
 float desk=(1.-smoothstep(.025,.065,abs(hit.y+.24)))*(1.-smoothstep(.20,.35,abs(hit.x)))*back;
 float lamp=thread(hit.x*7.,.22)*smoothstep(.22,.42,hit.y)*back;
 room+=details*(vec3(.025,.035,.05)*desk+mix(vec3(1.,.59,.29),accent,.35)*lamp*.16);
 room*=.12+working*mix(.18,1.,step(.24,identity))*1.15;
 return mix(accent*(.015+working*.09),room,details);
}
vec3 architecture(vec3 base,vec3 N,vec3 V,vec3 L,vec3 p,vec3 authoredPoint,float phase,float seconds,float sunVisibility,vec3 position){
 float role=u_materialDomain.y,localY=authoredPoint.y;
 // Remove the fixed light baked into the existing surface-mesh colours.
 // True geometry normals, sun and sky then illuminate the same triangles once.
 if(u_materialDomain.w<.5){
  vec3 authoredN=normalize(cross(dFdx(authoredPoint),dFdy(authoredPoint)));
  vec3 worldN=cross(dFdx(position),dFdy(position));
  if(dot(worldN,V)<0.)authoredN=-authoredN;
  base/=max(.55,.55+.45*max(0.,dot(authoredN,normalize(vec3(.4,1.,.3)))));
 }
 bool paving=abs(role-8.)<.1||abs(role-20.)<.1||abs(role-21.)<.1||abs(role-6.)<.1;
 bool pad=abs(role-7.)<.1,solar=abs(role-22.)<.1;

 bool vegetation=role>16.5&&role<17.5;
 bool person=role>8.5&&role<10.5;
 float glass=vegetation||person||paving||pad||solar?0.:smoothstep(1.5,2.2,base.b/max(base.r,.01))*smoothstep(1.4,2.,base.g/max(base.r,.01));
 float inlay=vegetation||person||paving||solar?0.:smoothstep(.44,.70,base.g-base.r)*smoothstep(.4,.65,base.g);
 vec3 localN=normalize(cross(dFdx(p),dFdy(p)));
 vec3 w=abs(localN);
 vec2 uv=w.z>w.x&&w.z>w.y?p.xy:w.x>w.y?p.zy:p.xz;
 vec2 grid=uv/vec2(3.6,3.2),cell=fract(grid),edge=min(cell,1.-cell);
 float footprint=max(length(dFdx(p)),length(dFdy(p)));
 float fine=1.-smoothstep(.4,1.8,footprint);
 float panel=structureHash(vec3(floor(grid),phase*113.));
 float panelAA=max(fwidth(grid.x),fwidth(grid.y));
 float joint=(1.-smoothstep(.006,.018+panelAA,min(edge.x,edge.y)))*fine;
 vec2 corner=min(abs(cell-.065),abs(cell-.935));
 float bolts=(1.-smoothstep(max(0.,.009-panelAA),.009+panelAA,length(corner)))*(1.-smoothstep(.01,.06,panelAA));
 vec3 pigmentPaint=pigment(phase+p.y*.009+p.x*.005+sin(p.z*.021)*.16);
 // Hyperion architecture.hpp: coarse finish plus filtered pits, repairs,
 // runoff and concrete aggregate, using the existing packed material image.
 vec2 materialUV=uv/3.6+phase*vec2(7.1,3.7);
 vec2 materialDx=dFdx(materialUV),materialDy=dFdy(materialUV);
 vec4 finish=texture2DGrad(s_materialDetail,materialUV*.125,materialDx*.125,materialDy*.125);
 vec4 detailField=texture2DGrad(s_materialDetail,materialUV,materialDx,materialDy);
 float wear=smoothstep(.35,.75,finish.b),pores=detailField.a*detailField.g;
 float upright=1.-abs(dot(N,normalize(u_environment.xyz)));
 float runoff=smoothstep(.42,.75,finish.a)*upright*wear;
 vec3 albedo=pow(max(base,vec3(.001)),vec3(1.85))*.62;
 albedo*=mix(vec3(1.),.65+pigmentPaint*.9,.40);
 albedo*=.72+finish.r*.38+detailField.r*.16*fine;
 albedo=mix(albedo,albedo*.45+vec3(.012,.018,.024),runoff*.38);
 albedo*=1.-pores*.16*fine;
 albedo*=1.-joint*.25;
 albedo+=vec3(.025,.029,.032)*bolts;
 float rough=mix(.78,.17,glass),metal=mix(.04,.28,glass);
 rough=clamp(rough+(detailField.a-.5)*.12+wear*.10,.14,.98);
 if(paving||pad){
  float kerb=smoothstep(.30,.50,base.r+base.g*.15);
  vec3 aggregate=mix(vec3(.025,.035,.044),vec3(.12,.135,.14),kerb);
  albedo=aggregate*(.67+detailField.r*.44+finish.b*.22);
  rough=.92;metal=0.;
 }
 if(solar){
  float cells=(1.-smoothstep(.025,.055+panelAA,min(edge.x,edge.y)))*fine;
  albedo=vec3(.006,.016,.045)*(1.-cells*.65);rough=.23;metal=.12;
 }

 if(vegetation){albedo=base*.23;rough=.92;metal=0.;}
 if(person){albedo=base*.48;rough=.76;metal=.05;}
 if(glass>.1)albedo=mix(albedo,vec3(.008,.025,.044)*(.75+panel*.5),glass);
 float day=smoothstep(-.15,.18,u_surface.x);
 // Hyperion structureBump's recessed panel joints, in actual local metres.
 // Position and silhouettes stay untouched; unresolved joints fade out.
 if(!vegetation && !person){
  float relief=(-joint*.018+(detailField.r-.5)*.012*fine-pores*.006*fine+bolts*.005)/max(u_materialDomain.z,1e-12);
  N=materialBump(N,position,relief,.28);
 }
 vec3 nx=dFdx(N),ny=dFdy(N);float variance=min(.25,max(dot(nx,nx),dot(ny,ny))*.5);
 rough=pow(min(1.,pow(rough,4.)+variance),.25);
 float fresnel=pow(1.-clamp(dot(N,V),0.,1.),4.);
 // Streamed paving has camera-relative vertices, rather than a building's
 // local footing height. Its illumination must not follow that origin.
 float footing=u_materialDomain.w>.5?1.:mix(.42,1.,smoothstep(.015,.15,localY));
 vec3 skyUp=normalize(u_environment.xyz);
 float skyFacing=clamp(dot(N,skyUp)*.5+.5,0.,1.);
 vec3 sunColour=mix(vec3(1.18,.40,.16),vec3(1.08,.98,.84),smoothstep(0.,.40,u_surface.x));
 sunColour=mix(vec3(1.),sunColour,clamp(u_environment.w*.7,0.,.8));
 vec3 c=brdf(albedo,N,V,L,rough,metal)*sunVisibility*sunColour;
 vec3 skyIrradiance=mix(vec3(.018,.028,.055),vec3(.075,.13,.22),day);
 vec3 groundBounce=vec3(.035,.024,.018)*(.2+.8*day);
 c+=albedo*mix(groundBounce,skyIrradiance,skyFacing)*footing;
 vec3 r=reflect(-V,N);
 vec3 sky=reflectedEnvironment(r,skyUp,day,phase,rough);
 sky*=mix(.24,1.,clamp(u_environment.w*.7,0.,1.));
 c+=sky*glass*(.14+fresnel*1.6);
 c*=1.-joint*.24;
 // Lit rooms recede into dark glass; they never bleach the facade coating.
 float room=step(.37,panel),blind=smoothstep(.18,.28,cell.y)*(1.-smoothstep(.80,.86,cell.y));
 vec3 roomColour=mix(vec3(.12,.65,.72),vec3(1.,.49,.16),step(.53,panel));
 if(glass>.1){
  vec3 wallN=normalize(cross(dFdx(position),dFdy(position)));if(dot(wallN,V)<0.)wallN=-wallN;
  vec3 interior=windowInterior(V,wallN,grid,position,roomColour,panel,.18+(1.-day)*.82);
  c=mix(c,interior+sky*(.05+fresnel*.65),glass*(1.-fresnel*.78)*fine*.84);
 }
 c+=roomColour*glass*room*blind*(.035+(1.-day)*.85)*fine;
 c+=pigment(r.y*.22+r.z*.19+(paving?0.:p.y*.018)+phase)*(glass*(fresnel*.13+.008)+fresnel*.035);
 float trace=thread(p.y*.31+sin(p.x*.19+p.z*.17)*2.,.035);
 // Spectral light follows construction joints and glazing rather than
 // painting the whole street with a high-energy interference contour.
 c+=pigmentPaint*joint*.035*(.25+.75*day)*(paving?0.:1.);
 if(glass>.1)c+=pigmentPaint*trace*.025*fine;
 if(vegetation)c+=base*pow(max(0.,dot(-L,V)),2.)*max(0.,-dot(N,L))*sunVisibility*.18;

 c+=mix(vec3(.08,.65,.48),pigment(phase+p.y*.021+seconds*.001),.62)*inlay*1.45;
 return c;
}
