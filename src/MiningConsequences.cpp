#include "infinity/MiningConsequences.hpp"
#include <algorithm>
#include <numbers>
namespace infinity {
namespace {
double unit(uint64_t id,unsigned n){return double(mix64(id+uint64_t(n)*0x9e3779b97f4a7c15ULL)>>11)*0x1p-53;}
glm::vec3 cutColour(Mineral m){switch(m){case Mineral::Iron:return {.64f,.45f,.31f};case Mineral::Ice:return {.49f,.76f,.87f};
    case Mineral::Olivine:return {.24f,.34f,.21f};case Mineral::Graphite:return {.28f,.31f,.36f};default:return {.52f,.48f,.59f};}}
}
CavernWear cavernWear(const Universe&,const Borehole& b,const ExplorationState& s){
    CavernWear wear;const auto pool=b.planet.child(Kind::Surface,encodeTile(tileAt(glm::normalize(b.entry),7)));
    ResourceStock capacity{1,1,1,1},extracted{};
    for(const auto& p:s.prospects)if(p.address==pool){capacity=p.capacity;extracted=p.extracted;break;}
    const auto faces=cavernFinds(b);
    for(unsigned i=0;i<faces.size();++i){auto& f=wear.faces[i];f.face=faces[i];const auto r=f.face.resource;
        f.remaining=capacity[r]>0?std::clamp(double(capacity[r]-extracted[r])/capacity[r],0.,1.):0;
        f.worked=double(b.finds[i].minedKg);wear.minedKg+=b.finds[i].minedKg;
        wear.key=mix64(wear.key^mix64(uint64_t(capacity[r]))^mix64(uint64_t(extracted[r]))^mix64(b.finds[i].minedKg+i));
    }
    return wear;
}
glm::vec3 CavernWear::shade(glm::dvec3 p,glm::vec3 original) const {
    auto colour=original;
    for(const auto& w:faces){const auto& f=w.face;const auto delta=p-f.local;const double plane=glm::dot(delta,f.inward);
        if(std::abs(plane)>2.2)continue;
        const double distance=glm::length(delta-f.inward*plane),radius=f.radius*1.55;
        const double patch=1-smoothstep(radius*.65,radius,distance);if(patch<=0)continue;
        const double grain=.75+.25*std::sin(glm::dot(p,glm::dvec3(7.1,3.7,5.3)));
        const double coverage=(1-smoothstep(radius*std::sqrt(w.remaining)*.78,radius*std::sqrt(w.remaining)+.001,distance))*w.remaining;
        const auto ore=glm::mix(material(f.mineral).color,glm::vec3(.8f,.85f,.94f),.2f)*float(.75+.25*grain);
        // Only the ore-bearing coating recedes. Host facets and collision stay intact.
        const double scar=(1-std::exp(-w.worked/35.))*patch;
        const double grooves=.65+.35*std::abs(std::sin(glm::dot(p,glm::dvec3(22,11,3))));
        auto exposed=glm::mix(original,cutColour(f.mineral)*float(grooves),float(std::max(1-w.remaining,scar)*.85));
        colour=glm::mix(colour,glm::mix(exposed,ore,float(coverage*(1-scar*.75))),float(patch));
    }
    return colour;
}
Mesh cavernTailings(const Borehole& b,const CavernWear& wear){
    Mesh mesh;
    for(const auto& worked:wear.faces)for(unsigned i=0;i<unsigned(std::min(12.,std::floor(worked.worked/3.)));++i){const auto& f=worked.face;const auto id=mix64(b.id^f.id);
        const double a=unit(id,i*4)*2*std::numbers::pi,spread=.08+unit(id,i*4+1)*.30;
        const auto direction=glm::normalize(glm::dvec3(std::cos(a)*spread,-1,std::sin(a)*spread));
        const auto p=direction*boreRoomRadius(b,direction)-glm::dvec3(0,b.finalDepth(),0)-direction*.06;
        const auto basis=tangentFrame(-direction);const double size=.12+unit(id,i*4+2)*.16;
        const auto habit=f.mineral==Mineral::Graphite||f.mineral==Mineral::Ice?glm::dvec3(1,.3,1):glm::dvec3(1);
        const auto first=uint32_t(mesh.vertices.size());
        for(const auto corner:{glm::dvec3(1,0,0),glm::dvec3(-.6,0,.8),glm::dvec3(-.5,0,-.7),glm::dvec3(0,.65,0)}){
            const auto point=p+basis*(corner*habit)*size;mesh.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(cutColour(f.mineral)*.8f)});}
        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+3,first+1,first+2,first+3,first+2,first,first+3,first,first+2,first+1});
    }
    return mesh;
}
ProbeMotes miningImpactMotes(glm::dvec3 origin,glm::dvec3 inward,glm::dvec3 down,uint64_t id,Mineral mineral,double seconds){
    ProbeMotes out;const bool icy=mineral==Mineral::Ice,metal=mineral==Mineral::Iron;
    const auto basis=tangentFrame(inward);
    for(unsigned i=0;i<64;++i){auto& m=out.items[out.count++];const double life=.4+unit(id,i)*.65;
        const double age=std::fmod(std::max(0.,seconds)+unit(id,i+100)*life,life);
        const double a=unit(id,i+200)*2*std::numbers::pi,speed=.4+unit(id,i+300)*1.8;
        const auto velocity=basis*glm::dvec3(std::cos(a)*speed,1.2+unit(id,i+400),std::sin(a)*speed);
        m.position=origin+velocity*age+down*(.8*age*age);
        m.spark=metal&&i%5==0;m.fragment=!m.spark&&i%3==0;
        m.stretch=mineral==Mineral::Graphite?glm::dvec3(1,.15,1):icy?glm::dvec3(1.3,.25,1):glm::dvec3(1);
        m.colour=m.spark?glm::vec3(1.5f,.67f,.15f):glm::mix(cutColour(mineral),material(mineral).color,.4f);
        m.radius=m.fragment?.035+unit(id,i+500)*.06:icy?.10:.055;
        m.opacity=float((1-age/life)*(icy?.33:.65));
    }
    return out;
}
ProbeMotes cavernMiningMotes(const Borehole& b,const CavernFind& f,double seconds){
    auto out=miningImpactMotes(f.local+f.inward*.12,f.inward,{0,-1,0},f.id,f.mineral,seconds);
    for(size_t i=0;i<out.count;++i){auto& m=out.items[i];const auto centre=m.position+glm::dvec3(0,b.finalDepth(),0);
        const auto dir=glm::normalize(centre);const double radius=boreRoomRadius(b,dir);
        if(glm::length(centre)>radius-.08)m.position=dir*(radius-.08)-glm::dvec3(0,b.finalDepth(),0);}
    return out;
}
glm::vec3 prospectWearTint(const Universe& u,const Node& node,const ExplorationState& state){
    if(state.prospects.empty()||node.address.kind()<Kind::Rock||node.address.kind()>Kind::Grain)return {1,1,1};
    const auto pool=prospectAddress(u,node.address);if(!pool)return {1,1,1};
    for(const auto& p:state.prospects)if(p.address==*pool){
        const auto mineral=node.environment.mineral;unsigned resource=mineral==Mineral::Ice?0:mineral==Mineral::Iron?1:mineral==Mineral::Graphite?3:2;
        for(const auto& seam:state.seams)if(seam.pool==p.address&&seam.marked&&
            (seam.source.ancestorOf(node.address)||node.address.ancestorOf(seam.source))){resource=seam.resource;break;}
        const double wear=p.capacity[resource]>0?std::clamp(double(p.extracted[resource])/p.capacity[resource],0.,1.):1.;
        return glm::mix(glm::vec3(1),glm::vec3(.43f,.46f,.48f),float(wear));}
    return {1,1,1};
}
} // namespace infinity
