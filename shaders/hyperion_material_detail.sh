SAMPLER2D(s_materialDetail,3);
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

SAMPLER2D(s_materialNoise,5);
// Hyperion shaders.hpp::noise with its exact lookup image. Unlike enlarged
// rock cells this gives continuous strata. The 4096-unit material period is
// divisible by the lookup's 256-cell period at every octave used below.
float strataNoise(vec3 p) {
 vec3 cell=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 // Wrap the integer lookup before adding its fractional interpolation.
 // Large unwrapped UVs quantize f into visible bands on Retina displays.
 vec2 uv=mod(cell.xy+vec2(37.,17.)*cell.z,vec2(256.))+f.xy;
 vec2 rg=texture2DLod(s_materialNoise,vec2((uv+.5)/256.),0.).rg;
 float resolved=1.-smoothstep(.6,2.,max(length(dFdx(p)),length(dFdy(p))));
 return mix(.5,mix(rg.r,rg.g,f.z),resolved);
}
