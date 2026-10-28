// Source: Hyperion flight/architecture.hpp, structureLight and its glass,
// ceramic coating, room lighting, prismatic paint and grazing-film branches.
// Only material presentation; local metre coordinates never follow the camera.
float structureHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
vec3 architecture(vec3 base,vec3 N,vec3 V,vec3 L,vec3 p,float localY,float phase,float seconds,float sunVisibility){
 float role=u_materialDomain.y;
 bool vegetation=role>16.5&&role<17.5;
 bool person=role>8.5&&role<10.5;
 float glass=vegetation||person?0.:smoothstep(1.5,2.2,base.b/max(base.r,.01))*smoothstep(1.4,2.,base.g/max(base.r,.01));
 float inlay=vegetation||person?0.:smoothstep(.44,.70,base.g-base.r)*smoothstep(.4,.65,base.g);
 vec3 localN=normalize(cross(dFdx(p),dFdy(p)));
 vec3 w=abs(localN);
 vec2 uv=w.z>w.x&&w.z>w.y?p.xy:w.x>w.y?p.zy:p.xz;
 vec2 grid=uv/vec2(3.6,3.2),cell=fract(grid),edge=min(cell,1.-cell);
 float footprint=max(length(dFdx(p)),length(dFdy(p)));
 float fine=1.-smoothstep(.4,1.8,footprint);
 float panel=structureHash(vec3(floor(grid),phase*113.));
 float joint=(1.-smoothstep(.006,.018,min(edge.x,edge.y)))*fine;
 vec3 pigmentPaint=pigment(phase+p.y*.009+p.x*.005+sin(p.z*.021)*.16);
 vec3 albedo=pow(max(base,vec3(.001)),vec3(1.15))*.80;
 albedo*=mix(vec3(1.),.65+pigmentPaint*.9,.40);
 albedo*=.92+panel*.08*fine;
 albedo*=1.-joint*.25;
 float rough=mix(.78,.17,glass),metal=mix(.04,.28,glass);
 if(vegetation){albedo=base*.23;rough=.92;metal=0.;}
 if(person){albedo=base*.48;rough=.76;metal=.05;}
 if(glass>.1)albedo=mix(albedo,vec3(.008,.025,.044)*(.75+panel*.5),glass);
 float day=smoothstep(-.15,.18,u_surface.x);
 float fresnel=pow(1.-clamp(dot(N,V),0.,1.),4.);
 float footing=mix(.62,1.,smoothstep(.015,.15,localY));
 vec3 c=brdf(albedo,N,V,L,rough,metal)*sunVisibility;
 c+=albedo*mix(vec3(.07,.09,.14),vec3(.28,.34,.43),day)*footing;
 vec3 r=reflect(-V,N),sky=mix(vec3(.008,.016,.030),vec3(.10,.17,.24),day);
 c+=sky*glass*(.10+fresnel*1.15);
 float room=step(.37,panel),blind=smoothstep(.18,.28,cell.y)*(1.-smoothstep(.80,.86,cell.y));
 vec3 roomColour=mix(vec3(.12,.65,.72),vec3(1.,.49,.16),step(.53,panel));
 c+=roomColour*glass*room*blind*(.035+(1.-day)*.85)*fine;
 c+=pigment(r.y*.22+r.z*.19+p.y*.018+phase)*(glass*(fresnel*.13+.008)+fresnel*.035);
 float trace=thread(p.y*.31+sin(p.x*.19+p.z*.17)*2.,.035);
 c+=pigmentPaint*trace*.075*fine*(.35+.65*day);
 c+=mix(vec3(.08,.65,.48),pigment(phase+p.y*.021+seconds*.001),.62)*inlay*1.45;
 return c;
}
