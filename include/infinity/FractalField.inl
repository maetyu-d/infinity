// Shared float mathematics: compiled as both C++ and Metal. Coordinates are metres.
struct RutSample {float height;float2 gradient;float compaction;float curvature;};
inline float fr_rut_cell() {return .125f;}
inline float fr_rut_limit() {return .32f;}
inline float fr_rut_curvature() {return 96.0f;}
// B-spline node differences bound each gradient component by .75*depth/cell.
// With 32 cm nodes, |grad rut| <= 2.716; including the terraced fractal, .18 is a
// conservative distance scale even on the steepest possible groove wall.
inline float fr_rut_scale() {return .18f;}
inline float4 fr_rut_weights(float t) {
    float u=1-t;return float4(u*u*u,3*t*t*t-6*t*t+4,-3*t*t*t+3*t*t+3*t+1,t*t*t)/6.0f;
}
inline float4 fr_rut_derivatives(float t) {float u=1-t;return float4(-u*u*.5f,1.5f*t*t-2*t,-1.5f*t*t+t+.5f,t*t*.5f);}
inline float fr_rut_max4(float4 v) {return max(max(v.x,v.y),max(v.z,v.w));}
inline float fr_rut_row_second(float4 v) {return max(abs(v.x-2*v.y+v.z),abs(v.y-2*v.z+v.w));}
inline float fr_rut_row_first(float4 v) {return max(max(abs(v.y-v.x),abs(v.z-v.y)),abs(v.w-v.z));}
inline float fr_rut_cell_curvature(float4 a,float4 b,float4 c,float4 d) {
    float xx=max(max(fr_rut_row_second(a),fr_rut_row_second(b)),max(fr_rut_row_second(c),fr_rut_row_second(d)));
    float zz=max(fr_rut_max4(abs(a-2.0f*b+c)),fr_rut_max4(abs(b-2.0f*c+d)));
    float xz=max(max(fr_rut_row_first(b-a),fr_rut_row_first(c-b)),fr_rut_row_first(d-c));
    return (max(xx,zz)+xz)/(fr_rut_cell()*fr_rut_cell());
}
inline RutSample fr_rut_accumulate(RutSample s,float2 node,float wx,float wz,float dx,float dz) {
    s.height-=node.x*wx*wz;s.gradient-=node.x*float2(dx*wz,wx*dz)/fr_rut_cell();s.compaction+=node.y*wx*wz;return s;
}
#if !defined(INFINITY_GPU)
class Deformation;
RutSample fr_cpu_rut(const Deformation*,float2);
#endif
struct TerrainFeature {float4 placement,shape;};
struct World {float phase;uint seed;
#if defined(INFINITY_GPU)
    constant float4* features;int featureCount;
#endif
#if !defined(INFINITY_GPU)
    const Deformation* tracks=nullptr;
    int route=0;float loopLength=1200;int markerCount=24;float markerSpacing=180;const float4* markerCache=nullptr;
    const TerrainFeature* features=nullptr;int featureCount=0;
#endif
};
struct Vehicle {float4 placement,attitude,tyres,compression,steering,rotation,dents,systems;};
struct Field {float distance;int material;};
inline uint fr_hash(uint x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
inline float fr_random(uint x) {return float(fr_hash(x)&0xffffffu)/16777215.0f;}
inline float fr_noise2(float2 p,uint seed) {
    float2 cell=floor(p),f=p-cell;f=f*f*(float2(3)-2.0f*f);
    int ix=int(cell.x),iz=int(cell.y);uint key=uint(ix)*73856093u^uint(iz)*19349663u^seed;
    float a=fr_random(key),b=fr_random(uint(ix+1)*73856093u^uint(iz)*19349663u^seed);
    float c=fr_random(uint(ix)*73856093u^uint(iz+1)*19349663u^seed),d=fr_random(uint(ix+1)*73856093u^uint(iz+1)*19349663u^seed);
    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y)*2.0f-1.0f;
}
struct NoiseSample {float value;float2 gradient;};
inline NoiseSample fr_noise_sample(float2 p,uint seed) {
    float2 cell=floor(p),t=p-cell,f=t*t*(float2(3)-2.0f*t),df=6.0f*t*(float2(1)-t);
    int ix=int(cell.x),iz=int(cell.y);
    float a=fr_random(uint(ix)*73856093u^uint(iz)*19349663u^seed);
    float b=fr_random(uint(ix+1)*73856093u^uint(iz)*19349663u^seed);
    float c=fr_random(uint(ix)*73856093u^uint(iz+1)*19349663u^seed);
    float d=fr_random(uint(ix+1)*73856093u^uint(iz+1)*19349663u^seed);
    return NoiseSample{mix(mix(a,b,f.x),mix(c,d,f.x),f.y)*2.0f-1.0f,
        float2(mix(b-a,d-c,f.y)*df.x,mix(c-a,d-b,f.x)*df.y)*2.0f};
}
inline float fr_fbm(float2 p,uint seed) {
    float value=0,weight=.5f;
    for(int i=0;i<4;++i) {value+=weight*fr_noise2(p,seed+uint(i)*917u);p=p*2.03f+float2(7.13f,3.71f);weight*=.49f;}
    return value;
}
inline float fr_center(float z,float phase) {return 92.0f*sin(z*.0028f)+54.0f*(sin(z*.006f+phase)-sin(phase))+27.0f*sin(z*.011f);}
inline float fr_heading(float z,float phase) {return atan2(fr_center(z+1,phase)-fr_center(z-1,phase),2.0f);}
inline float2 fr_circuit_radii(uint seed) {return float2(150+35*fr_random(seed+887u),175+30*fr_random(seed+223u));}
inline float2 fr_circuit(float theta,float phase,uint seed) {
    float2 r=fr_circuit_radii(seed);float g=1+.035f*(sin(3*theta+phase)-sin(phase))+.0125f*sin(5*theta);
    return float2(r.x*sin(theta)*g,r.y*(1-cos(theta)*g));
}
inline float fr_circuit_heading(float theta,float phase,uint seed) {
    float2 r=fr_circuit_radii(seed);float g=1+.035f*(sin(3*theta+phase)-sin(phase))+.0125f*sin(5*theta),d=.105f*cos(3*theta+phase)+.0625f*cos(5*theta);
    return atan2(r.x*(cos(theta)*g+sin(theta)*d),r.y*(sin(theta)*g-cos(theta)*d));
}
inline float fr_endless_center(float z,float phase,uint seed) {
    return fr_center(z,phase)+48*(fr_noise2(float2(z/700,3.71f),seed+1301u)-fr_noise2(float2(0,3.71f),seed+1301u))+18*(fr_noise2(float2(z/260,8.13f),seed+1753u)-fr_noise2(float2(0,8.13f),seed+1753u));
}
#if !defined(INFINITY_GPU)
inline float2 fr_route_point(float distance,World w) {
    if(w.route==1)return fr_circuit(distance/w.loopLength*6.28318530718f,w.phase,w.seed);
    return float2(w.route==2?fr_endless_center(distance,w.phase,w.seed):fr_center(distance,w.phase),distance);
}
inline float fr_route_heading(float distance,World w) {
    if(w.route==1)return fr_circuit_heading(distance/w.loopLength*6.28318530718f,w.phase,w.seed);
    auto a=fr_route_point(distance-1,w),b=fr_route_point(distance+1,w);return atan2(b.x-a.x,b.y-a.y);
}
#endif
// Nine noise bands form a heterogeneous desert: broad basins, elongated
// sharp-crested dunes and seven bands of finer ridges. Low basins are smoother
// and high country is rougher. The race route never reshapes this surface.
inline float fr_height_max() {return 52.0f;}
inline float fr_terrain_slope() {return 2.4f;}
inline float fr_terrain_scale() {return .38f;}
inline float fr_macro_height(float region) {
    float band=region*3,level=floor(band),t=clamp((band-level-.15f)/.70f,0.0f,1.0f);
    return 12*(level+t*t*(3-2*t));
}
inline float fr_macro_derivative(float region) {
    float band=region*3,t=clamp((band-floor(band)-.15f)/.70f,0.0f,1.0f);
    return 36*6*t*(1-t)/.70f;
}
inline float fr_relief_gain(float region) {
    float t=clamp((region-.15f)/.70f,0.0f,1.0f);return .25f+.75f*t*t*(3-2*t);
}
inline float fr_relief_derivative(float region) {
    float t=clamp((region-.15f)/.70f,0.0f,1.0f);return .75f*6*t*(1-t)/.70f;
}
inline float2 fr_octave(float2 q,float frequency) {return float2(.8f*q.x-.6f*q.y,.6f*q.x+.8f*q.y)*frequency+float2(7.13f,3.71f);}
inline float fr_ridged_height(float2 p,World w,int octaves) {
    if(octaves<=0)return 0;
    float2 q=p/192.0f+float2(3.11f,8.71f);float region=.5f+.5f*fr_noise2(q,w.seed),height=fr_macro_height(region);
    if(octaves==1)return height;
    q=fr_octave(p/192.0f,1);float crest=1-abs(fr_noise2(float2(q.x,q.y*.35f),w.seed+917u));
    float relief=12*crest*crest,amplitude=1.65f;
    q=fr_octave(q,2);
    for(int i=2;i<octaves;++i){relief+=amplitude*(1-abs(fr_noise2(q,w.seed+uint(i)*917u)));q=fr_octave(q,2);amplitude*=.55f;}
    return height+fr_relief_gain(region)*relief;
}
inline float fr_raw_height(float2 p,World w) {return fr_ridged_height(p,w,9);}
struct TerrainSample {float height;float2 gradient;};
inline TerrainFeature fr_feature_at(World w,int i) {
#if defined(INFINITY_GPU)
    return TerrainFeature{w.features[i*2],w.features[i*2+1]};
#else
    return w.features[i];
#endif
}
inline TerrainSample fr_feature_sample(float2 p,World w) {
    TerrainSample out={0,float2(0)};
    for(int i=0;i<w.featureCount;++i){auto f=fr_feature_at(w,i);float2 forward=float2(f.placement.z,f.placement.w),right=float2(forward.y,-forward.x),d=p-float2(f.placement.x,f.placement.y);
        float u=dot(d,right)/f.shape.y,v=dot(d,forward)/f.shape.z,t=1-u*u-v*v;if(t<=0)continue;
        out.height+=f.shape.x*t*t*t;
        out.gradient-=6*f.shape.x*t*t*(right*(u/f.shape.y)+forward*(v/f.shape.z));
    }return out;
}
// Jack supports never overlap, so these maxima also bound their union globally.
inline float2 fr_feature_bounds(float2 center,float radius,World w) {
    float2 bounds=float2(0);
    for(int i=0;i<w.featureCount;++i){auto f=fr_feature_at(w,i);float reach=max(f.shape.y,f.shape.z)+radius;auto d=center-float2(f.placement.x,f.placement.y);
        if(dot(d,d)<=reach*reach){bounds.x=max(bounds.x,f.shape.x);bounds.y=max(bounds.y,30*f.shape.x/(min(f.shape.y,f.shape.z)*min(f.shape.y,f.shape.z)));}}
    return bounds;
}
inline float fr_world_scale(World w) {return w.featureCount>0?.25f:fr_terrain_scale();}
inline float fr_soil_scale(World w) {return w.featureCount>0?.15f:fr_rut_scale();}
inline float fr_height(float2 p,World w) {
    float h=fr_raw_height(p,w)+fr_feature_sample(p,w).height;
#if !defined(INFINITY_GPU)
    if(w.tracks)h+=fr_cpu_rut(w.tracks,p).height;
#endif
    return h;
}
inline TerrainSample fr_terrain_sample(float2 p,World w) {
    float2 q=p/192.0f+float2(3.11f,8.71f),dx=float2(1.0f/192,0),dz=float2(0,1.0f/192);
    NoiseSample broad=fr_noise_sample(q,w.seed);float region=.5f+.5f*broad.value;
    float2 regionGradient=(broad.gradient.x*dx+broad.gradient.y*dz)*.5f;
    q=fr_octave(p/192.0f,1);dx=float2(.8f/192,-.6f/192);dz=float2(.6f/192,.8f/192);float2 nextDx;
    NoiseSample dune=fr_noise_sample(float2(q.x,q.y*.35f),w.seed+917u);float crest=1-abs(dune.value);
    float relief=12*crest*crest,amplitude=1.65f;
    float2 reliefGradient=-24*crest*(dune.value>=0?1.0f:-1.0f)*(dune.gradient.x*dx+dune.gradient.y*dz*.35f);
    q=fr_octave(q,2);nextDx=(.8f*dx-.6f*dz)*2.0f;dz=(.6f*dx+.8f*dz)*2.0f;dx=nextDx;
    for(int i=2;i<9;++i) {
        NoiseSample n=fr_noise_sample(q,w.seed+uint(i)*917u);
        relief+=amplitude*(1.0f-abs(n.value));
        reliefGradient-=amplitude*(n.value>=0?1.0f:-1.0f)*(n.gradient.x*dx+n.gradient.y*dz);
        q=fr_octave(q,2);nextDx=(.8f*dx-.6f*dz)*2.0f;dz=(.6f*dx+.8f*dz)*2.0f;dx=nextDx;amplitude*=.55f;
    }
    float gain=fr_relief_gain(region),height=fr_macro_height(region)+gain*relief;
    float2 gradient=(fr_macro_derivative(region)+fr_relief_derivative(region)*relief)*regionGradient+gain*reliefGradient;
#if !defined(INFINITY_GPU)
    if(w.tracks){auto rut=fr_cpu_rut(w.tracks,p);height+=rut.height;gradient+=rut.gradient;}
#endif
    auto feature=fr_feature_sample(p,w);return TerrainSample{height+feature.height,gradient+feature.gradient};
}
// Exact value-noise range over a small UV rectangle, plus a Hessian bound.
// Cubic interpolation is monotone in either coordinate for fixed other input.
inline float3 fr_noise_bounds(float2 low,float2 high,uint seed) {
    float2 first=floor(low),last=floor(high);float minimum=1,maximum=-1,curvature=0;
    for(int ix=0;ix<2;++ix)for(int iz=0;iz<2;++iz) {
        float2 cell=first+float2(float(ix),float(iz));if(cell.x>last.x||cell.y>last.y)continue;
        int x=int(cell.x),z=int(cell.y);
        float a=fr_random(uint(x)*73856093u^uint(z)*19349663u^seed),b=fr_random(uint(x+1)*73856093u^uint(z)*19349663u^seed);
        float c=fr_random(uint(x)*73856093u^uint(z+1)*19349663u^seed),d=fr_random(uint(x+1)*73856093u^uint(z+1)*19349663u^seed);
        float2 lo=clamp(low-cell,0.0f,1.0f),hi=clamp(high-cell,0.0f,1.0f);
        lo=lo*lo*(float2(3)-2.0f*lo);hi=hi*hi*(float2(3)-2.0f*hi);
        float p=mix(mix(a,b,lo.x),mix(c,d,lo.x),lo.y)*2-1;
        float q=mix(mix(a,b,hi.x),mix(c,d,hi.x),lo.y)*2-1;
        float r=mix(mix(a,b,lo.x),mix(c,d,lo.x),hi.y)*2-1;
        float s=mix(mix(a,b,hi.x),mix(c,d,hi.x),hi.y)*2-1;
        minimum=min(minimum,min(min(p,q),min(r,s)));maximum=max(maximum,max(max(p,q),max(r,s)));
        float diagonal=12*max(max(abs(b-a),abs(d-c)),max(abs(c-a),abs(d-b)));
        curvature=max(curvature,diagonal+4.5f*abs(d-c-b+a));
    }
    return float3(minimum,maximum,curvature);
}
inline float3 fr_octave_bounds(float2 q,float2 dx,float2 dz,World w,uint octave,float cellSize) {
    float2 radius=float2(abs(dx.x)+abs(dx.y),abs(dz.x)+abs(dz.y))*(cellSize*.5f);
    float2 padding=max(float2(.00001f),abs(q)*.000002f);
    return fr_noise_bounds(q-radius-padding,q+radius+padding,w.seed+octave*917u);
}
inline float2 fr_terrain_bounds(float2 center,World w,float cellSize) {
    float2 q=center/192.0f+float2(3.11f,8.71f),dx=float2(1.0f/192,0),dz=float2(0,1.0f/192);
    auto broad=fr_octave_bounds(q,dx,dz,w,0u,cellSize);
    float regionUpper=.5f+.5f*broad.y,gainUpper=fr_relief_gain(regionUpper);
    float upper=fr_macro_height(regionUpper),curvature=40*broad.z/(192*192)+.164f;
    q=fr_octave(center/192.0f,1);dx=float2(.8f/192,-.6f/192);dz=float2(.6f/192,.8f/192);float2 nextDx;
    auto dune=fr_octave_bounds(float2(q.x,q.y*.35f),dx,dz*.35f,w,1u,cellSize);
    float closest=dune.x<=0&&dune.y>=0?0.0f:min(abs(dune.x),abs(dune.y)),crest=1-closest;
    float reliefUpper=12*crest*crest,reliefCurvature=24*(18+crest*dune.z)/(192*192);
    float amplitude=1.65f,frequency=1.0f/96;
    q=fr_octave(q,2);nextDx=(.8f*dx-.6f*dz)*2.0f;dz=(.6f*dx+.8f*dz)*2.0f;dx=nextDx;
    for(int i=2;i<9;++i) {
        auto interval=fr_octave_bounds(q,dx,dz,w,uint(i),cellSize);
        float nearZero=interval.x<=0&&interval.y>=0?0.0f:min(abs(interval.x),abs(interval.y));
        reliefUpper+=amplitude*(1-nearZero);reliefCurvature+=amplitude*frequency*frequency*interval.z;
        q=fr_octave(q,2);nextDx=(.8f*dx-.6f*dz)*2.0f;dz=(.6f*dx+.8f*dz)*2.0f;dx=nextDx;frequency*=2;amplitude*=.55f;
    }
    // Global product-rule terms: 2*|grad gain|*|grad relief| + relief*|H gain|
    // are below .0713. Ridge cusps are concave, so cannot violate this upper bound.
    return float2(upper+gainUpper*reliefUpper+.003f,(curvature+gainUpper*reliefCurvature+.076f)*1.01f)+fr_feature_bounds(center,cellSize*.708f,w);
}
// A lower quadratic bounds the ray-height residual. Noise has Hessian
// norm <= 30; the multifractal and its product-rule terms stay below 3.
// The absolute-value
// ridge cusps bend the residual upwards, so a bound of 3 remains valid.
inline float fr_terrain_advance(float residual,float2 gradient,float3 ray,float extraCurvature=0) {
    float derivative=ray.y-dot(gradient,float2(ray.x,ray.z));
    float curvature=max(.00001f,(3.0f+extraCurvature)*(ray.x*ray.x+ray.z*ray.z));
    float root=sqrt(derivative*derivative+2.0f*curvature*max(0.0f,residual));
    return derivative<0?2.0f*residual/(root-derivative):(derivative+root)/curvature;
}
inline float3 fr_rotate_x(float3 p,float a) {float c=cos(a),s=sin(a);return float3(p.x,c*p.y-s*p.z,s*p.y+c*p.z);}
inline float3 fr_rotate_y(float3 p,float a) {float c=cos(a),s=sin(a);return float3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);}
inline float3 fr_rotate_z(float3 p,float a) {float c=cos(a),s=sin(a);return float3(c*p.x-s*p.y,s*p.x+c*p.y,p.z);}
inline float3 fr_local(float3 p,Vehicle car) {
    p-=float3(car.placement.x,car.placement.y,car.placement.z);
    p=fr_rotate_y(p,-car.placement.w);p=fr_rotate_x(p,-car.attitude.x);return fr_rotate_z(p,-car.attitude.y);
}
inline float fr_box(float3 p,float3 b) {float3 q=abs(p)-b;return length(max(q,float3(0)))+min(max(q.x,max(q.y,q.z)),0.0f);}
inline float fr_capsule(float3 p,float3 a,float3 b,float radius) {float3 pa=p-a,ba=b-a;return length(pa-ba*clamp(dot(pa,ba)/dot(ba,ba),0.0f,1.0f))-radius;}
inline float fr_ellipsoid(float3 p,float3 r) {float k0=length(p/r),k1=length(p/(r*r));return k1>1e-6f?k0*(k0-1)/k1:-min(r.x,min(r.y,r.z));}
inline float fr_menger(float3 p,float3 bounds,int iterations) {
    float d=fr_box(p,bounds);float scale=1;
    float3 q=p/max(bounds,float3(.01f));
    for(int i=0;i<iterations;++i) {
        float3 a=q*scale-2.0f*floor(q*scale*.5f)-float3(1);
        scale*=3;
        float3 r=abs(float3(1)-3.0f*abs(a));
        // Convert each slab to metres before the CSG operations. A single
        // minimum-axis scale collapses wide portals' visible recursive holes.
        float3 slab=(r-float3(1))*bounds/scale;
        float cut=min(max(slab.x,slab.y),min(max(slab.y,slab.z),max(slab.z,slab.x)));
        d=max(d,cut);
    }
    return d;
}
inline Field fr_union(Field a,float d,int material) {return d<a.distance?Field{d,material}:a;}
// One cuboid and four plain, capped cylinders. These helpers define both
// the visible vehicle and its collision surface.
inline float3 fr_block_center(Vehicle car) {
    float left=max(car.dents.x,car.dents.z),right=max(car.dents.y,car.dents.w);
    float front=max(car.dents.x,car.dents.y),rear=max(car.dents.z,car.dents.w);
    return float3((left-right)*.035f,.02f,.12f+(rear-front)*.04f);
}
inline float3 fr_block_bounds(Vehicle car) {
    float left=max(car.dents.x,car.dents.z),right=max(car.dents.y,car.dents.w);
    float front=max(car.dents.x,car.dents.y),rear=max(car.dents.z,car.dents.w);
    return float3(.84f-(left+right)*.035f,.30f-(1-car.attitude.z)*.04f,1.65f-(front+rear)*.04f);
}
inline float3 fr_wheel_center(Vehicle car,int i) {
    return float3(i%2?1.01f:-1.01f,-.54f+car.compression[i],i<2?1.48f:-1.24f);
}
inline float fr_wheel_radius(Vehicle car,int i) {return .46f*(.65f+.35f*car.tyres[i]);}
inline float fr_cylinder_x(float3 p,float radius,float halfWidth) {
    float2 q=float2(abs(p.x)-halfWidth,length(float2(p.y,p.z))-radius);
    return length(max(q,float2(0)))+min(max(q.x,q.y),0.0f);
}
inline Field fr_vehicle_local(float3 p,Vehicle car,bool head,bool wheels) {
    Field result={fr_box(p-fr_block_center(car),fr_block_bounds(car)),5};
    if(wheels)for(int i=0;i<4;++i) {
        float3 q=fr_rotate_y(p-fr_wheel_center(car,i),i<2?-car.steering[i]:0.0f);
        result=fr_union(result,fr_cylinder_x(q,fr_wheel_radius(car,i),.20f),6);
    }
    result.distance*=.72f;return result;
}
inline Field fr_vehicle(float3 p,Vehicle car,bool head,bool wheels) {return fr_vehicle_local(fr_local(p,car),car,head,wheels);}
// Functional checkpoint markers sit on the unmodified fractal terrain.
inline float3 fr_checkpoint_post(int gate,int side,World w) {
#if !defined(INFINITY_GPU)
    float distance=float(gate)*w.markerSpacing,yaw=fr_route_heading(distance,w);auto point=fr_route_point(distance,w);
    float3 base=float3(point.x,0,point.y)+fr_rotate_y(float3(float(side)*13,0,0),yaw);
#else
    float z=float(gate)*180,yaw=fr_heading(z,w.phase);
    float3 base=float3(fr_center(z,w.phase),0,z)+fr_rotate_y(float3(float(side)*13,0,0),yaw);
#endif
    base.y=fr_raw_height(float2(base.x,base.z),w);return base;
}
inline Field fr_environment(float3 p,World w) {
    // h < 52 m; marker tops < 56 m. Switch bounds while still
    // positive, so the bound cannot become a false collision surface.
    if(p.y>fr_height_max()+7)return Field{(p.y-fr_height_max()-4)*.8f,0};
    // Cubic value noise has |grad| <= 3*sqrt(2). The octave sum bounds
    // |grad h| below 2.4; .38*sqrt(1+2.4^2)<1, so tracing is conservative.
    float height=fr_raw_height(float2(p.x,p.z),w)+fr_feature_sample(float2(p.x,p.z),w).height,scale=fr_world_scale(w);
#if !defined(INFINITY_GPU)
    if(w.tracks){auto rut=fr_cpu_rut(w.tracks,float2(p.x,p.z));height+=rut.height;if(rut.height<0)scale=fr_soil_scale(w);}
#endif
    Field result={(p.y-height)*scale,0};
#if !defined(INFINITY_GPU)
    if(w.markerCache) {
        for(int i=0;i<w.markerCount*2;++i){float3 q=p-float3(w.markerCache[i].x,w.markerCache[i].y,w.markerCache[i].z);float bound=max(.14f,result.distance+.14f);if(q.x*q.x+q.z*q.z>bound*bound)continue;result=fr_union(result,fr_capsule(q,float3(0,-fr_rut_limit(),0),float3(0,3.2f,0),.14f),15);}
        return result;
    }
    int gate=int(floor(p.z/w.markerSpacing+.5f));
    if(gate>=1&&(w.markerCount==0||gate<=w.markerCount)&&abs(p.z-float(gate)*w.markerSpacing)<32) {
#else
    int gate=int(floor(p.z/180+.5f));
    if(gate>=1&&gate<=24&&abs(p.z-float(gate)*180)<32) {
#endif
        for(int side=-1;side<=1;side+=2){float3 q=p-fr_checkpoint_post(gate,side,w);result=fr_union(result,fr_capsule(q,float3(0,-fr_rut_limit(),0),float3(0,3.2f,0),.14f),15);}
    }
    return result;
}
// Analytic intervals intersect the same box/cylinder zero sets used above.
struct ImplicitHit {float distance;float3 normal;int material;};
inline ImplicitHit fr_nearest_hit(ImplicitHit a,ImplicitHit b) {return b.distance<a.distance?b:a;}
inline ImplicitHit fr_ray_block(float3 ro,float3 rd,float3 center,float3 bounds,float maximum) {
    ro-=center;
    float3 inverse=float3(rd.x<0?-1.0f:1.0f,rd.y<0?-1.0f:1.0f,rd.z<0?-1.0f:1.0f)/max(abs(rd),float3(1e-10f));
    float3 u=(-bounds-ro)*inverse,v=(bounds-ro)*inverse,lo=min(u,v),hi=max(u,v);
    float begin=max(lo.x,max(lo.y,lo.z)),end=min(hi.x,min(hi.y,hi.z));
    float t=begin>=.06f?begin:end;
    if(begin>end||t<.06f||t>=maximum)return ImplicitHit{maximum,float3(0),0};
    float3 p=ro+rd*t,face=abs(p)/bounds,n=float3(0);
    if(face.x>=face.y&&face.x>=face.z)n.x=p.x<0?-1.0f:1.0f;
    else if(face.y>=face.z)n.y=p.y<0?-1.0f:1.0f;
    else n.z=p.z<0?-1.0f:1.0f;
    return ImplicitHit{t,n,5};
}
inline ImplicitHit fr_ray_wheel(float3 ro,float3 rd,float radius,float halfWidth,float maximum) {
    float begin=-1e20f,end=1e20f;
    float a=rd.y*rd.y+rd.z*rd.z,b=ro.y*rd.y+ro.z*rd.z,c=ro.y*ro.y+ro.z*ro.z-radius*radius;
    if(a<1e-10f) {if(c>0)return ImplicitHit{maximum,float3(0),0};}
    else {
        float disc=b*b-a*c;if(disc<0)return ImplicitHit{maximum,float3(0),0};
        float root=sqrt(disc);begin=(-b-root)/a;end=(-b+root)/a;
    }
    if(abs(rd.x)<1e-10f) {if(abs(ro.x)>halfWidth)return ImplicitHit{maximum,float3(0),0};}
    else {
        float u=(-halfWidth-ro.x)/rd.x,v=(halfWidth-ro.x)/rd.x;
        begin=max(begin,min(u,v));end=min(end,max(u,v));
    }
    float t=begin>=.06f?begin:end;
    if(begin>end||t<.06f||t>=maximum)return ImplicitHit{maximum,float3(0),0};
    float3 p=ro+rd*t,n;
    if(abs(abs(p.x)-halfWidth)<.0001f)n=float3(p.x<0?-1.0f:1.0f,0,0);
    else n=normalize(float3(0,p.y,p.z));
    return ImplicitHit{t,n,6};
}
inline ImplicitHit fr_vehicle_ray(float3 origin,float3 direction,Vehicle car,bool head,bool wheels,float maximum) {
    float3 ro=fr_local(origin,car),rd=fr_rotate_z(fr_rotate_x(fr_rotate_y(direction,-car.placement.w),-car.attitude.x),-car.attitude.y);
    ImplicitHit hit=fr_ray_block(ro,rd,fr_block_center(car),fr_block_bounds(car),maximum);
    if(wheels)for(int i=0;i<4;++i) {
        float steer=i<2?car.steering[i]:0.0f;
        float3 o=fr_rotate_y(ro-fr_wheel_center(car,i),-steer),d=fr_rotate_y(rd,-steer);
        auto wheel=fr_ray_wheel(o,d,fr_wheel_radius(car,i),.20f,hit.distance);
        wheel.normal=fr_rotate_y(wheel.normal,steer);hit=fr_nearest_hit(hit,wheel);
    }
    hit.normal=fr_rotate_y(fr_rotate_x(fr_rotate_z(hit.normal,car.attitude.y),car.attitude.x),car.placement.w);return hit;
}

// Render attachments use their own primitives; the light turret does not alter
// the established chassis/suspension collision hull. systems = yaw,pitch,recoil,flash.
inline float3 fr_world_vector(float3 p,Vehicle car) {return fr_rotate_y(fr_rotate_x(fr_rotate_z(p,car.attitude.y),car.attitude.x),car.placement.w);}
inline float3 fr_turret_pivot(Vehicle car) {auto c=fr_block_center(car);return c+float3(0,fr_block_bounds(car).y+.50f,-1.10f);}
inline float3 fr_turret_direction(Vehicle car) {return fr_rotate_y(fr_rotate_x(float3(0,0,1),-car.systems.y),car.systems.x);}
inline float3 fr_turret_center(Vehicle car,int part) {
    auto pivot=fr_turret_pivot(car);
    if(part==0)return pivot-float3(0,.32f,0);
    if(part==1)return pivot;
    return pivot+fr_rotate_y(float3(0,.04f,.18f),car.systems.x)+fr_turret_direction(car)*(.48f-.10f*car.systems.z);
}
inline float3 fr_turret_bounds(int part) {return part==0?float3(.34f,.18f,.34f):part==1?float3(.42f,.19f,.34f):float3(.10f,.10f,.56f);}
inline float3 fr_turret_turn(float3 q,Vehicle car,int part,bool inverse) {
    if(part==0)return q;
    if(inverse){q=fr_rotate_y(q,-car.systems.x);return part==2?fr_rotate_x(q,car.systems.y):q;}
    if(part==2)q=fr_rotate_x(q,-car.systems.y);return fr_rotate_y(q,car.systems.x);
}
inline float3 fr_turret_muzzle(Vehicle car) {return float3(car.placement.x,car.placement.y,car.placement.z)+fr_world_vector(fr_turret_center(car,2)+fr_turret_direction(car)*.58f,car);}
inline Field fr_visual_vehicle(float3 p,Vehicle car,bool head,bool wheels,bool turret=true) {
    auto result=fr_vehicle(p,car,head,wheels);if(!turret)return result;auto local=fr_local(p,car);
    for(int part=0;part<3;++part){auto q=fr_turret_turn(local-fr_turret_center(car,part),car,part,true);auto b=fr_turret_bounds(part);
        float d=part==1?fr_box(q,b):part==0?fr_cylinder_x(float3(q.y,q.x,q.z),b.x,b.y):fr_cylinder_x(float3(q.z,q.y,q.x),b.x,b.z);
        result=fr_union(result,d*.72f,7+part);
    }return result;
}
inline ImplicitHit fr_visual_vehicle_ray(float3 ro,float3 rd,Vehicle car,bool head,bool wheels,float maximum,bool turret=true) {
    auto result=fr_vehicle_ray(ro,rd,car,head,wheels,maximum);if(!turret)return result;
    auto origin=fr_local(ro,car),direction=fr_rotate_z(fr_rotate_x(fr_rotate_y(rd,-car.placement.w),-car.attitude.x),-car.attitude.y);
    for(int part=0;part<3;++part){auto p=fr_turret_turn(origin-fr_turret_center(car,part),car,part,true),d=fr_turret_turn(direction,car,part,true),b=fr_turret_bounds(part);
        auto hit=part==1?fr_ray_block(p,d,float3(0),b,result.distance):part==0?fr_ray_wheel(float3(p.y,p.x,p.z),float3(d.y,d.x,d.z),b.x,b.y,result.distance):fr_ray_wheel(float3(p.z,p.y,p.x),float3(d.z,d.y,d.x),b.x,b.z,result.distance);
        if(hit.distance>=result.distance)continue;
        if(part==0)hit.normal=float3(hit.normal.y,hit.normal.x,hit.normal.z);else if(part==2)hit.normal=float3(hit.normal.z,hit.normal.y,hit.normal.x);
        hit.normal=fr_world_vector(fr_turret_turn(hit.normal,car,part,false),car);hit.material=7+part;result=hit;
    }return result;
}
// Rendering can draw each component inside its own projected bounds, avoiding
// repeated whole-car intersections at every native-resolution body pixel.
inline ImplicitHit fr_vehicle_component_ray(float3 origin,float3 direction,Vehicle car,int component,float maximum) {
    auto ro=fr_local(origin,car),rd=fr_rotate_z(fr_rotate_x(fr_rotate_y(direction,-car.placement.w),-car.attitude.x),-car.attitude.y);
    ImplicitHit hit={maximum,float3(0),5};
    if(component==0)hit=fr_ray_block(ro,rd,fr_block_center(car),fr_block_bounds(car),maximum);
    else if(component<5){int wheel=component-1;float turn=wheel<2?car.steering[wheel]:0.0f;
        hit=fr_ray_wheel(fr_rotate_y(ro-fr_wheel_center(car,wheel),-turn),fr_rotate_y(rd,-turn),fr_wheel_radius(car,wheel),.20f,maximum);
        hit.normal=fr_rotate_y(hit.normal,turn);
    }else {int part=component-5;auto p=fr_turret_turn(ro-fr_turret_center(car,part),car,part,true),d=fr_turret_turn(rd,car,part,true),b=fr_turret_bounds(part);
        hit=part==1?fr_ray_block(p,d,float3(0),b,maximum):part==0?fr_ray_wheel(float3(p.y,p.x,p.z),float3(d.y,d.x,d.z),b.x,b.y,maximum):fr_ray_wheel(float3(p.z,p.y,p.x),float3(d.z,d.y,d.x),b.x,b.z,maximum);
        if(part==0)hit.normal=float3(hit.normal.y,hit.normal.x,hit.normal.z);else if(part==2)hit.normal=float3(hit.normal.z,hit.normal.y,hit.normal.x);
        hit.normal=fr_turret_turn(hit.normal,car,part,false);hit.material=7+part;
    }
    hit.normal=fr_world_vector(hit.normal,car);return hit;
}
inline ImplicitHit fr_ray_capsule(float3 origin,float3 direction,float3 start,float3 end,float radius,float maximum) {
    auto ba=end-start,oa=origin-start;float length2=dot(ba,ba),bard=dot(ba,direction),baoa=dot(ba,oa);
    ImplicitHit hit={maximum,float3(0),10};
    // Cross products avoid subtracting almost equal large terms when looking
    // along a tracer from the gunner's camera.
    auto cd=cross(direction,ba),co=cross(oa,ba);float a=dot(cd,cd),b=dot(co,cd),c=dot(co,co)-radius*radius*length2;
    if(a>1e-10f){float disc=b*b-a*c;if(disc>=0){float root=sqrt(disc);for(int side=0;side<2;++side){float t=(-b+(side?root:-root))/a,y=baoa+t*bard;
        if(t>=.06f&&t<hit.distance&&y>=0&&y<=length2)hit={t,normalize(oa+direction*t-ba*(y/length2)),10};}}}
    for(int cap=0;cap<2;++cap){auto center=cap?end:start,q=origin-center;float b=dot(q,direction),disc=b*b-dot(q,q)+radius*radius;
        if(disc<0)continue;float root=sqrt(disc);for(int side=0;side<2;++side){float t=-b+(side?root:-root),y=dot(origin+direction*t-start,ba);
            if(t>=.06f&&t<hit.distance&&(length2<1e-10f||(cap?y>=length2:y<=0)))hit={t,normalize(origin+direction*t-center),10};}}
    return hit;
}
