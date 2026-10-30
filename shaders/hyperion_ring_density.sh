// Optical depth of the generated Roche-confined debris field. Derivative
// filtering resolves bands without allocating a per-planet ring texture.
float ringDensity(vec4 ring,float radius,float footprint){
 if(ring.z<=0.||radius<=ring.x||radius>=ring.y)return 0.;
 float u=(radius-ring.x)/(ring.y-ring.x),du=footprint/max(.001,ring.y-ring.x);
 float edge=smoothstep(0.,max(.035,du),u)*(1.-smoothstep(1.-max(.035,du),1.,u));
 float gap=1.-smoothstep(.014,.022+du,abs(u-(.54+ring.w*.12)));
 float narrow=1.-smoothstep(.004,.010+du,abs(u-(.27+ring.w*.07)));
 float bands=.48+.30*sin(u*91.+ring.w*6.283)*(1.-smoothstep(.02,.08,du))
                  +.16*sin(u*263.+ring.w*13.)*(1.-smoothstep(.005,.025,du));
 return ring.z*edge*(1.-gap*.97)*(1.-narrow*.82)*max(.04,bands);
}
