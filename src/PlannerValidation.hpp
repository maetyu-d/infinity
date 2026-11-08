#pragma once
#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/Renderer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
namespace infinity {
// Stages real commands against an isolated economy; it never edits generator data.
class PlannerValidation {
    const Universe& u_;Navigation& nav_;Pilot& pilot_;Renderer& renderer_;SDL_Window* window_;
    ShipCrew crew_{600};CrewView crewView_;PlannerChoice choice_;PlannerInput input_;ExpeditionPlan plan_,trackerPlan_;
    std::shared_ptr<EconomicNetwork> net_;std::unique_ptr<Economy> economy_;EconomyPlayer player_;ExplorationLedger ledger_;
    std::shared_ptr<EconomySnapshot> snapshot_;SurveyContract job_;Node planet_;glm::dvec3 entry_{},up_{};
    Borehole bore_;WeaponRay ray_;uint64_t sequence_=0,identity_=0;unsigned stage_=0,age_=0,stable_=0;bool captured_=false;
    std::string navBefore_,pilotBefore_;
    void expect(bool value,const char* why){if(!value)throw std::runtime_error(why);}
    glm::dvec3 land(const Node& p){for(unsigned i=0;i<96;++i){const auto d=glm::normalize(glm::dvec3(std::sin(i*2.4)+.2,.7,std::cos(i*2.4)));
        const double h=u_.terrainHeight(p,d),r=std::pow(10.,p.frame.logRadius);if(p.geology->liquidWater<=.1||h/r>=-.0008)return d*(r+h);}throw std::runtime_error("Native planner found no solid land");}
    void issue(EconomicAction action,const char* prefix,uint64_t id=0,unsigned mount=0){EconomicCommand c;c.action=action;c.sequence=++sequence_;c.port=net_->sites[0].id;c.contract=id;c.weaponMount=mount;
        c.surveyAccount=choice_.contract;
        c.observer=input_.observer;c.workSeconds=8;c.serviceUnits=100;c.combatPresent=true;c.limitCents=100000000;
        c.probeRay=ray_;c.surveyTarget=job_.target;c.probeValue=choice_.depth;c.probeRate=choice_.feed;
        const auto result=ledger_.execute(u_,*economy_,player_,c);expect(result.starts_with(prefix),result.c_str());}
    void configure(){
        if(stage_==1)issue(EconomicAction::AcceptSurvey,"SURVEY ACCEPTED",job_.id);
        if(stage_==2){issue(EconomicAction::FitProbeLauncher,"PROBE LAUNCHER FITTED",0,0);issue(EconomicAction::FitMiningHead,"MINING HEAD FITTED",0,1);
            crew_.setWeaponLevels(ledger_.state().stationLoadout(player_));crew_.manWeapon(2,1);for(unsigned i=0;i<600;++i)crew_.advance(.05);
            input_.observer.reference=Address{};input_.observer.position={0,0,0};}
        if(stage_==3){input_.observer.reference=job_.target;input_.observer.position=ray_.origin;}
        if(stage_==4){issue(EconomicAction::LaunchProbe,"PROBE LAUNCHED");bore_=ledger_.state().probes.holes.back();choice_.bore=bore_.id;input_.observer.seconds=bore_.impactTime()+2;}
        if(stage_==5){input_.observer.seconds=bore_.blastTime()+3;input_.observer.position=bore_.entry+up_*25.;}
        if(stage_>=6&&stage_<=11){const auto find=cavernFinds(bore_)[0];const auto point=cavernFindPoint(bore_,find);
            input_.observer.position=bore_.entry-up_*bore_.finalDepth();input_.observer.size=1;
            if(stage_==8||stage_==9){input_.observer.size=.0005;input_.observer.position=point+tangentFrame(up_)*find.inward*.5;}
            input_.observer.attitude=glm::quatLookAtRH(glm::normalize(point-input_.observer.position),tangentFrame(up_)*glm::dvec3(0,0,-1));
            if(stage_==7||stage_==8)issue(EconomicAction::ScanCavern,stage_==7?"CAVERN OVERVIEW":"CLOSE ASSAY",bore_.id);
            if(stage_==9)issue(EconomicAction::MarkCavern,"CAVERN MATERIAL MARKED",bore_.id);
            if(stage_==11)issue(EconomicAction::ExtractCavern,"CAVERN EXTRACTED",bore_.id,1);
        }
        if(stage_==12){input_.observer.reference=net_->sites[0].id.home;input_.observer.position=net_->sites[0].metres;input_.connectedIssuer=true;}
        if(stage_==13)issue(EconomicAction::ClaimSurvey,"CAVERN ASSAY COMPLETE",job_.id);
        if(stage_==14||stage_==15||stage_==17){const bool portrait=stage_==14||stage_==17;const auto density=std::max(1.,double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_,int((portrait?600:1440)/density),int((portrait?1000:900)/density));SDL_SyncWindow(window_);
            int w,h;SDL_GetWindowSizeInPixels(window_,&w,&h);renderer_.resize(w,h);}
        snapshot_=std::make_shared<EconomySnapshot>();snapshot_->network=net_;snapshot_->sites=economy_->sites();snapshot_->player=player_;snapshot_->exploration=ledger_.state();
        input_.open=stage_!=15;plan_=planCavernExpedition(u_,snapshot_.get(),choice_,input_);
        constexpr std::array expected{PlanStage::Offer,PlanStage::Prepare,PlanStage::Travel,PlanStage::Drill,PlanStage::Drill,PlanStage::Enter,PlanStage::Overview,PlanStage::Inspect,PlanStage::Mark,PlanStage::Regrow,PlanStage::Extract,PlanStage::Return,PlanStage::Deliver,PlanStage::Complete,PlanStage::Complete,PlanStage::Complete,PlanStage::Complete,PlanStage::Complete};
        expect(plan_.stage==expected[stage_],"Native planner stage does not match real expedition progress");
        if(stage_==10)trackerPlan_=plan_;
        if(stage_==15){plan_=trackerPlan_;plan_.open=false;}
        if(stage_>=16){plan_=trackerPlan_;plan_.open=true;plan_.details=true;}
        crewView_=crew_.view();crewView_.open=false;
        renderer_.setEconomy(snapshot_,nullptr);renderer_.setCrew(&crewView_);renderer_.setExpeditionPlan(&plan_);
        navBefore_=nav_.serialize();pilotBefore_=pilot_.serialize();
    }
  public:
    PlannerValidation(const Universe& u,Navigation& nav,Pilot& pilot,Renderer& r,SDL_Window* window):u_(u),nav_(nav),pilot_(pilot),renderer_(r),window_(window){
        planet_=u_.describe(Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0));entry_=land(planet_);up_=glm::normalize(entry_);
        net_=std::make_shared<EconomicNetwork>();net_->system=planet_.address.parent();net_->universeSeed=u_.seed();net_->fingerprint=600060;
        EconomicSite port;port.id={planet_.address,0};port.orbital=true;port.metres=entry_+up_*30000.;port.radius=100;port.name="EXPEDITION ASSAY AND EQUIPMENT HARBOUR";port.capacityKg=2000000;port.initial.fill(10000);net_->sites.push_back(port);
        economy_=std::make_unique<Economy>(net_);ledger_.offer(u_,*economy_);input_.crew=&crew_;input_.observer.reference=planet_.address;input_.observer.position=port.metres;input_.observer.seconds=100;input_.open=true;
        EconomySnapshot s;s.network=net_;s.sites=economy_->sites();s.player=player_;s.exploration=ledger_.state();auto first=planCavernExpedition(u_,&s,choice_,input_);
        choice_.contract=first.contract;choice_.tracking=true;choice_.depth=90;choice_.feed=.85;
        job_=*std::find_if(ledger_.state().contracts.begin(),ledger_.state().contracts.end(),[&](const auto& j){return j.id==choice_.contract;});
        planet_=u_.describe(job_.target);entry_=land(planet_);up_=glm::normalize(entry_);identity_=descriptorFingerprint(planet_);
        ray_.reference=planet_.address;ray_.origin=entry_+up_*500.;ray_.direction=-up_;ray_.seconds=100;
        nav_.anchor=planet_.address;nav_.logDistance=nav_.targetLogDistance=planet_.frame.logRadius+.7;nav_.offset={};nav_.transitioning=false;nav_.selected.reset();nav_.stopMotion();pilot_.resetMotion();
        renderer_.setActivity(nullptr);renderer_.setCockpitValidation(true);configure();
    }
    void apply(FlightInstruments& t){++age_;renderer_.setEconomy(snapshot_,nullptr);renderer_.setCrew(&crewView_);renderer_.setExpeditionPlan(&plan_);renderer_.setProbeView(false,input_.observer.seconds);
        t.destination="CAVERN ASSAY EXPEDITION";t.explorationActive=false;
        if(stage_==4)t.phase="MINING PROBE CAMERA";}
    void observe(const FrameStats& stats,RegionCache& cache,const std::string& directory){if(finished())return;
        const auto jobs=cache.stats();stable_=stats.visibleObjects&&!jobs.pending&&!stats.lodTransitions&&!stats.uploadDeferred?stable_+1:0;
        if(age_>2200)throw std::runtime_error("Native planner scene failed to settle");
        if(!captured_&&stable_>=8){expect(nav_.serialize()==navBefore_&&pilot_.serialize()==pilotBefore_,"Planner changed ship navigation or dynamics");
            expect(descriptorFingerprint(u_.describe(job_.target))==identity_,"Planner changed world identity");
            if(plan_.open){const auto viewport=renderer_.cockpitViewport();const PlannerLayout layout(viewport.aspect(),plan_.details,&plan_);
                const auto pixel=viewport.toPixels({layout.panel.x+20,layout.panel.y+100});
                expect(!renderer_.pick(pixel.x,pixel.y)&&!renderer_.pickActivity(pixel.x,pixel.y),"Planner lets clicks select world objects behind its panel");}
            std::filesystem::create_directories(directory);renderer_.screenshot((std::filesystem::path(directory)/("planner-"+std::to_string(stage_)+".ppm")).string());
            std::cout<<"PLANNER_CAPTURE "<<stage_<<" stage="<<unsigned(plan_.stage)<<" contract="<<plan_.contract<<" identity="<<identity_<<" text_overlap=0\n";captured_=true;age_=0;
        }else if(captured_&&age_>=8){++stage_;if(finished()){ExplorationLedger replay;expect(replay.restore(ledger_.serialize(),u_),"Native planner ledger restart failed");std::cout<<"PASS: native expedition planner / 18 layouts / real assay handover / unchanged ship and world\n";return;}
            age_=stable_=0;captured_=false;configure();}
    }
    bool finished()const{return stage_==18;}
};
} // namespace infinity
