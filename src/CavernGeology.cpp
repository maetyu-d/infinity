#include "infinity/MiningProbe.hpp"
#include "infinity/Prospecting.hpp"
#include <algorithm>
#include <numbers>
#include <numeric>

namespace infinity {
namespace {
constexpr double tau = 2 * std::numbers::pi;
unsigned resource(Mineral m) {
    return m == Mineral::Ice ? 0 : m == Mineral::Iron ? 1 : m == Mineral::Graphite ? 3 : 2;
}
glm::vec3 pigment(Mineral m) {
    switch (m) {
    case Mineral::Iron: return {.92f,.46f,.13f};
    case Mineral::Ice: return {.22f,.78f,.96f};
    case Mineral::Graphite: return {.09f,.13f,.20f};
    case Mineral::Olivine: return {.31f,.71f,.36f};
    default: return {.71f,.44f,.88f};
    }
}
uint64_t locationSeed(const Borehole& b) {
    return mix64(b.geology.seed ^ mix64(uint64_t(int64_t(std::floor(b.entry.x / 64.)))) ^
                 mix64(uint64_t(int64_t(std::floor(b.entry.y / 64.)))) ^
                 mix64(uint64_t(int64_t(std::floor(b.entry.z / 64.)))));
}
glm::dvec3 crystalAxes(const CavernCrystal& c) {
    const double extent=c.mineral==Mineral::Iron?.72:c.mineral==Mineral::Olivine?.68:
        c.mineral==Mineral::Quartz?.95:.65;
    return {c.radius/.7,c.radius/.7,c.height/extent};
}
std::vector<glm::dvec4> habitPlanes(Mineral mineral) {
    // The same half-spaces as Processes.cpp::crystalDensity, including
    // olivine's orthorhombic bevels and iron's truncated cubic habit.
    std::vector<glm::dvec4> planes;
    if(mineral==Mineral::Iron||mineral==Mineral::Olivine) {
        const auto limits=mineral==Mineral::Iron?glm::dvec3(.72):glm::dvec3(.68,.85,.68);
        for(unsigned axis=0;axis<3;++axis)for(double sign:{-1.,1.}){glm::dvec4 p{};p[axis]=sign;p.w=limits[axis];planes.push_back(p);}
        for(double x:{-1.,1.})for(double z:{-1.,1.}) {
            if(mineral==Mineral::Olivine)planes.push_back({x,0,z,.82*std::sqrt(2.)});
            else for(double y:{-1.,1.})planes.push_back({x,y,z,.95*std::sqrt(3.)});
        }
    } else {
        for(unsigned k=0;k<6;++k){const double a=k*tau/6;
            planes.push_back({std::cos(a),std::sin(a),0,.7});
            if(mineral==Mineral::Quartz)for(double sign:{-1.,1.})planes.push_back({std::cos(a),std::sin(a),sign*.65,1.025});
        }
        const double z=mineral==Mineral::Quartz?.95:.65;
        planes.push_back({0,0,1,z});planes.push_back({0,0,-1,z});
    }
    return planes;
}
}
CavernRecipe cavernRecipe(const Universe& u, const Borehole& b) {
    CavernRecipe r;
    const auto p = u.describe(b.planet);
    if (!hasSolidSurface(p)) return r;
    const auto up = glm::normalize(b.entry);
    const auto frame = tangentFrame(up);
    const auto g = geologySample(*p.geology, up);
    const auto e = resourceExposure(p, up, g);
    r.seed = p.planetSeed;
    r.fractions = e.fractions;
    r.temperature = p.environment.temperature;
    r.cooling = p.environment.coolingRate;
    r.sediment = std::clamp(g.sediment + g.erosion * 400, 0., 1.);
    r.fractureStrength = std::clamp(g.convergence + g.shear + g.divergence + g.volcanism, 0., 1.);
    r.bedding = glm::normalize(up * (1.6 + 2 * r.sediment) + frame * glm::dvec3(g.convergence,0,g.shear));
    r.fracture = glm::normalize(frame * glm::dvec3(.8 + g.shear, .25 * g.volcanism, .4 + g.divergence));
    r.substrate = p.environment.mineral;
    return r;
}
std::array<CavernFind,cavernFindCount> cavernFinds(const Borehole& b) {
    std::array<CavernFind,cavernFindCount> out{};
    const Random random{locationSeed(b)};
    const auto& f = b.geology.fractions;
    const bool icy = b.geology.temperature < 250 && f[0] > .08;
    const bool carbon = f[3] > .10 && b.geology.temperature < 650;
    const Mineral silicate = b.geology.substrate == Mineral::Olivine ? Mineral::Olivine : Mineral::Quartz;
    for (unsigned i = 0; i < out.size(); ++i) {
        auto& d = out[i]; d.index = i; d.feature = CavernFeature(i);
        d.mineral = i == 0 ? (icy && f[0] > f[2] ? Mineral::Ice : silicate)
                    : i == 1 ? (f[1] > .015 ? Mineral::Iron : carbon ? Mineral::Graphite : silicate)
                             : icy ? Mineral::Ice : carbon && f[3] > f[2] ? Mineral::Graphite : silicate;
        d.resource = resource(d.mineral);
        // A missing constituent cannot become a deposit through visual variety.
        if (f[d.resource] <= 0) {
            d.resource = unsigned(std::max_element(f.begin(),f.end())-f.begin());
            d.mineral = d.resource == 0 ? Mineral::Ice : d.resource == 1 ? Mineral::Iron
                         : d.resource == 3 ? Mineral::Graphite : silicate;
        }
        const double angle = i * tau / 3 + random.signedUnit(i * 3) * .25;
        const double theta = i == 2 ? 2.45 : 1.10 + i * .65;
        const auto dir = glm::dvec3(std::sin(theta)*std::cos(angle),std::cos(theta),std::sin(theta)*std::sin(angle));
        d.local = dir * boreRoomRadius(b,dir) - glm::dvec3(0,b.finalDepth(),0);
        d.inward = -dir;
        d.radius = i == 2 ? 2.4 : 3.4;
        d.concentration = std::clamp(f[d.resource] * (1.2 + b.geology.fractureStrength), .05, 1.);
        d.id = mix64(locationSeed(b) ^ mix64(i + 0x43415645524eULL));
    }
    return out;
}
glm::dvec3 cavernFindPoint(const Borehole& b, const CavernFind& f) {
    return b.entry + tangentFrame(glm::normalize(b.entry)) * (f.local + f.inward * 1.6);
}
CavernCrystals cavernCrystals(const Borehole& b) {
    CavernCrystals result;
    if (!b.geologyVersion) return result;
    const auto f = cavernFinds(b)[2];
    const auto frame = tangentFrame(f.inward);
    const Random random{f.id};
    result.count = result.items.size();
    for (size_t i = 0; i < result.count; ++i) {
        auto& c = result.items[i];
        const double angle = i * 2.399963229728653, radius = i ? .45 + .16 * i : 0;
        c.centre = f.local + frame * glm::dvec3(std::cos(angle)*radius,0,std::sin(angle)*radius);
        c.orientation = glm::normalize(frame * glm::angleAxis(random.signedUnit(i)*.22,glm::dvec3(1,0,0)));
        c.radius = .18 + .15 * random.unit(i+20);
        c.height = f.mineral == Mineral::Graphite ? .12 : .55 + .9 * random.unit(i+40) * (1.2-b.geology.cooling*.4);
        c.mineral = f.mineral;
    }
    return result;
}
double cavernCrystalClearance(const CavernCrystal& c, glm::dvec3 local) {
    const auto q = glm::inverse(c.orientation) * (local-c.centre);
    const double bound=glm::length(glm::dvec3(c.radius*1.4,c.radius*1.4,c.height));
    if(glm::length(q)>bound+4)return glm::length(q)-bound;
    static const auto habits=[] {
        std::array<std::vector<glm::dvec4>,5> values;
        for(unsigned i=0;i<values.size();++i)values[i]=habitPlanes(Mineral(i));
        return values;
    }();
    const auto axes=crystalAxes(c);
    const glm::dvec3 p{q.x,q.z,q.y};
    double clearance=-1e30;
    for(const auto plane:habits[size_t(c.mineral)]) {
        const auto normal=glm::dvec3(plane)/axes;
        clearance=std::max(clearance,(glm::dot(normal,p)-plane.w)/glm::length(normal));
    }
    return clearance;
}
Mesh cavernCrystalMesh(const CavernCrystal& c) {
    Mesh mesh;const auto planes=habitPlanes(c.mineral);std::vector<glm::dvec3> points;
    for(size_t a=0;a<planes.size();++a)for(size_t b=a+1;b<planes.size();++b)for(size_t d=b+1;d<planes.size();++d) {
        const auto x=glm::dvec3(planes[a]),y=glm::dvec3(planes[b]),z=glm::dvec3(planes[d]);
        const double determinant=glm::dot(x,glm::cross(y,z));if(std::abs(determinant)<1e-10)continue;
        const auto p=(planes[a].w*glm::cross(y,z)+planes[b].w*glm::cross(z,x)+planes[d].w*glm::cross(x,y))/determinant;
        if(std::any_of(planes.begin(),planes.end(),[&](auto n){return glm::dot(glm::dvec3(n),p)>n.w+1e-8;}))continue;
        if(std::none_of(points.begin(),points.end(),[&](auto q){return glm::length(q-p)<1e-7;}))points.push_back(p);
    }
    const auto axes=crystalAxes(c);
    const auto base=glm::mix(material(c.mineral).color,glm::vec3(.65f,.82f,.98f),.30f);
    for(size_t plane=0;plane<planes.size();++plane){const auto n=glm::dvec3(planes[plane]);std::vector<glm::dvec3> face;
        for(const auto p:points)if(std::abs(glm::dot(n,p)-planes[plane].w)<1e-7)face.push_back(p);
        if(face.size()<3)continue;
        auto centre=std::accumulate(face.begin(),face.end(),glm::dvec3(0))/double(face.size());
        const auto u=glm::normalize(glm::cross(n,std::abs(n.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0))),v=glm::normalize(glm::cross(n,u));
        std::sort(face.begin(),face.end(),[&](auto a,auto b){a-=centre;b-=centre;return std::atan2(glm::dot(a,v),glm::dot(a,u))<std::atan2(glm::dot(b,v),glm::dot(b,u));});
        const auto first=uint32_t(mesh.vertices.size());
        for(auto p:face){p*=axes;p=c.centre+c.orientation*glm::dvec3(p.x,p.z,p.y);
            mesh.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(base*float(.62+.12*(plane%4)))});}
        for(unsigned i=1;i+1<face.size();++i)mesh.indices.insert(mesh.indices.end(),{first,first+i,first+i+1});
    }
    return mesh;
}
glm::vec3 cavernWallColour(const Borehole& b, glm::dvec3 local) {
    const auto& g = b.geology;
    const auto world = b.entry + tangentFrame(glm::normalize(b.entry)) * local;
    const double fold = fractalNoise(world * .045,g.seed,2);
    const double bedding = std::sin(glm::dot(world,g.bedding)*1.1 + fold * (1+g.fractureStrength*3));
    const double grain = .5+.5*fractalNoise(world*.38,g.seed^0x475241494eULL,2);
    auto base = pigment(g.substrate) * float(.34 + .17*grain);
    base = glm::mix(base,glm::vec3(.61f,.51f,.37f),float((.18+g.sediment*.35)*(.5+.5*bedding)));
    const double vein = 1-smoothstep(.06,.24,std::abs(std::sin(glm::dot(world,g.fracture)*.23+fold*.8)));
    const auto veinMineral = g.fractions[1]>.015 ? Mineral::Iron : g.fractions[3]>.1 ? Mineral::Graphite : Mineral::Quartz;
    base = glm::mix(base,pigment(veinMineral),float(vein*(.5+g.fractureStrength*.4)));
    if (b.geologyVersion)
        for (const auto& f:cavernFinds(b)) {
            const double coverage=1-smoothstep(f.radius*.55,f.radius,glm::length(local-f.local));
            base=glm::mix(base,pigment(f.mineral)*float(f.feature==CavernFeature::Crystals?.55:.85),float(coverage*.9));
        }
    return base;
}
std::string cavernFindName(const CavernFind& f) {
    return std::string(material(f.mineral).name) +
           (f.feature==CavernFeature::Crystals ? " CRYSTAL POCKET" : f.feature==CavernFeature::Vein ? " MINERAL SEAM" : " EXPOSED STRATA");
}
} // namespace infinity
