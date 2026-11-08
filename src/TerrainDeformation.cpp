#include "infinity/Fractal.hpp"
#include <algorithm>
#include <cmath>
namespace infinity::fractal {
namespace {
int tile(int x){return int(std::floor(double(x)/Deformation::tileSize));}
// Exact integral of the quartic elliptical footprint along the swept segment.
// Integrating over its clipped support prevents long steps from depositing a
// full-strength footprint at every point along the segment.
double footprint(glm::dvec2 delta,glm::dvec2 movement,glm::dvec2 right,glm::dvec2 forward,double width) {
    glm::dvec2 p(glm::dot(delta,right)/width,glm::dot(delta,forward)/.18);
    glm::dvec2 v(-glm::dot(movement,right)/width,-glm::dot(movement,forward)/.18);
    double a=glm::dot(v,v),b=2*glm::dot(p,v),c=glm::dot(p,p);
    if(a<1e-12)return std::pow(std::max(0.,1-c),2);
    double centre=-b/(2*a),peak=1-c+b*b/(4*a);if(peak<=0)return 0;
    double radius=std::sqrt(peak/a),low=std::max(0.,centre-radius),high=std::min(1.,centre+radius);if(high<=low)return 0;
    double mid=(low+high)*.5,half=(high-low)*.5;
    double w=1-(a*mid*mid+b*mid+c),slope=2*a*mid+b;
    return std::clamp(2*half*(w*w+(slope*slope-2*w*a)*half*half/3+a*a*std::pow(half,4)/5),0.,1.);
}
}
RutSample fr_cpu_rut(const Deformation* field,float2 p){return field?field->sample(p):RutSample{0,float2(0),0};}
RutSample Deformation::sample(glm::vec2 p) const {
    RutSample result{0,glm::vec2(0),0};if(tiles.empty())return result;
    glm::vec2 grid=p/fr_rut_cell(),base=glm::floor(grid),f=grid-base;
    int ix=int(base.x)-1,iz=int(base.y)-1,tx=tile(ix),tz=tile(iz),lastX=tile(ix+3),lastZ=tile(iz+3);
    const Tile* near[2][2]{};bool any=false;
    for(int z=0;z<=lastZ-tz;++z)for(int x=0;x<=lastX-tx;++x){auto it=tiles.find(key(tx+x,tz+z));if(it!=tiles.end()){near[z][x]=&it->second;any=true;}}
    if(!any)return result;
    auto wx=fr_rut_weights(f.x),wz=fr_rut_weights(f.y),dx=fr_rut_derivatives(f.x),dz=fr_rut_derivatives(f.y);
    for(int z=0;z<4;++z)for(int x=0;x<4;++x){int gx=ix+x,gz=iz+z,bx=tile(gx),bz=tile(gz);auto* data=near[bz-tz][bx-tx];if(data)result=fr_rut_accumulate(result,data->nodes[(gz-bz*tileSize)*tileSize+gx-bx*tileSize],wx[x],wz[z],dx[x],dz[z]);}
    return result;
}
void Deformation::stamp(glm::dvec2 from,glm::dvec2 to,glm::dvec2 forward,double width,double load,double rolling,double shearing) {
    if(load<100||!std::isfinite(load+width+rolling+shearing+from.x+from.y+to.x+to.y+forward.x+forward.y)||rolling<0||shearing<0||rolling+shearing<1e-7||glm::length(to-from)>2.)return;
    // Calibrated at a 10 cm rolling segment: twice the old physical depth,
    // with no additional digging caused by how that distance is subdivided.
    double pressure=std::clamp(load/2400.,.05,2.5),cut=(rolling*.24+shearing*.54)*pressure,mark=(rolling+shearing*.7)*4.*pressure;
    double halfWidth=std::clamp(width*.5,.12,.28),reach=std::max(halfWidth,.18);
    if(glm::length(forward)<.5)forward={0,1};else forward=glm::normalize(forward);
    glm::dvec2 right(forward.y,-forward.x),movement=to-from;
    int minX=int(std::floor((std::min(from.x,to.x)-reach)/fr_rut_cell())),maxX=int(std::ceil((std::max(from.x,to.x)+reach)/fr_rut_cell()));
    int minZ=int(std::floor((std::min(from.y,to.y)-reach)/fr_rut_cell())),maxZ=int(std::ceil((std::max(from.y,to.y)+reach)/fr_rut_cell()));
    uint64_t change=serial+1;bool changed=false;
    for(int z=minZ;z<=maxZ;++z)for(int x=minX;x<=maxX;++x) {
        glm::dvec2 point=glm::dvec2(x,z)*double(fr_rut_cell());
        double weight=footprint(point-from,movement,right,forward,halfWidth);if(weight<1e-9)continue;
        int tx=tile(x),tz=tile(z);auto& data=tiles[key(tx,tz)];auto& node=data.nodes[(z-tz*tileSize)*tileSize+x-tx*tileSize];
        auto next=node;
        next.x=std::min(fr_rut_limit(),node.x+float((fr_rut_limit()-node.x)*-std::expm1(-cut*weight/fr_rut_limit())));
        next.y=std::min(1.f,node.y+float((1-node.y)*-std::expm1(-mark*weight)));
        if(next==node)continue;
        node=next;data.revision=change;changed=true;
    }
    if(changed)serial=change;
}
void Deformation::clear(){tiles.clear();++serial;}
uint64_t Deformation::tileRevision(int x,int z) const {auto it=tiles.find(key(x,z));return it==tiles.end()?0:it->second.revision;}
void Deformation::copyTile(int x,int z,std::span<glm::vec2> out) const {
    auto it=tiles.find(key(x,z));if(it==tiles.end())std::fill(out.begin(),out.end(),glm::vec2(0));else std::copy(it->second.nodes.begin(),it->second.nodes.end(),out.begin());
}
}
