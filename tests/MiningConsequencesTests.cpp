#include "infinity/MiningConsequences.hpp"
#include "infinity/Economy.hpp"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace infinity;
namespace {unsigned checks=0;void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}}
int main(){try{
    Universe u;const auto home=Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0);const auto planet=u.describe(home);
    const auto identity=descriptorFingerprint(planet);glm::dvec3 entry{};
    for(unsigned n=0;n<96;++n){const auto d=glm::normalize(glm::dvec3(std::sin(n*2.4)+.2,.7,std::cos(n*2.4)));const double h=u.terrainHeight(planet,d),r=std::pow(10.,planet.frame.logRadius);
        if(planet.geology->liquidWater<=.1||h/r>=-.0008){entry=d*(r+h);break;}}
    check(glm::length(entry)>0,"No solid fixture terrain");const auto up=glm::normalize(entry);
    WeaponRay ray;ray.reference=home;ray.origin=entry+up*500.;ray.direction=-up;ray.seconds=100;
    auto planned=planMiningProbe(u,home,ray,0,501);check(bool(planned.hole),"Cannot plan consequence fixture");auto b=*planned.hole;
    const auto pool=home.child(Kind::Surface,encodeTile(tileAt(glm::normalize(b.entry),7)));
    ExplorationState state;state.probes.holes.push_back(b);state.prospects.push_back({pool,{},prospectResources(u,pool)});
    const auto f=cavernFinds(b)[0];const auto pristine=cavernWear(u,b,state);const auto geometry=boreGeometry(u,b,true);
    check(pristine.faces[0].remaining==1&&cavernTailings(b,pristine).vertices.empty(),"Untouched cave invents depletion or debris");
    state.prospects[0].extracted[f.resource]=state.prospects[0].capacity[f.resource]/2;b.finds[0].minedKg=20;
    const auto worked=cavernWear(u,b,state);check(worked.key!=pristine.key&&worked.faces[0].remaining<.6,"Shared depletion does not change presentation");
    check(glm::length(worked.shade(f.local,{.3f,.3f,.3f})-pristine.shade(f.local,{.3f,.3f,.3f}))>.02,"Extraction exposes no new cut face");
    const auto debris=cavernTailings(b,worked);check(debris.vertices.size()>0&&debris.vertices.size()<=144,"Persistent cuttings missing or unbounded");
    for(const auto& v:debris.vertices)check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"Tailings have invalid coordinates");
    const auto after=boreGeometry(u,b,true);check(after->solid.vertices.size()==geometry->solid.vertices.size()&&
        std::memcmp(after->solid.vertices.data(),geometry->solid.vertices.data(),geometry->solid.vertices.size()*sizeof(Vertex))==0,"Mining appearance rewrites host geometry or crystal collisions");
    state.prospects[0].extracted=state.prospects[0].capacity;const auto empty=cavernWear(u,b,state);
    check(empty.faces[0].remaining==0,"Fully mined reserve retains ore coverage");
    auto neighbour=b;neighbour.id=502;neighbour.entry+=tangentFrame(up)*glm::dvec3(.1,0,0);neighbour.finds={};
    const auto shared=cavernWear(u,neighbour,state);check(shared.faces[0].remaining==0&&cavernTailings(neighbour,shared).vertices.empty(),"Another hole refills ore or invents local extraction debris");
    const auto rock=u.describe(pool.child(Kind::Mountain,0).child(Kind::Rock,0));const auto rockPool=prospectAddress(u,rock.address);check(bool(rockPool),"Surface rock has no shared resource address");
    ExplorationState surface;surface.prospects.push_back({*rockPool,{100,100,100,100},{100,100,100,100}});
    check(prospectWearTint(u,rock,surface).x<.5,"Spent surface outcrop retains its untouched finish");
    auto lattice=rock;lattice.address=lattice.address.child(Kind::Grain,0).child(Kind::Lattice,0);
    check(prospectWearTint(u,lattice,surface)==glm::vec3(1),"Surface wear recolours the underlying atomic lattice");
    for(auto mineral:{Mineral::Quartz,Mineral::Iron,Mineral::Olivine,Mineral::Ice,Mineral::Graphite}){
        auto face=f;face.mineral=mineral;const auto a=cavernMiningMotes(b,face,123),again=cavernMiningMotes(b,face,123);
        check(a.count==64&&a.count==again.count,"Material particles are unbounded or missing");unsigned sparks=0;
        for(size_t n=0;n<a.count;++n){const auto& m=a.items[n];sparks+=m.spark;
            check(m.position==again.items[n].position&&m.colour==again.items[n].colour,"Presentation randomness changes while time and identity are fixed");
            check(std::isfinite(glm::length(m.position))&&m.radius>0&&m.opacity>=0&&m.opacity<=1,"Invalid material-specific particle");
            if(m.fragment&&mineral==Mineral::Graphite)check(m.stretch.y<.2,"Graphite chips do not follow their layered habit");}
        check(bool(sparks)==(mineral==Mineral::Iron),"Non-metal mining produces metal sparks");
    }
    auto network=std::make_shared<EconomicNetwork>();network->system=home.parent();network->universeSeed=u.seed();network->fingerprint=610061;
    EconomicSite port;port.id={home,0};port.orbital=true;port.metres=entry+up*30000.;port.radius=100;port.capacityKg=2000000;port.initial.fill(10000);network->sites.push_back(port);
    Economy economy(network);EconomyPlayer player;ExplorationLedger accounts;accounts.offer(u,economy);
    const auto offer=*std::find_if(accounts.state().contracts.begin(),accounts.state().contracts.end(),[](const auto& j){return j.cavern;});
    EconomicCommand c;c.port=port.id;c.sequence=1;c.action=EconomicAction::AcceptSurvey;c.contract=offer.id;c.surveyAccount=offer.id;
    SurveyObserver observer;observer.reference=home;observer.position=port.metres;observer.hull=95;c.observer=observer;
    check(accounts.execute(u,economy,player,c).starts_with("SURVEY ACCEPTED"),"Cannot open scoped expense account");
    check(expeditionAccountFor(accounts.state(),0,home.parent())==offer.id,"Browsing an offer or hiding the HUD loses the sole active expedition account");
    auto ambiguous=accounts.state();auto alternate=offer;alternate.id^=123;alternate.phase=SurveyPhase::Accepted;ambiguous.contracts.push_back(alternate);
    check(expeditionAccountFor(ambiguous,0,home.parent())==0&&expeditionAccountFor(ambiguous,offer.id,home.parent())==offer.id,"Ambiguous purchases are silently assigned to the wrong expedition");
    auto account=[&]()->const SurveyAccounting&{return std::find_if(accounts.state().contracts.begin(),accounts.state().contracts.end(),[&](const auto& j){return j.id==offer.id;})->accounting;};
    c.sequence=2;c.action=EconomicAction::FitProbeLauncher;c.limitCents=100000000;c.weaponMount=0;auto cash=player.credits;
    check(accounts.execute(u,economy,player,c).starts_with("PROBE LAUNCHER FITTED")&&account().equipment==cash-player.credits,"Hardware cost is not its actual net charge");
    c.sequence=3;c.action=EconomicAction::ReloadProbe;cash=player.credits;
    check(accounts.execute(u,economy,player,c).starts_with("PROBES RELOADED")&&account().ammunition==cash-player.credits,"Probe purchases use an invented price");
    c.sequence=4;c.action=EconomicAction::RepairHull;c.serviceUnits=1;cash=player.credits;
    check(economy.execute(c,player).starts_with("HULL REPAIR PAID"),"Cannot pay for scoped repairs");accounts.recordExpense(c,cash,player);
    const auto repairs=account().repairs;check(repairs==cash-player.credits&&repairs>0,"Paid repair receipt missing from expedition");
    accounts.recordExpense(c,cash,player);check(account().repairs==repairs,"Repair callback charges the report twice");
    c.sequence=5;c.action=EconomicAction::Buy;c.good=Good::Water;c.kg=1;cash=player.credits;economy.execute(c,player);accounts.recordExpense(c,cash,player);
    check(account().repairs==repairs&&account().lastCharge==4,"Unrelated trading contaminates expedition profit");
    c.sequence=6;c.action=EconomicAction::RepairHull;c.surveyAccount=0;cash=player.credits;economy.execute(c,player);accounts.recordExpense(c,cash,player);
    check(account().repairs==repairs,"Unscoped repair is attributed to an unrelated expedition");
    c.sequence=7;c.action=EconomicAction::ReloadProbe;c.surveyAccount=offer.id;c.limitCents=0;const auto ammo=account().ammunition;cash=player.credits;
    accounts.execute(u,economy,player,c);check(player.credits==cash&&account().ammunition==ammo,"Rejected order appears as a paid cost");
    ExplorationLedger saved;check(saved.restore(accounts.serialize(),u)&&saved.state().contracts.back().accounting.repairs==repairs,"Expense categories do not survive restart");
    std::ostringstream legacy;legacy<<"INFINITE_EXPLORATION 5 1 0 "<<noWeaponMount<<" 0 1 120000 0\n0\n0\n1\n77 "<<std::quoted(ActivityId{home,0}.str())<<' '<<std::quoted(home.str())<<" 2 2 120000 0 0 1\n0\n"<<std::quoted(serializeProbes(MiningProbeState{}))<<'\n';
    ExplorationLedger loaded;check(loaded.restore(legacy.str(),u),"Pre-accounting save no longer loads");
    check(!loaded.state().contracts[0].accounting.recorded&&loaded.state().contracts[0].accounting.paid==120000,"Legacy save invents a historical cost basis");
    ExplorationLedger roundtrip;check(roundtrip.restore(loaded.serialize(),u),"Migrated accounting save does not round-trip");
    check(descriptorFingerprint(u.describe(home))==identity,"Consequences modified the procedural world");
    std::cout<<"PASS: "<<checks<<" depletion, shared reserves, exposed surfaces, persistent debris, material particles and save migration checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
