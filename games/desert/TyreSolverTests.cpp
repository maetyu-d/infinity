#include "TyreSolver.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace desert;
using namespace infinity::buggy;
static int checks=0;
static void check(bool b,const char* m){++checks;if(!b)throw std::runtime_error(m);}
static auto patches(double brake=0) {
    std::array<TyreContact,4> out;
    for(int i=0;i<4;++i){auto& c=out[i];c.arm={side(i)*track*.5,-.9,axle(i)};c.forward={0,0,1};c.right={1,0,0};c.load=mass*gravity*.25;c.capacity=.9*c.load;c.radius=tyreRadius;c.brakeTorque=brake;}
    return out;
}
static double energy(const Car& c,glm::dvec3 v,glm::dvec3 w){double e=.5*(mass*glm::dot(v,v)+pitchInertia*w.x*w.x+yawInertia*w.y*w.y+rollInertia*w.z*w.z);for(auto& t:c.wheels)e+=.5*tyreInertia*t.angularSpeed*t.angularSpeed;return e;}
int main()try {
    std::cout<<std::unitbuf;
    double largestGain=0;
    for(int scenario=0;scenario<512;++scenario){
        Car car;auto contacts=patches(scenario%3==0?2500:scenario%3==1?0:75);
        glm::dvec3 v(14*std::sin(scenario*.731),0,22*std::cos(scenario*.317)),w(2*std::sin(scenario*.11),3*std::cos(scenario*.37),1.5*std::sin(scenario*.23));
        for(int i=0;i<4;++i){car.wheels[i].angularSpeed=70*std::sin(scenario*.51+i*.93);contacts[i].load*=.4+1.1*std::abs(std::sin(scenario+i));contacts[i].capacity=.82*contacts[i].load;if((scenario+i)%7==0)contacts[i].load=contacts[i].capacity=0;double steer=.4*std::sin(scenario*.3+i);contacts[i].forward={std::sin(steer),0,std::cos(steer)};contacts[i].right={std::cos(steer),0,-std::sin(steer)};}
        auto rotation=glm::angleAxis(scenario*.13,glm::dvec3(0,1,0))*glm::angleAxis(.7*std::sin(scenario),glm::dvec3(1,0,0))*glm::angleAxis(.5*std::cos(scenario),glm::dvec3(0,0,1));
        for(auto& c:contacts){c.arm=rotation*c.arm;c.forward=rotation*c.forward;c.right=rotation*c.right;}
        double before=energy(car,v,w);solveTyres(car,contacts,rotation,v,w,step);double after=energy(car,v,w);largestGain=std::max(largestGain,after-before);
        check(std::isfinite(after)&&after<=before+1e-7,"unpowered tyre/brake solve cannot create kinetic energy");
        for(const auto& c:contacts){check(glm::length(c.impulse)<=c.capacity*step+1e-8,"combined braking and lateral impulse respects the friction circle");check(std::abs(c.brakeImpulse)<=c.brakeTorque*step+1e-8,"rotor holding never exceeds finite brake torque");}
    }
    Car held;auto contacts=patches(1200);for(auto& c:contacts)c.arm.y=0;
    glm::dvec3 velocity{},angular{},drift{};
    for(int frame=0;frame<120*10;++frame){velocity+=glm::dvec3(.6,0,1.4)*step;solveTyres(held,contacts,{1,0,0,0},velocity,angular,step);drift+=velocity*step;}
    check(glm::length(drift)<.001,"static grip holds an externally loaded braked chassis without low-speed creep");
    Car weakBrake;contacts=patches(8);for(auto& c:contacts)c.arm.y=0;velocity={};angular={};
    for(int frame=0;frame<120*2;++frame){velocity.z+=2.*step;solveTyres(weakBrake,contacts,{1,0,0,0},velocity,angular,step);}
    check(velocity.z>2,"insufficient brake torque lets the rotor turn instead of falsely locking the vehicle");
    Car spinning;contacts=patches();velocity={};angular={};spinning.wheels[2].angularSpeed=70;
    double before=energy(spinning,velocity,angular);solveTyres(spinning,contacts,{1,0,0,0},velocity,angular,step);
    check(velocity.z>0&&spinning.wheels[2].angularSpeed<70,"spinning loaded rotor transfers energy into chassis motion");
    check(energy(spinning,velocity,angular)<=before+1e-7,"rotor/chassis contact dissipates slip without adding energy");
    Car forward,reverse;auto a=patches(400),b=a;glm::dvec3 va(3,0,12),vb=-va,wa(.2,.4,.1),wb=-wa;
    for(int i=0;i<4;++i){forward.wheels[i].angularSpeed=25+i;reverse.wheels[i].angularSpeed=-25-i;}
    solveTyres(forward,a,{1,0,0,0},va,wa,step);solveTyres(reverse,b,{1,0,0,0},vb,wb,step);
    check(glm::length(va+vb)<1e-10&&glm::length(wa+wb)<1e-10,"tyre response is symmetric when all motion reverses");
    for(double direction:{-1.,1.}){
        Course empty(42);Car coasting;coasting.reset(empty);coasting.position.y+=100;
        for(auto& wheel:coasting.wheels)wheel.angularSpeed=direction*50;
        for(int i=0;i<60;++i)coasting.step(empty,{},step);
        for(const auto& wheel:coasting.wheels)check(wheel.angularSpeed*direction>=-1e-10&&std::abs(wheel.angularSpeed)<50,"engine braking dissipates either rotation direction without reversing a rotor");
    }
    Course terrain(42);Car parked;bool mildSlope=false;
    // Rear-only brakes are not expected to hold every steep bank. Select a
    // genuinely sloping patch well inside their available holding friction.
    for(double s=0;s<=1000;s+=10){auto point=terrain.point(s);auto n=terrain.normal(point.x,point.z);double grade=std::hypot(n.x,n.z)/n.y;if(grade>.02&&grade<.12){parked.reset(terrain,s);mildSlope=true;break;}}
    check(mildSlope,"parking fixture has a mild physical slope within rear-brake holding capacity");auto start=parked.position;
    for(int i=0;i<120*12;++i)parked.step(terrain,{0,0,true,false},step);
    check(glm::length(parked.position-start)<.015&&glm::length(parked.velocity)<.0001,"handbrake holds a mild fractal slope after suspension settles");
    std::cout<<"TYRE_SOLVER energyGain="<<largestGain<<" staticDrift="<<glm::length(drift)<<" parkedDrift="<<glm::length(parked.position-start)<<"\n";
    std::cout<<"PASS "<<checks<<" coupled tyre and brake checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
