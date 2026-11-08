#include "infinity/SurfaceRecon.hpp"
#include "infinity/Urban.hpp"
#include "infinity/MajorLandforms.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace infinity {
glm::dvec2 reconPosition(const SurfaceMap& map,glm::dvec3 point){
    const auto p=glm::inverse(map.axes)*(point-map.centre);return {p.x,p.z};
}
std::shared_ptr<const SurfaceMap> sampleSurfaceMap(const Universe& u,const Node& n,Tile tile,const std::atomic<bool>* cancel){
    if(n.address.kind()!=Kind::Planet||!hasSolidSurface(n))return {};
    auto map=std::make_shared<SurfaceMap>();map->planet=n.address;map->tile=tile;
    const auto up=tileDirection(tile);const double radius=std::pow(10.,n.frame.logRadius);
    map->axes=tangentFrame(up);map->centre=up*(radius+u.terrainHeight(n,up));
    const double step=map->radius*2/(reconGrid-1);
    for(unsigned z=0;z<reconGrid;++z){
        if(cancel&&cancel->load())return {};
        for(unsigned x=0;x<reconGrid;++x){auto& c=map->cells[z*reconGrid+x];
            const auto d=glm::normalize(up+map->axes*glm::dvec3(x*step-map->radius,0,z*step-map->radius)/radius);
            const double h=u.terrainHeight(n,d);c.metres=d*(radius+h);c.height=float(h);
            c.dry=n.geology->liquidWater<=.1||h/radius>=-.0008;
            const auto field=geologySample(*n.geology,d);const auto e=resourceExposure(n,d,field);
            c.resource=unsigned(std::max_element(e.fractions.begin(),e.fractions.end())-e.fractions.begin());
            c.concentration=float(e.concentration*e.exposure);
        }
    }
    for(unsigned z=0;z<reconGrid;++z)for(unsigned x=0;x<reconGrid;++x){
        const auto loX=x?x-1:x,hiX=std::min(x+1,reconGrid-1),loZ=z?z-1:z,hiZ=std::min(z+1,reconGrid-1);
        const double dx=(map->cells[z*reconGrid+hiX].height-map->cells[z*reconGrid+loX].height)/((hiX-loX)*step);
        const double dz=(map->cells[hiZ*reconGrid+x].height-map->cells[loZ*reconGrid+x].height)/((hiZ-loZ)*step);
        map->cells[z*reconGrid+x].slope=float(std::atan(std::hypot(dx,dz)));
    }
    return map;
}
SurfaceMapCache::~SurfaceMapCache(){
    {std::lock_guard lock(mutex_);for(auto& e:entries_)if(e.cancel)e.cancel->store(true);}
    tasks_.wait();
}
void SurfaceMapCache::wait(){tasks_.wait();}
size_t SurfaceMapCache::entries()const{std::lock_guard lock(mutex_);return entries_.size();}
std::shared_ptr<const SurfaceMap> SurfaceMapCache::request(const Universe& u,const Node& n,glm::dvec3 direction){
    if(!hasSolidSurface(n)||!std::isfinite(glm::length(direction))||glm::length(direction)<1e-12)return {};
    const auto tile=tileAt(glm::normalize(direction),14);const auto key=encodeTile(tile);
    std::lock_guard lock(mutex_);++serial_;
    for(auto& e:entries_)if(e.planet==n.address&&e.tile==key){e.used=serial_;if(e.map)return e.map;goto fallback;}
    for(auto it=entries_.begin();it!=entries_.end();)if(it->cancel){it->cancel->store(true);it=entries_.erase(it);}else ++it;
    if(entries_.size()>=4){const auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.used<b.used;});entries_.erase(oldest);}
    {auto token=std::make_shared<std::atomic<bool>>(false);entries_.push_back({n.address,key,serial_,{},token});
        tasks_.run([this,&u,n,tile,key,token]{std::shared_ptr<const SurfaceMap> map;
            try{map=sampleSurfaceMap(u,n,tile,token.get());}catch(...){token->store(true);}
            std::lock_guard guard(mutex_);
            const auto e=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.cancel==token&&e.tile==key;});
            if(e==entries_.end())return;
            if(token->load()||!map)entries_.erase(e);else{e->map=std::move(map);e->cancel.reset();}
        });
    }
