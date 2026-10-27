// Ported verbatim from Hyperion src/flight/shaders.hpp PRISM_PIGMENT_GLSL.

// Actual Hyperion psychedelic look. No simulation state or world generation.
#define psychedelic 1.0
#define extremeQuality 0.0
vec3 pigment(float h){return pow(.5+.5*cos(6.283185*(h+vec3(0.,.68,.34))),vec3(psychedelic>.5?1.85:1.35));}
float thread(float p,float width){float span=fwidth(p),aa=clamp(span*.8,.002,.75),resolved=1.-smoothstep(.65,1.75,span);float strand=1.-smoothstep(width,width+aa,abs(sin(p)));float coverage=2.*asin(clamp(width,0.,1.))/3.14159265;return mix(coverage,strand,resolved);}
vec3 silk(vec3 direction,float phase,float seconds){
 float t=seconds*.045;vec3 p=direction*3.2;
 vec3 motion=vec3(sin(t*.31),cos(t*.27),sin(t*.19+1.7));
 vec3 q=p+.20*sin(p.yzx*2.8+motion*.7+phase);
 q+=.065*sin(q.zxy*6.8-motion*.5);
 float fold=q.y*3.7+sin(q.x*3.4+motion.x*.5)*1.7+.38*sin(q.z*5.-q.y*2.+motion.y*.5);
 float crossFold=q.z*4.2+sin(q.y*3.6-motion.z*.5)*1.4;
 vec3 front=pigment(fold*.18+phase+sin(t*.11)*.045),back=pigment(crossFold*.15+phase+.36+sin(t*.13+2.)*.035);
 float foldResolved=1.-smoothstep(1.,3.,fwidth(fold)),crossResolved=1.-smoothstep(1.,3.,fwidth(crossFold));float sheet=mix(.384,pow(.5+.5*sin(fold),1.9),foldResolved),rear=mix(.3125,pow(.5+.5*sin(crossFold),3.),crossResolved);
 float fine=thread(fold*17.+.6*sin(crossFold*2.),.035),lace=thread(fold*43.+sin(crossFold*3.1),.018);
 float crest=thread(fold+.15,.055),shade=mix(.55,.28+.72*pow(.5+.5*cos(fold+.8),2.),foldResolved);
 // Lift the broad pigment sheets before the fine fibres: colour stays rich
 // at distance without making subpixel threads brighter or less stable.
 return front*(.07+sheet*shade*.66+fine*.28+lace*.09)+back*rear*.26+mix(front,vec3(.35,1.,.82),.2)*crest*.42;
}
// Optional maximum psychedelic interference tapestry. Coordinates belong to the universe or object,
// so flight motion reveals the pattern without moving the ground or the HUD.
vec3 ecstatic(vec3 direction,float phase,float seconds){
 vec3 p=direction*4.8;float t=seconds*.018;
 p+=.62*sin(p.yzx*1.7+vec3(t*.33,-t*.27,t*.19)+phase*3.);
 p+=.25*sin(p.zxy*3.1+vec3(2.,.7,4.)-t*.14);
 float a=p.y*3.1+sin(p.x*1.8+p.z*.65)*2.7;
 float b=p.z*2.7+sin(p.y*1.6-p.x*.8)*2.4;
 float f=.5+.5*sin(a),g=.5+.5*sin(b);
 float mainThread=thread(a*3.8+.55*sin(b),.065);
 float fineThread=thread(a*13.+sin(b*2.),.025);
 float crossThread=thread(b*5.2+.4*sin(a),.030);
 vec3 front=pigment(a*.085+phase+t*.018),rear=pigment(b*.09+phase+.32-t*.012);
 float resolved=1.-smoothstep(.7,2.5,max(fwidth(a),fwidth(b)));
 float sheet=mix(.35,pow(f,1.7),resolved),back=mix(.31,pow(g,2.2),resolved);
 return front*(.025+sheet*.82+mainThread*.65+fineThread*.24)+rear*(back*.48+crossThread*.26);
}
