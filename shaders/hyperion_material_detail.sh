SAMPLER2D(s_materialDetail,3);
uniform vec4 u_surface;
// Hyperion closeMaterial's three planar samples, with complete mipmaps.
// The 16 m planet tile divides the modulo-64 origin period exactly, so
// floating-origin changes cannot shift its phase. Rocks inherit their own
// existing physical material spacing from the same read-only adapter.
vec4 closeField(vec3 q,vec3 normal,vec3 dx,vec3 dy,float frequency){
 vec3 w=pow(abs(normal),vec3(4.));w/=max(dot(w,vec3(1.)),.0001);
 vec3 p=q*frequency;
 return texture2DGrad(s_materialDetail,p.yz,dx.yz*frequency,dy.yz*frequency).rgba*w.x+
        texture2DGrad(s_materialDetail,p.zx,dx.zx*frequency,dy.zx*frequency).rgba*w.y+
        texture2DGrad(s_materialDetail,p.xy,dx.xy*frequency,dy.xy*frequency).rgba*w.z;
}
