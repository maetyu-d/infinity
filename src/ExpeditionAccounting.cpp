#include "infinity/Economy.hpp"
#include <algorithm>
namespace infinity {
uint64_t expeditionAccountFor(const ExplorationState& state,uint64_t selected,const Address& system){
    uint64_t local=0,only=0;unsigned localCount=0,count=0;
    for(const auto& j:state.contracts)if(j.cavern&&j.phase==SurveyPhase::Accepted){
        if(j.id==selected)return j.id;
        only=j.id;++count;if(j.target.parent()==system){local=j.id;++localCount;}}
    return localCount==1?local:count==1?only:0;
}
std::string ExplorationLedger::execute(const Universe& u,Economy& e,EconomyPlayer& p,const EconomicCommand& c){
    const auto credits=p.credits;const auto last=p.lastCommand,mined=state_.minedKg;const auto holes=state_.probes.holes.size();
    auto result=executeOrder(u,e,p,c);
    if(c.sequence>last&&p.lastCommand==c.sequence){
        recordExpense(c,credits,p);
        auto job=std::find_if(state_.contracts.begin(),state_.contracts.end(),[&](const auto& j){return j.id==c.surveyAccount&&j.cavern&&j.phase==SurveyPhase::Accepted&&j.accounting.recorded;});
        if(job!=state_.contracts.end()){
            if(c.action==EconomicAction::LaunchProbe&&state_.probes.holes.size()>holes&&state_.probes.holes.back().planet==job->target)++job->accounting.probesUsed;
            if(c.action==EconomicAction::ExtractCavern&&state_.minedKg>mined){
                const auto bore=std::find_if(state_.probes.holes.begin(),state_.probes.holes.end(),[&](const auto& b){return b.id==c.contract;});
                if(bore!=state_.probes.holes.end()&&bore->planet==job->target)job->accounting.extractedKg+=state_.minedKg-mined;}
        }
    }
    return result;
}
void ExplorationLedger::recordExpense(const EconomicCommand& c,int64_t before,const EconomyPlayer& p){
    if(!c.surveyAccount||p.lastCommand!=c.sequence||before==p.credits)return;
    auto job=std::find_if(state_.contracts.begin(),state_.contracts.end(),[&](const auto& j){return j.id==c.surveyAccount&&j.cavern&&j.phase==SurveyPhase::Accepted&&j.accounting.recorded;});
    if(job==state_.contracts.end()||c.sequence<=job->accounting.lastCharge)return;
    auto& a=job->accounting;int64_t* bucket=nullptr;
    switch(c.action){
    case EconomicAction::FitMiningHead:case EconomicAction::UpgradeScanner:case EconomicAction::FitProbeLauncher:
    case EconomicAction::SellMiningHead:case EconomicAction::SellProbeLauncher:bucket=&a.equipment;break;
    case EconomicAction::ReloadProbe:bucket=&a.ammunition;break;
    case EconomicAction::RepairHull:bucket=&a.repairs;break;
    default:return;
    }
    const int64_t delta=before-p.credits;
    if(std::abs(delta)>1000000000000LL||std::abs(*bucket+delta)>1000000000000LL)return;
    *bucket+=delta;a.lastCharge=c.sequence;
}
void ExplorationLedger::offerFollowup(const Universe& u,Economy& e){
    if(state_.contracts.size()>=maxSurveyContracts||e.network_->sites.empty())return;
    std::optional<SurveyContract> prior;
    for(const auto& j:state_.contracts)if(j.cavern&&!j.supplyKg&&j.phase==SurveyPhase::Complete&&j.target.parent()==e.network_->system&&
        std::none_of(state_.contracts.begin(),state_.contracts.end(),[&](const auto& next){return next.previous==j.id;}))prior=j;
    if(!prior)return;
    const auto count=u.planetCount(u.describe(e.network_->system));if(!count)return;
    SurveyContract next;next.id=mix64(prior->id^0x4e45585441535341ULL);if(!next.id)next.id=1;
    if(std::any_of(state_.contracts.begin(),state_.contracts.end(),[&](const auto& j){return j.id==next.id;}))return;
    next.issuer=prior->issuer;next.grade=2;next.cavern=true;next.reward=prior->reward;next.previous=prior->id;
    for(uint64_t n=1;n<=count;++n){const auto target=e.network_->system.child(Kind::Planet,(prior->target.id()+n)%count);
        if(!hasSolidSurface(u.describe(target)))continue;
        next.target=target;next.name="CAVERN ASSAY / "+u.describe(target).label();state_.contracts.push_back(next);return;}
}
} // namespace infinity
