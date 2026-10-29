// Occupied portions of a hollow shell, in planet radii. Opaque geometry
// limits the ray before the empty inner interval is removed.
vec4 cloudSpan(vec2 uv,out vec3 o,out vec3 d){
 o=vec3(0.);d=vec3(0.);if(u_weather[0].w<=0.||u_weather[3].w<.5)return vec4(0.);
 vec2 ndc=uv*2.-1.;
 vec3 cameraRay=normalize(vec3(ndc.x*u_atmosphere[3].x,ndc.y*u_atmosphere[3].y*u_atmosphere[3].z,-1.));
 o=u_atmosphere[0].xyz;d=normalize(mul(u_atmosphereRotation,vec4(cameraRay,0.)).xyz);
 vec2 outer=sphereSpan(o,d,u_weather[0].y),inner=sphereSpan(o,d,u_weather[0].x);
 float start=max(0.,outer.x),end=outer.y;
 vec2 ground=sphereSpan(o,d,1.);if(ground.x>0.)end=min(end,ground.x);
 float depth=texture2D(s_opaqueDepth,uv).r;
 if(depth>0.){float near=u_atmosphere[2].y,far=u_atmosphere[2].z;float z=near*far/(near+depth*(far-near));end=min(end,z/max(-cameraRay.z,.0001)/u_atmosphere[2].x);}
 if(end<=start)return vec4(0.);
 float aEnd=end,bStart=end;
 if(inner.y>start && inner.x<end){aEnd=clamp(inner.x,start,end);bStart=clamp(inner.y,start,end);}
 return vec4(start,aEnd,bStart,end);
}
