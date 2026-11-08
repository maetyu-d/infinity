#include "infinity/Ellipsoid.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <stdexcept>

namespace infinity::ellipsoid {
static bool finite(Vec v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
static bool valid(const Ellipsoid& e) {
    double q=glm::dot(e.orientation,e.orientation);
    return finite(e.center)&&finite(e.radii)&&glm::all(glm::greaterThan(e.radii,Vec(1e-7)))&&std::isfinite(q)&&q>1e-12&&uint8_t(e.material)<16;
}
static uint32_t rgba(glm::dvec3 c) {
    c=glm::clamp(c,Vec(0),Vec(1))*255.+Vec(.5);
    return uint32_t(c.x)|(uint32_t(c.y)<<8)|(uint32_t(c.z)<<16)|0xff000000;
}
static glm::dvec3 unpack(uint32_t c) {return Vec(c&255,(c>>8)&255,(c>>16)&255)/255.;}
Ellipsoid volume(Vec c,Vec r,Material m,Rotation q) {return {c,q,r,m,-1};}
Ellipsoid segment(Vec a,Vec b,double radius,Material m,double flatten) {
    Vec delta=b-a;double length=glm::length(delta);
    if(length<1e-7) return volume(a,Vec(radius),m);
    return volume((a+b)*.5,{radius,length*.5+radius*.15,radius*flatten},m,glm::rotation(Vec(0,1,0),delta/length));
}
std::vector<Ellipsoid> resolve(const Model& model,Transform root) {
    if(!finite(root.center)||!std::isfinite(glm::dot(root.orientation,root.orientation))||glm::dot(root.orientation,root.orientation)<1e-12) throw std::invalid_argument("Invalid ellipsoid root");
    root.orientation=glm::normalize(root.orientation);
    std::vector<Transform> joints(model.joints.size());std::vector<uint8_t> visited(joints.size());
    std::function<void(size_t)> visit=[&](size_t i) {
        if(visited[i]==2) return;
        if(visited[i]==1) throw std::invalid_argument("Ellipsoid joint cycle");
        visited[i]=1;const auto& j=model.joints[i];Transform parent=root;
        if(j.parent>=0) {
            if(size_t(j.parent)>=joints.size()) throw std::invalid_argument("Ellipsoid joint parent out of bounds");
            visit(size_t(j.parent));parent=joints[j.parent];
        }
        if(!finite(j.offset)||!std::isfinite(glm::dot(j.rotation,j.rotation))||glm::dot(j.rotation,j.rotation)<1e-12)
            throw std::invalid_argument("Invalid ellipsoid joint transform");
        joints[i]={parent.center+parent.orientation*j.offset,glm::normalize(parent.orientation*j.rotation)};visited[i]=2;
    };
    for(size_t i=0;i<joints.size();++i) visit(i);
    std::vector<Ellipsoid> result;result.reserve(model.parts.size());
    for(auto e:model.parts) {
        if(!valid(e)) throw std::invalid_argument("Invalid ellipsoid primitive");
        Transform parent=root;
        if(e.parent>=0) {
            if(size_t(e.parent)>=joints.size()) throw std::invalid_argument("Ellipsoid primitive parent out of bounds");
            parent=joints[e.parent];
        }
        e.center=parent.center+parent.orientation*e.center;e.orientation=glm::normalize(parent.orientation*e.orientation);e.parent=-1;
        result.push_back(e);
    }
    return result;
}
std::optional<Hit> intersect(const Ellipsoid& e,Vec origin,Vec direction,double near) {
    if(!valid(e)||!finite(origin)||!finite(direction)||!std::isfinite(near)) return {};
    Rotation rotation=glm::normalize(e.orientation),inverse=glm::conjugate(rotation);
    Vec o=(inverse*(origin-e.center))/e.radii,d=(inverse*direction)/e.radii;
    double a=glm::dot(d,d),b=glm::dot(o,d),k=glm::dot(o,o)-1;
    if(a<1e-24) return {};
    double discriminant=b*b-a*k;
    double tolerance=1e-13*(b*b+std::abs(a*k)+1);
    if(discriminant < -tolerance) return {};
    double radical=std::sqrt(std::max(0.,discriminant));
    // Stable quadratic roots retain accuracy for small distant volumes.
    double q=-b-std::copysign(radical,b);
    double first,second;
    if(std::abs(q)>1e-24) {first=q/a;second=k/q;} else first=second=-b/a;
    if(first>second) std::swap(first,second);
    double t=first>=near?first:second;
    if(t<near||!std::isfinite(t)) return {};
    Vec unit=o+t*d,normal=glm::normalize(rotation*(unit/e.radii));
    return Hit{t,origin+direction*t,normal,unit};
}
Vec Camera::ray(double x,double y,int width,int height) const {
    double tangent=std::tan(verticalFov*.5);
    return orientation*Vec((2*x/width-1)*tangent*aspect,(1-2*y/height)*tangent,1);
}
Vec Camera::toView(Vec world) const {return glm::conjugate(orientation)*(world-eye);}
LookupTables::LookupTables() {
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        double u=2.*x/(size-1)-1,v=2.*y/(size-1)-1,r=u*u+v*v;size_t i=y*size+x;
        mask[i]=r<=1;depth[i]=r<=1?float(std::sqrt(1-r)):0;
    }
    relight(light);
}
void LookupTables::relight(Vec direction) {
    light=glm::normalize(direction);
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        size_t i=y*size+x;double u=2.*x/(size-1)-1,v=2.*y/(size-1)-1;
        Vec n(u,v,-double(depth[i]));
        shade[i]=mask[i]?uint8_t(std::clamp(int(std::round(std::max(0.,glm::dot(n,light))*15)),0,15)):0;
    }
}
float LookupTables::profile(double u,double v) const {
    double x=std::clamp((u*.5+.5)*(size-1),0.,double(size-1)),y=std::clamp((v*.5+.5)*(size-1),0.,double(size-1));
    int ix=int(x),iy=int(y),jx=std::min(ix+1,size-1),jy=std::min(iy+1,size-1);
    return float(std::lerp(std::lerp(double(depth[iy*size+ix]),double(depth[iy*size+jx]),x-ix),
                           std::lerp(double(depth[jy*size+ix]),double(depth[jy*size+jx]),x-ix),y-iy));
}
uint8_t LookupTables::lighting(Vec normal) const {
    if(normal.z>0) return uint8_t(std::clamp(int(std::round(std::max(0.,glm::dot(normal,light))*15)),0,15));
    int x=std::clamp(int(std::round((normal.x*.5+.5)*(size-1))),0,size-1);
    int y=std::clamp(int(std::round((normal.y*.5+.5)*(size-1))),0,size-1);
    if(!mask[y*size+x]) return uint8_t(std::clamp(int(std::round(std::max(0.,glm::dot(normal,light))*15)),0,15));
    return shade[y*size+x];
}
Rasterizer::Rasterizer(int width,int height) {
    const std::array<Vec,16> bases={Vec(.87,.66,.40),Vec(.55,.39,.23),Vec(.89,.52,.20),Vec(.27,.63,.57),
        Vec(.75,.31,.18),Vec(.77,.55,.37),Vec(.34,.25,.17),Vec(.19,.25,.30),Vec(.28,.40,.23),Vec(.58,.40,.29),
        Vec(.91,.84,.64),Vec(.25,.40,.47),Vec(.81,.68,.49),Vec(.43,.48,.47),Vec(.80,.22,.18),Vec(.98,.74,.31)};
    for(int m=0;m<16;++m) for(int s=0;s<16;++s) {
        double level=.25+.75*std::pow(double(s)/15,.85);
        shades[m*16+s]=rgba(bases[m]*level+Vec(.025,.017,.012));
    }
    // The sky's ramp includes the warm horizon; still a fixed 256-colour frame.
    for(int s=0;s<16;++s) shades[int(Material::Sky)*16+s]=rgba(glm::mix(Vec(.17,.30,.37),Vec(.81,.68,.49),double(s)/15));
    for(int r=0;r<32;++r) for(int g=0;g<32;++g) for(int b=0;b<32;++b) {
        Vec color=Vec(r,g,b)/31.;double best=1e9;int selected=0;
        for(int material:{int(Material::Sand),int(Material::DarkSand),int(Material::Haze)}) for(int s=0;s<16;++s) {
            int index=material*16+s;Vec difference=unpack(shades[index])-color;double error=glm::dot(difference,difference);
            if(error<best) {best=error;selected=index;}
        }
        colorLookup[(r<<10)|(g<<5)|b]=uint8_t(selected);
    }
    resize(width,height);
}
uint8_t Rasterizer::quantize(Vec color) const {
    auto c=glm::ivec3(glm::clamp(color,Vec(0),Vec(1))*31.+Vec(.5));
    return colorLookup[(c.x<<10)|(c.y<<5)|c.z];
}
void Rasterizer::resize(int width,int height) {
    if(width<16||height<16||width>1280||height>960) throw std::invalid_argument("Ellipsoid framebuffer dimensions out of bounds");
    w=width;h=height;size_t size=size_t(w)*h;colors.resize(size);indices.resize(size);depth.resize(size);owner.resize(size);rays.resize(size);
}
void Rasterizer::begin(const Camera& value,Vec direction) {
    camera=value;
    if(!finite(camera.eye)||!std::isfinite(camera.verticalFov)||camera.verticalFov<=.01||camera.verticalFov>=3.05||
       !std::isfinite(camera.aspect)||camera.aspect<=0||!std::isfinite(camera.near)||!std::isfinite(camera.far)||camera.near<=0||camera.far<=camera.near)
        throw std::invalid_argument("Invalid ellipsoid camera");
    double rotationLength=glm::dot(camera.orientation,camera.orientation);
    if(!std::isfinite(rotationLength)||rotationLength<1e-12||!finite(direction)||glm::length(direction)<1e-12)
        throw std::invalid_argument("Invalid ellipsoid camera rotation or light");
    camera.orientation=glm::normalize(camera.orientation);light=glm::normalize(glm::conjugate(camera.orientation)*glm::normalize(direction));
    double tangent=std::tan(camera.verticalFov*.5);
    projectionY=h/(2*tangent);projectionX=w/(2*tangent*camera.aspect);
    tables.relight(light);nextId=0;candidates=hits=0;
    std::fill(depth.begin(),depth.end(),std::numeric_limits<double>::infinity());std::fill(owner.begin(),owner.end(),-1);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        size_t i=y*w+x;rays[i]={(2*(x+.5)/w-1)*tangent*camera.aspect,(1-2*(y+.5)/h)*tangent,1};
        indices[i]=uint8_t(int(Material::Sky)*16+std::clamp(int(double(y)/h*23),0,15));
        colors[i]=shades[indices[i]];
    }
}
void Rasterizer::clearDepth() {std::fill(depth.begin(),depth.end(),std::numeric_limits<double>::infinity());std::fill(owner.begin(),owner.end(),-1);}
void Rasterizer::background(std::span<const uint8_t> pixels,std::span<const double> depths) {
    if(pixels.size()!=colors.size()||depths.size()!=depth.size()) throw std::invalid_argument("Background/depth mask dimensions differ");
    for(size_t i=0;i<colors.size();++i) {
        if(std::isnan(depths[i])||depths[i]<=0) throw std::invalid_argument("Background mask uses positive camera-space depth");
        indices[i]=pixels[i];colors[i]=shades[pixels[i]];depth[i]=depths[i];owner[i]=-1;
    }
}
void Rasterizer::draw(std::span<const Ellipsoid> volumes,Mode mode,Debug debug) {
    double fy=projectionY,fx=projectionX;
    for(const auto& world:volumes) {
        int id=nextId++;
        if(!valid(world)) continue;
        Ellipsoid e=world;e.center=camera.toView(world.center);e.orientation=glm::normalize(glm::conjugate(camera.orientation)*world.orientation);
        glm::dmat3 rotation=glm::mat3_cast(e.orientation),inverse=glm::transpose(rotation);
        Vec extent=glm::abs(rotation[0])*e.radii.x+glm::abs(rotation[1])*e.radii.y+glm::abs(rotation[2])*e.radii.z;
        if(e.center.z+extent.z<camera.near || e.center.z-extent.z>camera.far) continue;
        int left=0,right=w-1,top=0,bottom=h-1;
        if(e.center.z-extent.z>camera.near) {
            double loX=1e30,hiX=-1e30,loY=1e30,hiY=-1e30;
            for(int i=0;i<8;++i) {
                Vec p=e.center+extent*Vec(i&1?1:-1,i&2?1:-1,i&4?1:-1);
                double x=w*.5+fx*p.x/p.z,y=h*.5-fy*p.y/p.z;
                loX=std::min(loX,x);hiX=std::max(hiX,x);loY=std::min(loY,y);hiY=std::max(hiY,y);
            }
            if(hiX<0||loX>=w||hiY<0||loY>=h) continue;
            left=std::max(0,int(std::floor(std::max(-1.,loX))));right=std::min(w-1,int(std::ceil(std::min(double(w),hiX))));
            top=std::max(0,int(std::floor(std::max(-1.,loY))));bottom=std::min(h-1,int(std::ceil(std::min(double(h),hiY))));
        }
        Vec invR=Vec(1)/e.radii;
        glm::dmat3 transform=glm::dmat3(invR.x,0,0,0,invR.y,0,0,0,invR.z)*inverse;
        Vec o=transform*(-e.center);
        double k=glm::dot(o,o)-1;
        // Perspective conic of the quadratic discriminant. Its Cholesky map
        // carries a unit-sphere LUT into the oriented projected footprint.
        glm::dmat3 q=glm::transpose(transform)*transform;
        Vec qc=q*e.center;
        glm::dmat3 conic=glm::outerProduct(qc,qc)-k*q;
        double h00=-conic[0][0],h01=-conic[0][1],h11=-conic[1][1],det=h00*h11-h01*h01;
        glm::dvec2 center(0);double peak=0,l00=0,l10=0,l11=0;
        bool lut=mode==Mode::Lookup&&k>0&&h00>0&&h11>0&&det>1e-20&&e.center.z-extent.z>camera.near;
        if(lut) {
            glm::dvec2 f(conic[2][0],conic[2][1]);center={(h11*f.x-h01*f.y)/det,(h00*f.y-h01*f.x)/det};
            peak=conic[2][2]+glm::dot(f,center);
            lut=peak>0;
            if(lut) {l00=std::sqrt(h00/peak);l10=h01/(peak*l00);l11=std::sqrt(std::max(0.,h11/peak-l10*l10));}
        }
        double rootPeak=lut?std::sqrt(peak):0;
        // Columns are bounded and each winning pixel updates shared camera-Z.
        for(int x=left;x<=right;++x) for(int y=top;y<=bottom;++y) {
            ++candidates;size_t i=y*w+x;Vec ray=rays[i],d=transform*ray;
            double a=glm::dot(d,d),b=glm::dot(o,d),t=0;Vec unit;
            if(lut) {
                double du=ray.x-center.x,dv=ray.y-center.y,u=l00*du+l10*dv,v=l11*dv;
                if(u*u+v*v>1) continue;
                double profile=tables.profile(u,v);
                t=(-b-rootPeak*profile)/a;
                if(t<camera.near) t=(-b+rootPeak*profile)/a;
                unit=o+t*d;
            } else {
                double discriminant=b*b-a*k;if(discriminant<0) continue;
                double radical=std::sqrt(discriminant),first=(-b-radical)/a,second=(-b+radical)/a;
                t=first>=camera.near?first:second;unit=o+t*d;
            }
            if(t<camera.near||t>camera.far||t>=depth[i]||!std::isfinite(t)) continue;
            Vec normal=glm::normalize(rotation*(unit/e.radii));
            int shade=mode==Mode::Lookup?tables.lighting(normal):std::clamp(int(std::round(std::max(0.,glm::dot(normal,light))*15)),0,15);
            int material=int(world.material);
            if(debug==Debug::Parts) material=(id*7+3)%16;
            int paletteIndex=material*16+shade;
            if(debug==Debug::Parts && (std::abs(std::sin(std::atan2(unit.z,unit.x)*12))<.055 || std::abs(std::sin(std::asin(std::clamp(unit.y,-1.,1.))*12))<.04))
                paletteIndex=int(Material::Bone)*16+15;
            double fog=std::clamp((t-100)/800.,0.,.85);
            uint32_t color=shades[paletteIndex];
            if(fog>.04 && debug==Debug::None) {
                Vec target=glm::mix(unpack(color),Vec(.81,.68,.49),fog);
                // Quantise fog back into this material's palette ramp.
                double best=1e9;int selected=paletteIndex;
                for(int s=0;s<16;++s) {
                    int candidate=int(Material::Haze)*16+s;
                    double error=glm::dot(unpack(shades[candidate])-target,unpack(shades[candidate])-target);
                    if(error<best) {best=error;selected=candidate;}
                }
                if(fog>.5) paletteIndex=selected;
                color=shades[paletteIndex];
            }
            depth[i]=t;owner[i]=id;indices[i]=uint8_t(paletteIndex);colors[i]=color;++hits;
        }
    }
    if(debug==Debug::Parts) for(const auto& e:volumes) if(valid(e)) axes(e);
}
void Rasterizer::triangle(Vec a,Vec b,Vec c,glm::vec3 ca,glm::vec3 cb,glm::vec3 cc,bool fog) {
    struct Point {Vec p;glm::vec3 c;};
    std::array<Point,3> input={Point{camera.toView(a),ca},Point{camera.toView(b),cb},Point{camera.toView(c),cc}};
    std::array<Point,5> clipped;size_t count=0;
    for(size_t i=0;i<input.size();++i) {
        auto p=input[i],q=input[(i+1)%input.size()];bool pa=p.p.z>=camera.near,qa=q.p.z>=camera.near;
        if(pa) clipped[count++]=p;
        if(pa!=qa) {double t=(camera.near-p.p.z)/(q.p.z-p.p.z);clipped[count++]={glm::mix(p.p,q.p,t),glm::mix(p.c,q.c,float(t))};}
    }
    if(count<3) return;
    double fy=projectionY,fx=projectionX;
    for(size_t part=1;part+1<count;++part) {
        Point points[3]={clipped[0],clipped[part],clipped[part+1]};glm::dvec2 screen[3];
        for(int i=0;i<3;++i) screen[i]={w*.5+fx*points[i].p.x/points[i].p.z,h*.5-fy*points[i].p.y/points[i].p.z};
        auto edge=[](glm::dvec2 a,glm::dvec2 b,glm::dvec2 p) {return (p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);};
        double area=edge(screen[0],screen[1],screen[2]);if(std::abs(area)<.03) continue;
        double minX=std::min({screen[0].x,screen[1].x,screen[2].x}),maxX=std::max({screen[0].x,screen[1].x,screen[2].x});
        double minY=std::min({screen[0].y,screen[1].y,screen[2].y}),maxY=std::max({screen[0].y,screen[1].y,screen[2].y});
        if(maxX<0||minX>=w||maxY<0||minY>=h) continue;
        int left=int(std::max(0.,std::floor(minX))),right=int(std::min(double(w-1),std::ceil(maxX)));
        int top=int(std::max(0.,std::floor(minY))),bottom=int(std::min(double(h-1),std::ceil(maxY)));
        for(int y=top;y<=bottom;++y) for(int x=left;x<=right;++x) {
            glm::dvec2 p(x+.5,y+.5);
            double u=edge(screen[1],screen[2],p)/area,v=edge(screen[2],screen[0],p)/area,w0=1-u-v;
            if(u< -1e-8||v< -1e-8||w0< -1e-8) continue;
            u/=points[0].p.z;v/=points[1].p.z;w0/=points[2].p.z;double z=1/(u+v+w0);
            size_t i=y*w+x;if(z>=depth[i]||z>camera.far) continue;
            Vec color=(Vec(points[0].c)*u+Vec(points[1].c)*v+Vec(points[2].c)*w0)*z;
            if(fog) color=glm::mix(color,Vec(.81,.68,.49),std::clamp((z-80)/800.,0.,.9));
            uint8_t index=quantize(color);depth[i]=z;owner[i]=-1;indices[i]=index;colors[i]=shades[index];
        }
    }
}
void Rasterizer::line(Vec a,Vec b,uint32_t color) {
    a=camera.toView(a);b=camera.toView(b);if(a.z<camera.near||b.z<camera.near) return;
    double fy=h/(2*std::tan(camera.verticalFov*.5)),fx=w/(2*std::tan(camera.verticalFov*.5)*camera.aspect);
    glm::dvec2 p(w*.5+fx*a.x/a.z,h*.5-fy*a.y/a.z),q(w*.5+fx*b.x/b.z,h*.5-fy*b.y/b.z);
    double count=std::max(std::abs(p.x-q.x),std::abs(p.y-q.y));if(count>w*4+h*4) return;
    for(int i=0;i<=int(count);++i) {double t=count>0?i/count:0;auto c=glm::mix(p,q,t);int x=int(c.x),y=int(c.y);if(x>=0&&x<w&&y>=0&&y<h) colors[y*w+x]=color;}
}
void Rasterizer::axes(const Ellipsoid& e) {
    if(glm::length(camera.toView(e.center))>35) return;
    for(int i=0;i<3;++i) {Vec axis(0);axis[i]=e.radii[i]*1.25;line(e.center,e.center+e.orientation*axis,i==0?0xff3355ee:i==1?0xff55dd44:0xffee9955);}
}
void Rasterizer::finish(Debug debug) {
    if(debug==Debug::Depth) {
        for(size_t i=0;i<colors.size();++i) {
            int v=std::isfinite(depth[i])?std::clamp(int(255*(1-std::log1p(depth[i])/std::log1p(camera.far))),0,255):0;
            colors[i]=uint32_t(v)|(uint32_t(v)<<8)|(uint32_t(v)<<16)|0xff000000;
        }
    } else if(debug==Debug::Parts) {
        auto original=owner;
        for(int y=1;y<h-1;++y) for(int x=1;x<w-1;++x) {
            int i=y*w+x,id=original[i];if(id<0) continue;
            if(original[i-1]!=id||original[i+1]!=id||original[i-w]!=id||original[i+w]!=id) colors[i]=0xffd8eeff;
        }
    }
}
}