fallback:
    for(auto& e:entries_)if(e.planet==n.address&&e.map){
        const auto eye=glm::normalize(direction)*glm::length(e.map->centre);
        if(glm::length(reconPosition(*e.map,eye))<e.map->radius*.75)return e.map;
    }
    return {};
}
const ReconPoint* SurfaceReconView::target()const{
    const auto p=std::find_if(points.begin(),points.end(),[&](const auto& p){return p.id==selected;});
    return p==points.end()?nullptr:&*p;
}
namespace {
glm::dvec3 eyeIn(const Universe& u,const SurveyObserver& o,const Address& planet){
    return o.reference==planet?o.position:u.pointRelative(o.reference,o.position/std::pow(10.,u.describe(o.reference).frame.logRadius),planet,0);
}
uint64_t markerId(const Address& a,uint64_t tag){const auto id=mix64(seedOf(a,tag));return id?id:1;}
bool onMap(const SurfaceMap& m,glm::dvec3 p){const auto xy=reconPosition(m,p);return std::abs(xy.x)<m.radius*.94&&std::abs(xy.y)<m.radius*.94;}
}
SurfaceReconView describeSurfaceRecon(const Universe& u,std::shared_ptr<const SurfaceMap> map,const ReconInput& in,ReconOrders& orders,double dt){
    SurfaceReconView v;v.open=in.open;v.selected=in.selected;v.map=std::move(map);v.loading=!v.map;
    if(!v.map){v.status="TERRAIN SURVEY LOADING / APPROACH A SOLID WORLD";return v;}
    const auto& m=*v.map;const auto n=u.describe(m.planet);const auto climate=planetClimate(n);
    v.eye=eyeIn(u,in.observer,m.planet);v.ship=reconPosition(m,v.eye);
    const auto forward=glm::inverse(m.axes)*(u.relative(in.observer.reference,m.planet,0).orientation*(in.observer.attitude*glm::dvec3(0,0,-1)));
    v.heading=glm::length(glm::dvec2(forward.x,forward.z))>1e-6?glm::normalize(glm::dvec2(forward.x,forward.z)):glm::dvec2(0,-1);
    v.scanQuality=surveyExposure(u,in.observer,m.planet).scanQuality;
    const WeatherSampler weather(climate,in.observer.seconds);
    auto add=[&](ReconPoint p){
        if(!onMap(m,p.metres)||std::any_of(v.points.begin(),v.points.end(),[&](const auto& other){return other.id==p.id;}))return;
        if(v.points.size()>=40){
            if(p.kind!=ReconKind::Incident)return;
            const auto expendable=std::find_if(v.points.begin(),v.points.end(),[](const auto& p){return p.kind!=ReconKind::Incident;});
            if(expendable==v.points.end())return;v.points.erase(expendable);
        }
        v.points.push_back(std::move(p));
    };
    if(n.geology->landform){
        const auto address=majorLandformAddress(n);const auto direction=u.describe(address).geographicDirection;
        ReconPoint p;p.id=markerId(address,0x4d414a4f52ULL);p.kind=ReconKind::Estimate;p.address=address;
        p.metres=direction*(std::pow(10.,n.frame.logRadius)+u.terrainHeight(n,direction));
        p.name=std::string(majorLandformName(n.geology->landform->type));
        p.detail="GEOLOGICAL LANDMARK / F12 SURVEY / CHECK LOCAL HAZARDS";add(std::move(p));
    }
    // A few broad returns are estimates from the inherited exposure field. They
    // never create journal entries, discoveries, cargo, sites or economic jobs.
    std::array<const ReconCell*,4> best{};const ReconCell* landing=nullptr;double landingScore=1e30;
    for(const auto& c:m.cells){if(!c.dry||!onMap(m,c.metres))continue;
        if(!best[c.resource]||c.concentration>best[c.resource]->concentration)best[c.resource]=&c;
        const auto xy=reconPosition(m,c.metres);const double score=c.slope*5000+glm::length(xy-v.ship);
        if(c.slope<.18&&score<landingScore){landing=&c;landingScore=score;}
    }
    for(unsigned resource=0;resource<4;++resource)if(const auto c=best[resource];c&&c->concentration>.04){
        const auto pool=m.planet.child(Kind::Surface,encodeTile(tileAt(glm::normalize(c->metres),7)));
        ReconPoint p;p.id=markerId(pool,0x52455455524eULL+resource);p.kind=ReconKind::Estimate;p.metres=c->metres;p.address=pool;p.resource=resource;
        if(in.economy)for(const auto& record:in.economy->exploration.journal)if(record.prospect==pool)p.grade=std::max(p.grade,record.grade);
        if(p.grade>=2)p.kind=ReconKind::Assay;
        p.name=std::string(p.grade>=2?"ASSAY / ":"ESTIMATE / ")+std::string(resourceName(resource));
        p.detail=p.grade>=2?"REGIONAL ASSAY / LOCAL FACE STILL NEEDS INSPECTION":"BROAD RETURN / F12 SURVEY TO CONFIRM";add(std::move(p));
    }
    if(landing){const auto direction=glm::normalize(landing->metres);
        const double slope=std::acos(std::clamp(glm::dot(direction,u.terrainNormal(n,direction)),-1.,1.));
        const auto w=weather.sample(direction,0);const auto hazard=surfaceConditions(n,direction,slope,w);
        const bool clear=in.world&&in.world->planet==m.planet&&
            !surfaceCollision(*in.world,landing->metres+direction*24.,landing->metres+direction*3.,6.);
        if(clear&&slope<.24&&!hazard.landingHold){ReconPoint p;p.id=markerId(m.planet.child(Kind::Surface,encodeTile(tileAt(direction,16))),0x4c414e44494e47ULL);
            p.kind=ReconKind::Landing;p.metres=landing->metres;p.name="POSSIBLE LANDING AREA";
            p.detail="SAMPLED GENTLE GROUND / CHECK VISUALLY / L LAND";add(std::move(p));}
    }
    if(in.world&&in.world->planet==m.planet)for(const auto& city:in.world->settlements){
        ReconPoint p;p.id=markerId(m.planet,city.seed);p.kind=ReconKind::Settlement;p.metres=city.landingPoint;p.site=ActivityId{m.planet,surfaceActivitySlot+city.slot};
        p.name=settlementDisplayName(city);p.detail=std::to_string(settlementPopulation(city))+" RESIDENTS / ORIGINAL SERVICE PORT";add(std::move(p));
        if(city.urban){
            constexpr std::array<std::string_view,7> names{"HOUSING","COMMERCIAL CENTRE","CIVIC DISTRICT","INDUSTRIAL BELT","AGRICULTURAL BELT","UTILITY DISTRICT","PARK DISTRICT"};
            std::array<const UrbanBlock*,7> nearest{};std::array<double,7> ranges;ranges.fill(1e30);
            for(const auto& block:city.urban->blocks){const auto road=urbanRoad(city,*city.urban,block,0,0);const double range=glm::length(reconPosition(m,road.a));const auto use=size_t(block.use);
                if(range<ranges[use]){ranges[use]=range;nearest[use]=&block;}}
            for(size_t use=0;use<nearest.size();++use)if(nearest[use]&&ranges[use]<m.radius){
                const auto& block=*nearest[use];const auto road=urbanRoad(city,*city.urban,block,0,0);
                ReconPoint district;district.id=markerId(m.planet,urbanBlockId(*city.urban,block));district.kind=ReconKind::Settlement;district.metres=road.a;
                district.site=ActivityId{m.planet,surfaceActivitySlot+city.slot};district.name=std::string(names[use]);
                district.detail=settlementDisplayName(city)+" / STREET JUNCTION / SERVICES AT ORIGINAL PORT";add(std::move(district));
            }
        }
    }
    if(in.economy&&in.economy->network&&in.economy->network->system==m.planet.parent()){
        const auto& e=*in.economy;
        for(const auto& b:e.exploration.probes.holes)if(b.planet==m.planet&&b.geologyVersion&&in.observer.seconds>=b.blastTime()){
            ReconPoint p;p.id=mix64(b.id^0x424f5245ULL);p.kind=ReconKind::Entrance;p.metres=b.entry;p.grade=1;
            p.name="SAVED CAVERN ENTRANCE";p.detail="CONFIRMED EXCAVATION / APPROACH THE OPEN SHAFT";add(std::move(p));
        }
        for(const auto& job:e.recoveries)if(job.site<e.network->sites.size()&&job.phase!=RecoveryPhase::Expired){
            const auto& d=e.network->sites[job.site];if(d.id.home!=m.planet||d.orbital)continue;
            ReconPoint p;p.id=mix64(job.id^seedOf(d.id.home,d.id.slot)^0x4649454c44ULL);p.kind=ReconKind::Incident;p.metres=d.metres;p.site=d.id;
            p.recovery=job.id;p.phase=job.phase;p.actionable=!e.catchingUp&&(job.phase==RecoveryPhase::Open||job.phase==RecoveryPhase::Accepted);
            p.name=(job.kind==RecoveryKind::Maintenance?"PLANT FAILURE / ":job.kind==RecoveryKind::Restart?"PRODUCTION STOP / ":"URGENT SUPPLIES / ")+d.name;
            p.detail=job.phase==RecoveryPhase::Complete?"RECOVERY COMPLETE / LOCAL OUTCOME RECORDED":job.phase==RecoveryPhase::Working?"LOCAL CREW REPAIRING / MACHINERY WILL RESUME":
                job.phase==RecoveryPhase::Open?"FUNDED INCIDENT / HOLD 0 AT THE SITE TO ACCEPT":"BRING REQUIRED PARTS / LAND / HOLD 0 TO TRANSFER";
            if(job.phase<=RecoveryPhase::Accepted){p.detail.clear();for(size_t g=0;g<goodCount;++g)if(job.required[g]>job.delivered[g])
                p.detail+=(p.detail.empty()?"NEEDS ":" / ")+std::to_string(job.required[g]-job.delivered[g])+" KG "+std::string(goodInfo(Good(g)).name);}
            add(std::move(p));
        }
        for(const auto& f:e.flights)if(f.weatherHold&&f.disabledAt<0&&f.destination<e.network->sites.size()){
            const auto& d=e.network->sites[f.destination];if(d.id.home!=m.planet||d.orbital)continue;
            ReconPoint p;p.id=markerId(d.id.home,uint64_t(d.id.slot)^0x57454154484552ULL);p.kind=ReconKind::Incident;p.metres=d.metres;p.site=d.id;p.waiting=true;
            p.name="FREIGHT WEATHER HOLD / "+d.name;p.detail="ACTUAL INBOUND CARGO / LANDING RESUMES WHEN WEATHER CLEARS";add(std::move(p));
        }
    }
    for(unsigned z=0;z<5;++z)for(unsigned x=0;x<5;++x){const auto& c=m.cells[(z*5)*reconGrid+x*5];const auto w=weather.sample(glm::normalize(c.metres),0);
        const auto h=surfaceConditions(n,glm::normalize(c.metres),c.slope,w);
        v.weather[z*5+x]={reconPosition(m,c.metres),h.severity};
    }
    const auto priority=[](const ReconPoint& p){return p.kind!=ReconKind::Incident?0:p.actionable?3:p.waiting?2:1;};
    std::stable_sort(v.points.begin(),v.points.end(),[&](const auto& a,const auto& b){return priority(a)>priority(b);});
    v.selected=in.selected;
    if(!v.target()&&!v.points.empty())v.selected=v.points.front().id;
    v.status="8 KM / PINK ESTIMATE / GREEN ASSAY / AMBER RISK";
    const auto target=v.target();
    if(orders.target!=v.selected){orders.target=v.selected;orders.assessment=0;orders.work=0;}
    const double nav=in.crew?std::clamp(in.crew->efficiency(CrewStation::Navigation),0.,1.5):0;
    const double engineer=in.crew?std::clamp(in.crew->efficiency(CrewStation::Engineering),0.,1.5):0;
    const double elapsed=std::isfinite(dt)?std::clamp(dt,0.,.5):0;
    if(orders.navigator&&nav>.1&&target){orders.assessment=std::min(1.,orders.assessment+elapsed*nav/4.);
        v.navWatching=true;v.confidence=orders.assessment*std::min(1.,nav);
        double highest=-1e30;const auto end=reconPosition(m,target->metres);
        for(unsigned i=0;i<=24;++i){const auto p=glm::mix(v.ship,end,i/24.);
            const auto grid=(p/m.radius+1.)*.5*double(reconGrid-1);
            const auto x=unsigned(std::clamp(std::round(grid.x),0.,double(reconGrid-1))),z=unsigned(std::clamp(std::round(grid.y),0.,double(reconGrid-1)));
            highest=std::max(highest,double(m.cells[z*reconGrid+x].height));}
        const double targetHeight=glm::length(target->metres)-std::pow(10.,n.frame.logRadius);
        v.safeAltitude=std::max(250.,highest-targetHeight+120.+(1-std::min(1.,nav))*180.);
        const double horizon=15+std::min(1.,nav)*75;
        const auto prediction=sampleWeather(climate,glm::normalize(target->metres),0,in.observer.seconds+horizon);
        v.navigator=orders.assessment<1?"NAV / ASSESSING ROUTE "+std::to_string(int(orders.assessment*100))+"%":
            "NAV EST / CLEARANCE "+std::to_string(int(v.safeAltitude))+" M / "+std::to_string(int(horizon))+"S "+std::string(weatherName(prediction.type));
    }else {if(orders.navigator)orders.assessment=0;
        v.navigator=orders.navigator?"NAV / NEED A READY NAVIGATOR IN F9":"CTRL-F5 / ASK NAVIGATOR TO ASSESS ROUTE";}
    if(orders.engineer&&engineer>.1){v.engineerWatching=true;
        const double altitude=glm::length(v.eye)-std::pow(10.,n.frame.logRadius)-u.terrainHeight(n,glm::normalize(v.eye));
        const auto local=weather.sample(glm::normalize(v.eye),std::max(0.,altitude));
        const auto exposure=surfaceExposure(n,local,altitude,glm::normalize(v.eye));
        auto projected=in.exposure;const double horizon=20+std::min(1.,engineer)*100;double when=-1;
        for(double t=.5;t<=horizon;t+=.5)if(projected.advance(exposure,in.observer.size,.5)>0){when=t;break;}
        v.engineer=in.iceLoad>.25?"ENG / ICE STRAIN / RELEASE CUTTER / BACK AWAY":when>=0?"ENG EST / DAMAGE IN "+std::to_string(int(std::floor(when)))+"S / SEEK SHELTER":
            "ENG EST / NO DAMAGE IN "+std::to_string(int(horizon))+"S AT CURRENT CONDITIONS";
    }else v.engineer=orders.engineer?"ENG / NEED A READY ENGINEER IN F9":"ALT-F5 / ASK ENGINEER TO WATCH EXPOSURE";
    v.action=target&&target->actionable?"LAND AT SITE / HOLD 0 / F2 JOBS FOR PARTS":"F4 ROUTE / SHIFT-F5 NEXT / F5 CLOSE";
    return v;
}
void cycleReconPoint(uint64_t& id,const SurfaceReconView& v,int direction){
    if(v.points.empty()){id=0;return;}const auto p=std::find_if(v.points.begin(),v.points.end(),[&](const auto& p){return p.id==id;});
    const auto i=p==v.points.end()?0:size_t(p-v.points.begin());id=v.points[(i+v.points.size()+direction)%v.points.size()].id;
}
std::optional<uint64_t> pickReconPoint(const SurfaceReconView& v,glm::dvec2 normalized){
    if(!v.map)return {};double best=.08;std::optional<uint64_t> selected;
    for(const auto& p:v.points){const auto d=glm::length(reconPosition(*v.map,p.metres)/v.map->radius-normalized);
        if(d<best){best=d;selected=p.id;}}return selected;
}
bool fieldServiceAccess(const Universe& u,const EconomicSite& site,const SurveyObserver& o,std::string& why){
    if(site.orbital||site.id.home.kind()!=Kind::Planet||!o.landed||o.field||!std::isfinite(o.size)||o.size<.99||o.size>1||
       !std::isfinite(o.hull)||o.hull<=0||!std::isfinite(glm::length(o.velocity))||glm::length(o.velocity)>3||
       !std::isfinite(o.seconds)||o.seconds<0||o.seconds>1e12){why="LAND AT FULL SIZE / DRIVE OFF / STOP AT THE INCIDENT";return false;}
    if(surveySystem(o.reference)!=surveySystem(site.id.home)){why="INCIDENT IS IN ANOTHER SYSTEM";return false;}
    const auto eye=eyeIn(u,o,site.id.home);
    if(!std::isfinite(glm::length(eye))||glm::length(eye-site.metres)>180){why="APPROACH THE MARKED SITE / LAND WITHIN 180 M";return false;}
    const auto n=u.describe(site.id.home);if(!hasSolidSurface(n)){why="NO SOLID SITE";return false;}
    const auto direction=glm::normalize(eye);
    const double altitude=glm::length(eye)-std::pow(10.,n.frame.logRadius)-u.terrainHeight(n,direction);
    if(altitude< -5||altitude>25){why="LAND ON THE ACTUAL SURFACE BEFORE FIELD WORK";return false;}
    const auto d=glm::normalize(site.metres);const auto weather=sampleWeather(planetClimate(n),d,0,o.seconds);
    if(surfaceConditions(n,d,0,weather).outdoorHold){why="FIELD WORK HELD BY WEATHER / WAIT IN SHELTER";return false;}
    why="SITE ACCESS CONFIRMED / HOLD 0 TO RESPOND";return true;
}
std::string respondFieldIncident(const Universe& u,Economy& e,EconomyPlayer& p,const EconomicCommand& c){
    if(!c.sequence||c.sequence<=p.lastCommand)return "ORDER ALREADY PROCESSED";
    const auto reject=[&](std::string why){p.lastCommand=c.sequence;return why;};
    const auto site=e.siteIndex(c.port);
    if(c.action!=EconomicAction::RespondFieldIncident||!site||!c.observer)return reject("SELECT A LOCAL FIELD INCIDENT");
    const auto job=std::find_if(e.recoveries().begin(),e.recoveries().end(),[&](const auto& j){return j.site==*site&&j.id==c.contract;});
    if(job==e.recoveries().end()||job->phase>RecoveryPhase::Accepted||e.tick()>job->deadline)return reject("FIELD INCIDENT CLOSED / REFRESH THE SCANNER");
    std::string why;if(!fieldServiceAccess(u,e.network().sites[*site],*c.observer,why))return reject(why);
    if(c.observer->seconds>(e.tick()+2)*economyTickSeconds||c.observer->seconds+economyTickSeconds<e.tick()*economyTickSeconds)
        return reject("FIELD OBSERVATION EXPIRED / HOLD 0 AGAIN");
    if(job->phase==RecoveryPhase::Accepted&&(!std::isfinite(c.workSeconds)||c.workSeconds<8||c.serviceUnits<20||c.serviceUnits>150))
        return reject("FIELD TRANSFER REQUIRES COMPLETED WORK AT THE SITE");
    auto order=c;order.action=job->phase==RecoveryPhase::Open?EconomicAction::AcceptRecovery:EconomicAction::DeliverRecovery;
    return e.execute(order,p);
}
} // namespace infinity
