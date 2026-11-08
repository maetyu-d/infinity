#include "infinity/Urban.hpp"
#include "infinity/Economy.hpp"
#include <chrono>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>
using namespace infinity;
namespace {
size_t checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
}
int main(){try{
    Universe universe;
    const auto address=Address{}.child(Kind::Galaxy,0).child(Kind::System,26).child(Kind::Planet,14);
    const auto planet=universe.describe(address);const auto descriptor=descriptorFingerprint(planet);
    const auto original=generateSurfaceLife(universe,planet);check(original&&!original->settlements.empty(),"No retained town for urban tests");
    auto core=*std::find_if(original->settlements.begin(),original->settlements.end(),[](const auto& city){return city.slot==1;});
    check(bool(core.urban),"Natural regional-city fixture lost its expansion");const auto growth=core.urban->growth;core.urban.reset();const auto fingerprint=cityFingerprint(core);
    const auto port=core.landingPoint;const auto seed=core.seed;
    // Selection is rare and tied to canonical history and endowments. A cold,
    // resource-poor or young outpost cannot become a metropolis by rolling RNG.
    unsigned opportunities=0,metros=0;
    auto mature=core;mature.foundingYears=850;mature.sealed=false;
    auto fertile=planet;fertile.frame.logRadius=6.8;fertile.environment.temperature=285;fertile.environment.waterActivity=.8;
    fertile.environment.chemistry={.65,.25,.08,.02};fertile.environment.ageGyr=4;fertile.environment.mineral=Mineral::Quartz;
    fertile.geology=planetProcess(fertile.seed,fertile.frame.logRadius,fertile.environment,true);
    for(unsigned i=0;i<4096;++i){auto candidate=fertile;candidate.seed=mix64(fertile.seed^i);
        const auto growth=urbanGrowth(candidate,mature);opportunities+=growth.tier!=UrbanTier::None;metros+=growth.tier==UrbanTier::Metropolis;
        check(growth.seed==urbanGrowth(candidate,mature).seed&&growth.tier==urbanGrowth(candidate,mature).tier,"Growth depends on request order");
        auto young=mature;young.foundingYears=5;check(urbanGrowth(candidate,young).tier==UrbanTier::None,"New colony became an old metropolis");
        auto hostile=candidate;hostile.environment.temperature=800;
        check(urbanGrowth(hostile,mature).tier==UrbanTier::None,"Unsuitable world grew a city");
    }
    check(opportunities>0&&opportunities<500&&metros>0&&metros<150,"Urban opportunity distribution is absent or commonplace");
    std::cout<<"URBAN_OPPORTUNITY candidates=4096 eligible="<<opportunities<<" metropolis="<<metros<<'\n';
    // Real terrain, retained port and neighbouring buildings constrain the
    // expansion even when the opportunity profile is supplied explicitly.
    const auto begin=std::chrono::steady_clock::now();
    auto region=generateUrbanRegion(universe,planet,core,growth,original->settlements);
    check(bool(region),"Valid city opportunity produced no connected development");
    auto city=core;city.urban=region;
    check(core.seed==seed&&core.landingPoint==port&&cityFingerprint(city)==fingerprint,"Urban development rewrote the retained town centre");
    check(descriptorFingerprint(universe.describe(address))==descriptor,"Urban planning edited the planet descriptor");
    check(region->blocks.size()>=20&&region->buildings>500&&region->growth.radius>core.radius*2.5,"Large city is still a small prop grid");
    check(region->residentBytes()<1024*1024,"Compact city exceeded one MiB");
    check(!region->accessRoads.empty(),"City grid is disconnected from its old port");
    uint64_t people=0,jobs=0;unsigned buildings=0;std::set<uint64_t> identities;std::set<BuildingUse> districts;
    const auto replay=generateUrbanRegion(universe,planet,core,growth,original->settlements);auto repeat=core;repeat.urban=replay;
    check(replay&&urbanFingerprint(city)==urbanFingerprint(repeat),"Urban geometry changed after regeneration");
    for(const auto& block:region->blocks){
        check(region->block(block.x,block.z)==&block,"Urban spatial index lost a district block");
        districts.insert(block.use);
        for(unsigned plot=0;plot<urbanPlotCount;++plot)if(block.occupied&(uint64_t(1)<<plot)){
            const auto b=urbanBuilding(city,*region,block,plot);++buildings;people+=b.inhabitants;jobs+=b.jobs;
            check(identities.insert(b.id).second,"Urban buildings share an identity");
            const auto local=urbanLocal(city,*region,b.position);const auto back=urbanPoint(city,*region,local,glm::length(b.position)-region->planetRadius);
            check(glm::length(back-b.position)<1e-6,"Urban coordinates failed a spherical round trip");
            check(glm::length(local)>core.radius,"Urban building obstructs the retained pad");
            const double terrain=universe.terrainHeight(planet,glm::normalize(b.position));
            check(planet.geology->liquidWater<.08||terrain>2.2,"Urban building is underwater");
            check(glm::length(b.position)-region->planetRadius-b.foundationMetres<terrain+.01,"Urban foundation floats above terrain");
            if(buildings%89==0&&b.inhabitants){
                const auto person=urbanResident(city,*region,block,plot,0,10),later=urbanResident(city,*region,block,plot,0,20);
                check(person.id==later.id&&person.homeBuilding==b.id&&later.homeBuilding==b.id&&person.position!=later.position,"Urban resident lost a stable home or never moves");
                const auto a=b.position+b.orientation*glm::dvec3(0,b.dimensions.y*.5,-b.dimensions.z-30),z=b.position+b.orientation*glm::dvec3(0,b.dimensions.y*.5,b.dimensions.z+30);
                check(urbanCollision(city,*region,a,z,2).has_value(),"Ship tunnels through an urban building");
            }
        }
        for(unsigned edge=0;edge<2;++edge)for(unsigned segment:{0u,15u,31u}){
            const auto road=urbanRoad(city,*region,block,edge,segment);const auto centre=(road.a+road.b)*.5;
            for(unsigned corner:{1u,2u,5u,6u}){const auto point=centre+glm::dvec3(road.surface[corner]);
                const double h=glm::length(point)-region->planetRadius-universe.terrainHeight(planet,glm::normalize(point));
                check(std::abs(h-.16)<.012,"Urban pavement detached from the authoritative terrain");}
        }
    }
    check(buildings==region->buildings&&people==region->population&&jobs==region->jobs,"Urban population or infrastructure is invented independently of buildings");
    check(districts.size()>=4&&region->utilities&&region->industry&&region->agriculture,"Giant city lacks functional districts");
    check(settlementPopulation(city)==people+core.population&&settlementExtent(city)>core.radius,"Life/contact summaries omit the regional city");
    SurfaceWorld world=*original;*std::find_if(world.settlements.begin(),world.settlements.end(),[&](const auto& s){return s.slot==city.slot;})=city;
    check(summarizeSurfaceLife(world).population>=settlementPopulation(city),"Life scanner ignores urban residents");
    check(!surfaceCollision(world,port+core.direction*70.,port+core.direction*2.,2),"Development obstructs the original landing footprint");
    const auto capitalAddress=Address{}.child(Kind::Galaxy,0).child(Kind::System,167).child(Kind::Planet,0);
    const auto capitalPlanet=universe.describe(capitalAddress);const auto capital=generateSurfaceLife(universe,capitalPlanet);
    const auto giant=std::find_if(capital->settlements.begin(),capital->settlements.end(),[](const auto& s){return s.urban&&s.urban->growth.tier==UrbanTier::Metropolis;});
    check(giant!=capital->settlements.end()&&giant->urban->buildings>15000&&giant->urban->growth.radius>10000,"No natural metropolis with a genuine city-scale footprint");
    check(capital->residentBytes()<4*1024*1024&&giant->urban->residentBytes()<1024*1024,"Metropolis exceeds the existing world cache budget");
    if(giant->sealed)for(const auto& block:giant->urban->blocks)if(block.use==BuildingUse::Park)for(unsigned plot=0;plot<urbanPlotCount;++plot)if(block.occupied&(uint64_t(1)<<plot))
        check(urbanBuilding(*giant,*giant->urban,block,plot).kind!=SurfaceMesh::Tree,"Exposed trees grow outside a sealed city's protection");
    std::atomic<bool> cancelled=true;
    check(!generateUrbanRegion(universe,planet,core,growth,original->settlements,&cancelled),"Cancelled city job published geometry");
    // Expansion retains site IDs and offers a strict previous-descriptor
    // fingerprint for existing economic saves; funds and stocks are not reset.
    auto legacy=std::make_shared<EconomicNetwork>();legacy->fingerprint=1234;EconomicSite site;site.id={address,2000};site.seed=44;site.initial[size_t(Good::Food)]=40;legacy->sites.push_back(site);
    Economy old(legacy);old.advanceTo(3);Economy self(legacy);check(self.restore(old.serialize()),"Existing ledger fixture cannot restore itself");const auto saved=old.serialize();const auto money=old.moneyTotal();
    auto expanded=std::make_shared<EconomicNetwork>(*legacy);expanded->fingerprint=5678;expanded->legacyFingerprint=1234;expanded->sites[0].population=people;expanded->legacyInitialTotal=old.initialTotal();expanded->legacyEndowmentMoney=money;
    Economy restored(expanded);check(restored.restore(saved)&&restored.moneyTotal()==money&&restored.sites()[0].stock==old.sites()[0].stock,"City upgrade discarded an existing economic ledger");
    const auto migrated=restored.serialize();Economy again(expanded);check(again.restore(migrated)&&again.initialTotal()==old.initialTotal()&&again.moneyTotal()==money,"Migrated ledger lost its original endowment on a second restart");
    auto unrelated=std::make_shared<EconomicNetwork>(*expanded);unrelated->legacyFingerprint=0;Economy wrong(unrelated);check(!wrong.restore(saved),"Unrelated economic fingerprint was accepted");
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"URBAN_REGION blocks="<<region->blocks.size()<<" buildings="<<buildings<<" people="<<people<<" extent_m="<<region->growth.radius*2<<" bytes="<<region->residentBytes()<<" validation_ms="<<ms<<'\n';
    std::cout<<"PASS: "<<checks<<" rare-city, terrain, identity, inhabitants, collision, cancellation and saved-economy checks\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
