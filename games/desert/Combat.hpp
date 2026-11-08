#pragma once
#include "Desert.hpp"
namespace desert {
infinity::fractal::Vehicle armedVehicle(const Car&);
glm::dvec3 gunDirection(const Car&);
glm::dvec3 gunCamera(const Car&);
struct BulletHit {glm::dvec3 point{};int car=-1,material=0;double distance=0;};
BulletHit traceBullet(const Race&,int owner,glm::dvec3 origin,glm::dvec3 direction,double range=280.);
void aimTurret(Car&,glm::dvec3 target,double dt);
BulletHit weaponHit(const Race&,int owner);
const char* weaponName(Weapon);
glm::dvec3 towMount(const Car&);
struct CableView {glm::dvec3 start{},end{};double sag=0;};
CableView cableView(const Race&,int owner);
void solveTowCable(Race&,int owner,bool reel,double dt);
Pickup coursePickup(const Course&,int station);
glm::dvec2 rutPathPoint(glm::dvec2 center,glm::dvec2 forward,int strand,double u);
glm::dvec2 jackFootprintPoint(glm::dvec2 center,glm::dvec2 forward,double angle,double radius=1);
}
