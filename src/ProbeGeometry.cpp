#include "infinity/MiningProbe.hpp"
#include <algorithm>
namespace infinity {
std::shared_ptr<Geometry> boreGeometry(const Universe& u, const Borehole& b, bool chamber,
                                      const std::atomic<bool>* cancel) {
    auto g = std::make_shared<Geometry>();
    g->address = g->referenceFrame = b.planet; g->seed = b.id;
    g->viewKey = unsigned(chamber);
    const auto planet = u.describe(b.planet);
    g->descriptor = descriptorFingerprint(planet);
    const double shaft = chamber ? b.shaftRadius : 1.3;
    std::array<double,boreSectors> rim{};
    const auto basis=tangentFrame(glm::normalize(b.entry));
    const double planetRadius=std::pow(10.,planet.frame.logRadius);
    for(unsigned k=0;k<boreSectors;++k){const double a=k*6.283185307179586/boreSectors;
        const auto d=glm::normalize(b.entry+basis*glm::dvec3(shaft*std::cos(a),0,shaft*std::sin(a)));
        const auto surface=d*(planetRadius+u.terrainHeight(planet,d));
        rim[k]=(glm::inverse(basis)*(surface-b.entry)).y;
    }
    auto vertex = [&](glm::dvec3 p, bool hot = false) {
        auto color = cavernWallColour(b,p);
        if (hot) color = {.98f, .39f, .09f};
        g->solid.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color)});
    };
    auto connect = [&](unsigned previous, unsigned next) {
        for (unsigned k = 0; k < boreSectors; ++k) {
            const auto j = (k + 1) % boreSectors;
            g->solid.indices.insert(g->solid.indices.end(),
                {previous+k, next+k, previous+j, previous+j, next+k, next+j});
        }
    };
    constexpr unsigned rings=80;
    const double depth=b.finalDepth(),radius=b.roomRadius();
    g->detail=unsigned(std::llround(depth*1000000));
    const double end = chamber ? depth - std::sqrt(radius*radius - shaft*shaft) : depth;
    for (unsigned ring = 0; ring <= rings; ++ring) {
        if (cancel && cancel->load()) return {};
        const unsigned first = unsigned(g->solid.vertices.size());
        for (unsigned k = 0; k < boreSectors; ++k) {
            const double a = k * 6.283185307179586 / boreSectors;
            const double y=glm::mix(rim[k],-end,double(ring)/rings);
            vertex({shaft * std::cos(a), y, shaft * std::sin(a)});
        }
        if (ring) connect(first-boreSectors, first);
    }
    if (!chamber) {
        const auto centre = unsigned(g->solid.vertices.size()); vertex({0, -end, 0}, true);
        for (unsigned k = 0; k < boreSectors; ++k)
            g->solid.indices.insert(g->solid.indices.end(), {centre, centre-boreSectors+k,
                                      centre-boreSectors+(k+1)%boreSectors});
    } else {
        const double top = std::asin(shaft / radius);
        unsigned previous = rings*boreSectors;
        for (unsigned ring = 1; ring <= rings; ++ring) {
            if (cancel && cancel->load()) return {};
            const double theta = top + (3.141592653589793-top)*ring/rings;
            const unsigned first = unsigned(g->solid.vertices.size());
            for (unsigned k = 0; k < boreSectors; ++k) {
                const double a = k*6.283185307179586/boreSectors;
                auto direction = glm::dvec3(std::sin(theta)*std::cos(a), std::cos(theta), std::sin(theta)*std::sin(a));
                auto p = direction * boreRoomRadius(b, direction);
                p.y -= depth; vertex(p);
            }
            connect(previous, first); previous = first;
        }
        // Exposed bedding and fracture fill follow planet-frame planes. The
        // ribbons sit on the existing wall triangles, preserving its shape.
        const double topTheta=std::asin(shaft/radius);
        auto wallPoint=[&](glm::dvec3 direction) {
            direction=glm::normalize(direction);
            const double theta=std::acos(std::clamp(direction.y,-1.,1.));
            const double row=std::clamp((theta-topTheta)/(3.141592653589793-topTheta)*rings,0.,double(rings)-1e-6);
            double angle=std::atan2(direction.z,direction.x);if(angle<0)angle+=6.283185307179586;
            const double column=angle/6.283185307179586*boreSectors;
            const unsigned i=unsigned(row),j=unsigned(column)%boreSectors,k=(j+1)%boreSectors;
            auto at=[&](unsigned r,unsigned s){const auto& v=g->solid.vertices[(rings+r)*boreSectors+s];return glm::dvec3(v.x,v.y,v.z);};
            const double u=row-i,v=column-std::floor(column);
            const auto p=u+v<=1 ? at(i,j)*(1-u-v)+at(i+1,j)*u+at(i,k)*v
                : at(i+1,k)*(u+v-1)+at(i,k)*(1-u)+at(i+1,j)*(1-v);
            return p-direction*.001;
        };
        auto traceLayer=[&](glm::dvec3 normal,double offset,double width,glm::vec3 colour) {
            if(std::abs(offset)>radius*.92)return;
            normal=glm::normalize(normal);
            const auto basis=tangentFrame(normal);
            std::array<glm::dvec3,2> prior{};bool connected=false;
            for(unsigned k=0;k<=96;++k){const double a=k*6.283185307179586/96;
                std::array<glm::dvec3,2> edge{};bool allowed=true;
                for(unsigned side=0;side<2;++side){const double h=std::clamp((offset+(side?width:-width))/radius,-.99,.99);
                    const auto d=basis*glm::dvec3(std::sqrt(1-h*h)*std::cos(a),h,std::sqrt(1-h*h)*std::sin(a));
                    if(std::acos(std::clamp(d.y,-1.,1.))<topTheta+.08)allowed=false;
                    edge[side]=wallPoint(d);}
                if(allowed&&connected){const auto first=uint32_t(g->solid.vertices.size());
                    for(const auto p:{prior[0],prior[1],edge[0],edge[1]})g->solid.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(colour)});
                    g->solid.indices.insert(g->solid.indices.end(),{first,first+2,first+1,first+1,first+2,first+3});}
                connected=allowed;prior=edge;
            }
        };
        const auto inverse=glm::inverse(basis);
        const auto worldCentre=b.entry-basis*glm::dvec3(0,depth,0);
        const auto bedNormal=inverse*b.geology.bedding;
        const double phase=glm::dot(worldCentre,b.geology.bedding),spacing=3.141592653589793/1.1;
        const int first=int(std::floor(-radius/spacing)),last=int(std::ceil(radius/spacing));
        const double aligned=std::round(phase/spacing)*spacing-phase;
        for(int layer=first;layer<=last;++layer) {
            if(cancel&&cancel->load())return {};
            traceLayer(bedNormal,aligned+layer*spacing,.018+b.geology.sediment*.025,
                glm::mix(glm::vec3(.16f,.13f,.20f),glm::vec3(.54f,.42f,.30f),float(b.geology.sediment)));
        }
        const double fracturePhase=glm::dot(worldCentre,b.geology.fracture),fractureSpacing=3.141592653589793/.23;
        const double fractureOffset=std::round(fracturePhase/fractureSpacing)*fractureSpacing-fracturePhase;
        const auto vein=cavernFinds(b)[1];
        const auto veinColour=vein.mineral==Mineral::Iron?glm::vec3(.98f,.55f,.16f):
            glm::mix(material(vein.mineral).color,glm::vec3(.60f,.85f,.96f),.25f);
        for(int layer=-1;layer<=1;++layer)
            traceLayer(inverse*b.geology.fracture,fractureOffset+layer*fractureSpacing,
                .07+.08*b.geology.fractureStrength,veinColour);
        const auto crystals=cavernCrystals(b);
        for(size_t index=0;index<crystals.count;++index) {
            if(cancel&&cancel->load())return {};
            const auto mesh=cavernCrystalMesh(crystals.items[index]);
            const auto first=uint32_t(g->solid.vertices.size());
            g->solid.vertices.insert(g->solid.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());
            for(const auto i:mesh.indices)g->solid.indices.push_back(first+i);
        }
    }
    g->solid.compact(); return g;
}
namespace {
struct CutVertex { glm::dvec3 p; glm::vec3 n; glm::vec4 c; };
glm::vec4 unpack(uint32_t c) { return glm::vec4(c&255, (c>>8)&255, (c>>16)&255, c>>24)/255.f; }
CutVertex interpolate(const CutVertex& a, const CutVertex& b, double t) {
    return {glm::mix(a.p,b.p,t),glm::mix(a.n,b.n,float(t)),glm::mix(a.c,b.c,float(t))};
}
}
std::shared_ptr<Geometry> excavatedTerrain(const Geometry& base, std::span<const Borehole> holes,
                                          bool, const std::atomic<bool>* cancel) {
    if (base.precisePositions.size() != base.solid.vertices.size()) return {};
    auto g = std::make_shared<Geometry>(base);
    const double metresPerUnit = std::pow(10.,base.logUnits);
    g->solid = {}; g->precisePositions.clear(); g->terrainNormals.clear();
    g->terrainAlbedo.clear(); g->exposureColors.clear();
    auto emit = [&](const std::vector<CutVertex>& poly) {
        if (poly.size() < 3) return;
        const auto start = unsigned(g->solid.vertices.size());
        for (const auto& v : poly) {
            auto c = rgba(glm::vec3(v.c), v.c.a);
            g->solid.vertices.push_back({float(v.p.x),float(v.p.y),float(v.p.z),c});
            g->precisePositions.push_back(v.p); g->terrainNormals.push_back(glm::normalize(v.n));
            g->terrainAlbedo.push_back(c); g->exposureColors.push_back(c);
        }
        for (unsigned k = 1; k+1 < poly.size(); ++k)
            g->solid.indices.insert(g->solid.indices.end(), {start,start+k,start+k+1});
    };
    for (size_t t = 0; t < base.solid.indexCount(); t+=3) {
        if (cancel && cancel->load()) return {};
        std::vector<std::vector<CutVertex>> pieces(1);
        for (unsigned k = 0; k < 3; ++k) {
            const auto i = base.solid.index(t+k);
            const auto c = base.exposureColors.size()==base.solid.vertices.size() ? base.exposureColors[i] : base.solid.vertices[i].color;
            pieces[0].push_back({base.precisePositions[i], base.terrainNormals.empty()
                                  ? glm::vec3(glm::normalize(base.precisePositions[i])) : base.terrainNormals[i], unpack(c)});
        }
        for (const auto& h : holes) {
            const auto middle = (base.precisePositions[base.solid.index(t)] +
                                 base.precisePositions[base.solid.index(t+1)] +
                                 base.precisePositions[base.solid.index(t+2)]) / 3.;
            if (glm::dot(middle, h.entry) <= 0) continue;
            const auto basis = tangentFrame(glm::normalize(h.entry));
            const auto entry = h.entry / metresPerUnit;
            const double apothem = h.shaftRadius / metresPerUnit * std::cos(3.141592653589793/boreSectors);
            std::vector<std::vector<CutVertex>> next;
            for (auto poly : pieces) {
                for (unsigned side = 0; side < boreSectors && !poly.empty(); ++side) {
                    const double a = (side+.5)*6.283185307179586/boreSectors;
                    const auto n = basis * glm::dvec3(std::cos(a),0,std::sin(a));
                    auto distance = [&](const CutVertex& v) { return glm::dot(v.p-entry,n)-apothem; };
                    std::vector<CutVertex> outside, inside;
                    auto last = poly.back(); auto ld = distance(last);
                    for (const auto& v : poly) {
                        const auto d = distance(v);
                        if ((d >= 0) != (ld >= 0)) {
                            auto cut = interpolate(last,v,ld/(ld-d)); outside.push_back(cut); inside.push_back(cut);
                        }
                        (d >= 0 ? outside : inside).push_back(v);
                        last = v; ld = d;
                    }
                    if (outside.size() >= 3) next.push_back(std::move(outside));
                    poly = std::move(inside);
                }
            }
            pieces = std::move(next);
        }
        for (const auto& poly : pieces) emit(poly);
    }
    g->solid.compact(); return g;
}
} // namespace infinity
