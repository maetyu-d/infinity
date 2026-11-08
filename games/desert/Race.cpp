#include "Desert.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace desert {
static double wrap(double a){return std::remainder(a,2*std::numbers::pi);}
static glm::dvec2 forward(double h){return {std::sin(h),std::cos(h)};}
static bool invertedAtRest(const Course& c,const Car& car){return car.airtime>=2&&!car.groundContacts&&glm::length(car.velocity)<1&&std::abs(car.verticalSpeed)<1&&(car.orientation()*glm::dvec3(0,1,0)).y<=.2&&car.position.y-c.height(car.position.x,car.position.z)<=3.2;}
Race::Race(uint64_t seed,RaceFormat kind,int laps,double seconds):course(seed,kind),lapTarget(std::clamp(laps,1,12)),duration(std::clamp(seconds,1.,3600.)),seriesSeed(seed),baseLaps(lapTarget),baseDuration(duration){start();state=State::Menu;}
void Race::start(){
    course.tracks.clear();course.jacks.clear();player.reset(course);for(int i=0;i<3;++i)rivals[i].reset(course,12.+i*9.,(i%2?1.:-1.)*3.8);
    elapsed=penalty=finishTime=messageTime=recoveryDelay=lapStarted=finishDistance=0;countdown=3.5;nextGate=1;respawns=0;place=4;missed=false;resultsVisible=true;state=State::Countdown;
    rivalNextGate.fill(1);rivalDelay.fill(0);rivalFinished.fill(0);lapTimes.clear();resetWeapons();
}
void Race::regenerate(){course=Course(infinity::mix64(course.seed),format());seriesSeed=course.seed;start();state=State::Menu;}
void Race::advance(){
    if(format()==RaceFormat::Stage){regenerate();return;}
    auto kind=format()==RaceFormat::Circuit?RaceFormat::Endless:RaceFormat::Circuit;++round;
    int variation=(round/2)%3;lapTarget=std::clamp(baseLaps+variation,1,12);duration=std::min(3600.,baseDuration+variation*60.);
    course=Course(infinity::mix64(seriesSeed^infinity::mix64(uint64_t(round))),kind);start();
}
void Race::recover(){
    detachCables(0);
    double s=(nextGate-1)*course.gateInterval()+3;auto damage=player.damage;double fuel=player.fuel;auto turret=player.turret;turret.shotLife=turret.hitMarker=turret.flash=0;
    player.reset(course,s);player.damage=damage;player.fuel=fuel;player.turret=turret;player.settle(course);penalty+=5;++respawns;messageTime=2;missed=false;
    if(format()!=RaceFormat::Stage)recoveryDelay=5;
}
bool Race::recoverAutopilot(){if(state!=State::Racing)return false;if(invertedAtRest(course,player)||(format()!=RaceFormat::Stage&&missed)){recover();return true;}return false;}
void Race::recoverRival(int i){
    detachCables(i+1);
    auto& car=rivals[i];auto damage=car.damage;double fuel=car.fuel;auto turret=car.turret;turret.shotLife=turret.hitMarker=turret.flash=0;
    double s=format()==RaceFormat::Stage?car.position.z:(rivalNextGate[i]-1)*course.gateInterval()+3;
    car.reset(course,s,i%2?3.:-3.);car.damage=damage;car.fuel=fuel;car.turret=turret;car.settle(course);if(format()!=RaceFormat::Stage)rivalDelay[i]=5;
}
void Race::pause(){if(state==State::Paused)state=resumeState;else if(state==State::Racing||state==State::Countdown||state==State::Finished){resumeState=state;state=State::Paused;}}
static Input driver(const Course& c,const Car& car,double lane,double pace){
    double s=c.project({car.position.x,car.position.z}),speed=std::abs(car.speed),ahead=12+speed*.65,h=c.heading(s);auto point=c.routePoint(s,lane);
    double lateral=c.format==RaceFormat::Stage?point.x-car.position.x:glm::dot(point-glm::dvec2(car.position.x,car.position.z),glm::dvec2(std::cos(h),-std::sin(h)));
    double desired=c.heading(s+ahead)+std::atan2(lateral,12+speed*.65),error=wrap(desired-car.yaw);
    double curve=wrap(c.heading(s+20)-c.heading(s))/20,desiredRate=curve*speed+error*1.25;
    double desiredAngle=std::atan(car.wheelbase*desiredRate/std::max(speed,5.))+(desiredRate-car.yawRate)*.045,peakCurve=0;
    for(double look:{0.,20.,40.,65.})peakCurve=std::max(peakCurve,std::abs(wrap(c.heading(s+look+15)-c.heading(s+look)))/15.);
    double target=std::min(pace,std::sqrt(3.5/std::max(.001,peakCurve))),crest=0,crossSlope=0;
    for(double look:{0.,8.,16.,26.,40.,55.}){
        auto p=c.routePoint(s+look,lane),back=c.routePoint(s+look-5,lane),front=c.routePoint(s+look+5,lane);
        auto right=c.format==RaceFormat::Stage?glm::dvec2(1,0):glm::dvec2(std::cos(c.heading(s+look)),-std::sin(c.heading(s+look)));
        crest=std::max(crest,(2*c.height(p.x,p.y)-c.height(back.x,back.y)-c.height(front.x,front.y))/25.);
        crossSlope=std::max(crossSlope,std::abs(c.height(p.x+right.x*2,p.y+right.y*2)-c.height(p.x-right.x*2,p.y-right.y*2))/4.);
    }
    target=std::min(target,std::sqrt(5./std::max(.001,crest)));target*=std::clamp(1.-crossSlope*.55,.70,1.);
    double available=std::max(1.,.80*.90*9.81-crossSlope*9.81);target=std::min(target,std::sqrt(available/std::max(.001,peakCurve+.90*std::max(0.,crest))));
    if(std::abs(car.roll)>.35)target=std::min(target,10.);target*=std::clamp(std::cos(error),.15,1.);target=std::max(3.,target-std::min(8.,std::abs(lateral)*.35));
    if(car.damage.condition()<.65)target*=.80;
    if(std::abs(lateral)>9&&speed<3){auto f=forward(car.yaw);auto probe=car.position+glm::dvec3(f.x*1.9,-.20,f.y*1.9);auto obstacle=infinity::fractal::fr_environment(glm::vec3(probe),c.field());if(obstacle.material==15&&obstacle.distance<.15f)return {-1.,std::clamp(-error*2.,-1.,1.),false,false,false};}
    return {std::clamp((target-car.speed)*.60,-1.,1.),std::clamp(desiredAngle/car.steeringLimit(),-1.,1.),false,false,car.damage.wrecked()&&speed<1.};
}
Input Race::ai(const Car& car,int index)const{return driver(course,car,index%2?2.9:-2.9,22.+index*.8);}
Input Race::autopilot()const{return driver(course,player,respawns?((respawns%2)?2.5:-2.5):0,respawns?16.:19.);}
void Race::updateGates(const Car& car,glm::dvec3 before,int& next,bool& invalid){
    double s=next*course.gateInterval();auto gate=course.routePoint(s);auto f=format()==RaceFormat::Stage?glm::dvec2(0,1):forward(course.heading(s));
    auto from=glm::dvec2(before.x,before.z)-gate,to=glm::dvec2(car.position.x,car.position.z)-gate;
    double a=glm::dot(from,f),b=glm::dot(to,f);
    if(a<0&&b>=0){auto crossing=glm::mix(from,to,a/(a-b));double lateral=format()==RaceFormat::Stage?crossing.x:glm::dot(crossing,glm::dvec2(f.y,-f.x));if(std::abs(lateral)<=13){++next;invalid=false;}else invalid=true;}
    if(progress(car,next)>s+25)invalid=true;
}
double Race::progress(const Car& car,int next)const {
    double s=course.project({car.position.x,car.position.z});if(format()==RaceFormat::Circuit){double reference=(next-1)*course.gateInterval();s+=std::round((reference-s)/course.lapLength)*course.lapLength;}return s;
}
double Race::distance()const{return state==State::Finished||(state==State::Paused&&resumeState==State::Finished)?finishDistance:course.distanceAlong(std::clamp(progress(player,nextGate),0.,nextGate*course.gateInterval()));}
int Race::lap()const{return format()==RaceFormat::Circuit?std::min(lapTarget,1+(nextGate-1)/course.circuitGates):1;}
int Race::gateInLap()const{return format()==RaceFormat::Circuit?(nextGate-1)%course.circuitGates+1:nextGate;}
double Race::timeRemaining()const{return std::max(0.,duration-elapsed);}
double Race::eventProgress()const{return format()==RaceFormat::Endless?std::clamp(elapsed/duration,0.,1.):std::clamp(distance()/(format()==RaceFormat::Circuit?course.lapLength*lapTarget:finishZ),0.,1.);}
std::string Race::recordKey()const {
    if(format()==RaceFormat::Stage)return std::string(combatEnabled?"best-420-stage-":"best-412-stage-")+std::to_string(course.seed)+".txt";
    return std::string(combatEnabled?"best-420-":"best-412-")+(format()==RaceFormat::Circuit?"circuit-"+std::to_string(lapTarget)+"laps-":"endless-"+std::to_string(int(duration))+"s-")+std::to_string(course.seed)+".txt";
}
void Race::step(const Input& input,double dt){
    if(dt<=0||!std::isfinite(dt))return;if(dt>infinity::buggy::step+1e-9){int n=int(std::ceil(std::min(dt,1.)/infinity::buggy::step));for(int i=0;i<n;++i)step(input,std::min(dt,1.)/n);return;}
    messageTime=std::max(0.,messageTime-dt);if(state==State::Countdown){countdown-=dt;if(countdown<=0){state=State::Racing;messageTime=1;}return;}
    if(state!=State::Racing&&state!=State::Finished)return;
    if(state==State::Finished){if(!resultsVisible)stepWeapons(input,dt,false);player.step(course,resultsVisible?Input{0,0,true,false}:input,dt);for(auto& car:rivals)car.step(course,{0,0,true,false},dt);std::array<Car*,4> cars={&player,&rivals[0],&rivals[1],&rivals[2]};for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)resolveContact(*cars[a],*cars[b]);return;}
    if(format()==RaceFormat::Endless)dt=std::min(dt,std::max(0.,duration-elapsed));
    auto before=player.position;int previous=nextGate;
    Input controls=input;if(recoveryDelay>0){recoveryDelay=std::max(0.,recoveryDelay-dt);controls={0,0,true,false,input.repair};}
    player.step(course,controls,dt);elapsed+=dt;
    std::array<glm::dvec3,3> rivalBefore;
    for(int i=0;i<3;++i){auto& car=rivals[i];rivalBefore[i]=car.position;Input control=ai(car,i);if(rivalDelay[i]>0||rivalFinished[i]>0){rivalDelay[i]=std::max(0.,rivalDelay[i]-dt);control={0,0,true,false};}car.step(course,control,dt);}
    std::array<Car*,4> cars={&player,&rivals[0],&rivals[1],&rivals[2]};for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)resolveContact(*cars[a],*cars[b]);
    stepWeapons(controls,dt);
    updateGates(player,before,nextGate,missed);if(nextGate!=previous)messageTime=1.5;
    if(format()==RaceFormat::Circuit&&nextGate!=previous&&(nextGate-1)%course.circuitGates==0){lapTimes.push_back(elapsed-lapStarted);lapStarted=elapsed;}
    for(int i=0;i<3;++i){bool invalid=false;updateGates(rivals[i],rivalBefore[i],rivalNextGate[i],invalid);bool offRoute=glm::length(course.routePoint(course.project({rivals[i].position.x,rivals[i].position.z}))-glm::dvec2(rivals[i].position.x,rivals[i].position.z))>30&&std::abs(rivals[i].speed)<3&&elapsed>12;
        if(offRoute||invertedAtRest(course,rivals[i])||(format()!=RaceFormat::Stage&&invalid))recoverRival(i);
        if(format()==RaceFormat::Circuit&&rivalNextGate[i]>course.circuitGates*lapTarget&&rivalFinished[i]==0)rivalFinished[i]=elapsed;
    }
    double playerDistance=std::clamp(progress(player,nextGate),0.,nextGate*course.gateInterval());place=1;for(int i=0;i<3;++i)if(std::clamp(progress(rivals[i],rivalNextGate[i]),0.,rivalNextGate[i]*course.gateInterval())>playerDistance+.001||rivalFinished[i]>0)++place;
    bool finished=format()==RaceFormat::Endless?elapsed>=duration-1e-9:nextGate>(format()==RaceFormat::Circuit?course.circuitGates*lapTarget:gateCount);
    if(finished){finishDistance=distance();state=State::Finished;finishTime=format()==RaceFormat::Endless?duration:elapsed+(format()==RaceFormat::Stage?penalty:0);}
}
double Race::gateDistance()const{auto p=course.routePoint(nextGate*course.gateInterval());return glm::length(p-glm::dvec2(player.position.x,player.position.z));}
}
