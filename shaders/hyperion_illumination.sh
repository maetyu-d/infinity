// Hyperion worldFragment/structureLight: hemispherical sky irradiance and
// broad reflected sky/film. Analytic, roughness-filtered and world-oriented;
// no screen-colour feedback or additional texture allocation.
vec3 reflectedEnvironment(vec3 R,vec3 up,float day,float phase,float rough,float air){
 float h=dot(R,up),horizon=pow(1.-abs(h),4.);
 vec3 zenith=mix(vec3(.005,.009,.025),vec3(.035,.095,.19),day);
 vec3 rim=mix(vec3(.013,.022,.045),vec3(.26,.32,.35),day);
 // An airless surface sees space, rather than a bright terrestrial sky.
 // Retain Hyperion's subdued spectral field in the reflected hemisphere.
 vec3 sky=mix(vec3(.003,.005,.012),mix(zenith,rim,horizon),air);
 vec3 film=pigment(dot(R,vec3(.19,.31,.23))+phase)*(.018+horizon*.045);
 sky+=film*(1.-rough*.65)*(.18+.82*air);
 return mix(vec3(.012,.009,.007)*(.2+.8*day),sky,smoothstep(-.18,.12,h));
}
// Building glazing retains its existing environment response.
vec3 reflectedEnvironment(vec3 R,vec3 up,float day,float phase,float rough){
 return reflectedEnvironment(R,up,day,phase,rough,1.);
}
// Rough dielectric Fresnel: unresolved rough facets do not turn an entire
// grazing landscape into a white mirror. Wet coatings retain their glints.
vec3 environmentReflectance(vec3 f0,float fresnel,float rough){
 return f0+(max(vec3(1.-rough),f0)-f0)*fresnel;
}
vec3 diffuseEnvironment(vec3 N,vec3 up,vec3 albedo,float day,float air){
 float skyFacing=clamp(dot(N,up)*.5+.5,0.,1.);
 vec3 sky=mix(vec3(.018,.028,.055),vec3(.070,.12,.21),air)*(.15+.85*day);
 vec3 bounce=mix(vec3(.025,.017,.012),albedo*.09,air)*(.15+.85*day);
 return albedo*mix(bounce,sky,skyFacing);
}
vec3 materialBump(vec3 N,vec3 position,float height,float maxSlope){
 vec3 dx=dFdx(position),dy=dFdy(position),a=cross(dy,N),b=cross(N,dx);
 float det=dot(dx,a);
 vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
 gradient*=min(1.,maxSlope/max(length(gradient),.001));
 return normalize(N-gradient);
}
