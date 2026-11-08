#include "infinity/SurfaceOperations.hpp"
#include "infinity/Economy.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
void SurfaceWorkClock::advance(double activity, double observed) {
    if(!std::isfinite(observed)||observed<0||!std::isfinite(activity))return;
    activity=std::clamp(activity,0.,1.);
    if(last<0||observed<last)seconds=activity>0?observed:0;
    else seconds+=(observed-last)*activity;
    last=observed;
}
SettlementWork settlementWork(const SurfaceWorld& world, const SurfaceSettlement& city,
                              const WeatherSample& weather, const EconomySnapshot* s) {
    SettlementWork result;
    result.status = "OPERATIONS / AWAITING LOCAL LEDGER";
    if (!s || s->catchingUp || !s->network || s->network->system != world.planet.parent()) return result;
    const ActivityId id{world.planet, surfaceActivitySlot + city.slot};
    const auto index = s->siteIndex(id);
    if (!index || *index >= s->sites.size() || *index >= s->portSignals.size()) return result;
    const auto& state = s->sites[*index]; const auto& signal = s->portSignals[*index];
    result.synchronized = true; result.power = signal.power;
    result.damaged = state.outageUntil > s->tick || state.condition < 450 || state.health < 500;
    result.outdoorHold = weather.storm > .58 || (weather.pressureBar > .003 &&
        (weather.gustMetresPerSecond > 30 || weather.visibilityMetres < 500)) ||
        weather.temperatureK>(city.sealed?700:430);
    const bool operating = state.bottleneck == "OPERATING" || state.bottleneck.empty();
    result.activity = result.damaged || result.outdoorHold || result.power < .15 ? 0 :
        std::clamp(double(signal.power * signal.readiness), 0., 1.);
    for (const auto& f : s->flights) {
        if (f.disabledAt >= 0 || f.kg <= 0) continue;
        result.loading += f.location == *index && f.phase == FreightPhase::Loading;
        result.deliveries += f.destination == *index &&
            (f.phase == FreightPhase::Transit || f.phase == FreightPhase::Holding);
        const bool loading=f.location==*index&&f.phase==FreightPhase::Loading;
        const bool incoming=f.destination==*index&&(f.phase==FreightPhase::Transit||f.phase==FreightPhase::Holding);
        if((loading||incoming)&&result.orderCount<4)
            result.orders[result.orderCount++]={mix64(city.seed^seedOf(f.id.home,s->network->universeSeed)^f.id.slot),
                loading?SurfaceDuty::LoadFreight:SurfaceDuty::ReceiveFreight,unsigned(f.good),f.kg};
    }
    if(operating&&!result.damaged&&result.power>=.15)
        for(auto recipe:s->network->sites[*index].recipes){
            if(recipe>=productionRecipes().size()||result.orderCount>=6)continue;
            const auto& r=productionRecipes()[recipe];bool supplied=true,needed=false;
            for(size_t g=0;g<goodCount;++g)if(r.inputs[g]>state.stock[g]-state.reserved[g])supplied=false;
            const auto& d=s->network->sites[*index];
            const bool extracting=std::all_of(r.inputs.begin(),r.inputs.end(),[](auto kg){return kg==0;});
            for(size_t g=0;g<goodCount;++g)if(r.outputs[g]){
                needed|=state.stock[g]-state.reserved[g]<d.target[g]*3;
                if(extracting&&d.reserves[g]-state.extracted[g]<r.outputs[g])supplied=false;
            }
            if(!supplied||!needed)continue;
            const auto output=std::max_element(r.outputs.begin(),r.outputs.end());
            if(*output>0)result.orders[result.orderCount++]={mix64(city.seed^recipe^0x50524f44554345ULL),
                SurfaceDuty::Production,unsigned(output-r.outputs.begin()),*output};
        }
    if(result.damaged&&result.orderCount<6&&state.stock[size_t(Good::Tools)]>0&&state.stock[size_t(Good::Machinery)]>0)
        result.orders[result.orderCount++]={mix64(city.seed^0x524550414952ULL),SurfaceDuty::Maintenance,unsigned(Good::Machinery),1};
    const bool producing=std::any_of(result.orders.begin(),result.orders.begin()+result.orderCount,
        [](const auto& o){return o.duty==SurfaceDuty::Production;});
    result.machineryActivity=operating&&(producing||s->network->sites[*index].recipes.empty())?result.activity:0;
    result.status = result.outdoorHold ? "OUTDOOR CREWS / WEATHER HOLD"
        : result.damaged ? "MACHINERY OFFLINE / F2 SITE STATUS"
        : result.power < .15 ? "POWER LIMITED / MACHINERY IDLE"
        : !operating ? "PRODUCTION HOLD / " + state.bottleneck
        : !producing&&!s->network->sites[*index].recipes.empty()?"PRODUCTION HOLD / INPUTS NOT READY"
        : "WORKING / " + std::to_string(result.loading) + " LOADING / " +
          std::to_string(result.deliveries) + " INBOUND";
    return result;
}
std::vector<SurfaceWorkActor> sampleSurfaceWork(const SurfaceWorld&, const SurfaceSettlement& city,
                                              const SettlementWork& work, double seconds) {
    std::vector<SurfaceWorkActor> result;
    if (!work.synchronized || !std::isfinite(seconds) || seconds < 0 || seconds > 1e12) return result;
    result.reserve(28);
    const bool timed=work.animationSeconds>=0;
    const double clock=timed?work.animationSeconds:work.activity>.02?seconds:0.;
    const double machineClock=work.machinerySeconds>=0?work.machinerySeconds:work.machineryActivity>.02?seconds:0.;
    const double movement=work.machinerySeconds>=0?1.:work.machineryActivity;
    const auto tint = work.damaged ? glm::vec3(.84f,.26f,.12f) :
        work.outdoorHold ? glm::vec3(.63f,.47f,.19f) : glm::vec3(.86f,.66f,.26f);
    for (const auto& b : city.buildings) {
        if (result.size() >= 20) break;
        if (b.use != BuildingUse::Industry && b.use != BuildingUse::Utilities && b.kind != SurfaceMesh::Warehouse) continue;
        const Random r{mix64(b.id ^ 0x53555246574f524bULL)};
        const auto id = mix64(b.id ^ 0x4d414348494e4553ULL);
        const auto pivot = b.position + b.orientation * glm::dvec3(0,b.dimensions.y,0);
        // Roof equipment fits inside the existing conservative collision box.
        const double size = std::max(.12, std::min({b.dimensions.x*.12,b.dimensions.z*.12,b.dimensions.y*.04,3.}));
        if (b.use == BuildingUse::Utilities) {
            const double travel = .12 * std::sin(machineClock * 1.7 + r.unit(0)*6.28) * movement;
            result.push_back({id,b.id,SurfaceMachine::Drill,pivot,glm::dvec3(size),
                b.orientation * glm::angleAxis(travel,glm::dvec3(0,0,1)),tint,{}});
        } else {
            const double yaw = (std::sin(machineClock * .13 + r.unit(0)*6.28)*.7) * movement;
            result.push_back({id,b.id,SurfaceMachine::CraneBase,pivot,glm::dvec3(size),b.orientation,tint,{}});
            result.push_back({mix64(id),b.id,SurfaceMachine::CraneBoom,pivot+ b.orientation*glm::dvec3(0,size*1.8,0),
                              glm::dvec3(size),b.orientation*glm::angleAxis(yaw,glm::dvec3(0,1,0)),tint,{}});
        }
    }
    // Rovers follow the sampled street strips, including their retained height.
    // Their cargo is a visual handling cue, never additional economic stock.
    for(unsigned count=0;count<work.orderCount&&count<6;++count){
        const auto order=work.orders[count];
        glm::dvec3 depot=city.landingPoint;
        const auto use=order.duty==SurfaceDuty::Production?BuildingUse::Industry:BuildingUse::Commerce;
        for(const auto& b:city.buildings)if(b.use==use||b.kind==SurfaceMesh::Warehouse){depot=b.position;break;}
        const CityStreet* selected=nullptr;double closest=1e300;
        for(const auto& candidate:city.streets){
            if(!candidate.roadCount||candidate.kind==StreetClass::Footway||candidate.width<4.4||candidate.roadBegin>=city.roads.size())continue;
            const auto& road=city.roads[candidate.roadBegin];
            const double range=glm::length((road.a+road.b)*.5-depot);
            // Distinct nearby approaches distribute queued real work over roads.
            const double score=range*(.75+Random{order.id^candidate.id}.unit(0)*.5);
            if(score<closest){closest=score;selected=&candidate;}
        }
        if(!selected)continue;
        const auto& street=*selected;
        const auto id = mix64(order.id ^ 0x434152474f434152ULL);
        const Random r{id};
        double t = std::fmod(clock * (.018 + r.unit(0)*.012) + r.unit(1), 1.);
        const bool returning = t > .5; t = returning ? (1-t)*2 : t*2;
        const size_t ordinal = std::min(size_t(t*street.roadCount), size_t(street.roadCount-1));
        const size_t index = street.roadBegin + ordinal;
        if (index >= city.roads.size()) continue;
        const auto& road = city.roads[index];
        const double along = std::clamp(t*street.roadCount-ordinal,0.,1.);
        const auto up = glm::normalize((road.a+road.b)*.5);
        const auto forward = glm::normalize(road.b-road.a)*(returning ? -1. : 1.);
        const auto sideways = glm::normalize(glm::cross(forward,up));
        const auto centre=(road.a+road.b)*.5;
        const auto near=centre+glm::dvec3(road.surface[1]+road.surface[2])*.5;
        const auto far=centre+glm::dvec3(road.surface[5]+road.surface[6])*.5;
        const auto position = glm::mix(near,far,along)+up*.04+sideways*std::min(road.width*.20,1.5);
        const auto cargoTint=order.duty==SurfaceDuty::Maintenance?glm::vec3(1,.42,.12):
            order.duty==SurfaceDuty::Production?glm::vec3(.37,.82,.65):glm::vec3(.32,.65,1.);
        result.push_back({id,0,SurfaceMachine::CargoRover,position,{1,1,1},glm::quatLookAtRH(forward,up),cargoTint,order});
    }
    return result;
}
} // namespace infinity
