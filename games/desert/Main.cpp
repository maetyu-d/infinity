#include "Audio.hpp"
#include "Combat.hpp"
#include <numbers>
#include "Desert.hpp"
#include "Renderer.hpp"
#include "FramePacer.hpp"
#include "WindowSizing.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
using namespace desert;
struct Options {
    uint64_t seed=20261101;
    int pickupDemo=0,frames=0,width=1024,height=640,laps=3;double duration=180;RaceFormat format=RaceFormat::Circuit;
    bool turretView=false,weaponsDemo=false,displayCheck=false,gunnerDemo=false,benchmark=false,uncapped=false,crt=true,demo=false,mute=false,retina=false,exhibit=false,legacy=false,orbit=false,hires=true,cpu=false,chase=false,damageDemo=false,vista=false,trackStudy=false;
    int debug=0;
    std::string capture,data;
};
static Options options(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        std::string arg=argv[i];
        auto value=[&]() {if(i+1==argc) throw std::runtime_error("Missing value: "+arg);return std::string(argv[++i]);};
        if(arg=="--seed") o.seed=std::stoull(value());
        else if(arg=="--frames") o.frames=std::max(1,std::stoi(value()));
        else if(arg=="--width") o.width=std::clamp(std::stoi(value()),800,3840);
        else if(arg=="--height") o.height=std::clamp(std::stoi(value()),500,2160);
        else if(arg=="--capture") o.capture=value();
        else if(arg=="--data") o.data=value();
        else if(arg=="--laps")o.laps=std::clamp(std::stoi(value()),2,12);
        else if(arg=="--duration")o.duration=std::clamp(std::stod(value()),30.,3600.);
        else if(arg=="--circuit")o.format=RaceFormat::Circuit;
        else if(arg=="--endless")o.format=RaceFormat::Endless;
        else if(arg=="--stage")o.format=RaceFormat::Stage;
        else if(arg=="--pickup-demo") {o.pickupDemo=std::clamp(std::stoi(value()),1,2);o.weaponsDemo=true;o.demo=true;}
        else if(arg=="--weapons-demo") {o.weaponsDemo=true;o.demo=true;}
        else if(arg=="--display-check") {o.displayCheck=true;o.demo=true;o.mute=true;}
        else if(arg=="--turret-view")o.turretView=true;
        else if(arg=="--gunner-demo") {o.gunnerDemo=true;o.demo=true;}
        else if(arg=="--demo") o.demo=true;
        else if(arg=="--mute") o.mute=true;
        else if(arg=="--retina") o.retina=true;
        else if(arg=="--benchmark")o.benchmark=true;
        else if(arg=="--uncapped")o.uncapped=true;
        else if(arg=="--sharp") o.crt=false;
        else if(arg=="--fractal-study" || arg=="--splat-study" || arg=="--ellipsoid-demo") o.exhibit=true;
        else if(arg=="--fractal-vista") {o.exhibit=true;o.vista=true;}
        else if(arg=="--legacy-graphics") o.cpu=true;
        else if(arg=="--lut") o.cpu=true;
        else if(arg=="--orbit") o.orbit=true;
        else if(arg=="--hires") o.hires=true;
        else if(arg=="--retro") o.hires=false;
        else if(arg=="--cpu-renderer") o.cpu=true;
        else if(arg=="--chase") o.chase=true;
        else if(arg=="--damage-demo") {o.damageDemo=true;o.demo=true;o.format=RaceFormat::Stage;}
        else if(arg=="--track-study") {o.trackStudy=true;o.exhibit=true;}
        else if(arg=="--fractal-debug" || arg=="--splat-debug" || arg=="--ellipsoid-debug") o.debug=std::clamp(std::stoi(value()),0,2);
        else if(arg=="--help") {
            std::cout<<"Dune Breaker / Infinity engine\n--circuit --endless --laps N --duration SECONDS --seed N --demo --gunner-demo --turret-view --weapons-demo --pickup-demo 1|2 --display-check --frames N --capture image.ppm --mute --retina --width N --height N --data DIR\n--chase --sharp --uncapped --benchmark --retro --cpu-renderer --damage-demo --track-study --fractal-study --fractal-vista --orbit --hires --fractal-debug 0|1|2 --legacy-graphics\n";std::exit(0);
        } else throw std::runtime_error("Unknown argument: "+arg);
    }
    if(o.displayCheck&&o.frames==0)o.frames=360;
    return o;
}
int main(int argc,char** argv) try {
    auto o=options(argc,argv);
    SDL_SetAppMetadata("Dune Breaker","4.21.3","org.infinite.dunebreaker");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)) throw std::runtime_error(SDL_GetError());
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    SDL_Window* window=SDL_CreateWindow("DUNE BREAKER | Infinity Motorsport",o.width,o.height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(o.benchmark?SDL_WINDOW_ALWAYS_ON_TOP:0));
    if(!window) throw std::runtime_error(SDL_GetError());
    infinity::fitWindowToDisplay(window,true);
    SDL_SetWindowMinimumSize(window,800,500);
    Race race(o.seed,o.format,o.laps,o.duration);race.combatEnabled=true;
    std::string data=o.data;
    if(data.empty()) {char* pref=SDL_GetPrefPath("Infinite","DuneBreaker");if(pref) {data=pref;SDL_free(pref);}}
    double best=0;bool recordFailure=false;
    auto bestFile=[&]() { return fs::path(data)/race.recordKey(); };
    auto loadBest=[&]() {best=0;recordFailure=false;if(!data.empty()) {std::ifstream f(bestFile());double v=0;if(f>>v && std::isfinite(v) && v>0 && v<(race.distanceRecord()?1e9:86400)) best=v;}};
    loadBest();
    {
        Renderer renderer(window);Audio audio;FramePacer framePacer(window);
        renderer.crtEffects=o.crt;renderer.vista=o.vista;renderer.chase=o.chase;renderer.retinaTerrain=o.retina;renderer.gpuEnabled=!o.cpu;renderer.exhibit=o.exhibit;renderer.fractalGraphics=!o.legacy;renderer.orbit=o.orbit;renderer.highResolution=o.hires;
        renderer.fractalDebug=o.debug;renderer.tacticalGunner=!o.turretView;
        if(o.trackStudy) {
            race.start();race.state=State::Racing;
            for(int i=0;i<120*12;++i){race.recoverAutopilot();race.step(race.autopilot(),1./120.);}
            race.pause();renderer.orbitYaw=race.player.yaw;renderer.orbitPitch=.32;renderer.orbitDistance=10.;
        }
        bool fireHeld=false,cutHeld=false;
        bool running=true,muted=o.mute,help=false,drag=false,wasFinished=false,resumeAfterHelp=false,userPaused=false;
        auto releaseLook=[&](){drag=false;fireHeld=cutHeld=false;SDL_SetWindowRelativeMouseMode(window,false);};
        auto closeControls=[&](){help=false;if(resumeAfterHelp&&race.state==State::Paused)race.pause();resumeAfterHelp=false;};
        auto showControls=[&](){
            releaseLook();if(help){closeControls();return;}
            resumeAfterHelp=!renderer.exhibit&&(race.state==State::Racing||race.state==State::Countdown||race.state==State::Finished);
            if(resumeAfterHelp)race.pause();help=true;
        };
        auto performAction=[&](UiAction action){
            switch(action){
                case UiAction::Continue:
                    releaseLook();if(help){closeControls();break;}if(renderer.exhibit){renderer.exhibit=false;break;}
                    if(race.state==State::Finished){race.advance();loadBest();}else if(race.state==State::Menu)race.start();else if(race.state==State::Paused)race.pause();userPaused=false;break;
                case UiAction::Pause:releaseLook();if(help)closeControls();race.pause();userPaused=race.state==State::Paused;break;
                case UiAction::Retry:releaseLook();help=resumeAfterHelp=userPaused=false;race.start();break;
                case UiAction::NewTerrain:releaseLook();help=resumeAfterHelp=userPaused=false;race.regenerate();loadBest();break;
                case UiAction::Controls:showControls();break;
                case UiAction::CloseControls:releaseLook();closeControls();break;
                case UiAction::FreeRoam:releaseLook();if(race.state==State::Finished)race.resultsVisible=!race.resultsVisible;break;
                case UiAction::Audio:muted=!muted;break;
                case UiAction::Fullscreen:SDL_SetWindowFullscreen(window,!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));break;
                case UiAction::Quit:running=false;break;
                default:break;
            }
            if(action==UiAction::Continue||action==UiAction::Retry||action==UiAction::NewTerrain)renderer.resetGunnerAim(race);
        };
        if(o.demo) {race.start();race.countdown=0;race.state=State::Racing;if(o.weaponsDemo){double s=o.pickupDemo?45.+(o.pickupDemo-1)*110.-8:30;race.player.reset(race.course,s);race.nextGate=int(s/race.course.gateInterval())+1;}}
        if(o.damageDemo) {
            auto obstacles=race.course.obstaclesNear({race.course.center(350)+90,350});
            for(const auto& obstacle:obstacles) if(!obstacle.soft&&obstacle.height>1.15&&obstacle.radius<3) {
                double z=obstacle.center.y-17;
                race.player.reset(race.course,z,obstacle.center.x-race.course.center(z));race.player.yaw=0;
                race.player.velocity={0,37};race.player.speed=37;race.nextGate=std::clamp(int(z/gateSpacing)+1,1,gateCount);break;
            }
        }
        renderer.resetGunnerAim(race);
        double accumulator=0,visualTime=0;
        auto previous=std::chrono::steady_clock::now();
        int frame=0;double fpsTime=0;int fpsFrames=0;
        double physicsSeconds=0,renderSeconds=0,warmStarted=0;std::vector<double> frameTimes;
        auto runStarted=std::chrono::steady_clock::now();
        double pacingWait=0,lastPhysicsMs=0,lastRenderMs=0;int hitchReports=0;
        while(running) {
            auto now=std::chrono::steady_clock::now();
            double wallDt=std::max(0.,std::chrono::duration<double>(now-previous).count());previous=now;
            if(o.frames>0&&frame>=180){frameTimes.push_back(wallDt*1000.);
                if(wallDt>.020&&hitchReports++<12)std::cerr<<"FRAME_HITCH frame="<<frame<<" wall_ms="<<wallDt*1000<<" physics_ms="<<lastPhysicsMs<<" render_ms="<<lastRenderMs<<" pacing_ms="<<pacingWait*1000<<'\n';
            }
            renderer.frameWorkSeconds=std::max(.001,wallDt-pacingWait);
            double dt=std::min(wallDt,.25);
            if((o.demo || o.exhibit) && o.frames>0) dt=1./60.;
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_EVENT_QUIT || event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) running=false;
                if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
                    if(race.state==State::Racing || race.state==State::Countdown) {race.pause();userPaused=false;}
                    releaseLook();
                }
                if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT){
                    auto action=renderer.actionAt(event.button.x,event.button.y);if(action!=UiAction::None){performAction(action);continue;}
                }
                bool panel=help||(!renderer.exhibit&&(race.state==State::Menu||race.state==State::Paused||(race.state==State::Finished&&race.resultsVisible)));
                if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&!panel) {
                    if(renderer.exhibit){drag=true;SDL_SetWindowRelativeMouseMode(window,true);}
                    else if(event.button.button==SDL_BUTTON_LEFT)fireHeld=true;
                    else if(event.button.button==SDL_BUTTON_RIGHT)cutHeld=true;
                }
                if(event.type==SDL_EVENT_MOUSE_BUTTON_UP){if(event.button.button==SDL_BUTTON_LEFT)fireHeld=false;if(event.button.button==SDL_BUTTON_RIGHT)cutHeld=false;if(renderer.exhibit){drag=false;SDL_SetWindowRelativeMouseMode(window,false);}}
                if(event.type==SDL_EVENT_MOUSE_MOTION&&!panel) {
                    if(renderer.exhibit&&drag&&renderer.orbit) {
                        renderer.orbitYaw-=double(event.motion.xrel)*.006;
                        renderer.orbitPitch=std::clamp(renderer.orbitPitch+double(event.motion.yrel)*.004,-.05,1.1);
                    }else if(!renderer.exhibit&&SDL_GetWindowRelativeMouseMode(window)) {
                        if(renderer.tacticalGunner)renderer.moveGunnerCursor(event.motion.xrel,event.motion.yrel);
                        else {race.player.turret.yaw=std::remainder(race.player.turret.yaw+double(event.motion.xrel)*.0028,2*std::numbers::pi);
                        race.player.turret.pitch=std::clamp(race.player.turret.pitch-double(event.motion.yrel)*.0028,-.4,.70);}
                    }
                }
                if(event.type==SDL_EVENT_MOUSE_WHEEL&&!renderer.exhibit&&!panel&&event.wheel.y!=0)race.selectWeapon(0,event.wheel.y<0?1:-1);
                if(event.type==SDL_EVENT_MOUSE_WHEEL && renderer.exhibit)
                    renderer.orbitDistance=std::clamp(renderer.orbitDistance-double(event.wheel.y)*.6,4.,18.);
                if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) switch(event.key.scancode) {
                    case SDL_SCANCODE_RETURN:performAction(UiAction::Continue);break;
                    case SDL_SCANCODE_ESCAPE:
                        if(help)closeControls();else if(renderer.exhibit)renderer.exhibit=false;else if(race.state==State::Menu)running=false;else performAction(UiAction::Pause);
                        releaseLook();break;
                    case SDL_SCANCODE_G:if(!renderer.exhibit){releaseLook();renderer.tacticalGunner=!renderer.tacticalGunner;renderer.resetGunnerAim(race);}break;
                    case SDL_SCANCODE_C:if(!renderer.exhibit) renderer.chase=!renderer.chase;break;
                    case SDL_SCANCODE_F2:releaseLook();if(help)closeControls();renderer.vista=false;renderer.exhibit=!renderer.exhibit;renderer.fractalGraphics=true;help=false;break;
                    case SDL_SCANCODE_F7:releaseLook();if(help)closeControls();renderer.vista=!renderer.vista||!renderer.exhibit;renderer.exhibit=renderer.vista;help=false;break;
                    case SDL_SCANCODE_F3:renderer.gpuEnabled=!renderer.gpuEnabled;break;
                    case SDL_SCANCODE_F4:renderer.orbit=!renderer.orbit;break;
                    case SDL_SCANCODE_F8:renderer.crtEffects=!renderer.crtEffects;break;
                    case SDL_SCANCODE_F5:renderer.highResolution=!renderer.highResolution;break;
                    case SDL_SCANCODE_F6:renderer.fractalDebug=(renderer.fractalDebug+1)%3;break;
                    case SDL_SCANCODE_W:if(renderer.exhibit) renderer.walking=!renderer.walking;break;
                    case SDL_SCANCODE_P:performAction(UiAction::Pause);break;
                    case SDL_SCANCODE_TAB:if(race.state==State::Finished) race.resultsVisible=!race.resultsVisible;break;
                    case SDL_SCANCODE_R:
                        if(race.state==State::Paused || race.state==State::Finished) performAction(UiAction::Retry);
                        else if(race.state==State::Racing) race.recover();
                        break;
                    case SDL_SCANCODE_N:performAction(UiAction::NewTerrain);break;
                    case SDL_SCANCODE_M:muted=!muted;break;
                    case SDL_SCANCODE_H:case SDL_SCANCODE_F1:showControls();break;
                    case SDL_SCANCODE_F11:SDL_SetWindowFullscreen(window,!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN));break;
                    case SDL_SCANCODE_Q:if(renderer.exhibit || race.state==State::Paused || race.state==State::Menu || race.state==State::Finished) running=false;break;
                    case SDL_SCANCODE_F12: {
                        fs::path capturePath=fs::path(data)/"screenshots"/("desert-"+std::to_string(SDL_GetTicks())+".ppm");
                        fs::create_directories(capturePath.parent_path());renderer.capture(capturePath.string());break;
                    }
                    default:break;
                }
            }
            bool captureMouse=o.frames==0&&!renderer.exhibit&&!help&&(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)&&
                (race.state==State::Racing||race.state==State::Countdown||(race.state==State::Finished&&!race.resultsVisible));
            if(!renderer.exhibit&&SDL_GetWindowRelativeMouseMode(window)!=captureMouse){SDL_SetWindowRelativeMouseMode(window,captureMouse);if(!captureMouse)fireHeld=cutHeld=false;}
            if(!drag) {renderer.lookYaw*=std::exp(-dt*8);renderer.lookPitch*=std::exp(-dt*8);}
            const bool* keys=SDL_GetKeyboardState(nullptr);
            Input input;
            input.throttle=(keys[SDL_SCANCODE_W]||keys[SDL_SCANCODE_UP]?1.:0.)-(keys[SDL_SCANCODE_S]||keys[SDL_SCANCODE_DOWN]?1.:0.);
            input.steer=(keys[SDL_SCANCODE_D]||keys[SDL_SCANCODE_RIGHT]?1.:0.)-(keys[SDL_SCANCODE_A]||keys[SDL_SCANCODE_LEFT]?1.:0.);
            input.repair=keys[SDL_SCANCODE_F];input.fire=fireHeld;input.cut=cutHeld;
            input.brake=keys[SDL_SCANCODE_SPACE];input.boost=keys[SDL_SCANCODE_LSHIFT]||keys[SDL_SCANCODE_RSHIFT];
            if(o.demo) {
                if(!renderer.exhibit&&!o.damageDemo)race.recoverAutopilot();
                bool repairHeld=input.repair;
                input=o.damageDemo?Input{race.player.impactSerial?0.:1.,0,race.player.impactSerial>0,false,false}:race.autopilot();
                input.repair=repairHeld;input.fire=fireHeld;input.cut=cutHeld;
                if(race.state==State::Paused&&!userPaused&&!help) race.pause();
            }
            auto pointGunner=[&](glm::dvec3 point){if(renderer.tacticalGunner)renderer.pointGunnerAt(race,point);else race.aimGunnerAt(point,dt);};
            if(o.gunnerDemo){
                int target=0;double nearest=1e20;for(int i=0;i<3;++i){double d=glm::length(race.rivals[i].position-race.player.position);if(d<nearest){nearest=d;target=i;}}
                pointGunner(race.rivals[target].position+race.rivals[target].orientation()*glm::dvec3(0,.08,0));input.fire=true;
            }
            if(o.weaponsDemo){
                auto& gun=race.player.turret;Weapon desired=gun.ammo[2]>0?Weapon::DuneJack:gun.ammo[1]>0?Weapon::RutStitcher:Weapon::TowHarpoon;
                if(gun.weapon!=desired){gun.weapon=desired;gun.cable={};}
                if(desired!=Weapon::TowHarpoon){auto point=race.player.position+race.player.orientation()*glm::dvec3(0,0,13);point.y=race.course.height(point.x,point.z);pointGunner(point);input.fire=true;}
                else {int target=0;double near=1e20;for(int i=0;i<3;++i){double d=glm::length(race.rivals[i].position-race.player.position);if(d<near){near=d;target=i;}}pointGunner(race.rivals[target].position);input.fire=race.rutsFired>0&&race.jacksRaised>0;}
            }
            if(renderer.tacticalGunner&&!renderer.exhibit&&!help&&(race.state==State::Racing||race.state==State::Countdown||(race.state==State::Finished&&!race.resultsVisible))){
                bool ready=renderer.updateGunnerAim(race,dt);if(!ready&&!race.player.turret.cable.active())input.fire=false;
            }
            auto physicsStarted=std::chrono::steady_clock::now();
            accumulator=std::min(accumulator+dt,32*infinity::buggy::step);
            while(accumulator>=infinity::buggy::step) {if(!renderer.exhibit) race.step(input,infinity::buggy::step);accumulator-=infinity::buggy::step;}
            lastPhysicsMs=1000.*std::chrono::duration<double>(std::chrono::steady_clock::now()-physicsStarted).count();physicsSeconds+=lastPhysicsMs/1000.;
            if(renderer.exhibit && renderer.orbit) {
                renderer.orbitYaw+=dt*((keys[SDL_SCANCODE_RIGHT]?1.:0.)-(keys[SDL_SCANCODE_LEFT]?1.:0.));
                renderer.orbitPitch=std::clamp(renderer.orbitPitch+dt*((keys[SDL_SCANCODE_DOWN]?1.:0.)-(keys[SDL_SCANCODE_UP]?1.:0.)),-.05,1.1);
            }
            visualTime+=dt;
            if(race.state==State::Finished && !wasFinished) {
                double score=race.distanceRecord()?race.distance():race.finishTime;
                if(best<=0 || (race.distanceRecord()?score>best:score<best)) {
                    best=score;
                    if(!data.empty()) {
                        std::error_code error;fs::create_directories(data,error);
                        if(error)recordFailure=true;else{std::ofstream f(bestFile());f<<best<<'\n';f.flush();recordFailure=!f;}
                        if(recordFailure)std::cerr<<"Record could not be saved to "<<bestFile()<<'\n';
                    }
                }
                std::cout<<"FINISH format="<<int(race.format())<<" distance="<<race.distance()<<" time="<<race.finishTime<<" position="<<race.place<<" recoveries="<<race.respawns<<'\n';
            }
            wasFinished=race.state==State::Finished;
            audio.update(race,muted || renderer.exhibit);
            if(o.displayCheck){
                auto changed=[&](const char* phase){std::cout<<"DISPLAY_CHECK frame="<<frame<<" phase="<<phase<<'\n';};
                if(frame==60){renderer.highResolution=false;changed("fast");}
                if(frame==80||frame==120){SDL_Event toggle{};toggle.type=SDL_EVENT_KEY_DOWN;toggle.key.scancode=SDL_SCANCODE_G;SDL_PushEvent(&toggle);changed(frame==80?"gunner-camera-toggle":"gunner-map-toggle");}
                if(frame==100){renderer.crtEffects=false;changed("clean");}
                if(frame==140){SDL_SetWindowSize(window,800,500);SDL_SyncWindow(window);changed("small");}
                if(frame==180){renderer.highResolution=true;changed("full-resolution");}
                if(frame==220){renderer.crtEffects=true;changed("crt");}
                if(frame==260){SDL_SetWindowSize(window,1100,680);SDL_SyncWindow(window);changed("resize");}
            }
            auto renderStarted=std::chrono::steady_clock::now();
            renderer.recordSaveFailed=recordFailure;renderer.draw(race,visualTime,best,muted,help);
            lastRenderMs=1000.*std::chrono::duration<double>(std::chrono::steady_clock::now()-renderStarted).count();renderSeconds+=lastRenderMs/1000.;
            ++frame;fpsTime+=wallDt;++fpsFrames;
            if(frame==180)warmStarted=std::chrono::duration<double>(std::chrono::steady_clock::now()-runStarted).count();
            if(fpsTime>2.) {
                std::string title="DUNE BREAKER | Infinity Motorsport | "+std::to_string(int(std::lround(fpsFrames/fpsTime)))+" FPS";
                SDL_SetWindowTitle(window,title.c_str());fpsTime=0;fpsFrames=0;
            }
            if(o.frames>0 && frame==o.frames && !o.capture.empty()) {
                fs::path path(o.capture);if(!path.parent_path().empty()) fs::create_directories(path.parent_path());renderer.capture(o.capture);
            }
            if(o.frames>0 && frame>=o.frames+3) running=false;
            pacingWait=0;
            if(!o.uncapped&&running){auto beforeWait=std::chrono::steady_clock::now();framePacer.wait();pacingWait=std::chrono::duration<double>(std::chrono::steady_clock::now()-beforeWait).count();}
        }
        double wallSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-runStarted).count();
        if(!frameTimes.empty()){std::sort(frameTimes.begin(),frameTimes.end());auto percentile=[&](double q){return frameTimes[std::min(frameTimes.size()-1,size_t(q*(frameTimes.size()-1)))];};
            double sum=0;for(double x:frameTimes)sum+=x;
            std::cout<<"FRAME_TIMES mean_ms="<<sum/frameTimes.size()<<" p50_ms="<<percentile(.50)<<" p95_ms="<<percentile(.95)<<" p99_ms="<<percentile(.99)<<" over20ms="<<std::count_if(frameTimes.begin(),frameTimes.end(),[](double x){return x>20.;})<<" samples="<<frameTimes.size()<<'\n';}
        std::cout<<"FRAME_COST physics_ms="<<physicsSeconds*1000/std::max(1,frame)<<" render_ms="<<renderSeconds*1000/std::max(1,frame)<<'\n';
        if(frame>180)std::cout<<"STEADY_FPS "<<(frame-180)/std::max(.001,wallSeconds-warmStarted)<<'\n';
        std::cout<<"DESERT_RUN frames="<<frame<<" wall_seconds="<<wallSeconds<<" average_fps="<<frame/wallSeconds<<" seed="<<race.course.seed<<" format="<<int(race.format())<<" lap="<<race.lap()<<" distance="<<race.distance()<<" elapsed="<<race.elapsed<<" remaining="<<race.timeRemaining()<<" z="<<race.player.position.z<<" gate="<<race.nextGate<<" speed="<<race.player.speed<<" condition="<<race.player.damage.condition()<<" impacts="<<race.player.impactSerial<<" state="<<int(race.state)<<'\n';
        std::cout<<"COMBAT shots="<<race.player.turret.shots<<" last_target="<<race.player.turret.lastTarget<<" weapon="<<weaponName(race.player.turret.weapon)<<" cable="<<race.player.turret.cable.target<<" ai_shots="<<race.rivals[0].turret.shots+race.rivals[1].turret.shots+race.rivals[2].turret.shots<<'\n';
        std::cout<<"WEAPONS pickups="<<race.pickupsCollected<<" cables="<<race.cablesLatched<<" rut_shots="<<race.rutsFired<<" jacks="<<race.jacksRaised<<'\n';
        std::cout<<"TERRAIN_TRACKS tiles="<<race.course.tracks.tileCount()<<" revision="<<race.course.tracks.revision()<<'\n';
    }
    SDL_DestroyWindow(window);SDL_Quit();return 0;
} catch(const std::exception& error) {std::cerr<<"Dune Breaker: "<<error.what()<<'\n';SDL_Quit();return 1;}
