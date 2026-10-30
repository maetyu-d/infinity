$input v_pattern
#include <bgfx_shader.sh>
#include "hyperion_ring_density.sh"
uniform vec4 u_rings[4];
void main(){
 float r=length(v_pattern.xz),footprint=fwidth(r);
 float density=ringDensity(u_rings[0],r,footprint);
 if(density<.0001)discard;
 vec3 L=normalize(u_rings[2].xyz),V=normalize(u_rings[3].xyz-v_pattern);
 float sunlight=1.;float along=dot(v_pattern,L);
 float closest=dot(v_pattern,v_pattern)-along*along;
 float shadowEdge=max(.003,footprint*2.);
 if(along<0.)sunlight=smoothstep(1.-shadowEdge,1.+shadowEdge,closest);
 float grazing=max(.08,abs(V.y)),alpha=(1.-exp(-density/grazing))*u_rings[1].w;
 float phase=pow(max(0.,dot(-V,L)),6.);
 float illumination=.13+sunlight*(.45+.55*sqrt(abs(L.y))+phase*.18);
 vec3 colour=u_rings[1].rgb*illumination*(.82+density*.6);
 gl_FragColor=vec4(colour,alpha);
}
