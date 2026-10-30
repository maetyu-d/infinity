SAMPLER2D(s_weatherField,7);
SAMPLER2D(s_weatherNext,8);
uniform vec4 u_weather[4];
uniform mat4 u_weatherFromEye;
// Read-only snapshots of Infinite's sampleWeather. The pole rows share a
// single physical direction; longitude wraps without a seam.
vec4 opticalWeatherField(vec3 n,float lod){
 vec2 uv=vec2(atan2(n.z,n.x)/6.283185+.5,acos(clamp(n.y,-1.,1.))/3.14159265);
 uv.y=uv.y*(63./64.)+.5/64.;
 return mix(texture2DLod(s_weatherField,uv,lod),texture2DLod(s_weatherNext,uv,lod),u_weather[1].x);
}
vec3 opticalWeather(vec3 n,float lod){return opticalWeatherField(n,lod).rgb;}
// Hyperion worldFragment's projected weather shadow, on the retained terrain
// and buildings. Opaque structure shadows are still resolved independently.
float weatherSun(vec3 position,vec3 eye,vec3 light){
 if(u_weather[3].w<.5 || u_weather[1].w<=0.)return 1.;
 vec3 n=normalize(mul(u_weatherFromEye,vec4(position-eye,1.)).xyz);
 vec3 L=normalize(mul(mat3(u_weatherFromEye),light));
 float sun=dot(n,L);if(sun<-.03)return 1.;
 float height=(u_weather[0].x+u_weather[0].y)*.5-1.;
 vec3 cloud=opticalWeather(normalize(n+L*height/max(.15,sun)),0.);
 return 1.-cloud.r*min(.65,u_weather[0].z*.9)*smoothstep(-.03,.15,sun)*u_weather[1].w;
}
