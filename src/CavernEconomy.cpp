#include "infinity/Economy.hpp"
#include "infinity/StationControl.hpp"
#include <numeric>

namespace infinity {
bool supplyBoreMatches(const SurveyContract& job,const Borehole& b) {
    return b.planet==job.target&&(!job.supplySite||
        job.supplySite->id()==encodeTile(tileAt(glm::normalize(b.entry),7)));
}
uint64_t surfaceSupplyMined(const ExplorationState& s,const SurveyContract& job) {
    uint64_t total=0;
    for(const auto& b:s.probes.holes)if(supplyBoreMatches(job,b))
        for(const auto& f:cavernFinds(b))if(f.resource==job.supplyResource)total+=b.finds[f.index].minedKg;
    return total;
}
std::optional<CavernAssaySample> cavernAssaySample(const ExplorationState& s,const SurveyContract& job,const EconomyPlayer& p) {
    constexpr std::array goods{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon};
    std::optional<CavernAssaySample> result;
    const auto kg=job.supplyKg?job.supplyKg:10;
    if(job.supplyKg&&surfaceSupplyMined(s,job)<job.supplyMinedBaseline+uint64_t(kg))return {};
    for(const auto& b:s.probes.holes)if(supplyBoreMatches(job,b))
        for(const auto& find:cavernFinds(b)) {
            const auto& r=b.finds[find.index];
            if((!job.supplyKg||find.resource==job.supplyResource)&&r.grade>=job.grade&&r.revision>job.acceptedRevision&&r.marked&&r.minedKg>=uint64_t(job.supplyKg?1:10)&&
               r.minedRevision>job.acceptedRevision&&p.availableCargo(goods[find.resource])>=kg)
                result=CavernAssaySample{b.id,find.index,find.resource};
        }
    return result;
}
namespace {
glm::dvec3 eyeInPlanet(const Universe& u,const SurveyObserver& o,const Address& planet) {
    return o.reference==planet ? o.position : u.pointRelative(o.reference,
        o.position/std::pow(10.,u.describe(o.reference).frame.logRadius),planet,0);
}
bool validObserver(const SurveyObserver& o) {
    return std::isfinite(glm::length(o.position))&&std::isfinite(glm::length(o.velocity))&&
        std::isfinite(o.hull)&&o.hull>0&&std::isfinite(o.size)&&o.size>=1e-12&&o.size<=1&&
        !o.field&&std::isfinite(o.seconds)&&o.seconds>=0&&o.seconds<=1e12&&
        std::isfinite(glm::length(o.attitude))&&std::abs(glm::length(o.attitude)-1)<1e-6;
}
}
bool cavernSurveyAccess(const Universe& u,const SurveyObserver& o,const Borehole& b,const CavernFind& find,bool close) {
    if(!validObserver(o)||!b.geologyVersion||o.seconds<b.blastTime())return false;
    const auto eye=eyeInPlanet(u,o,b.planet),point=cavernFindPoint(b,find);
    const double range=glm::length(point-eye);
    if(range>80||boreClearance(b,eye,o.seconds)<0)return false;
    if(close&&(o.size>.001||range>std::max(.6,find.radius*.5)||glm::length(o.velocity)>std::max(1e-8,5*o.size)))return false;
    const auto forward=u.relative(o.reference,b.planet,0).orientation*(o.attitude*glm::dvec3(0,0,-1));
    return range<.1||glm::dot(forward,(point-eye)/range)>.75;
}
MiningAccess cavernMiningAccess(const Universe& u,const SurveyObserver& o,const Borehole& b,const CavernFind& f,
                                unsigned mount,std::span<const ActivityBody> nearby) {
    MiningAccess a;a.status="CAVERN MINING / FULL SIZE / STOP / FACE THE MARKED MATERIAL";
    if(!validObserver(o)||o.size<.99||mount>=maxWeaponMounts||glm::length(o.velocity)>3||o.seconds<b.blastTime())return a;
    const auto eye=eyeInPlanet(u,o,b.planet),point=cavernFindPoint(b,f);
    if(glm::length(point-eye)>80||boreClearance(b,eye,o.seconds)<2.5)return a;
    const auto orientation=u.relative(o.reference,b.planet,0).orientation*o.attitude;
    a.ray.reference=b.planet;a.ray.origin=eye+orientation*glm::dvec3(weaponMountLateralMetres(mount),2,0);
    a.ray.direction=glm::normalize(point-a.ray.origin);a.ray.right=orientation*glm::dvec3(1,0,0);
    a.ray.seconds=o.seconds;
    for(unsigned i=0;i<=32;++i)
        if(boreClearance(b,glm::mix(a.ray.origin,point,double(i)/32),o.seconds)<-.03) {
            a.status="CAVERN MINING / ROCK OR CRYSTAL BLOCKS THE CUTTER LINE";return a;
        }
    auto check=a.ray;check.scale=.001;
    if(traceWeapon(u,check,glm::length(point-check.origin),nearby).body) {
        a.status="CAVERN MINING / INTERVENING SHIP / HOLD FIRE";return a;
    }
    a.deposit=b.planet.child(Kind::Surface,encodeTile(tileAt(glm::normalize(b.entry),7)));
    a.endpoint=point;a.allowed=true;a.status="MARKED CAVERN MATERIAL / CTRL-SPACE EXTRACT";
    return a;
}
std::string ExplorationLedger::executeCavern(const Universe& u,Economy& e,EconomyPlayer& p,const EconomicCommand& c) {
    auto found=std::find_if(state_.probes.holes.begin(),state_.probes.holes.end(),[&](const auto& b){return b.id==c.contract;});
    if(found==state_.probes.holes.end()||found->planet.parent()!=e.network_->system||!c.observer||!validObserver(*c.observer)||
       surveySystem(c.observer->reference)!=std::optional<Address>(e.network_->system))return "CAVERN NOT AVAILABLE IN THIS SYSTEM";
    auto& b=*found;const auto& o=*c.observer;
    if(c.action==EconomicAction::ControlProbe) {
        auto next=b;std::string status;
        if(!controlProbe(next,c.probeControl,c.probeValue,o.seconds,status))return status;
        if(c.probeControl==ProbeControl::Depth||c.probeControl==ProbeControl::Detonate)
            for(const auto& other:state_.probes.holes)if(other.id!=b.id&&other.planet==b.planet) {
                const auto x=next.entry-glm::normalize(next.entry)*next.finalDepth();
                const auto y=other.entry-glm::normalize(other.entry)*other.finalDepth();
                if(glm::length(x-y)<next.roomRadius()+other.roomRadius()+5)return "DRILL TARGET INTERSECTS AN EXISTING CAVERN / CONTROL REJECTED";
            }
        b=std::move(next);return status;
    }
    if(c.cavernFind>=cavernFindCount||!b.geologyVersion)return "SELECT AN EXPOSED CAVERN DEPOSIT";
    const auto find=cavernFinds(b)[c.cavernFind];auto& record=b.finds[c.cavernFind];
    if(c.action==EconomicAction::ScanCavern) {
        if(!std::isfinite(c.workSeconds)||c.workSeconds<8||c.serviceUnits<20||c.serviceUnits>150||!cavernSurveyAccess(u,o,b,find,false))return "ENTER THE CAVERN / FACE A DEPOSIT / HOLD F12";
        const bool close=cavernSurveyAccess(u,o,b,find,true);
        const unsigned grade=close?std::clamp(state_.scannerLevel+1,2u,3u):1;
        if(grade<record.grade)return "HIGHER PRECISION CAVERN RECORD RETAINED";
        record.grade=grade;record.revision=++state_.revision;
        return (close?"CLOSE ASSAY RECORDED / CTRL-F12 MARK / ":"CAVERN OVERVIEW RECORDED / SHRINK FOR CLOSE INSPECTION / ")+cavernFindName(find);
    }
    if(c.action==EconomicAction::MarkCavern) {
        if(record.grade<2||!cavernSurveyAccess(u,o,b,find,true))return "SHRINK BELOW 12 MM / STOP NEAR THE FACE / HOLD F12 BEFORE MARKING";
        record.marked=true;return "CAVERN MATERIAL MARKED / RETURN TO FULL SIZE IN THE CHAMBER / CTRL-SPACE MINE";
    }
    if(c.action!=EconomicAction::ExtractCavern||!record.marked||!state_.miningLevel||
       c.weaponMount!=state_.miningMount||!std::isfinite(c.workSeconds)||c.workSeconds<6||!c.combatPresent)return "MARK THE DEPOSIT / FIT AND OPERATE A MINING HEAD";
    const auto access=cavernMiningAccess(u,o,b,find,c.weaponMount);
    if(!access.allowed)return access.status;
    auto pool=std::find_if(state_.prospects.begin(),state_.prospects.end(),[&](const auto& x){return x.address==access.deposit;});
    if(pool==state_.prospects.end()) {
        if(state_.prospects.size()>=maxProspects)return "PROSPECT REGISTER FULL / DEPLETION RECORDS RETAINED";
        state_.prospects.push_back({access.deposit,{},prospectResources(u,access.deposit)});pool=std::prev(state_.prospects.end());
    }
    constexpr std::array goods{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon};
    const auto g=size_t(goods[find.resource]);
    const int64_t kg=std::max<int64_t>(0,std::min({pool->capacity[find.resource]-pool->extracted[find.resource],
        int64_t(20*state_.miningLevel),p.capacityKg-p.massKg(),(p.capacityLitres-p.volumeLitres())/goodInfo(goods[find.resource]).litresPerKg}));
    if(!kg)return pool->capacity[find.resource]==pool->extracted[find.resource] ? "CAVERN MATERIAL DEPLETED / SHARED SURFACE RESERVE EXHAUSTED" : "HOLD FULL / DEPOSIT RETAINED";
    pool->extracted[find.resource]+=kg;p.cargo[g]+=kg;record.minedKg+=kg;state_.minedKg+=kg;
    record.minedRevision=++state_.revision;
    if(!e.sites_.empty()){e.sites_[0].imported[g]+=kg;e.sites_[0].exported[g]+=kg;}
    return "CAVERN EXTRACTED "+std::to_string(kg)+" KG "+std::string(resourceName(find.resource))+" / SELL CARGO OR DELIVER THE ASSAY SAMPLE";
}
} // namespace infinity
