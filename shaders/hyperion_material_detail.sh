SAMPLER2D(s_materialDetail,3);
// Smooth reconstruction while magnifying the baked height field. Bilinear
// height has a discontinuous slope at each texel; differentiating that slope
// makes close ice look like rectangular tiles. Four bilinear taps reconstruct
// a C2 cubic B-spline without another image or a larger material texture.
vec4 materialSample(vec2 uv,vec2 dx,vec2 dy){
 float footprint=max(length(dx),length(dy))*512.;
 if(footprint>=1.5)return texture2DGrad(s_materialDetail,uv,dx,dy).rgba;
 vec2 p=uv*512.-.5,cell=floor(p),f=fract(p),a=1.-f;
 vec2 w0=a*a*a/6.,w1=(3.*f*f*f-6.*f*f+4.)/6.;
 vec2 w2=(-3.*f*f*f+3.*f*f+3.*f+1.)/6.,w3=f*f*f/6.;
 vec2 g0=w0+w1,g1=w2+w3;
 vec2 lo=(cell-1.+w1/g0+.5)/512.,hi=(cell+1.+w3/g1+.5)/512.;
 vec4 reconstructed=texture2DGrad(s_materialDetail,lo,dx,dy).rgba*g0.x*g0.y+
             texture2DGrad(s_materialDetail,vec2(hi.x,lo.y),dx,dy).rgba*g1.x*g0.y+
             texture2DGrad(s_materialDetail,vec2(lo.x,hi.y),dx,dy).rgba*g0.x*g1.y+
             texture2DGrad(s_materialDetail,hi,dx,dy).rgba*g1.x*g1.y;
 if(footprint<=.75)return reconstructed;
 return mix(reconstructed,texture2DGrad(s_materialDetail,uv,dx,dy).rgba,smoothstep(.75,1.5,footprint));
}
// Hyperion closeMaterial's three planar samples, with complete mipmaps.
// The 16 m planet tile divides the modulo-64 origin period exactly, so
// floating-origin changes cannot shift its phase. Rocks inherit their own
// existing physical material spacing from the same read-only adapter.
vec4 closeField(vec3 q,vec3 normal,vec3 dx,vec3 dy,float frequency){
 vec3 w=pow(abs(normal),vec3(4.));w/=max(dot(w,vec3(1.)),.0001);
 // Hyperion buildingField's coherent projection pruning. On a flat face
 // skip insignificant planes rather than paying for twelve cubic taps.
 w*=step(vec3(.001),w);w/=max(dot(w,vec3(1.)),.0001);
 vec3 p=q*frequency;
 vec4 field=vec4(0.);
 if(w.x>0.)field+=materialSample(p.yz,dx.yz*frequency,dy.yz*frequency)*w.x;
 if(w.y>0.)field+=materialSample(p.zx,dx.zx*frequency,dy.zx*frequency)*w.y;
 if(w.z>0.)field+=materialSample(p.xy,dx.xy*frequency,dy.xy*frequency)*w.z;
 return field;
}

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
