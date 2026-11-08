#include "TyreSolver.hpp"
#include <algorithm>
#include <cmath>

namespace desert {
void solveTyres(Car& car,std::array<TyreContact,4>& contacts,glm::dquat rotation,
                glm::dvec3& linear,glm::dvec3& angular,double dt) {
    using namespace infinity::buggy;
    struct Constraint {glm::dvec3 x{},y{};double xx=0,xy=0,yy=0,gx=0,gy=0;};
    std::array<Constraint,4> constraints;
    const glm::dvec3 inverseInertia(1/pitchInertia,1/yawInertia,1/rollInertia);
    const auto inverseRotation=glm::conjugate(rotation);
    auto project=[](glm::dvec2 impulse,double limit){double length=glm::length(impulse);return length>limit?impulse*(limit/length):impulse;};
    for(int i=0;i<4;++i) {
        auto& c=contacts[i];auto& w=car.wheels[i];auto& k=constraints[i];
        c.impulse={};c.brakeImpulse=0;
        w.angularSpeed+=c.driveTorque*dt/tyreInertia;
        if(c.load<1) {
            w.angularSpeed=std::copysign(std::max(0.,std::abs(w.angularSpeed)-c.brakeTorque*dt/tyreInertia),w.angularSpeed)*std::exp(-dt*.14);
            continue;
        }
        k.x=inverseRotation*glm::cross(c.arm,c.forward);k.y=inverseRotation*glm::cross(c.arm,c.right);
        k.xx=1/mass+glm::dot(k.x*inverseInertia,k.x);k.xy=glm::dot(k.x*inverseInertia,k.y);k.yy=1/mass+glm::dot(k.y*inverseInertia,k.y);
        double vx=std::abs(glm::dot(linear,c.forward)+glm::dot(angular,k.x));
        // Steady rolling gives F = stiffness * slip. As rolling speed tends
        // to zero this becomes a sticking contact, rather than a force which
        // requires perpetual sideways creep to hold a stopped car on a slope.
        double blend=std::clamp(vx/.75,0.,1.);blend=blend*blend*(3-2*blend);
        k.gx=vx*blend/(c.longitudinalStiffness*dt);k.gy=vx*blend/(c.corneringStiffness*dt);
    }
    for(int iteration=0;iteration<8;++iteration)for(int i=0;i<4;++i) {
        auto& c=contacts[i];if(c.load<1)continue;
        auto& w=car.wheels[i];const auto& k=constraints[i];
        double vx=glm::dot(linear,c.forward)+glm::dot(angular,k.x),vy=glm::dot(linear,c.right)+glm::dot(angular,k.y);
        double brakeLimit=c.brakeTorque*dt,limit=c.capacity*dt;
        auto solve=[&](double a,double x){
            double b=k.xy,d=k.yy+k.gy,det=a*d-b*b;
            glm::dvec2 rhs(-x-k.gx*c.impulse.x,-vy-k.gy*c.impulse.y);
            glm::dvec2 change((d*rhs.x-b*rhs.y)/det,(a*rhs.y-b*rhs.x)/det);
            return project(c.impulse+change,limit);
        };
        // First solve a held rotor, and calculate the brake impulse needed to
        // keep it held. If that exceeds the real brake capacity, solve again
        // with a moving rotor and that brake at its torque limit.
        auto next=solve(k.xx+k.gx,vx);
        double brake=c.brakeImpulse+c.radius*(next.x-c.impulse.x)-tyreInertia*w.angularSpeed;
        if(std::abs(brake)>brakeLimit) {
            brake=std::clamp(brake,-brakeLimit,brakeLimit);
            double freeSlip=vx-c.radius*(w.angularSpeed+(brake-c.brakeImpulse)/tyreInertia);
            next=solve(k.xx+c.radius*c.radius/tyreInertia+k.gx,freeSlip);
        }
        auto delta=next-c.impulse;c.impulse=next;
        linear+=(c.forward*delta.x+c.right*delta.y)/mass;
        angular+=(k.x*delta.x+k.y*delta.y)*inverseInertia;
        w.angularSpeed+=((brake-c.brakeImpulse)-delta.x*c.radius)/tyreInertia;
        c.brakeImpulse=brake;
    }
}
}
