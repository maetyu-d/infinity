#include "Desert.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <numbers>
using namespace desert;
static int checks=0;
static void check(bool condition,const std::string& name) {++checks;if(!condition) throw std::runtime_error(name);}
static bool finite(const Car& c) {return std::isfinite(c.position.x)&&std::isfinite(c.position.y)&&std::isfinite(c.position.z)&&std::isfinite(c.speed);}
int main(int argc,char** argv) try {
    std::cout<<std::unitbuf;
    Course a(42),b(42),c(43);
    double difference=0;
    for(int i=-30;i<=30;++i) {
        double x=i*37.13,z=i*91.37;
        check(a.height(x,z)==b.height(x,z),"seeded repeatability, including negative coordinates");
        check(std::abs(a.height(x,z)-a.height(x+1e-6,z))<.001,"continuous terrain at tile samples");
        check(std::abs(glm::length(a.normal(x,z))-1)<1e-9,"normalized surface normal");
        difference+=std::abs(a.height(x,z)-c.height(x,z));
    }
    check(difference>100,"different seeds produce different terrain");
    Car car;car.reset(a);Race roadDriver(42,desert::RaceFormat::Stage);
    for(int i=0;i<2400;++i) {
        roadDriver.player=car;auto driving=roadDriver.autopilot();driving.boost=i>800;
        car.step(a,driving,1./120.);
        check(finite(car),"finite suspension and motion");
        check(car.position.y>=a.height(car.position.x,car.position.z)+.39-1e-9,"chassis hard-stop cannot penetrate terrain");
    }
    std::cout<<"DRIVE travel="<<car.travel<<" position="<<car.position.x<<","<<car.position.z<<" impacts="<<car.impactSerial<<" condition="<<car.damage.condition()<<"\n";
    check(car.travel>100,"accelerator moves the vehicle");
    check(car.fuel<.7,"boost consumes finite reserve");
    for(int i=0;i<1800;++i) car.step(a,{0,0,true,false},1./120.);
    check(std::abs(car.speed)<2,"handbrake arrests the buggy");
    check(car.fuel>.95,"boost recharges");
    car.reset(a);
    for(int i=0;i<360;++i) car.step(a,{-1,0,false,false},1./120.);
    check(car.speed<0,"reverse gear");
    Race race(42,desert::RaceFormat::Stage);race.start();
    race.step({1,0,false,false},1);
    check(race.player.position.z==0,"countdown holds the grid");
    race.pause();auto countdown=race.countdown;race.step({},1);check(race.countdown==countdown,"pause freezes countdown");race.pause();
    race.state=State::Racing;race.player.position=race.course.point(179.98);race.player.position.y+=.85;
    race.player.velocity={0,42};race.player.yaw=0;race.player.speed=42;
    race.step({},1./120.);
    check(race.nextGate==2,"swept checkpoint crossing");
    race.nextGate=1;race.player.reset(race.course,179.9,25);race.player.velocity={0,42};race.player.yaw=0;
    race.step({},1./120.);check(race.nextGate==1 && race.missed,"missing a gate does not advance race");
    race.recover();check(race.nextGate==1 && race.penalty==5 && !race.missed,"recovery restores last gate with time penalty");
    race.pause();auto position=race.player.position;race.step({1,1,false,true},1);
    check(race.player.position==position,"pause freezes the simulation");
    // Component damage is mechanical, persists through recovery and can be
    // repaired only while stopped. Healthy suspension has independent contacts.
    Car healthy;healthy.reset(a);
    // Hold all-wheel repair brakes: an unbraked car should roll down a bank,
    // rather than being mistaken for an unsettled suspension on this terrain.
    for(int i=0;i<600;++i) healthy.step(a,{0,0,true,false,true},1./120.);
    check(healthy.groundContacts>=3,"four-point suspension settles on terrain");
    check(std::abs(healthy.verticalSpeed)<.5,"spring/damper suspension settles");
    healthy.impact(23,{-std::sin(healthy.yaw),-std::cos(healthy.yaw)},0);
    check(healthy.damage.chassis<.9 && healthy.damage.tyres[0]<1 && healthy.damage.suspension<1,"impacts damage actual components");
    check(healthy.impactSerial>0 && healthy.impactFlash>0,"impact produces presentation event");
    auto damaged=healthy.damage.condition();healthy.velocity={};
    for(int i=0;i<1800;++i) healthy.step(a,{0,0,true,false,true},1./120.);
    check(healthy.damage.condition()>damaged+.1,"stationary field repair restores components");
    Race recovery(42,desert::RaceFormat::Stage);recovery.start();recovery.state=State::Racing;
    recovery.player.impact(27,{1,0},0);auto retained=recovery.player.damage;
    recovery.recover();check(recovery.player.damage.chassis==retained.chassis,"recovery cannot erase damage");
    check(recovery.player.damage.tyres==retained.tyres,"recovery retains tyre damage");
    Course slowCourse=a,fastCourse=a;Car slowStep,fastStep;slowStep.reset(slowCourse);fastStep.reset(fastCourse);
    for(int i=0;i<240;++i) {slowStep.step(slowCourse,{.6,.1,false,false},1./60.);fastStep.step(fastCourse,{.6,.1,false,false},1./120.);fastStep.step(fastCourse,{.6,.1,false,false},1./120.);}
    check(glm::length(slowStep.position-fastStep.position)<1e-6,"handling is stable across render frame rates");
    Car landing;landing.reset(a);landing.position.y+=12;landing.groundContacts=0;landing.grounded=false;
    for(int i=0;i<700;++i) landing.step(a,{},1./120.);
    check(landing.damage.suspension<.99,"hard landings damage suspension");
    check(std::isfinite(landing.yawRate)&&std::isfinite(landing.pitchRate),"hard landing remains finite");
    auto obstacles=a.obstaclesNear({300,500}),again=a.obstaclesNear({300,500});
    check(!obstacles.empty()&&obstacles.size()==again.size(),"physical scenery generated deterministically");
    for(size_t i=0;i<obstacles.size();++i) check(obstacles[i].center==again[i].center,"collision scenery matches stable tile addresses");
    Car normalEngine,weakEngine;normalEngine.reset(a);weakEngine.reset(a);weakEngine.damage.engine=.3;
    for(int i=0;i<240;++i) {normalEngine.step(a,{1,0,false,false},1./120.);weakEngine.step(a,{1,0,false,false},1./120.);}
    std::cout<<"ACCEL normal="<<normalEngine.speed<<" weak="<<weakEngine.speed<<" normalRPM="<<normalEngine.rpm<<"\n";
    check(normalEngine.speed>weakEngine.speed*1.15,"engine damage reduces short-interval acceleration by at least 15 percent");
    auto solid=std::find_if(obstacles.begin(),obstacles.end(),[](const Obstacle& o){return !o.soft&&o.height>1.1&&o.radius<3;});
    check(solid!=obstacles.end(),"collision fixture has a functional checkpoint post");
    Car impactCar;impactCar.reset(a,solid->center.y-5,solid->center.x-a.center(solid->center.y-5));
    auto start=impactCar.position;impactCar.position={solid->center.x,a.height(solid->center.x,solid->center.y)+.80,solid->center.y+2};impactCar.velocity={0,30};impactCar.yaw=0;
    impactCar.collide(a,start);
    check(impactCar.impactSerial>0&&impactCar.damage.chassis<1,"swept scenery collision applies real damage");
    check(impactCar.velocity.y<1,"solid checkpoint collision arrests incoming motion");
    // Mechanical invariants: wheel spin cannot manufacture airborne propulsion,
    // locked rear wheels slide, and paired contacts cannot create kinetic energy.
    Car flight;flight.reset(a);flight.position.y+=40;flight.grounded=false;flight.groundContacts=0;
    auto flightStart=flight.position;double flightTime=.4;
    for(int i=0;i<96;++i) flight.step(a,{1,.4,false,false},1./240.);
    check(glm::length(glm::dvec2(flight.position.x-flightStart.x,flight.position.z-flightStart.z))<1e-8,"airborne throttle cannot create forward thrust");
    check(std::abs(flight.position.y-(flightStart.y-.5*9.81*flightTime*flightTime))<.025,"airborne chassis follows gravity");
    check(flight.wheels[2].angularSpeed>5 && std::abs(flight.wheels[2].rotation)>1,"airborne driven tyres spin independently of distance travelled");
    Car steeringCar;steeringCar.reset(a);
    for(int i=0;i<120;++i) steeringCar.step(a,{0,.7,false,false},1./240.);
    check(steeringCar.wheels[1].steer>steeringCar.wheels[0].steer && steeringCar.wheels[0].steer>0,"inside front wheel has the tighter Ackermann angle");
    check(steeringCar.wheels[2].steer==0 && steeringCar.wheels[3].steer==0,"rear uprights do not inherit front steering");
    Course steeringCourse(42);Car rack;rack.reset(steeringCourse);rack.position.y+=100;
    for(int i=0;i<120;++i)rack.step(steeringCourse,{0,1,false,false},1./120.);
    double lock=rack.steerAngle;rack.velocity=glm::dvec2(std::sin(rack.yaw),std::cos(rack.yaw))*30.;rack.speed=30;
    rack.step(steeringCourse,{0,1,false,false},1./120.);
    check(std::abs(lock-rack.steerAngle)<=3.6/120.+1e-8,"speed changes cannot snap the steering rack angle");
    double previousAngle=std::abs(rack.steerAngle);
    for(int i=0;i<90;++i){rack.step(steeringCourse,{},1./120.);check(std::abs(rack.steerAngle)<=previousAngle+1e-6,"released steering centers without oscillation");previousAngle=std::abs(rack.steerAngle);}
    check(previousAngle<.001,"steering rack returns to center");
    for(int i=0;i<90;++i){double beforeAngle=rack.steerAngle;rack.step(steeringCourse,{0,i<45?-1.:1.,false,false},1./120.);check(std::abs(rack.steerAngle-beforeAngle)<=3.6/120.+1e-8,"countersteering respects physical rack speed");}
    Car handbrake;handbrake.reset(a);handbrake.velocity=glm::dvec2(std::sin(handbrake.yaw),std::cos(handbrake.yaw))*16.;handbrake.speed=16;
    for(auto& wheel:handbrake.wheels) wheel.angularSpeed=16/infinity::buggy::tyreRadius;
    bool rearSliding=false;
    for(int i=0;i<72;++i) {handbrake.step(a,{0,0,true,false},1./240.);for(int rear:{2,3})if(handbrake.wheels[rear].contact&&handbrake.wheels[rear].slipRatio<-.3&&handbrake.wheels[rear].longitudinalForce<0)rearSliding=true;}
    check(std::abs(handbrake.wheels[2].angularSpeed)<1 && std::abs(handbrake.wheels[0].angularSpeed)>8,"handbrake locks rear tyres while front wheels roll");
    check(rearSliding,"loaded locked rear tyres produce longitudinal sliding while braking");
    for(const auto& wheel:handbrake.wheels) {
        check(std::hypot(wheel.longitudinalForce,wheel.lateralForce)<=a.grip(wheel.point.x,wheel.point.z)*wheel.load*1.26+1,"braking and cornering stay inside available tyre grip");
    }
    Car first,second;first.position={0,5,0};second.position={.3,5,3.15};first.velocity={0,15};second.velocity={0,-5};
    auto momentum=first.velocity+second.velocity;
    auto kinetic=[](const Car& body) {using namespace infinity::buggy;return .5*(mass*(glm::dot(body.velocity,body.velocity)+body.verticalSpeed*body.verticalSpeed)+pitchInertia*body.pitchRate*body.pitchRate+yawInertia*body.yawRate*body.yawRate+rollInertia*body.rollRate*body.rollRate);};
    double energyBefore=kinetic(first)+kinetic(second);resolveContact(first,second);
    check(glm::length(first.velocity+second.velocity-momentum)<1e-9,"vehicle contacts conserve total linear momentum");
    check(kinetic(first)+kinetic(second)<=energyBefore+1e-6,"frictional collision cannot create kinetic energy");
    check(std::abs(first.yawRate)+std::abs(second.yawRate)>.1,"off-centre contact transfers angular momentum");
    Car slidingA,slidingB;slidingA.position={0,50,0};slidingB.position={0,50,3.29};slidingA.velocity={7,16};slidingB.velocity={-7,-2};
    auto slidingMomentum=slidingA.velocity+slidingB.velocity;double slidingEnergy=kinetic(slidingA)+kinetic(slidingB);
    resolveContact(slidingA,slidingB);
    check(std::abs(slidingA.velocity.x-slidingB.velocity.x)<12,"glancing body contacts transfer tangential friction instead of sliding freely");
    check(glm::length(slidingA.velocity+slidingB.velocity-slidingMomentum)<1e-9,"glancing collision conserves linear momentum");
    check(kinetic(slidingA)+kinetic(slidingB)<=slidingEnergy+1e-6,"contact manifold and friction dissipate energy");
    Car fastA,fastB;fastA.stepStartPosition={0,50,-4};fastB.stepStartPosition={0,50,4};fastA.position={0,50,4};fastB.position={0,50,-4};
    fastA.hasStepMotion=fastB.hasStepMotion=true;fastA.velocity={0,240};fastB.velocity={0,-240};
    resolveContact(fastA,fastB);
    check(fastA.position.z<fastB.position.z,"continuous vehicle collision catches cars that cross completely within one step");
    check(fastA.velocity.y<0&&fastB.velocity.y>0,"high-speed impact exchanges momentum at first contact");
    Car facePost;facePost.position={solid->center.x-.41,a.height(solid->center.x,solid->center.y)+.9,solid->center.y+2.5};facePost.velocity={0,60};
    auto faceBefore=facePost.position;faceBefore.z-=5;facePost.collide(a,faceBefore);
    check(facePost.position.z<solid->center.y-1.5,"thin checkpoint cannot pass through an unsampled region of the block face");
    check(facePost.velocity.y<20&&facePost.damage.chassis<1,"full-face post contact slows the car and applies damage");
    Car rotatingPost;rotatingPost.position={solid->center.x-1.25,a.height(solid->center.x,solid->center.y)+.9,solid->center.y-1.25};
    rotatingPost.yaw=std::numbers::pi*.5;rotatingPost.yawRate=10;rotatingPost.collide(a,rotatingPost.position);
    check(rotatingPost.yaw<1.5,"rotational sweep catches a post inside the corner arc but outside both endpoint boxes");
    Car tyrePost;tyrePost.position={solid->center.x-1.18,a.height(solid->center.x,solid->center.y)+1.,solid->center.y+3};tyrePost.velocity={0,60};
    auto tyreBefore=tyrePost.position;tyreBefore.z-=6;tyrePost.collide(a,tyreBefore);
    check(tyrePost.position.z<solid->center.y-1.6,"exposed wheel cylinder catches a post that clears the block body");
    for(int i=0;i<96;++i) {
        Car p,q;p.position={0,50,0};q.position={.7*std::sin(i*1.7),50+.4*std::cos(i*.31),2.3};
        p.yaw=i*.13;q.yaw=-i*.21;p.roll=.4*std::sin(i);q.pitch=.4*std::cos(i);
        p.velocity={12*std::sin(i*.73),18};q.velocity={7*std::cos(i*.51),-9};p.verticalSpeed=2;q.verticalSpeed=-3;
        p.rollRate=std::sin(i)*2;q.yawRate=std::cos(i)*3;
        auto total=glm::dvec3(p.velocity.x+q.velocity.x,p.verticalSpeed+q.verticalSpeed,p.velocity.y+q.velocity.y);
        double energy=kinetic(p)+kinetic(q);resolveContact(p,q);
        auto after=glm::dvec3(p.velocity.x+q.velocity.x,p.verticalSpeed+q.verticalSpeed,p.velocity.y+q.velocity.y);
        check(glm::length(after-total)<1e-8,"varied 3D collision conserves momentum");
        check(std::isfinite(kinetic(p)+kinetic(q))&&kinetic(p)+kinetic(q)<=energy*(1+1e-8),"varied contact manifolds cannot add kinetic energy");
    }
    Car inverted;inverted.reset(a);inverted.roll=std::numbers::pi;inverted.position.y+=2;
    // Check passive orientation before impact. Uneven ground can physically
    // roll an inverted car after landing; that is not automatic recovery.
    for(int i=0;i<60;++i) inverted.step(a,{},1./240.);
    check((inverted.orientation()*glm::dvec3(0,1,0)).y<0,"airborne inverted chassis does not auto-right itself");
    for(int i=60;i<720;++i) inverted.step(a,{},1./240.);
    std::cout<<"INVERTED y="<<inverted.position.y<<" contacts="<<inverted.groundContacts<<" roll="<<inverted.roll<<" pitch="<<inverted.pitch<<"\n";
    check(finite(inverted) && inverted.groundContacts==0,"inverted block rests on its roof rather than its tyres");
    Car tumble;tumble.reset(a);tumble.position.y+=100;tumble.pitchRate=3.;tumble.groundContacts=0;tumble.grounded=false;
    for(int i=0;i<360;++i) tumble.step(a,{},1./240.);
    check(finite(tumble) && std::abs(glm::length(tumble.orientation())-1)<1e-10,"full body tumble keeps normalized orientation");
    check((tumble.orientation()*glm::dvec3(0,1,0)).y<-.2,"quaternion body can rotate past vertical");
    Race overturnedRival(42,desert::RaceFormat::Stage);overturnedRival.state=State::Racing;
    auto& stranded=overturnedRival.rivals[0];stranded.roll=std::numbers::pi;stranded.airtime=3;stranded.groundContacts=0;stranded.damage.chassis=.8;
    overturnedRival.step({},1./120.);
    check((stranded.orientation()*glm::dvec3(0,1,0)).y>.2,"resting overturned rivals recover instead of permanently blocking the course");
    check(stranded.damage.chassis==.8,"rival recovery preserves damage");
    // Exercise complete races through the same controls used by human drivers.
    if(argc==1||std::string(argv[1])!="--mechanics-only")for(uint64_t seed:{20261101ULL,42ULL,9173ULL}) {
        Race run(seed,desert::RaceFormat::Stage);run.start();run.state=State::Racing;
        for(int i=0;i<120*480 && run.state==State::Racing;++i) {
            run.recoverAutopilot();run.step(run.autopilot(),1./120.);
            check(finite(run.player),"full race finite physics");
            if(run.missed) {std::cerr<<"DEBUG x="<<run.player.position.x<<" target="<<run.course.center(run.nextGate*gateSpacing)<<" z="<<run.player.position.z<<" speed="<<run.player.speed<<" air="<<run.player.airtime<<" yaw="<<run.player.yaw<<" y="<<run.player.position.y<<"\n"; throw std::runtime_error("autopilot missed gate "+std::to_string(run.nextGate)+" seed "+std::to_string(seed));}
        }
        if(run.state!=State::Finished) std::cerr<<"INCOMPLETE seed="<<seed<<" gate="<<run.nextGate<<" z="<<run.player.position.z<<" speed="<<run.player.speed<<" health="<<run.player.damage.condition()<<" pitch="<<run.player.pitch<<" roll="<<run.player.roll<<" contacts="<<run.player.groundContacts<<" x="<<run.player.position.x<<" target="<<run.course.center(run.player.position.z)<<"\n";
        check(run.state==State::Finished,"full seeded race reaches finish");
        check(run.nextGate==gateCount+1 && run.finishTime>60,"all gates required for finish");
        check(run.place>=1 && run.place<=4,"valid classification");
        check(run.respawns<=3&&run.penalty==run.respawns*5,"demo recovery retains checkpoint progress and its time penalty");
        std::cout<<"RACE seed="<<seed<<" time="<<run.finishTime<<" place="<<run.place<<" recoveries="<<run.respawns<<'\n';
    }
    std::cout<<"PASS "<<checks<<" desert checks\n";return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
