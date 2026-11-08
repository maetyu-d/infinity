#include "infinity/Economy.hpp"
#include <algorithm>

namespace infinity {
void ExplorationLedger::offerSurfaceSupply(const Universe& u,Economy& e) {
    constexpr std::array goods{Good::Ice,Good::Ore,Good::Silicate,Good::Carbon};
    if(state_.contracts.size()>=maxSurveyContracts)return;
    unsigned active=0;
    for(const auto& j:state_.contracts)if(j.supplyKg&&j.target.parent()==e.network_->system&&j.phase<=SurveyPhase::Accepted)++active;
    if(active>=2)return;
    // Runs on the existing economy worker. A bounded search uses actual local
    // recipes, available stock, finite deposits, terrain and geological faces.
    for(size_t i=0;i<std::min(size_t(32),e.sites_.size())&&active<2&&state_.contracts.size()<maxSurveyContracts;++i){
        const auto& d=e.network_->sites[i];const auto& s=e.sites_[i];
        if(d.orbital||d.id.home.kind()!=Kind::Planet||s.cash<50000)continue;
        if(std::any_of(state_.contracts.begin(),state_.contracts.end(),[&](const auto& j){return j.supplyKg&&j.issuer==d.id&&j.phase<=SurveyPhase::Accepted;}))continue;
        const auto planet=u.describe(d.id.home);if(!hasSolidSurface(planet)||glm::length(d.metres)<1)continue;
        Stock inputs{};
        for(auto recipe:d.recipes)if(recipe<productionRecipes().size()){
            const auto& r=productionRecipes()[recipe];bool needed=false;
            for(size_t g=0;g<goodCount;++g)if(r.outputs[g]&&s.stock[g]-s.reserved[g]<d.target[g]*3)needed=true;
            if(needed)for(size_t g=0;g<goodCount;++g)inputs[g]+=r.inputs[g];
        }
        bool offered=false;
        for(unsigned resource=0;resource<goods.size();++resource){
            const auto g=size_t(goods[resource]);
            const int64_t missing=std::min(d.target[g],inputs[g]*4)-(s.stock[g]-s.reserved[g]);
            if(inputs[g]<=0||missing<20)continue;
            const auto key=mix64(d.seed^mix64(resource)^0x535550504c595349ULL);
            const auto id=key?key:1;
            if(std::any_of(state_.contracts.begin(),state_.contracts.end(),[&](const auto& j){return j.id==id;}))continue;
            const auto origin=glm::normalize(d.metres);const auto tangent=tangentFrame(origin);
            const double radius=std::pow(10.,planet.frame.logRadius);
            for(unsigned k=0;k<12;++k){
                const double angle=k*2.399963229728653+Random{key}.unit(0)*6.28318530718;
                const auto tile=tileAt(glm::normalize(origin+tangent*glm::dvec3(std::cos(angle)*.055,0,std::sin(angle)*.055)),7);
                const auto direction=tileDirection(tile);const double h=u.terrainHeight(planet,direction);
                if((planet.geology->liquidWater>.1&&h/radius<-.0008)||glm::dot(direction,u.terrainNormal(planet,direction))<.985)continue;
                const auto site=planet.address.child(Kind::Surface,encodeTile(tile));
                int64_t available=prospectResources(u,site)[resource];
                for(const auto& p:state_.prospects)if(p.address==site)available=p.capacity[resource]-p.extracted[resource];
                const int64_t kg=std::min<int64_t>({missing,available,60});if(kg<20)continue;
                WeaponRay ray;ray.reference=planet.address;ray.origin=direction*(radius+h+500);ray.direction=-direction;
                auto probe=planMiningProbe(u,planet.address,ray,0,id);
                if(!probe.hole)continue;
                const auto finds=cavernFinds(*probe.hole);
                if(std::none_of(finds.begin(),finds.end(),[&](const auto& f){return f.resource==resource;}))continue;
                SurveyContract j;j.id=id;j.issuer=d.id;j.target=planet.address;j.cavern=true;j.grade=2;
                j.supplySite=site;j.supplyResource=resource;j.supplyKg=kg;
                j.reward=45000+kg*goodInfo(goods[resource]).baseCents*2;
                if(j.reward>s.cash)continue;
                j.name="LOCAL SUPPLY / "+std::to_string(kg)+" KG "+std::string(resourceName(resource))+" / "+planet.label();
                state_.contracts.push_back(std::move(j));++active;offered=true;break;
            }
            if(active>=2||offered)break;
        }
    }
}
} // namespace infinity
