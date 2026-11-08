#pragma once

// Metres, kilograms and seconds. Rendering and physics share one vehicle layout.
namespace infinity::buggy {
constexpr double mass=980, gravity=9.81;
constexpr double frontAxle=1.48, rearAxle=-1.24;
constexpr double wheelbase=frontAxle-rearAxle, track=2.02;
constexpr double tyreRadius=.46, suspensionRest=.54, suspensionTravel=.38;
constexpr double springRate=30000, pitchInertia=1550, rollInertia=820, yawInertia=1700;
constexpr double compressionDamping=2600,reboundDamping=3800,bumpStart=.29;
constexpr double pneumaticTrail=.045,steeringStiffness=9000;
constexpr double tyreInertia=2.1;
constexpr double cageFrontHeight=1.66,cageRearHeight=1.56;
constexpr double step=1./120.;
constexpr double side(int wheel) { return wheel%2?1.:-1.; }
constexpr double axle(int wheel) { return wheel<2?frontAxle:rearAxle; }
}
