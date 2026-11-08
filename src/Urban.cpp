#define GLM_FORCE_INLINE
#include "infinity/Urban.hpp"
#include "infinity/Prospecting.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <stdexcept>

namespace infinity {
namespace {
constexpr double pi=std::numbers::pi;
bool stopped(const std::atomic<bool>* c){return c&&c->load();}
uint64_t gridKey(int x,int z){return uint64_t(uint16_t(x))|(uint64_t(uint16_t(z))<<16);}
glm::dvec2 plotLocal(const UrbanRegion& region,const UrbanBlock& block,unsigned slot){
    const double step=region.cellWidth/urbanPlotsAcross;
    return {block.x*region.cellWidth+(double(slot%urbanPlotsAcross)+.5)*step,
            block.z*region.cellWidth+(double(slot/urbanPlotsAcross)+.5)*step};
}
struct Shape {SurfaceMesh kind;glm::dvec3 size;unsigned people=0,jobs=0;glm::vec3 tint;};
Shape shape(const SurfaceSettlement& city,const UrbanRegion& region,const UrbanBlock& block,unsigned slot){
    const Random r{mix64(urbanBlockId(region,block)^mix64(slot+0x706c6f74))};
    const double pitch=region.cellWidth/urbanPlotsAcross;
    const double frontage=pitch*(.46+r.unit(1)*.21),depth=pitch*(.48+r.unit(2)*.20);
    const bool dense=block.use==BuildingUse::Commerce;
    const unsigned floors=2+unsigned((4+block.density*(dense?85:22))*(.45+r.unit(3)*.55));
    Shape out{SurfaceMesh::Courtyard,{frontage,3.2*floors+2,depth},0,0,{1,1,1}};
    switch(block.use){
    case BuildingUse::Industry:out.kind=SurfaceMesh::Warehouse;out.size.y=12+r.unit(4)*18;break;
    case BuildingUse::Agriculture:out.kind=SurfaceMesh::Greenhouse;out.size.y=8+r.unit(4)*8;break;
    case BuildingUse::Utilities:out.kind=SurfaceMesh::Utility;out.size.y=14+r.unit(4)*20;break;
    case BuildingUse::Civic:out.kind=SurfaceMesh::Hall;out.size.y=22+r.unit(4)*28;break;
    case BuildingUse::Park:out.kind=city.sealed?SurfaceMesh::Greenhouse:SurfaceMesh::Tree;out.size=city.sealed?glm::dvec3(frontage,12,depth):glm::dvec3(7+r.unit(1)*9,9+r.unit(4)*16,7+r.unit(2)*9);break;
    default:
        out.kind=city.sealed?SurfaceMesh::HabitatCluster:floors>15?SurfaceMesh::SteppedTower:floors>5?SurfaceMesh::Courtyard:SurfaceMesh::RowHousing;
        if(dense&&block.density>.65&&slot==14){out.kind=SurfaceMesh::SteppedTower;out.size.y*=1.75;}
        out.people=unsigned(frontage*depth*std::max(1.,std::floor(out.size.y/3.2))*.65/(city.sealed?42.:34.)*(dense?.22:1.));
    }
    out.jobs=block.use==BuildingUse::Park?0:unsigned(frontage*depth*(dense?floors*.45:block.use==BuildingUse::Housing?.04:1.)/24.);
    constexpr std::array<glm::vec3,7> colours{{{.63,.74,.81},{.73,.65,.89},{.91,.83,.64},{.51,.61,.69},{.44,.78,.64},{.49,.76,.84},{.33,.66,.48}}};
    out.tint=colours[size_t(block.use)]*float(.82+r.unit(5)*.18);
    return out;
}
bool dry(const Node& p,double height){return p.geology->liquidWater<.08||height>2.2;}
void totals(const SurfaceSettlement& city,UrbanRegion& region){
    region.maxElevation=0;
    region.population=0;region.jobs=region.buildings=region.industry=region.agriculture=region.utilities=region.civic=0;
    for(auto& block:region.blocks){block.population=block.jobs=0;
        for(unsigned slot=0;slot<urbanPlotCount;++slot)if(block.occupied&(uint64_t(1)<<slot)){
            const auto b=shape(city,region,block,slot);region.maxElevation=std::max(region.maxElevation,double(block.floors[slot])+b.size.y*1.35);block.population+=b.people;block.jobs+=b.jobs;
            ++region.buildings;region.industry+=block.use==BuildingUse::Industry;region.agriculture+=block.use==BuildingUse::Agriculture;
            region.utilities+=block.use==BuildingUse::Utilities;region.civic+=block.use==BuildingUse::Civic;
        }
        region.population+=block.population;region.jobs+=block.jobs;
    }
}
}
std::string_view urbanTierName(UrbanTier tier){return tier==UrbanTier::Metropolis?"METROPOLIS":tier==UrbanTier::Regional?"REGIONAL CITY":"LOCAL SETTLEMENT";}
UrbanGrowth urbanGrowth(const Node& planet,const SurfaceSettlement& city){
    UrbanGrowth g;g.seed=featureSeed(city.seed,Feature::SurfaceSociety,0x757262616e000000ULL+urbanRecipeVersion);
    const auto climate=planetClimate(planet);const double radius=std::pow(10.,planet.frame.logRadius);
    if(!climate.solid||!planet.geology||radius<6e5||climate.gravity>20||climate.temperatureK<175||climate.temperatureK>365||city.buildings.empty())return g;
    // One world-level opportunity, not a new independent chance per outpost.
    const Random opportunity{featureSeed(planet.seed,Feature::SurfaceSociety,0x6d6574726f000001ULL)};
    if(opportunity.unit(0)>=.085)return g;
    const auto catchment=localCatchment(planet,city.direction);
    g.maturity=smoothstep(city.sealed?90.:180.,city.sealed?190.:750.,city.foundingYears);
    g.water=std::clamp(catchment.water*5+catchment.grade[0]*2+climate.moisture*.35,0.,1.);
    g.materials=std::clamp((catchment.grade[1]+catchment.grade[2])*4+(planet.environment.chemistry.metal+planet.environment.chemistry.silicate)*.35,0.,1.);
    const auto orbitalHub=glm::normalize(glm::dvec3(.3,1,.4));
    const double hubDistance=std::acos(std::clamp(glm::dot(city.direction,orbitalHub),-1.,1.))*radius;
    g.trade=.45+.55/(1.+hubDistance/250000.); // retained surface port plus proximity to the orbital hub
    g.energy=std::clamp(.35+g.materials*.4+(1-std::abs(climate.temperatureK-285)/200)*.25,0.,1.);
    g.support=.28*g.maturity+.22*g.water+.18*g.materials+.20*g.trade+.12*g.energy;
    if(g.maturity<.30||g.water<.10||g.materials<.14||g.trade<.30||g.support<.48)return g;
    const Random r{g.seed};
    g.tier=((!city.sealed&&city.foundingYears>420)||(city.sealed&&city.foundingYears>170&&g.energy>.72&&g.materials>.5))&&g.support>.64&&opportunity.unit(0)<.026?UrbanTier::Metropolis:UrbanTier::Regional;
    g.halfCells=g.tier==UrbanTier::Metropolis?16:11;
    g.radius=std::min(radius*.035,(g.tier==UrbanTier::Metropolis?13000.:5000.)*(.8+r.unit(0)*.6));
    if(g.radius<city.radius*2.5){g.tier=UrbanTier::None;g.radius=0;}
    return g;
}
size_t UrbanRegion::residentBytes()const{return sizeof(*this)+blocks.capacity()*sizeof(UrbanBlock)+lookup.capacity()*sizeof(int16_t)+accessRoads.capacity()*sizeof(SurfaceRoad);}
const UrbanBlock* UrbanRegion::block(int x,int z)const{
    const int n=growth.halfCells,side=n*2;
    if(x< -n||z< -n||x>=n||z>=n)return nullptr;
    const size_t key=size_t(z+n)*side+x+n;if(key>=lookup.size())return nullptr;
    const int index=lookup[key];return index<0||size_t(index)>=blocks.size()?nullptr:&blocks[size_t(index)];
}
uint64_t urbanBlockId(const UrbanRegion& region,const UrbanBlock& block){return mix64(region.growth.seed^mix64(gridKey(block.x,block.z)));}
glm::dvec3 urbanPoint(const SurfaceSettlement& city,const UrbanRegion& region,glm::dvec2 at,double height){
    return glm::normalize(city.direction+city.orientation*glm::dvec3(at.x,0,at.y)/region.planetRadius)*(region.planetRadius+height);
}
glm::dvec2 urbanLocal(const SurfaceSettlement& city,const UrbanRegion& region,glm::dvec3 point){
    const double projection=glm::dot(point,city.direction);if(projection<=0)return glm::dvec2(1e30);
    const auto local=glm::inverse(city.orientation)*(point*(region.planetRadius/projection)-city.direction*region.planetRadius);
    return {local.x,local.z};
}
glm::dquat urbanOrientation(glm::dvec3 forward,glm::dvec3 up){return glm::quatLookAtRH(forward,up);}
SurfaceBuilding urbanBuilding(const SurfaceSettlement& city,const UrbanRegion& region,const UrbanBlock& block,unsigned slot){
    if(slot>=urbanPlotCount||!(block.occupied&(uint64_t(1)<<slot)))throw std::out_of_range("Unoccupied urban parcel");
    const auto plan=shape(city,region,block,slot);const auto at=plotLocal(region,block,slot);
    SurfaceBuilding b;b.id=mix64(urbanBlockId(region,block)^mix64(slot+0x6275696c64696e67ULL));b.parcel=b.id;
    b.position=urbanPoint(city,region,at,block.floors[slot]);
    const auto up=glm::normalize(b.position);auto forward=city.orientation*glm::dvec3(0,0,-1);forward=glm::normalize(forward-up*glm::dot(forward,up));
    b.orientation=urbanOrientation(forward,up);b.kind=plan.kind;b.dimensions=plan.size;b.tint=plan.tint;
    b.foundationMetres=block.foundations[slot];b.inhabitants=plan.people;b.jobs=plan.jobs;b.use=block.use;
    b.district=uint16_t(block.use);return b;
}
SurfaceRoad urbanRoad(const SurfaceSettlement& city,const UrbanRegion& region,const UrbanBlock& block,unsigned edge,unsigned segment){
    if(edge>1||segment>=urbanRoadSteps)throw std::out_of_range("Invalid urban street");
    const double width=region.cellWidth*.025,step=region.cellWidth/urbanRoadSteps;
    const glm::dvec2 base(block.x*region.cellWidth,block.z*region.cellWidth);
    auto local=[&](unsigned end,double side){return base+(edge?glm::dvec2(end*step,side):glm::dvec2(side,end*step));};
    SurfaceRoad road;road.width=width;road.kind=StreetClass::Avenue;
    const auto a=urbanPoint(city,region,local(segment,0),(block.roads[edge][segment*2]+block.roads[edge][segment*2+1])*.5);
    const auto b=urbanPoint(city,region,local(segment+1,0),(block.roads[edge][(segment+1)*2]+block.roads[edge][(segment+1)*2+1])*.5);
    road.a=a;road.b=b;const auto centre=(a+b)*.5;
    for(unsigned end=0;end<2;++end)for(unsigned side=0;side<2;++side){
        const auto p=urbanPoint(city,region,local(segment+end,(side?1.:-1.)*width*.5),block.roads[edge][(segment+end)*2+side]);
        road.surface[end*4+side+1]=glm::vec3(p-centre);
        road.surface[end*4+(side?3:0)]=glm::vec3(p-centre);
    }
    return road;
}
std::shared_ptr<const UrbanRegion> generateUrbanRegion(const Universe& u,const Node& p,const SurfaceSettlement& city,
    const UrbanGrowth& growth,std::span<const SurfaceSettlement> neighbours,const std::atomic<bool>* cancel){
    if(growth.tier==UrbanTier::None||growth.halfCells<4||growth.halfCells>16||!std::isfinite(growth.radius)||growth.radius<=city.radius*2.5||!hasSolidSurface(p)||!p.geology||stopped(cancel))return {};
    auto region=std::make_shared<UrbanRegion>();region->growth=growth;region->planetRadius=std::pow(10.,p.frame.logRadius);
    region->cellWidth=growth.radius/growth.halfCells;const int n=growth.halfCells,side=n*2;
    region->lookup.assign(size_t(side*side),-1);region->blocks.reserve(size_t(pi*n*n));
    const Random r{growth.seed};
    const auto forecast=samplePlanningClimate(planetClimate(p),city.direction,glm::length(city.landingPoint)-region->planetRadius);
    auto wind=glm::inverse(city.orientation)*forecast.wind;glm::dvec2 downwind(wind.x,wind.z);
    downwind=glm::length(downwind)>.1?glm::normalize(downwind):glm::dvec2(1,0);
    const glm::dvec2 business{growth.radius*.28,growth.radius*-.12},satellite{-growth.radius*.36,growth.radius*.25};
    const auto height=[&](glm::dvec2 at){return u.terrainHeight(p,glm::normalize(urbanPoint(city,*region,at,0)));};
    for(int z=-n;z<n;++z)for(int x=-n;x<n;++x){
        if(stopped(cancel))return {};
        const glm::dvec2 at((x+.5)*region->cellWidth,(z+.5)*region->cellWidth);const double range=glm::length(at);
        const double edge=1.+.08*std::sin(std::atan2(at.y,at.x)*5+r.unit(1)*6.28);
        if(range>growth.radius*edge||range<city.radius+region->cellWidth*.75+160)continue;
        const auto point=urbanPoint(city,*region,at,0);bool overlap=false;
        for(const auto& other:neighbours)if(other.slot!=city.slot&&glm::length(glm::normalize(point)-other.direction)*region->planetRadius<other.radius+region->cellWidth*.8+120){overlap=true;break;}
        if(overlap)continue;
        UrbanBlock block;block.x=int16_t(x);block.z=int16_t(z);block.ground=float(height(at));
        const Random choice{urbanBlockId(*region,block)};
        const double businessRange=std::min(glm::length(at-business),glm::length(at-satellite)*1.3);
        block.density=float((1-smoothstep(growth.radius*.04,growth.radius*.42,businessRange))*(.72+growth.support*.28));
        const double windSide=glm::dot(at,downwind)/growth.radius;
        block.use=businessRange<growth.radius*.20?BuildingUse::Commerce:
            windSide>.48?BuildingUse::Industry:range>growth.radius*.78&&windSide<.2?BuildingUse::Agriculture:
            choice.unit(0)<.09?BuildingUse::Utilities:choice.unit(1)<.045?BuildingUse::Civic:
            std::abs(std::sin(at.x/growth.radius*9+std::sin(at.y/growth.radius*5)))<.13?BuildingUse::Park:BuildingUse::Housing;
        // Both road edges must be dry, gentle and connected end-to-end. A
        // later flood removes neighbourhoods cut off by water or steep terrain.
        bool roadsValid=true;const double width=region->cellWidth*.025;
        for(unsigned edgeIndex=0;edgeIndex<2&&roadsValid;++edgeIndex)for(unsigned step=0;step<=urbanRoadSteps&&roadsValid;++step){
            if(stopped(cancel))return {};
            for(unsigned shoulder=0;shoulder<2;++shoulder){
                const double across=(shoulder?1.:-1.)*width*.5,along=region->cellWidth*step/urbanRoadSteps;
                const glm::dvec2 q=glm::dvec2(x*region->cellWidth,z*region->cellWidth)+(edgeIndex?glm::dvec2(along,across):glm::dvec2(across,along));
                const double h=height(q);block.roads[edgeIndex][step*2+shoulder]=float(h+.16);
                roadsValid&=dry(p,h);
                if(step)roadsValid&=std::abs(h+.16-block.roads[edgeIndex][(step-1)*2+shoulder])/(region->cellWidth/urbanRoadSteps)<.17;
            }
        }
        if(!roadsValid)continue;
        for(unsigned slot=0;slot<urbanPlotCount;++slot){
            if(stopped(cancel))return {};
            const auto plan=shape(city,*region,block,slot);const auto local=plotLocal(*region,block,slot);
            const double h=height(local);const auto d=glm::normalize(urbanPoint(city,*region,local,0));
            if(!dry(p,h)||glm::dot(d,u.terrainNormal(p,d))<.985)continue;
            double low=h,high=h;bool valid=true;
            const auto up=d;auto forward=city.orientation*glm::dvec3(0,0,-1);forward=glm::normalize(forward-up*glm::dot(forward,up));
            const auto orientation=urbanOrientation(forward,up);
            for(int a:{-1,1})for(int b:{-1,1}){
                const auto corner=glm::normalize(d+orientation*glm::dvec3(a*plan.size.x*.5,0,b*plan.size.z*.5)/region->planetRadius);
                const double sample=u.terrainHeight(p,corner);valid&=dry(p,sample);low=std::min(low,sample);high=std::max(high,sample);
            }
            if(!valid||high-low>std::min(24.,plan.size.y*.3))continue;
            block.occupied|=uint64_t(1)<<slot;block.floors[slot]=float(high+.5);block.foundations[slot]=float(high-low+1.5);
            block.tallest=std::max(block.tallest,float(plan.size.y));
        }
        if(std::popcount(block.occupied)<6)continue;
        const auto index=int16_t(region->blocks.size());region->lookup[size_t(z+n)*side+x+n]=index;region->blocks.push_back(std::move(block));
    }
    if(region->blocks.size()<20||stopped(cancel))return {};
    // Use the innermost connected component, reached from the retained port.
    size_t root=0;double nearest=1e30;
    for(size_t i=0;i<region->blocks.size();++i){const auto& b=region->blocks[i];const double distance=glm::length(glm::dvec2(b.x+.5,b.z+.5));if(distance<nearest){nearest=distance;root=i;}}
    std::vector<bool> reached(region->blocks.size());std::queue<size_t> queue;queue.push(root);reached[root]=true;
    while(!queue.empty()){const auto& b=region->blocks[queue.front()];queue.pop();for(const auto offset:{glm::ivec2(1,0),glm::ivec2(-1,0),glm::ivec2(0,1),glm::ivec2(0,-1)}){
        const auto* adjacent=region->block(b.x+offset.x,b.z+offset.y);if(!adjacent)continue;const size_t id=size_t(adjacent-region->blocks.data());if(!reached[id]){reached[id]=true;queue.push(id);}
    }}
    std::vector<UrbanBlock> connected;connected.reserve(region->blocks.size());
    for(size_t i=0;i<reached.size();++i)if(reached[i])connected.push_back(region->blocks[i]);
    region->blocks=std::move(connected);std::fill(region->lookup.begin(),region->lookup.end(),int16_t(-1));
    for(size_t i=0;i<region->blocks.size();++i){const auto& b=region->blocks[i];region->lookup[size_t(b.z+n)*side+b.x+n]=int16_t(i);}
    if(region->blocks.size()<20)return {};
    // Join the regional grid to an existing outer junction. The road samples
    // reject water, steep grades and retained buildings instead of flattening
    // terrain or cutting a new route through the old town.
    std::vector<std::pair<double,size_t>> entrances;
    for(size_t i=0;i<region->blocks.size();++i){const auto& block=region->blocks[i];entrances.emplace_back(glm::length(glm::dvec2(block.x,block.z)),i);}
    std::sort(entrances.begin(),entrances.end());
    bool joined=false;
    for(size_t entrance=0;entrance<std::min(size_t(12),entrances.size())&&!joined;++entrance){
        const auto& rootBlock=region->blocks[entrances[entrance].second];
        const glm::dvec2 end(rootBlock.x*region->cellWidth,rootBlock.z*region->cellWidth);
        std::vector<std::pair<double,size_t>> starts;
        for(size_t j=0;j<city.junctions.size();++j)starts.emplace_back(glm::length(city.junctions[j].local-end),j);
        std::sort(starts.begin(),starts.end());
        for(size_t startIndex=0;startIndex<std::min(size_t(12),starts.size())&&!joined;++startIndex){
            if(stopped(cancel))return {};
            const auto start=city.junctions[starts[startIndex].second].local;const auto along=glm::normalize(end-start),across=glm::dvec2(-along.y,along.x);
            const unsigned steps=std::max(1u,unsigned(std::ceil(glm::length(end-start)/16.)));
            std::vector<SurfaceRoad> road;bool valid=true;glm::dvec3 previous{};
            for(unsigned step=0;step<=steps&&valid;++step){
                const auto at=glm::mix(start,end,double(step)/steps);
                const double h=height(at);auto point=urbanPoint(city,*region,at,h+.18);valid&=dry(p,h);
                if(step)valid&=std::abs(glm::length(point)-glm::length(previous))/std::max(1.,glm::length(point-previous))<.17;
                for(const auto& other:neighbours){if(!valid)break;for(const auto& building:other.buildings){
                    const auto local=glm::inverse(building.orientation)*(point-building.position);
                    if(std::abs(local.x)<building.dimensions.x*.5+9&&std::abs(local.z)<building.dimensions.z*.5+9){valid=false;break;}
                }}
                if(step&&valid){SurfaceRoad segment;segment.a=previous;segment.b=point;segment.width=16;segment.kind=StreetClass::Avenue;
                    const auto centre=(previous+point)*.5;
                    for(unsigned sideIndex=0;sideIndex<2;++sideIndex)for(unsigned endIndex=0;endIndex<2;++endIndex){
                        const auto where=glm::mix(start,end,double(step-1+endIndex)/steps)+across*(sideIndex?8.:-8.);
                        const double ground=height(where);valid&=dry(p,ground);const auto q=urbanPoint(city,*region,where,ground+.18);
                        segment.surface[endIndex*4+sideIndex+1]=glm::vec3(q-centre);segment.surface[endIndex*4+(sideIndex?3:0)]=glm::vec3(q-centre);
                    }
                    road.push_back(segment);
                }previous=point;
            }
            if(valid&&!road.empty()){region->accessRoads=std::move(road);joined=true;}
        }
    }
    if(!joined)return {};
    region->accessRoads.shrink_to_fit();
    totals(city,*region);
    if(region->population<50000)return {};
    region->blocks.shrink_to_fit();region->lookup.shrink_to_fit();
    return stopped(cancel)?nullptr:region;
}
SurfaceResident urbanResident(const SurfaceSettlement& city,const UrbanRegion& region,const UrbanBlock& block,unsigned plot,unsigned resident,double seconds){
    const auto home=urbanBuilding(city,region,block,plot);
    if(resident>=home.inhabitants||!std::isfinite(seconds)||seconds<0)throw std::out_of_range("Invalid urban resident");
    SurfaceResident person;person.homeBuilding=home.id;person.id=mix64(home.id^mix64(resident+0x70656f706c65ULL));person.suited=city.sealed;
    const Random r{person.id};const double progress=std::fmod(seconds*(.8+r.unit(0)*.5)+r.unit(1)*region.cellWidth*2,region.cellWidth*2);
    const bool returning=progress>region.cellWidth;const double along=returning?region.cellWidth*2-progress:progress;
    const unsigned step=std::min(urbanRoadSteps-1,unsigned(along/region.cellWidth*urbanRoadSteps));
    const auto road=urbanRoad(city,region,block,0,step);const double t=std::clamp(along/region.cellWidth*urbanRoadSteps-step,0.,1.);
    person.position=glm::mix(road.a,road.b,t);const auto up=glm::normalize(person.position),forward=glm::normalize((road.b-road.a)*(returning?-1.:1.));
    person.position+=glm::normalize(glm::cross(up,forward))*(road.width*.5-1)+up*.15;
    person.orientation=urbanOrientation(forward,up);person.outside=std::fmod(seconds/900+r.unit(2),1.)<.68;return person;
}
std::optional<double> urbanCollision(const SurfaceSettlement& city,const UrbanRegion& region,glm::dvec3 from,glm::dvec3 to,double radius){
    const auto delta=to-from;const double t=std::clamp(-glm::dot(from,delta)/std::max(1e-20,glm::dot(delta,delta)),0.,1.);
    if(glm::length(from+delta*t)-region.planetRadius>region.maxElevation+radius)return {};
    const auto a=urbanLocal(city,region,from),b=urbanLocal(city,region,to);const int n=region.growth.halfCells;
    if(std::min(a.x,b.x)>region.growth.radius*1.2+radius||std::max(a.x,b.x)<-region.growth.radius*1.2-radius||std::min(a.y,b.y)>region.growth.radius*1.2+radius||std::max(a.y,b.y)<-region.growth.radius*1.2-radius)return {};
    const auto cell=[&](double value){return int(std::clamp(std::floor(value/region.cellWidth),double(-n),double(n-1)));};
    const int x0=cell(std::min(a.x,b.x)-radius),x1=cell(std::max(a.x,b.x)+radius),z0=cell(std::min(a.y,b.y)-radius),z1=cell(std::max(a.y,b.y)+radius);
    std::optional<double> result;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){const auto* block=region.block(x,z);if(!block)continue;
        for(unsigned slot=0;slot<urbanPlotCount;++slot)if(block->occupied&(uint64_t(1)<<slot)){
            const auto building=urbanBuilding(city,region,*block,slot);const auto inverse=glm::inverse(building.orientation);
            const auto start=inverse*(from-building.position),delta=inverse*(to-from);
            const glm::dvec3 lo(-building.dimensions.x*.5-radius,-building.foundationMetres-radius,-building.dimensions.z*.5-radius);
            const glm::dvec3 hi(building.dimensions.x*.5+radius,building.dimensions.y*1.35+radius,building.dimensions.z*.5+radius);
            double near=0,far=1;for(int axis=0;axis<3;++axis){if(std::abs(delta[axis])<1e-20){if(start[axis]<lo[axis]||start[axis]>hi[axis]){near=2;break;}}
                else{double first=(lo[axis]-start[axis])/delta[axis],last=(hi[axis]-start[axis])/delta[axis];if(first>last)std::swap(first,last);near=std::max(near,first);far=std::min(far,last);}}
            if(near<=far&&near<=1&&(!result||near<*result))result=near;
        }
    }
    return result;
}
uint64_t settlementPopulation(const SurfaceSettlement& city){return uint64_t(city.population)+(city.urban?city.urban->population:0);}
double settlementExtent(const SurfaceSettlement& city){return city.urban?std::max(city.radius,city.urban->growth.radius*1.12):city.radius;}
std::string settlementDisplayName(const SurfaceSettlement& city){return city.urban?std::string(urbanTierName(city.urban->growth.tier))+" "+std::to_string(city.seed%100000):city.name;}
uint64_t urbanFingerprint(const SurfaceSettlement& city){
    if(!city.urban)return 0;const auto& region=*city.urban;uint64_t hash=mix64(region.growth.seed^region.population^region.buildings);
    const auto number=[&](double value){hash=mix64(hash^std::bit_cast<uint64_t>(value));};
    number(region.maxElevation);
    number(region.growth.radius);number(region.cellWidth);hash=mix64(hash^uint64_t(region.growth.tier));
    for(const auto& block:region.blocks){hash=mix64(hash^urbanBlockId(region,block)^block.occupied^uint64_t(block.use)^block.population^block.jobs);number(block.density);
        for(unsigned i=0;i<urbanPlotCount;++i){number(block.floors[i]);number(block.foundations[i]);}
        for(const auto& road:block.roads)for(auto value:road)number(value);
    }for(const auto& road:region.accessRoads)for(double v:{road.a.x,road.a.y,road.a.z,road.b.x,road.b.y,road.b.z})number(v);return hash;
}
} // namespace infinity
