uniform vec4 u_weatherVisual[3];
// Hyperion's distance-dependent weather haze, evaluated in the existing
// composite with the exact opaque depth. Visibility, type and storm strength
// come from the live forecast; the atlas retains the spatial weather front.
vec3 weatherVisibility(vec3 colour,vec2 uv){
 if(u_weatherVisual[0].x<=0.||u_weatherVisual[0].w<.5||u_atmosphere[0].w<=0.)return colour;
 vec2 ndc=uv*2.-1.;
 vec3 ray=normalize(vec3(ndc.x*u_atmosphere[3].x,ndc.y*u_atmosphere[3].y*u_atmosphere[3].z,-1.));
 vec3 o=u_atmosphere[0].xyz,d=normalize(mul(u_atmosphereRotation,vec4(ray,0.)).xyz);
 float H=max(1e-7,u_weatherVisual[0].y);
 // The local layer follows the ship's actual ground elevation, including
 // high plateaus and depressions in Infinite's unchanged terrain.
 vec2 span=sphereSpan(o,d,max(1.,length(o))+H*4.);
 float start=max(0.,span.x),end=span.y;
 float depth=texture2D(s_opaqueDepth,uv).r;
 if(depth>0.){float near=u_atmosphere[2].y,far=u_atmosphere[2].z;float z=near*far/(near+depth*(far-near));end=min(end,z/max(-ray.z,.0001)/u_atmosphere[2].x);}
 if(end<=start)return colour;
 float stepSize=(end-start)/3.,optical=0.,eyeHeight=length(o)-1.;
 for(int i=0;i<3;i++){
  float h=length(o+d*(start+(float(i)+.5)*stepSize))-1.;
  optical+=exp(clamp((eyeHeight-h)/H,-20.,2.))*stepSize;
 }
 vec4 field=opticalWeatherField(normalize(o+d*(start+end)*.35),0.);
 float local=u_weatherVisual[0].z>.5?u_weatherVisual[2].w:u_weatherVisual[2].z;
 float front=mix(local,u_weatherVisual[0].z>.5?field.a:field.b,
                 clamp(u_weather[3].w*u_weather[1].w,0.,1.));
 optical*=u_weatherVisual[0].x*clamp(front/max(local,.08),0.,1.25);
 float day=smoothstep(-.15,.18,dot(normalize(o),normalize(u_atmosphere[1].xyz)));
 vec3 tint=u_weatherVisual[1].rgb*(.10+.90*day)+vec3(.30,.45,.72)*u_weatherVisual[1].w*.65;
 vec3 transmission=exp(-optical*vec3(.88,1.,1.12));
 return colour*transmission+tint*(1.-transmission);
}
