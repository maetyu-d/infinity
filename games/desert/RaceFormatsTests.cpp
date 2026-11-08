#include "Desert.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace desert;
static int checks=0;void check(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
void cross(Race& race,int direction=1){double s=race.nextGate*race.course.gateInterval();auto damage=race.player.damage;double fuel=race.player.fuel;race.player.reset(race.course,s-direction*.10);race.player.damage=damage;race.player.fuel=fuel;race.player.settle(race.course);double h=race.course.heading(s);race.player.velocity=glm::dvec2(std::sin(h),std::cos(h))*(direction*30.);race.player.speed=direction*30.;for(auto& w:race.player.wheels)w.angularSpeed=direction*30./.46;race.state=State::Racing;race.step({},1./120.);}
int main(int argc,char**)try {
 std::cout<<std::unitbuf;
 Race circuit(20261101,RaceFormat::Circuit,2);auto& c=circuit.course;
 check(glm::length(c.routePoint(0)-c.routePoint(c.lapLength))<.001,"circuit closes in world space");
 check(std::abs(std::remainder(c.heading(0)-c.heading(c.lapLength),6.283185307179586))<.001,"circuit tangent is continuous across the seam");
 check(c.markers.size()==24&&c.lapLength>800,"generated circuit has physical checkpoint posts and useful length");
 for(int i=0;i<60;++i){double s=c.lapLength*i/60;check(std::abs(c.project(c.routePoint(s))-s)<.002,"circuit projection recovers route position");}
 for(auto p:c.markers)check(infinity::fractal::fr_environment(glm::vec3(p)+glm::vec3(0,1,0),c.field()).distance<0,"circuit posts are part of the collision world");
 auto point=c.routePoint(40,8);for(int i=0;i<30;++i)c.tracks.stamp(point,point+glm::dvec2(.01,0),{0,1},.4,2400,.01,.01);float rut=c.tracks.sample(glm::vec2(point)).height;
 circuit.player.damage.chassis=.8;circuit.player.fuel=.4;uint64_t seed=c.seed;
 for(int i=0;i<c.circuitGates;++i)cross(circuit);
 check(circuit.lap()==2&&circuit.state==State::Racing&&circuit.lapTimes.size()==1,"one lap advances to the next lap without ending a multi-lap event");
 check(c.seed==seed&&c.tracks.sample(glm::vec2(point)).height==rut,"lap transitions preserve the same rutted terrain");
 check(circuit.player.damage.chassis==.8&&circuit.player.fuel<.5,"laps retain damage and fuel state");
 for(int i=0;i<c.circuitGates;++i)cross(circuit);
 check(circuit.state==State::Finished&&circuit.lapTimes.size()==2,"race finishes only after all gates of all laps");
 check(std::abs(circuit.distance()-c.lapLength*2)<1,"circuit score counts complete laps through the seam");
 auto score=circuit.distance();circuit.player.position.x+=100;circuit.step({},.1);check(circuit.distance()==score,"free roam cannot change a finished result");
 circuit.pause();check(circuit.distance()==score,"pausing after the finish keeps the recorded distance");circuit.pause();
 Race reverse(42,RaceFormat::Circuit);cross(reverse,-1);check(reverse.nextGate==1,"wrong-way crossing cannot advance a gate");
 reverse.player.reset(reverse.course,reverse.course.lapLength-.1);reverse.player.velocity={0,20};reverse.state=State::Racing;reverse.step({},1./120.);check(reverse.lap()==1&&reverse.nextGate==1,"crossing the start area cannot skip the circuit");
 Race endless(42,RaceFormat::Endless,3,1);endless.nextGate=25;cross(endless);check(endless.nextGate==26&&endless.state==State::Racing,"endless route continues past the old 24-gate finish");
 auto poles=endless.course.obstaclesNear({endless.player.position.x,endless.player.position.z});check(!poles.empty(),"endless checkpoint scenery continues beyond 4.32 km");
 endless.elapsed=.995;endless.step({},.02);check(endless.state==State::Finished&&std::abs(endless.elapsed-1)<1e-9,"timed race stops exactly at its duration");
 check(endless.distance()>4320&&endless.distanceRecord(),"timed score measures validated route distance");
 score=endless.distance();endless.player.position.z+=500;endless.step({},1);check(endless.distance()==score,"timed distance stays fixed after expiry");
 Race paused(42,RaceFormat::Endless,3,60);paused.state=State::Racing;paused.pause();paused.step({},1);check(paused.elapsed==0,"pause freezes the timed event");paused.pause();paused.recover();paused.step({1,0,false,false},1);check(paused.timeRemaining()>58.99&&paused.recoveryDelay>3.99,"recovery costs driving time without shortening the event clock");
 Race series(42);for(int round=1;round<=6;++round){auto old=series.course.seed;series.state=State::Finished;series.advance();check(series.format()==(round%2?RaceFormat::Endless:RaceFormat::Circuit),"series alternates circuits and timed endless events");check(series.course.seed!=old&&series.course.tracks.tileCount()==0,"next event generates fresh terrain and clears previous-event ruts");check(series.lapTarget==3+(round/2)%3&&series.duration==180+60*((round/2)%3),"successive event pairs use three, four and five laps or minutes");}
 check(series.lapTarget==3&&series.duration==180,"event lengths cycle through three difficulty levels");
 check(circuit.recordKey()!=endless.recordKey(),"lap-time and timed-distance records have separate keys");
 if(argc>1){
  Race drive(20261101,RaceFormat::Circuit,3);drive.state=State::Racing;
  int lastLap=1;double lastGateTime=0;int lastGate=drive.nextGate;
  for(int frame=0;frame<120*900&&drive.state==State::Racing;++frame){
   drive.recoverAutopilot();drive.step(drive.autopilot(),1./120.);
   if(drive.nextGate!=lastGate){lastGateTime=drive.elapsed;lastGate=drive.nextGate;std::cout<<"CIRCUIT gate="<<lastGate-1<<" time="<<drive.elapsed<<" recoveries="<<drive.respawns<<"\n";}
   if(drive.lap()!=lastLap){lastLap=drive.lap();std::cout<<"LAP "<<lastLap<<" tiles="<<drive.course.tracks.tileCount()<<"\n";}
   if(drive.elapsed-lastGateTime>120)throw std::runtime_error("circuit driver stalled before gate "+std::to_string(drive.nextGate));
  }
  check(drive.state==State::Finished&&drive.lapTimes.size()==3,"physical driver completes a closed three-lap circuit");
  check(drive.course.tracks.tileCount()>0,"actual circuit driving deforms the terrain");
  std::cout<<"CIRCUIT_FINISH time="<<drive.finishTime<<" recoveries="<<drive.respawns<<" distance="<<drive.distance()<<"\n";
  Race timed(42,RaceFormat::Endless,3,30);timed.state=State::Racing;timed.nextGate=26;timed.player.reset(timed.course,4503);
  for(int i=0;i<3;++i){timed.rivalNextGate[i]=26;timed.rivals[i].reset(timed.course,4515+i*9,i%2?3.8:-3.8);}
  for(int frame=0;frame<120*31&&timed.state==State::Racing;++frame){timed.recoverAutopilot();timed.step(timed.autopilot(),1./120.);}
  check(timed.state==State::Finished&&std::abs(timed.elapsed-30)<1e-7,"physical endless driving finishes at the exact time limit");
  check(timed.nextGate>26&&timed.distance()>4700,"physical endless driving passes new gates beyond the old finish");
  std::cout<<"ENDLESS_FINISH time="<<timed.finishTime<<" distance="<<timed.distance()<<" recoveries="<<timed.respawns<<"\n";
 }
 std::cout<<"PASS "<<checks<<" race format and progression checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
