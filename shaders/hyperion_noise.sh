SAMPLER2D(s_materialNoise,5);
// Hyperion shaders.hpp::noise with its exact lookup image. Unlike enlarged
// rock cells this gives continuous strata. The 4096-unit material period is
// divisible by the lookup's 256-cell period at every octave used below.
float strataNoise(vec3 p,vec3 origin) {
 // Keep the fractional coordinate local. Adding the large periodic origin
 // first quantises centimetre-scale camera motion into rectangular normals.
 vec3 cell=floor(p)+origin,f=fract(p);f=f*f*(3.-2.*f);
 // Wrap the integer lookup before adding its fractional interpolation.
 // Large unwrapped UVs quantize f into visible bands on Retina displays.
 vec2 uv=mod(cell.xy+vec2(37.,17.)*cell.z,vec2(256.))+f.xy;
 vec2 rg=texture2DLod(s_materialNoise,vec2((uv+.5)/256.),0.).rg;
 float resolved=1.-smoothstep(.6,2.,max(length(dFdx(p)),length(dFdy(p))));
 return mix(.5,mix(rg.r,rg.g,f.z),resolved);
}
