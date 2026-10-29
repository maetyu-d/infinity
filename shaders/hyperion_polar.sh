uniform vec4 u_polar[2]; // inherited climate axis.xyz/strength; inner/outer/time
// Hyperion flight/shaders.hpp::auroraFragment. The optical shell uses planet
// radii rather than Hyperion's fixed chart units and shares reversed opaque
// depth with atmosphere. No stars, terrain or celestial objects are added.
vec2 polarSpan(vec2 uv, out vec3 o, out vec3 d){
 o=vec3(0.);d=vec3(0.);
 if(u_polar[0].w<=0.)return vec2(0.);
 vec2 ndc=uv*2.-1.;
 vec3 cameraRay=normalize(vec3(ndc.x*u_atmosphere[3].x,ndc.y*u_atmosphere[3].y*u_atmosphere[3].z,-1.));
 d=normalize(mul(u_atmosphereRotation,vec4(cameraRay,0.)).xyz);o=u_atmosphere[0].xyz;
 vec2 outer=sphereSpan(o,d,u_polar[1].y),inner=sphereSpan(o,d,u_polar[1].x);
 float start=max(0.,outer.x),end=outer.y;
 if(inner.x>0.)end=min(end,inner.x);
 else if(length(o)<u_polar[1].x)start=max(start,inner.y);
 vec2 ground=sphereSpan(o,d,1.);if(ground.x>0.)end=min(end,ground.x);
 float depth=texture2D(s_opaqueDepth,uv).r;
 if(depth>0.){float near=u_atmosphere[2].y,far=u_atmosphere[2].z;float z=near*far/(near+depth*(far-near));end=min(end,z/max(-cameraRay.z,.0001)/u_atmosphere[2].x);}
 return end>start?vec2(start,end):vec2(0.);
}

bool polarSegmentActive(vec3 o,vec3 d,vec2 span){
 vec3 axis=normalize(u_polar[0].xyz),L=normalize(u_atmosphere[1].xyz);
 vec3 entry=o+d*span.x,exitPoint=o+d*span.y;
 // Conservative bounds on the entire occupied ray segment. Skip day-side
 // and non-polar pixels before marching; no history or stochastic samples.
 if(min(dot(entry,L),dot(exitPoint,L))>.15*u_polar[1].y)return false;
 float a=dot(entry,axis),b=dot(exitPoint,axis);
 float low=a*b<=0.?0.:min(abs(a),abs(b))/u_polar[1].y;
 float high=max(abs(a),abs(b))/u_polar[1].x;
 if(high<.50||low>.91)return false;
 return true;
}

#if INFINITE_POLAR_RESOLVE
SAMPLER2D(s_polarLight,6);
// Hyperion cloudResolveFragment: bilinear weights with depth rejection.
// The occupied ray is also checked at full resolution so a thin foreground
// silhouette cannot inherit emission from its half-resolution neighbours.
vec3 resolvePolar(vec2 uv){
 vec3 o,d;vec2 span=polarSpan(uv,o,d);if(span.y<=span.x||!polarSegmentActive(o,d,span))return vec3(0.);
 vec2 texel=u_post[1].xy,pixel=uv/texel-.5,base=floor(pixel),f=fract(pixel);
 float depth=texture2D(s_opaqueDepth,uv).r,total=0.;vec3 colour=vec3(0.);
 for(int y=0;y<2;y++)for(int x=0;x<2;x++){
  vec2 tap=(base+vec2(float(x),float(y))+.5)*texel;
  float other=texture2D(s_opaqueDepth,tap).r;
  float compatible=(depth==0.&&other==0.)?1.:smoothstep(.93,.99,min(depth,other)/max(max(depth,other),1e-20));
  float weight=(x==0?1.-f.x:f.x)*(y==0?1.-f.y:f.y)*compatible;
  colour+=texture2D(s_polarLight,tap).rgb*weight;total+=weight;
 }
 return colour/max(total,.0001);
}
#else
vec3 polarEmission(vec2 uv){
 vec3 o,d;vec2 span=polarSpan(uv,o,d);
 float rayPixel=max(length(dFdx(d)),length(dFdy(d)));
 float start=span.x,end=span.y;if(end<=start)return vec3(0.);
 vec3 axis=normalize(u_polar[0].xyz),L=normalize(u_atmosphere[1].xyz);
 if(!polarSegmentActive(o,d,span))return vec3(0.);
 float stepSize=(end-start)/16.,drift=sin(u_polar[1].z*.027+u_atmosphere[2].w*31.);
 float angularPixel=rayPixel*max(1.,end);
 float rayFrequency=clamp(28./(u_polar[1].y-u_polar[1].x),129.,8192.);
 float phase=u_atmosphere[2].w;vec3 sum=vec3(0.);
 for(int i=0;i<16;i++){
  vec3 p=o+d*(start+(float(i)+.5)*stepSize),n=normalize(p);
  float height=clamp((length(p)-u_polar[1].x)/(u_polar[1].y-u_polar[1].x),0.,1.);
  float night=1.-smoothstep(-.25,.15,dot(n,L));
  float wave=sin(n.x*10.+n.z*7.+drift*.7)+.42*sin(n.z*21.-n.x*9.-drift*.4);
  float oval=abs(dot(n,axis))-(.70+wave*.045+height*.012),width=.017+.010*(.5+.5*sin(n.x*16.-n.z*11.));
  if(abs(oval)>.12||night<.005)continue;
  float filteredWidth=sqrt(width*width+angularPixel*angularPixel*.5);
  float curtain=exp(-oval*oval/(filteredWidth*filteredWidth))*width/filteredWidth;
  float rays=mix(.241074,.02+.98*pow(.5+.5*sin(n.x*rayFrequency+n.z*rayFrequency*.783+wave*3.+drift+height*.8),6.),1.-smoothstep(.65,2.6,angularPixel*rayFrequency*1.27));
  float veil=exp(-pow((oval+.02)*28.,2.))*.025;
  float envelope=pow(max(0.,sin(height*3.14159)),.7);
  vec3 colour=pigment(height*.30+phase+n.z*.18+.07*drift+oval*1.2);
  // Normalize by retained shell thickness, not an arbitrary world-unit size.
  sum+=colour*(curtain*rays+veil)*night*envelope*stepSize/(u_polar[1].y-u_polar[1].x)*1.04;
 }
 sum*=3.8/(1.+max(sum.r,max(sum.g,sum.b))*.35);
 float observer=.14+.86*smoothstep(u_polar[1].y,u_polar[1].y+(u_polar[1].y-u_polar[1].x),length(o));
 return sum*u_polar[0].w*observer;
}

#endif
