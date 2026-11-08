#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include "infinity/ProbePresentation.hpp"
#include "infinity/MiningConsequences.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace infinity {
class MiningProbeValidation {
    const Universe& u_; Navigation& ship_; Pilot& pilot_; Renderer& renderer_; SDL_Window* window_;
    Navigation camera_;
    Node planet_; Borehole hole_; MiningProbeState state_;
    std::shared_ptr<EconomicNetwork> network_; std::unique_ptr<Economy> economy_;
    ExplorationLedger ledger_; EconomyPlayer player_; MarketView market_;
    WeaponSystem miningWeapons_;std::shared_ptr<EconomySnapshot> miningReplay_;
    unsigned miningFind_=0;size_t miningTriangles_=0;
    std::string shipBefore_,pilotBefore_; uint64_t fingerprint_=0;
    unsigned stage_=0,age_=0,stable_=0;size_t triangles_=0;bool captured_=false;
    bool movie_=false,movieStarted_=false;unsigned movieFrame_=0,movieFrames_=0,drain_=0;
    bool reference_=false;
    bool benchmark_=false;
    std::vector<double> perfWall_,perfCpu_,perfProbe_;
    uint64_t perfUploads_=0,perfLightUpdates_=0,perfHidden_=0;
    void expect(bool b,const char* why){if(!b)throw std::runtime_error(why);}
    void placeShip(glm::dvec3 metres,glm::dvec3 forward) {
        const auto up=glm::normalize(hole_.entry);
        ship_.anchor=planet_.address.child(Kind::Surface,encodeTile(tileAt(up,16)));
        ship_.logDistance=ship_.targetLogDistance=1;ship_.transitioning=false;ship_.selected.reset();
        const auto frame=u_.relative(planet_.address,ship_.anchor,1);
        const auto q=glm::quatLookAtRH(glm::normalize(forward),tangentFrame(up)*glm::dvec3(0,0,-1));
        ship_.orientation=glm::normalize(frame.orientation*q);
        const auto p=u_.pointRelative(planet_.address,metres/std::pow(10.,planet_.frame.logRadius),ship_.anchor,1);
        ship_.offset=p-ship_.orientation*glm::dvec3(0,0,1);ship_.stopMotion();pilot_.resetMotion();
    }
    void configure() {
        hole_=ledger_.state().probes.holes.back();
        if(stage_>=16){
            auto command=[&](EconomicAction action,unsigned mount=1){EconomicCommand c;c.action=action;c.sequence=player_.lastCommand+1;c.port=network_->sites[0].id;c.weaponMount=mount;
                c.limitCents=100000000;c.serviceUnits=100;c.workSeconds=8;c.combatPresent=true;c.cavernFind=miningFind_;
                return c;};
            if(stage_==16){
                auto c=command(EconomicAction::FitMiningHead);SurveyObserver o;o.reference=planet_.address;o.position=network_->sites[0].metres;o.seconds=hole_.blastTime()+30;c.observer=o;
                expect(ledger_.execute(u_,*economy_,player_,c).starts_with("MINING HEAD FITTED"),"Native mining-head fit failed");
                const auto f=cavernFinds(hole_)[miningFind_];const auto point=cavernFindPoint(hole_,f);const auto up=glm::normalize(hole_.entry);
                o.position=hole_.entry-up*hole_.finalDepth();o.attitude=glm::quatLookAtRH(glm::normalize(point-o.position),tangentFrame(up)*glm::dvec3(0,0,-1));
                c=command(EconomicAction::ScanCavern);c.contract=hole_.id;c.observer=o;
                expect(ledger_.execute(u_,*economy_,player_,c).starts_with("CAVERN OVERVIEW"),"Native mining overview failed");
                o.size=.0005;o.position=point+tangentFrame(up)*f.inward*.5;o.attitude=glm::quatLookAtRH(glm::normalize(point-o.position),tangentFrame(up)*glm::dvec3(0,0,-1));
                c=command(EconomicAction::ScanCavern);c.contract=hole_.id;c.observer=o;
                expect(ledger_.execute(u_,*economy_,player_,c).starts_with("CLOSE ASSAY"),"Native close mining assay failed");
                c=command(EconomicAction::MarkCavern);c.contract=hole_.id;c.observer=o;
                expect(ledger_.execute(u_,*economy_,player_,c).starts_with("CAVERN MATERIAL MARKED"),"Native mining mark failed");
            }
            if(stage_==17||stage_==18){
                const auto f=cavernFinds(hole_)[miningFind_];const auto up=glm::normalize(hole_.entry);const auto pool=planet_.address.child(Kind::Surface,encodeTile(tileAt(up,7)));
                const auto total=prospectResources(u_,pool)[f.resource];
                for(unsigned n=0;n<300;++n){
                    int64_t taken=0;for(const auto& p:ledger_.state().prospects)if(p.address==pool)taken=p.extracted[f.resource];
                    if(taken>=int64_t(std::ceil(total*(stage_==17?.5:1.))))break;
                    auto c=command(EconomicAction::ExtractCavern);c.contract=hole_.id;
                    SurveyObserver o;o.reference=planet_.address;o.position=hole_.entry-up*hole_.finalDepth();o.seconds=hole_.blastTime()+30;
                    o.attitude=glm::quatLookAtRH(glm::normalize(cavernFindPoint(hole_,f)-o.position),tangentFrame(up)*glm::dvec3(0,0,-1));c.observer=o;
                    expect(ledger_.execute(u_,*economy_,player_,c).starts_with("CAVERN EXTRACTED"),"Native finite extraction failed");
                }
            }
        }
        hole_=ledger_.state().probes.holes.back();
        if(stage_==19)hole_=miningReplay_->exploration.probes.holes.back();
        if(stage_>=12&&stage_<=15){std::string status;
            if(stage_==12||stage_==13||stage_==15)
                expect(controlProbe(hole_,ProbeControl::Pause,0,hole_.impactTime()+2,status),"Native drill pause failed");
            if(stage_==13){
                expect(controlProbe(hole_,ProbeControl::Depth,110,hole_.impactTime()+6,status),"Native depth control failed");
                expect(controlProbe(hole_,ProbeControl::Rate,.65,hole_.impactTime()+6,status),"Native feed control failed");
                expect(controlProbe(hole_,ProbeControl::Resume,0,hole_.impactTime()+6,status),"Native resume failed");
            }
            if(stage_==14)expect(controlProbe(hole_,ProbeControl::Detonate,0,hole_.impactTime()+hole_.drillSeconds*.6,status),"Native early charge failed");
            if(stage_==15)expect(controlProbe(hole_,ProbeControl::Retract,0,hole_.impactTime()+6,status),"Native safe abort failed");
        }
        state_.holes[0]=hole_;
        frameProbeCamera(camera_,u_,hole_,clock());
        auto s=std::make_shared<EconomySnapshot>();s->network=network_;s->sites=economy_->sites();
        s->player=player_;s->exploration=ledger_.state();
        s->exploration.probes=state_;
        if(stage_==17)miningReplay_=std::make_shared<EconomySnapshot>(*s);
        if(stage_==19)s=miningReplay_;
        market_=economicMarketView(s.get(),network_->sites[0].id,3,10,!movie_&&stage_==0,MarketPage::Survey);
        market_.open=!movie_&&stage_==0;renderer_.setEconomy(s,&market_);renderer_.setActivity(nullptr);
        pilot_.setExcavations(std::make_shared<MiningProbeState>(state_),clock());
        if(stage_==6){const auto up=glm::normalize(hole_.entry);const auto side=tangentFrame(up)*glm::dvec3(1,0,0);
            const auto eye=hole_.entry+up*25.+side*25.;placeShip(eye,hole_.entry-up*5.-eye);}
        if(stage_==7){const auto up=glm::normalize(hole_.entry);placeShip(hole_.entry-up*hole_.depth,
            -up+tangentFrame(up)*glm::dvec3(.8,0,0));pilot_.update({},.03);
            expect(pilot_.ship().hull>99,"Native ship damaged in empty cavern");}
        if(stage_==8){const auto density=std::max(1.,double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_,int(600/density),int(1000/density));SDL_SyncWindow(window_);
            int w,h;SDL_GetWindowSizeInPixels(window_,&w,&h);renderer_.resize(w,h);}
        if(stage_==9){
            const auto density=std::max(1.,double(SDL_GetWindowPixelDensity(window_)));
            SDL_SetWindowSize(window_,int(1440/density),int(900/density));SDL_SyncWindow(window_);
            int w,h;SDL_GetWindowSizeInPixels(window_,&w,&h);renderer_.resize(w,h);
            const auto up=glm::normalize(hole_.entry);placeShip(hole_.entry-up*hole_.depth,up);
        }
        if(stage_==10){const auto up=glm::normalize(hole_.entry);placeShip(hole_.entry+up*8.,-up);}
        if(stage_==11){const auto find=cavernFinds(hole_)[2];const auto point=cavernFindPoint(hole_,find);
            const auto eye=point+tangentFrame(glm::normalize(hole_.entry))*
                (find.inward*5.+tangentFrame(find.inward)*glm::dvec3(3,0,0));
            expect(boreClearance(hole_,eye,clock())>3,"Crystal viewing position is not safely inside the real chamber");
            placeShip(eye,point-eye);pilot_.update({},.03);
            expect(pilot_.ship().hull>99,"Crystal inspection damaged a safely positioned full-size ship");}
        if(stage_>=16){const auto find=cavernFinds(hole_)[miningFind_];const auto point=cavernFindPoint(hole_,find);
            const auto up=glm::normalize(hole_.entry);auto eye=point+tangentFrame(up)*find.inward*6.;auto focus=point;
            if(stage_==18){focus=hole_.entry-up*(hole_.finalDepth()+boreRoomRadius(hole_,{0,-1,0}));eye=focus+up*6.;}
            placeShip(eye,focus-eye);
            pilot_.update({},.03);expect(pilot_.ship().hull>99,"Mining presentation moved the collision boundary");}
    }
  public:
    MiningProbeValidation(const Universe& u,Navigation& ship,Pilot& pilot,Renderer& renderer,SDL_Window* window,bool movie=false,bool reference=false,bool benchmark=false)
        :u_(u),ship_(ship),pilot_(pilot),renderer_(renderer),window_(window),camera_(u),movie_(movie),reference_(reference),benchmark_(benchmark){
        planet_=u_.describe(Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0));
        fingerprint_=descriptorFingerprint(planet_);
        if(!movie_)renderer_.setCockpitValidation(true);
        auto direction=glm::normalize(u_.relative(planet_.address.parent(),planet_.address,0).position);
        glm::dvec3 entry{};bool found=false;
        for(unsigned k=0;k<20;++k){auto d=glm::normalize(direction+tangentFrame(direction)*
                glm::dvec3(std::sin(k*2.4)*.1,0,std::cos(k*2.4)*.1));
            const double h=u_.terrainHeight(planet_,d),r=std::pow(10.,planet_.frame.logRadius);
            if(planet_.geology->liquidWater>.1&&h/r<-.0008)continue;
            entry=d*(r+h);found=true;break;}
        expect(found,"Native probe found no solid land");
        network_=std::make_shared<EconomicNetwork>();network_->system=planet_.address.parent();
        network_->universeSeed=u_.seed();network_->fingerprint=580058;
        EconomicSite port;port.id={planet_.address,0};port.orbital=true;port.radius=100;
        port.metres=entry+glm::normalize(entry)*30000.;port.capacityKg=2000000;port.initial.fill(1000);network_->sites.push_back(port);
        economy_=std::make_unique<Economy>(network_);
        EconomicCommand c;c.sequence=1;c.action=EconomicAction::FitProbeLauncher;c.weaponMount=0;c.port=port.id;c.limitCents=100000000;
        SurveyObserver o;o.reference=planet_.address;o.position=port.metres;o.seconds=100;c.observer=o;
        expect(ledger_.execute(u_,*economy_,player_,c).starts_with("PROBE LAUNCHER FITTED"),"Native turret-to-probe refit failed");
        c.sequence=2;c.action=EconomicAction::LaunchProbe;c.surveyTarget=planet_.address;
        o.position=entry+glm::normalize(entry)*(movie_?8000.:500.);c.observer=o;
        WeaponRay ray;ray.reference=o.reference;ray.origin=o.position;ray.direction=-glm::normalize(entry);ray.seconds=100;
        c.probeRay=ray;c.serviceUnits=100;
        expect(ledger_.execute(u_,*economy_,player_,c).starts_with("PROBE LAUNCHED"),"Native mining probe launch failed");
        state_=ledger_.state().probes;hole_=state_.holes.back();
        movieFrames_=unsigned(std::ceil((hole_.blastTime()-hole_.started+7.)*30));
        placeShip(hole_.launch,glm::normalize(hole_.entry-hole_.launch));
        shipBefore_=ship_.serialize();pilotBefore_=pilot_.serialize();configure();
    }
    double clock() const {
        if(stage_>=16)return hole_.blastTime()+30;
        if(movie_)return hole_.started+std::min(movieFrame_,movieFrames_-1)/30.;
        if(stage_==12||stage_==13)return hole_.impactTime()+8;
        if(stage_==14)return hole_.blastTime()+.08;
        if(stage_==15)return hole_.impactTime()+12;
        switch(stage_){case 0:return hole_.started;case 1:return hole_.started+hole_.flightSeconds*.4;
          case 2:return hole_.impactTime()+hole_.drillSeconds*.5;case 3:return hole_.blastTime()-.5;
          case 4:return hole_.blastTime()+.08;default:return hole_.blastTime()+2.;}
    }
    bool remote() const{return movie_||(stage_>=1&&stage_<=5)||stage_==8||(stage_>=12&&stage_<=15);}
    const Navigation& view() const{return remote()?camera_:ship_;}
    void advance(){++age_;if(movie_)frameProbeCamera(camera_,u_,hole_,clock());renderer_.setProbeView(remote(),clock());
        if(stage_>=16){miningWeapons_.advance(.03);renderer_.setWeapons(&miningWeapons_);
            if(stage_==19){const auto f=cavernFinds(hole_)[miningFind_];WeaponRay ray;ray.reference=planet_.address;
                ray.origin=hole_.entry-glm::normalize(hole_.entry)*hole_.finalDepth();ray.seconds=clock();miningWeapons_.utilityBeam(ray,cavernFindPoint(hole_,f),1);}}}
    void annotate(FlightInstruments& t){
        if(remote()){
            annotateDrillCamera(t,hole_,clock(),material(planet_.environment.mineral).density);
        }else{t.destination="MINING PROBE EXCAVATION";t.range=0;t.targetVisible=false;t.explorationActive=true;
            t.exploration={"MINING PROBE / EXCAVATION",stage_==7?"INSIDE THE REAL CAVERN / COLLISION ACTIVE":"REFIT / DEPLOY / BORING / BURIED CHARGE",
                           "PLANET GENERATION AND SEEDS RETAINED","CTRL-P LAUNCH / ALT-P CAMERA / CTRL-J ENTRY"};
            if(stage_==11)annotateCavern(t,u_,ship_,hole_,2);}
        if(stage_>=16)annotateCavern(t,u_,ship_,hole_,miningFind_);
    }
    void observe(const FrameStats& stats,RegionCache& cache,const std::string& directory,double wallMs){
        if(finished())return;
        if(movie_){
            cache.requestBore(planet_,hole_,false);cache.requestBore(planet_,hole_,true);
            if(movieFrame_>=movieFrames_){
                if(++drain_==8){
                    if(benchmark_){
                        expect(descriptorFingerprint(u_.describe(planet_.address))==fingerprint_,"Benchmark changed planet identity");
                        auto quantile=[](std::vector<double> v,double q){std::sort(v.begin(),v.end());return v[std::min(v.size()-1,size_t(v.size()*q))];};
                        std::cout<<"PROBE_PERF frames="<<perfWall_.size()<<" median_ms="<<quantile(perfWall_,.5)<<" p95_ms="<<quantile(perfWall_,.95)
                            <<" cpu_median_ms="<<quantile(perfCpu_,.5)<<" probe_cpu_median_ms="<<quantile(perfProbe_,.5)
                            <<" upload_bytes="<<perfUploads_<<" light_updates="<<perfLightUpdates_<<" hidden_frames="<<perfHidden_
                            <<" bore="<<hole_.id<<" descriptor="<<fingerprint_<<'\n';
                    }
                    std::cout<<"PASS: native probe "<<(benchmark_?"benchmark":"POV recording")<<" / "<<movieFrames_<<" frames / 30 FPS\n";
                }
                return;
            }
            if(!movieStarted_){
                const auto jobs=cache.stats();
                stable_=stats.visibleObjects&&!jobs.pending&&!stats.lodTransitions&&!stats.uploadDeferred?stable_+1:0;
                if(age_>2200)throw std::runtime_error("Probe video failed to warm its starting scene");
                if(stable_<10)return;
                movieStarted_=true;
            }
            expect(ship_.serialize()==shipBefore_&&pilot_.serialize()==pilotBefore_,"POV recording moved real ship");
            if(benchmark_){
                perfWall_.push_back(wallMs);perfCpu_.push_back(stats.cpuMs);perfProbe_.push_back(stats.probeCpuMs);
                perfUploads_+=stats.uploadBytes;perfLightUpdates_+=stats.probeLightUploads;perfHidden_+=stats.hiddenExterior;
            }else{
                const auto root=std::filesystem::path(directory);std::filesystem::create_directories(root);
                std::ostringstream file;file<<"frame-"<<std::setfill('0')<<std::setw(6)<<movieFrame_<<".ppm";
                renderer_.screenshot((root/file.str()).string());
            }
            if(movieFrame_%30==0)std::cout<<"PROBE_VIDEO "<<movieFrame_<<" seconds="<<clock()-hole_.started
                <<" depth="<<probePose(hole_,clock()).depth<<" phase="<<probePhaseName(probePose(hole_,clock()).phase)<<'\n';
            ++movieFrame_;return;
        }
        const auto jobs=cache.stats();const bool settled=stats.visibleObjects&&!jobs.pending&&!stats.lodTransitions&&!stats.uploadDeferred&&stats.triangles==triangles_;
        triangles_=stats.triangles;stable_=settled?stable_+1:0;
        if(age_>2200)throw std::runtime_error("Native probe scene failed to settle / "+std::to_string(stage_));
        if(!captured_&&stable_>=10){
            expect(descriptorFingerprint(u_.describe(planet_.address))==fingerprint_,"Native probe changed planet seed or identity");
            if(stage_>=1&&stage_<=5){expect(ship_.serialize()==shipBefore_,"Probe camera moved actual ship navigation");expect(pilot_.serialize()==pilotBefore_,"Probe camera changed actual ship state");}
            if(stage_>=2&&stage_<=7)expect(stats.cavernMeshes>0,"No physical bore or cavern submitted");
            if(remote())expect(stats.probeRigTriangles>0,"Probe feed lacks physical camera/drill hardware");
            if(stage_>=2&&stage_<=7)expect(stats.probeLitVertices>0,"No local work-light shading submitted");
            if(stage_==2||stage_==4||stage_==5)expect(stats.probeDustParticles>0,"Drill/blast dust missing");
            if(stage_==4)expect(stats.probeFlash>0,"Local detonation flash missing");
            if(stage_==11)expect(stats.cavernMeshes>0&&stats.probeLitVertices>0,"Native crystal geology not submitted");
            if(stage_==12)expect(probePose(hole_,clock()).phase==ProbePhase::Paused&&stats.probeDustParticles==0,"Paused drill still cuts material");
            if(stage_==13)expect(probePose(hole_,clock()).phase==ProbePhase::Boring&&stats.probeDustParticles>0,"Resumed drill did not follow new depth/feed");
            if(stage_==14)expect(stats.probeFlash>0&&hole_.finalDepth()<hole_.depth,"Early blast did not use its actual depth");
            if(stage_==15)expect(probePose(hole_,clock()).phase==ProbePhase::Recovered&&stats.probeFlash==0,"Aborted drill exploded");
            if(stage_==16)miningTriangles_=stats.cavernTriangles;
            if(stage_>=17){expect(stats.cavernTriangles==miningTriangles_,"Depletion replaced the underlying cavern geometry");
                expect(stats.miningWearVertices>0&&stats.miningTailings>0,"Saved depletion has no visible wear or persistent cuttings");}
            if(stage_==19)expect(stats.miningParticles>0,"Active cutter lacks material-specific particles");
            if(stage_==6)expect(stats.cutTerrainTiles>0,"Cavern entrance did not cut actual terrain");
            if(stage_==9||stage_==10){
                expect(!stats.hiddenExterior,"Shaft exit or exterior approach was incorrectly occluded");
                expect(stats.cutTerrainTiles>0,"Entrance terrain was not kept warm while looking underground");
            }
            if(!reference_&&((stage_>=2&&stage_<=5)||stage_==7||stage_==8)){
                expect(stats.hiddenExterior,"Covered underground view did not cull exterior rendering");
                if(remote())expect(stats.probeLightUploads==0,"Frozen work light was unnecessarily uploaded again");
            }
            const auto root=std::filesystem::path(directory);std::filesystem::create_directories(root);
            renderer_.screenshot((root/("probe-"+std::to_string(stage_)+".ppm")).string());
            std::cout<<"PROBE_CAPTURE "<<stage_<<" id="<<hole_.id<<" depth="<<probePose(hole_,clock()).depth
                     <<" meshes="<<stats.cavernMeshes<<" cut_tiles="<<stats.cutTerrainTiles
                     <<" rig="<<stats.probeRigTriangles<<" lit="<<stats.probeLitVertices
                     <<" dust="<<stats.probeDustParticles<<" flash="<<stats.probeFlash
                     <<" hidden="<<stats.hiddenExterior<<" light_uploads="<<stats.probeLightUploads<<" text_overlap=0"
                     <<" mining_wear="<<stats.miningWearVertices<<" tailings="<<stats.miningTailings<<" cutting_particles="<<stats.miningParticles<<'\n';
            captured_=true;age_=0;
        }else if(captured_&&age_>=8){++stage_;
            if(finished()){ExplorationLedger restored;expect(restored.restore(ledger_.serialize(),u_),"Native probe/cavern restart failed");
                std::cout<<"PASS: native mining probe / turret refit / independent first-person camera / real excavated cavern\n";return;}
            captured_=false;age_=stable_=0;configure();}
    }
    bool finished() const{return movie_?movieFrame_==movieFrames_&&drain_>=8:stage_==20;}
};
} // namespace infinity
