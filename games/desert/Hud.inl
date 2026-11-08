    struct UiButton {double x,y,w,h;UiAction action;};
    std::vector<UiButton> buttons;
    glm::dvec2 pointer{-1,-1};
    double canvasWidth()const{return width*std::max(900./height,1280./width);}
    double canvasHeight()const{return height*std::max(900./height,1280./width);}
    void beginUi(){
        buttons.clear();pointer={-1,-1};
        if(SDL_GetMouseFocus()==window){float x,y;SDL_GetMouseState(&x,&y);int w,h;SDL_GetWindowSize(window,&w,&h);if(w>0&&h>0){auto uv=gpu->displayToBeam({x/w,y/h});pointer=uv*glm::dvec2(canvasWidth(),canvasHeight());}}
    }
    void button(Mesh& m,double x,double y,double w,double h,const std::string& title,UiAction action,const std::string& key="",bool primary=false){
        bool hover=pointer.x>=x&&pointer.x<=x+w&&pointer.y>=y&&pointer.y<=y+h;
        rect(m,x,y,w,h,ink,.94f);vectorFrame(m,x,y,w,h,hover||primary?white:C(.55));
        if(primary)vectorFrame(m,x+4,y+4,w-8,h-8,C(.35));
        C foreground=white;
        double keyWidth=key.empty()?0:std::max(28.,textWidth(key,16./9)+18),em=w<180?20:24;
        double available=w-36-(key.empty()?0:keyWidth+12),measured=textWidth(title,em/9);
        if(measured>available)em=std::max(17.,em*available/measured);
        label(m,title,x+18,y+(h-em)*.5-1,em,foreground);
        if(!key.empty()){vectorFrame(m,x+w-keyWidth-12,y+(h-28)*.5,keyWidth,28,C(.4));centered(m,key,x+w-keyWidth*.5-12,y+(h-16)*.5-1,16,foreground);}
        buttons.push_back({x,y,w,h,action});
    }
    void hud(const Race& race,double best,bool muted,bool help,double time,bool compact=false,bool recordError=false,bool split=false,bool tactical=false) {
        Mesh m;const auto& car=race.player;const double W=canvasWidth(),H=canvasHeight();
        const C secondary(.72,.73,.75),faint(.35,.36,.38),danger=red;
        const bool circuit=race.format()==RaceFormat::Circuit,endless=race.format()==RaceFormat::Endless;
        int weakest=int(std::min_element(car.damage.tyres.begin(),car.damage.tyres.end())-car.damage.tyres.begin());
        const std::array<const char*,4> tyreNames={"Front-left","Front-right","Rear-left","Rear-right"};
        const std::array<const char*,4> tyreShort={"FL tyre","FR tyre","RL tyre","RR tyre"};
        const std::string format=circuit?"CIRCUIT":endless?"TIMED ENDLESS":"DESERT STAGE";
        const std::string event="RACE "+std::to_string(race.round+1)+" / "+format;
        bool modal=help||race.state==State::Menu||race.state==State::Paused||(race.state==State::Finished&&race.resultsVisible);
        if(modal){
            rect(m,0,0,W,H,ink,.46f);
            double panelW=std::min(1040.,W-72),x=(W-panelW)*.5,y=(H-684)*.5,left=x+40,right=x+panelW*.54,col=panelW*.42;
            card(m,x,y,panelW,684,.97f);rect(m,x+40,y+22,16,3,red);rect(m,x+58,y+22,16,3,green);rect(m,x+76,y+22,16,3,blue);
            if(help){
                label(m,"Controls",left,y+40,48);label(m,"Two local players: one drives, one operates the rear turret.",left,y+108,22,secondary);
                double column=(panelW-120)*.5;
                label(m,"DRIVING",left,y+176,18,cyan);label(m,"GUNNER AND RACE",left+column+40,y+176,18,cyan);
                const std::array<std::pair<const char*,const char*>,6> driving={{{"W / Up","Accelerate"},{"S / Down","Brake, then reverse"},{"A D / Arrows","Steer"},{"Space","Rear handbrake"},{"Shift","Boost"},{"F","Repair while stopped"}}};
                const std::array<std::pair<const char*,const char*>,6> other={{{"Mouse","Aim / map cursor"},{"Left click","Fire / hold to reel"},{"Right click","Cut tow cable"},{"Wheel","Select pickup weapon"},{"G / C","Gunner / driver view"},{"Esc / P","Pause / release mouse"}}};
                for(int group=0;group<2;++group)for(int i=0;i<6;++i){double bx=left+group*(column+40),by=y+218+i*49;auto row=group?other[i]:driving[i];rounded(m,bx,by,122,34,6,C(.15));centered(m,row.first,bx+61,by+6,18);label(m,row.second,bx+138,by+4,20,white);}
                label(m,"R Recover   Tab Results   F11 Full screen   F2 Study   F5 Quality   F8 CRT",left,y+548,18,secondary);
                button(m,left,y+600,col,56,"Back",UiAction::CloseControls,"H",true);
                label(m,"Closing controls resumes a race opened from driving.",right,y+605,18,secondary);
            } else if(race.state==State::Menu){
                label(m,"INFINITY MOTORSPORT",left,y+48,18,yellow);label(m,"Dune",left,y+96,76);label(m,"Breaker",left,y+174,76);
                label(m,"Two-player combat racing.",left,y+286,24);label(m,"Keyboard driver / mouse gunner.",left,y+326,21,secondary);
                card(m,right,y+72,col,302,.60f);label(m,event,right+22,y+94,18,cyan);
                label(m,circuit?std::to_string(race.lapTarget)+" laps":endless?durationText(race.duration):"4.3 km",right+22,y+134,48);
                std::string length=circuit?std::to_string(int(race.course.lapLength))+" m loop":endless?"Endless route":"Point-to-point route";
                label(m,length+" / 4 buggies",right+22,y+210,22,secondary);
                label(m,circuit?"The same course each lap.":endless?"Cover the greatest distance.":"Pass all 24 checkpoints.",right+22,y+274,21);
                label(m,circuit?"Ruts deepen as the field passes.":endless?"The clock runs during recovery.":"Finish in the shortest time.",right+22,y+310,20,secondary);
                button(m,left,y+404,col,68,"Start race",UiAction::Continue,"Enter",true);
                button(m,left,y+488,(col-16)*.5,54,"New terrain",UiAction::NewTerrain,"N");button(m,left+(col+16)*.5,y+488,(col-16)*.5,54,"Controls",UiAction::Controls,"H");
                button(m,right,y+404,(col-16)*.5,54,muted?"Audio off":"Audio on",UiAction::Audio,"M");button(m,right+(col+16)*.5,y+404,(col-16)*.5,54,"Full screen",UiAction::Fullscreen,"F11");
                label(m,"TERRAIN SEED",right,y+512,16,secondary);label(m,std::to_string(race.course.seed),right,y+544,22);
                button(m,left,y+580,164,44,"Quit",UiAction::Quit,"Q");
            } else if(race.state==State::Paused){
                label(m,"Paused",left,y+48,54);label(m,event,left,y+118,20,secondary);
                double statW=(panelW-112)/3;
                const std::array<std::string,3> names={endless?"TIME LEFT":"ELAPSED",circuit?"LAP":"DISTANCE","BUGGY CONDITION"};
                const std::array<std::string,3> values={endless?shortClock(race.timeRemaining()):clockText(race.elapsed),circuit?std::to_string(race.lap())+" / "+std::to_string(race.lapTarget):std::to_string(int(race.distance()))+" m",std::to_string(int(car.damage.condition()*100))+"%"};
                for(int i=0;i<3;++i){double bx=left+i*(statW+16);label(m,names[i],bx,y+174,16,secondary);label(m,values[i],bx,y+202,32,i==0?yellow:white);}
                rect(m,left,y+254,panelW-80,1,faint,.55f);
                button(m,left,y+286,col,62,"Resume race",UiAction::Continue,"Enter",true);button(m,left,y+364,col,54,"Retry race",UiAction::Retry,"R");
                button(m,left,y+434,col,54,"New terrain",UiAction::NewTerrain,"N");button(m,left,y+504,col,54,"Controls",UiAction::Controls,"H");button(m,left,y+594,164,44,"Quit",UiAction::Quit,"Q");
                label(m,"QUICK CONTROLS",right,y+290,18,cyan);
                const std::array<std::pair<const char*,const char*>,4> hints={{{"WASD","Drive"},{"Space","Handbrake"},{"Shift","Boost"},{"F","Repair while stopped"}}};
                for(int i=0;i<4;++i){label(m,hints[i].first,right,y+332+i*42,21,white);label(m,hints[i].second,right+116,y+332+i*42,21,secondary);}
                button(m,right,y+524,(col-16)*.5,54,muted?"Audio off":"Audio on",UiAction::Audio,"M");button(m,right+(col+16)*.5,y+524,(col-16)*.5,54,"Full screen",UiAction::Fullscreen,"F11");
            } else {
                label(m,endless?"Time's up":"Race complete",left,y+42,48);label(m,event,left,y+108,20,secondary);
                label(m,endless?"DISTANCE COVERED":"RACE TIME",left,y+176,18,secondary);
                std::string score=endless?std::to_string(int(race.distance()))+" m":clockText(race.finishTime);
                label(m,score,left,y+210,64,yellow);
                label(m,"Personal best  "+(endless?std::to_string(int(best>0?best:race.distance()))+" m":clockText(best>0?best:race.finishTime)),left,y+302,22,cyan);
                if(recordError)label(m,"Record could not be saved.",left,y+336,18,danger);
                const std::array<std::string,3> names={"POSITION","RECOVERIES","CONDITION"};
                const std::array<std::string,3> values={std::to_string(race.place)+" / 4",std::to_string(race.respawns),std::to_string(int(car.damage.condition()*100))+"%"};
                for(int i=0;i<3;++i){double bx=left+i*col/3;label(m,names[i],bx,y+368,16,secondary);label(m,values[i],bx,y+398,30);}
                card(m,right,y+160,col,280,.60f);
                if(circuit&&!race.lapTimes.empty()){
                    label(m,"Lap times",right+22,y+178,24);auto fastest=std::min_element(race.lapTimes.begin(),race.lapTimes.end());
                    int columns=race.lapTimes.size()>6?2:1,rows=int((race.lapTimes.size()+columns-1)/columns);
                    for(size_t i=0;i<race.lapTimes.size();++i){int column=int(i)/rows,row=int(i)%rows;double bx=right+22+column*(col-44)/columns,by=y+226+row*32;bool fast=race.lapTimes.begin()+i==fastest;
                        label(m,"Lap "+std::to_string(i+1),bx,by,columns==1?21:18,secondary);rightText(m,clockText(race.lapTimes[i]),right+col-22-(columns-column-1)*(col-44)/columns,by,columns==1?23:18,fast?cyan:white);}
                    label(m,"Best lap highlighted",right+22,y+414,17,cyan);
                }else{
                    label(m,endless?"Timed distance race":"Desert stage",right+22,y+180,24);label(m,endless?"Duration  "+durationText(race.duration):"Length  4.3 km",right+22,y+238,23,secondary);
                    label(m,"Checkpoints passed  "+std::to_string(race.nextGate-1),right+22,y+282,23);
                    label(m,"Retry to beat your record on this seed.",right+22,y+346,20,secondary);
                }
                int variation=((race.round+1)/2)%3;std::string next;
                if(race.format()==RaceFormat::Stage)next="Another desert stage";
                else if(circuit)next=durationText(std::min(3600.,race.baseDuration+variation*60.))+" / endless race";
                else next=std::to_string(std::clamp(race.baseLaps+variation,1,12))+" laps / closed circuit";
                card(m,right,y+464,col,94,.58f);label(m,"NEXT EVENT",right+20,y+478,16,secondary);label(m,next,right+20,y+508,24);
                button(m,left,y+486,col,64,"Next race",UiAction::Continue,"Enter",true);
                button(m,left,y+566,(col-16)*.5,52,"Retry",UiAction::Retry,"R");button(m,left+(col+16)*.5,y+566,(col-16)*.5,52,"Free roam",UiAction::FreeRoam,"Tab");
                button(m,left,y+634,164,34,"Quit",UiAction::Quit,"Q");label(m,"N generates another terrain seed.",right,y+594,19,secondary);
            }
            submit(m,3,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|uiBlend|BGFX_STATE_MSAA);return;
        }
        if(split){
            double half=W*.5,gx=W*.75,gy=H*.5,pad=30;
            rect(m,half-2,18,4,H-36,C(.5),.9f);
            label(m,"DRIVER",pad,28,20,cyan);rightText(m,endless?shortClock(race.timeRemaining()):clockText(race.elapsed),half-pad,28,28,yellow);
            label(m,circuit?"LAP "+std::to_string(race.lap())+" / "+std::to_string(race.lapTarget):"DISTANCE "+std::to_string(int(race.distance()))+" m",pad,70,25);
            rightText(m,"POSITION "+std::to_string(race.place)+" / 4",half-pad,75,20,secondary);
            label(m,"CHECKPOINT "+std::to_string(race.gateInLap())+"   /   "+std::to_string(int(race.gateDistance()))+" m",pad,112,20,yellow);
            rect(m,pad,146,half-pad*2,2,faint);rect(m,pad,146,(half-pad*2)*race.eventProgress(),2,cyan);
            label(m,tactical?"GUNNER / MAP":"GUNNER / TURRET",half+pad,28,20,magenta);rightText(m,"MOUSE AIM / LEFT FIRE",W-pad,31,18,secondary);
            // Screen-space reticle is centred on the gunner camera ray.
            C sight=car.turret.cable.active()?yellow:white;
            if(!tactical){for(int pass=0;pass<2;++pass)for(int side:{-1,1}){C c=pass?sight:ink;double stroke=pass?2.2:7;
                line(m,{gx+side*9,gy},{gx+side*25,gy},stroke,c);line(m,{gx,gy+side*9},{gx,gy+side*25},stroke,c);}
            disc(m,{gx,gy,0},4,ink,1,12);disc(m,{gx,gy,0},1.5,sight,1,8);
            if(car.turret.hitMarker>0)for(int sx:{-1,1})for(int sy:{-1,1})line(m,{gx+sx*31,gy+sy*31},{gx+sx*42,gy+sy*42},3,yellow);
            }
            double bottom=H-190;
            card(m,pad,bottom,half-pad*2,132,.80f);
            label(m,std::to_string(int(std::abs(car.speed)*3.6)),pad+18,bottom+12,64);label(m,"KM/H",pad+24,bottom+83,16,secondary);
            label(m,"GEAR",pad+158,bottom+20,16,secondary);label(m,car.gear<0?"R":std::to_string(car.gear),pad+166,bottom+48,38,yellow);
            double bx=pad+248,bw=half-pad-18-bx;
            label(m,"CONDITION  "+std::to_string(int(car.damage.condition()*100))+"%",bx,bottom+19,18,car.damage.condition()<.35?red:white);
            rect(m,bx,bottom+50,bw,5,faint);rect(m,bx,bottom+50,bw*car.damage.condition(),5,cyan);
            label(m,car.boosting?"BOOSTING":"BOOST / SHIFT",bx,bottom+72,16,secondary);rect(m,bx,bottom+103,bw,5,faint);rect(m,bx,bottom+103,bw*car.fuel,5,magenta);
            if(tactical){
                double y=bottom;const auto& gun=car.turret;const auto& cable=gun.cable;
                card(m,half+pad,y,half-pad*2,132,.80f);
                label(m,weaponName(gun.weapon),half+pad+18,y+12,25,sight);
                rightText(m,gun.weapon==Weapon::TowHarpoon?"UNLIMITED":std::to_string(gun.ammo[int(gun.weapon)])+" SHOTS",W-pad-18,y+17,18,secondary);
                if(cable.active()){
                    double load=std::clamp(cable.force/9000.,0.,1.);rect(m,half+pad+18,y+49,half-pad*2-36,4,faint);
                    if(load>0)rect(m,half+pad+18,y+49,(half-pad*2-36)*load,4,yellow);
                    label(m,"CABLE "+std::to_string(int(load*100))+"%",half+pad+18,y+65,17,yellow);
                    rightText(m,"HOLD LEFT TO REEL",W-pad-18,y+65,17,secondary);
                }else label(m,gun.pickupTime>0?gun.notice:gun.weapon==Weapon::TowHarpoon?"LEFT FIRE / HOLD TO REEL":"LEFT FIRE",half+pad+18,y+64,17,secondary);
            }else{
            card(m,half+pad,bottom,half-pad*2,132,.80f);
            const auto& gun=car.turret;const auto& cable=gun.cable;
            label(m,weaponName(gun.weapon),half+pad+18,bottom+15,25,sight);
            rightText(m,gun.weapon==Weapon::TowHarpoon?"UNLIMITED":std::to_string(gun.ammo[int(gun.weapon)])+" SHOTS",W-pad-18,bottom+20,18,secondary);
            double load=std::clamp(cable.force/9000.,0.,1.);rect(m,half+pad+18,bottom+57,half-pad*2-36,5,faint);
            if(cable.active()){if(load>0)rect(m,half+pad+18,bottom+57,(half-pad*2-36)*load,5,yellow);
                label(m,"CABLE LOAD "+std::to_string(int(load*100))+"%",half+pad+18,bottom+76,19,yellow);
                rightText(m,"RIGHT CLICK CUTS",W-pad-18,bottom+77,17,secondary);
            }else label(m,gun.weapon==Weapon::TowHarpoon?"HOLD LEFT TO REEL / RELEASE TO SLACKEN":"AIM AT GROUND / LEFT CLICK TO FIRE",half+pad+18,bottom+78,17,secondary);
            if(gun.pickupTime>0)label(m,gun.notice,half+pad+18,bottom+105,17,yellow);
            else label(m,"WHEEL SELECTS COLLECTED WEAPONS",half+pad+18,bottom+105,16,secondary);
            }
            centered(m,"WASD Drive   Space Brake   Shift Boost",half*.5,H-40,17,secondary);
            centered(m,tactical?"Wheel Weapon   Right Cut   G View":"Mouse Aim   Wheel Weapon   Right Cut   G View",gx,H-40,17,secondary);
            std::string warning;
            if(race.recoveryDelay>0)warning="RECOVERING / "+std::to_string(int(std::ceil(race.recoveryDelay)))+" SEC";
            else if(race.missed)warning="MISSED CHECKPOINT / TURN BACK";
            else if(car.damage.wrecked())warning="DISABLED / STOP + HOLD F TO REPAIR";
            else if((car.orientation()*V(0,1,0)).y<.2)warning="OVERTURNED / R TO RECOVER";
            else if(car.repairing)warning="REPAIRING / KEEP HOLDING F";
            else if(car.damage.tyres[weakest]<.25)warning=std::string(tyreShort[weakest])+" DAMAGED / HOLD F TO REPAIR";
            if(!warning.empty()){card(m,pad,166,half-pad*2,44,.9f);centered(m,warning,half*.5,178,19,yellow);}
            if(race.state==State::Racing&&warning.empty()){
                auto target=race.course.point(race.nextGate*race.course.gateInterval())+V(0,4.2,0);
                if(auto point=project(target)){double x=std::clamp(point->x*.5,90.,half-90.),y=std::clamp(point->y,190.,H-230.);
                    line(m,{x-9,y-6},{x,y+3},3,yellow);line(m,{x+9,y-6},{x,y+3},3,yellow);
                    centered(m,std::to_string(int(race.gateDistance()))+" m",x,y+16,17,yellow);
                }else centered(m,"CHECKPOINT BEHIND / TURN BACK",half*.5,174,18,yellow);
            }
            if(race.state==State::Countdown){std::string count=std::to_string(std::max(1,int(std::ceil(race.countdown))));centered(m,count,half*.5,H*.42,100,yellow);centered(m,count,gx,H*.32,70,yellow);}
            else if(race.elapsed<1.2&&race.state==State::Racing)centered(m,"GO",half*.5,H*.34,65,cyan);
            if(race.state==State::Finished)centered(m,"FREE ROAM / TAB FOR RESULTS",half*.5,168,20,yellow);
            submit(m,3,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|uiBlend|BGFX_STATE_MSAA);return;
        }
        // Three quiet instrument clusters keep the route and vehicle visible.
        card(m,28,28,254,112);label(m,event,46,40,16,secondary);
        label(m,circuit?"Lap "+std::to_string(race.lap())+" / "+std::to_string(race.lapTarget):std::to_string(int(race.distance()))+" m",46,66,32);
        label(m,"Checkpoint "+std::to_string(race.gateInLap())+(race.course.markerCount()?" / "+std::to_string(race.course.markerCount()):""),46,108,18,secondary);
        rect(m,46,135,218,3,faint,.65f);rect(m,46,135,218*race.eventProgress(),3,yellow);
        card(m,W*.5-158,28,316,112);centered(m,endless?"TIME LEFT":"ELAPSED",W*.5,40,16,secondary);
        centered(m,endless?shortClock(race.timeRemaining()):clockText(race.state==State::Finished?race.finishTime:race.elapsed+(race.format()==RaceFormat::Stage?race.penalty:0)),W*.5,61,44,endless&&race.timeRemaining()<30?yellow:white);
        centered(m,race.state==State::Finished?"Race complete":circuit&&!race.lapTimes.empty()?"Last lap  "+clockText(race.lapTimes.back()):circuit?"Closed circuit":endless?"Drive for distance":"Point-to-point",W*.5,115,17,secondary);
        card(m,W-174,28,146,112);centered(m,"POSITION",W-101,40,16,secondary);label(m,std::to_string(race.place),W-155,62,52,yellow);label(m,"/ 4",W-100,86,24,secondary);
        if(race.state==State::Finished)button(m,W-174,152,146,36,"Results",UiAction::FreeRoam,"Tab");else button(m,W-174,152,146,36,"Pause",UiAction::Pause,"Esc");
        button(m,W-174,198,146,36,"Controls",UiAction::Controls,"H");
        card(m,28,H-218,280,190);label(m,"KM/H",48,H-202,18,secondary);label(m,std::to_string(int(std::abs(car.speed)*3.6)),45,H-182,84);
        label(m,"GEAR",218,H-183,16,secondary);label(m,car.gear<0?"R":std::to_string(car.gear),224,H-152,36,yellow);
        label(m,car.boosting?"BOOSTING":"BOOST",48,H-73,16,car.boosting?magenta:secondary);rightText(m,"Shift",287,H-73,16,secondary);
        rounded(m,48,H-47,240,6,3,faint,.65f);if(car.fuel>.001)rounded(m,48,H-47,240*car.fuel,6,3,car.boosting?magenta:cyan);
        if(!car.grounded||(car.slip>.2&&std::abs(car.speed)>6)){card(m,28,H-260,210,32,.86f);label(m,!car.grounded?"AIRBORNE":"TYRE SLIP",48,H-257,20,yellow);}
        // The roadbook is anchored in the lower corner rather than the horizon.
        double mapX=W-278,mapY=H-260,mapW=250;card(m,mapX,mapY,mapW,232);
        label(m,"COURSE",mapX+18,mapY+12,16,secondary);rightText(m,std::to_string(int(race.gateDistance()))+" m",mapX+232,mapY+10,20,yellow);
        if(circuit){
            auto radii=infinity::fractal::fr_circuit_radii(uint32_t(race.course.seed&0xffffff));double scale=77./(std::max(radii.x,radii.y)*1.13+18);
            auto map=[&](glm::dvec2 p){return glm::clamp(glm::dvec2(mapX+125+p.x*scale,mapY+132-(p.y-radii.y)*scale),glm::dvec2(mapX+18,mapY+40),glm::dvec2(mapX+232,mapY+216));};
            auto previous=race.course.routePoint(0);double gateStart=(race.gateInLap()-1)*race.course.gateInterval(),gateEnd=race.gateInLap()*race.course.gateInterval();
            for(int i=1;i<=128;++i){double s=race.course.lapLength*i/128.;auto p=race.course.routePoint(s);line(m,map(previous),map(p),3,s>=gateStart&&s<=gateEnd?yellow:C(.44),.95f);previous=p;}
            for(int i=1;i<=race.course.circuitGates;++i){auto p=map(race.course.routePoint(i*race.course.gateInterval()));disc(m,{p.x,p.y,0},i==race.gateInLap()?5:2.5,i==race.gateInLap()?yellow:i<race.gateInLap()?cyan:white,1,12);}
            const std::array<C,3> colors={green,blue,cyan};
            for(int i=0;i<3;++i){auto p=map({race.rivals[i].position.x,race.rivals[i].position.z});disc(m,{p.x,p.y,0},4,colors[i],1,12);}
            auto p=map({car.position.x,car.position.z});glm::dvec2 f(std::sin(car.yaw),-std::cos(car.yaw)),r(f.y,-f.x);triangle(m,{p.x+f.x*8,p.y+f.y*8,0},{p.x-f.x*5+r.x*5,p.y-f.y*5+r.y*5,0},{p.x-f.x*5-r.x*5,p.y-f.y*5-r.y*5,0},white,false);triangle(m,{p.x+f.x*6,p.y+f.y*6,0},{p.x-f.x*3+r.x*3,p.y-f.y*3+r.y*3,0},{p.x-f.x*3-r.x*3,p.y-f.y*3-r.y*3,0},red,false);
        }else{
            double origin=car.position.z-40;auto map=[&](double x,double z){return glm::dvec2(std::clamp(mapX+125+(x-car.position.x)*.45,mapX+18,mapX+232),mapY+211-(z-origin)*.59);};
            auto previous=map(race.course.center(origin),origin);for(int i=1;i<=56;++i){double z=origin+i*5;auto p=map(race.course.center(z),z);line(m,previous,p,3,C(.48));previous=p;}
            for(int gate=std::max(1,int(origin/gateSpacing));gate<=(endless?int((origin+280)/gateSpacing):gateCount);++gate){double z=gate*gateSpacing;if(z<origin||z>origin+280)continue;auto p=map(race.course.center(z),z);rect(m,p.x-7,p.y-2,14,4,gate==race.nextGate?yellow:white);}
            const std::array<C,3> colors={green,blue,cyan};for(int i=0;i<3;++i){const auto& rival=race.rivals[i];double z=rival.position.z;if(z>=origin&&z<=origin+280){auto p=map(rival.position.x,z);disc(m,{p.x,p.y,0},4,colors[i],1,12);}}
            auto p=map(car.position.x,car.position.z);glm::dvec2 f(std::sin(car.yaw),-std::cos(car.yaw)),r(f.y,-f.x);triangle(m,{p.x+f.x*8,p.y+f.y*8,0},{p.x-f.x*5+r.x*5,p.y-f.y*5+r.y*5,0},{p.x-f.x*5-r.x*5,p.y-f.y*5-r.y*5,0},red,false);
        }
        // One priority message at a time avoids stacked warnings over the road.
        std::string title,detail;C messageColor=yellow;
        bool overturned=(car.orientation()*V(0,1,0)).y<.2;
        if(race.recoveryDelay>0){title="Recovering / "+std::to_string(int(std::ceil(race.recoveryDelay)))+" sec";detail="Rejoining at the last checkpoint";}
        else if(race.missed){title="Missed checkpoint";detail="Turn back, or R to recover / 5-second wait";messageColor=danger;}
        else if(overturned){title="Buggy overturned";detail="R to recover / 5-second wait";messageColor=danger;}
        else if(car.repairing){title="Repairing buggy";detail="Keep holding F";messageColor=cyan;}
        else if(car.damage.wrecked()){title="Buggy disabled";detail="Stop and hold F to repair";messageColor=danger;}
        else if(car.damage.tyres[weakest]<.25){title=std::string(tyreNames[weakest])+" tyre damaged";detail="Stop and hold F to repair";messageColor=danger;}
        else if(race.messageTime>0&&circuit&&race.gateInLap()==1&&race.nextGate>1){title="Lap complete";detail="Last lap  "+clockText(race.lapTimes.empty()?race.elapsed:race.lapTimes.back());messageColor=cyan;}
        if(!title.empty()){
            card(m,W*.5-280,164,560,90,.92f);centered(m,title,W*.5,176,27,messageColor);centered(m,detail,W*.5,216,20,white);
            if(car.repairing){rect(m,W*.5-248,246,496,3,faint);rect(m,W*.5-248,246,496*car.repairProgress,3,cyan);}
        }else if(race.state==State::Racing){
            auto target=race.course.point(race.nextGate*race.course.gateInterval())+V(0,4.2,0);
            if(auto point=project(target)){double x=std::clamp(point->x,340.,W-340.),y=std::clamp(point->y,190.,H*.60);line(m,{x-9,y-6},{x,y+3},3,yellow);line(m,{x+9,y-6},{x,y+3},3,yellow);rounded(m,x-45,y+13,90,30,7,ink,.73f);centered(m,std::to_string(int(race.gateDistance()))+" m",x,y+17,18);}
            else{card(m,W*.5-210,170,420,44,.82f);centered(m,"Checkpoint behind / turn around",W*.5,180,20,yellow);}
        }
        if(car.damage.condition()<.97||car.damage.tyres[weakest]<.8||car.repairing){
            double w=std::min(600.,W-660),x=(W-w)*.5;card(m,x,H-108,w,80,.90f);
            const std::array<std::pair<const char*,double>,4> parts={{{"Hull",car.damage.chassis},{"Engine",car.damage.engine},{"Susp.",car.damage.suspension},{tyreShort[weakest],car.damage.tyres[weakest]}}};
            for(int i=0;i<4;++i){double bx=x+18+i*(w-36)/4,bw=(w-36)/4-18;label(m,parts[i].first,bx,H-98,18,secondary);rightText(m,std::to_string(int(parts[i].second*100))+"%",bx+bw,H-98,18,parts[i].second>.6?white:danger);rect(m,bx,H-68,bw,4,faint,.75f);rect(m,bx,H-68,bw*parts[i].second,4,parts[i].second>.6?cyan:yellow);}
            centered(m,"Hold F while stopped to repair",W*.5,H-54,16,secondary);
        }
        if(race.state==State::Countdown){int count=int(std::ceil(race.countdown));disc(m,{W*.5,H*.43,0},82,ink,.82f,64);centered(m,count>3?"Ready":std::to_string(count),W*.5,count>3?H*.43-28:H*.43-56,count>3?38:100);centered(m,"Follow the white gate posts",W*.5,H*.43+108,24,white);}
        if(race.state==State::Racing&&race.elapsed<.75)centered(m,"GO",W*.5,H*.43-50,92,cyan);
        submit(m,3,BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|uiBlend|BGFX_STATE_MSAA);
    }
