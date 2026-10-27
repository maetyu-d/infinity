$input v_texcoord0
#include <bgfx_shader.sh>
uniform vec4 u_sky[2];
uniform mat4 u_skyRotation;
#include "hyperion_pigment.sh"
// Analytic equivalent of Hyperion skyFragment's mip-filtered dust lookup.
// No stars or celestial geometry are synthesized by this material.
float skyHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float skyNoise(vec3 p){vec3 c=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(skyHash(c),skyHash(c+vec3(1.,0.,0.)),f.x),mix(skyHash(c+vec3(0.,1.,0.)),skyHash(c+vec3(1.,1.,0.)),f.x),f.y),mix(mix(skyHash(c+vec3(0.,0.,1.)),skyHash(c+vec3(1.,0.,1.)),f.x),mix(skyHash(c+vec3(0.,1.,1.)),skyHash(c+vec3(1.,1.,1.)),f.x),f.y),f.z);}
float dust(vec3 p){return skyNoise(p)*.57+skyNoise(p*2.03+7.1)*.29+skyNoise(p*4.09+13.4)*.14;}
void main(){
 vec2 ndc=v_texcoord0*2.-1.;
 vec3 ray=normalize(vec3(ndc.x*u_sky[1].x,ndc.y*u_sky[1].y*u_sky[1].z,-1.));
 vec3 d=normalize(mul(u_skyRotation,vec4(ray,0.)).xyz);
 float phase=u_sky[0].y,t=u_sky[0].w;
 vec3 q=d*3.1+vec3(phase*13.,0.,phase*7.);
 float wisps=dust(q*1.7),band=exp(-pow((d.y*.75+d.x*.31+d.z*.12+.09+sin(d.z*3.)*.1)*3.8,2.));
 float cloud=pow(max(0.,wisps-.23),2.1)*band;
 float nearDust=skyNoise(q*3.1+3.),cavity=smoothstep(.44,.72,nearDust)*band;
 vec3 c=vec3(.004,.007,.016);
 c+=mix(vec3(.018,.12,.18),vec3(.20,.032,.14),smoothstep(.38,.68,wisps))*cloud*5.4;
 c*=exp(-cavity*1.65);
 // Hyperion skyFragment's maximum-look layers, adapted to Infinite's gas
 // abundance and ink gaps. Generated star and gas positions remain live.
 vec3 tapestry=ecstatic(d,phase,t);
 float folds=smoothstep(.08,.52,max(tapestry.r,max(tapestry.g,tapestry.b)));
 c=c*.58+tapestry*(.35+cloud*1.35)*folds+silk(d.zxy,phase+.31,t)*.20;
 c+=pigment(d.y*.8+d.x*.3+phase)*thread((d.y+sin(d.x*4.+d.z)*.19)*48.,.025)*.065;
 c*=exp(-cavity*.8);
 float density=.20+.80*clamp(u_sky[0].z,0.,1.);
 c=mix(vec3(.004,.007,.016),c,u_sky[0].x*density);
 gl_FragColor=vec4(c,1.);
}
