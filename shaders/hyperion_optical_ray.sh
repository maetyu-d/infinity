uniform vec4 u_atmosphere[4];
uniform mat4 u_atmosphereRotation;
vec2 sphereSpan(vec3 o,vec3 d,float r) {
    float b=dot(o,d),h=r*r-dot(cross(o,d),cross(o,d));
    if(h<0.)return vec2(-1.,-1.);h=sqrt(h);return vec2(-b-h,-b+h);
}
