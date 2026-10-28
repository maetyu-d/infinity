// Hyperion hue-preserving film curve, shared by world and cockpit.
vec3 toneColour(vec3 c){
 float brightness=dot(c,vec3(.299,.587,.114));c=max(vec3(0.),mix(vec3(brightness),c,1.28));
 c=max(c*1.36,vec3(0.));
 float peak=max(c.r,max(c.g,c.b)),mappedPeak=(peak*(2.51*peak+.03))/(peak*(2.43*peak+.59)+.14);
 vec3 film=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);
 c=clamp(mix(film,c/max(peak,.0001)*mappedPeak,.86),vec3(0.),vec3(1.));
 c=pow(c,vec3(1./2.2));
 return c;
}
