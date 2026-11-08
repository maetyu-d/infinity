#pragma once
#include "Desert.hpp"

namespace desert {
// One fixed contact plane per tyre for this 120 Hz step. Impulses are solved
// together with the chassis and finite brake torque, then discarded next step.
struct TyreContact {
    glm::dvec3 arm{},forward{},right{};
    double load=0,capacity=0,radius=.46,driveTorque=0,brakeTorque=0;
    double longitudinalStiffness=48000,corneringStiffness=23500;
    glm::dvec2 impulse{};
    double brakeImpulse=0;
};
void solveTyres(Car&,std::array<TyreContact,4>&,glm::dquat rotation,
                glm::dvec3& linear,glm::dvec3& localAngular,double dt);
}
