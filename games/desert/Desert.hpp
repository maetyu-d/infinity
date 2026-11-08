#pragma once
#include "infinity/Core.hpp"
#include "infinity/BuggyGeometry.hpp"
#include "infinity/Fractal.hpp"
#include <array>
#include <vector>
#include <string>
#include <unordered_map>

namespace desert {
constexpr double gateSpacing = 180.0;
constexpr int gateCount = 24;
constexpr double finishZ = gateSpacing * gateCount;
enum class RaceFormat { Stage=0,Circuit=1,Endless=2 };
struct Obstacle {
    glm::dvec2 center{};
    double radius=1,height=1;
    uint64_t id=0;
    bool soft=false;
    double base=0;
};
struct Course {
    uint64_t seed;
    double phase;
    RaceFormat format=RaceFormat::Stage;
    double lapLength=1200;
    int circuitGates=12;
    std::vector<glm::vec4> markers;
    mutable std::vector<double> arc{0};
    mutable infinity::fractal::Deformation tracks;
    std::vector<infinity::fractal::TerrainFeature> jacks;
    explicit Course(uint64_t value = 20261101,RaceFormat kind=RaceFormat::Stage);
    glm::dvec2 routePoint(double distance,double lane=0) const;
    double project(glm::dvec2 position) const;
    double distanceAlong(double parameter) const;
    double gateInterval() const;
    int markerCount() const;
    double noise(double x, double z, int octave = 0) const;
    double rawHeight(double x, double z) const;
    double center(double z) const;
    double heading(double z) const;
    double height(double x, double z) const;
    glm::dvec3 normal(double x, double z) const;
    glm::dvec3 point(double z, double lane = 0) const;
    std::vector<Obstacle> obstaclesNear(glm::dvec2 position) const;
    double grip(double x,double z) const;
    infinity::fractal::World field() const;
};
struct Input { double throttle=0,steer=0; bool brake=false,boost=false,repair=false,fire=false,cut=false; };
struct Damage {
    double chassis=1,engine=1,suspension=1,alignment=0;
    std::array<double,4> tyres{1,1,1,1},dents{};
    bool wrecked() const {return chassis<=.06 || engine<=.04;}
    double condition() const;
};
struct Wheel {
    glm::dvec3 point{};
    glm::dvec3 groundPoint{};
    glm::dvec2 trackPoint{};
    double compression=.08,load=0,slip=0,rotation=0;
    double angularSpeed=0,steer=0,slipRatio=0,slipAngle=0;
    double longitudinalForce=0,lateralForce=0;
    double soilDepth=0,compaction=0,shearing=0;
    bool contact=true,soilContact=false,hasTrackPoint=false;
};
enum class Weapon {TowHarpoon=0,RutStitcher=1,DuneJack=2};
struct TowCable {
    int target=-1; // -1 detached, -2 terrain, 0..3 vehicle
    glm::dvec3 localAnchor{},groundAnchor{};
    double length=0,force=0,age=0,obstructionClock=0,overload=0;
    bool active()const{return target!=-1;}
};
struct Pickup {int id=0;Weapon weapon=Weapon::RutStitcher;glm::dvec3 position{};};
struct Turret {
    double yaw=0,pitch=.06,cooldown=0,recoil=0,flash=0,hitMarker=0,shotLife=0,pickupTime=0;
    uint64_t shots=0;int lastTarget=-1;Weapon weapon=Weapon::TowHarpoon,lastShot=Weapon::TowHarpoon;
    std::array<int,3> ammo{};std::string notice;
    glm::dvec3 shotStart{},shotEnd{};TowCable cable;
};
struct Car {
    Turret turret;
    glm::dvec3 position{};
    glm::dvec2 velocity{};
    double yaw=0,pitch=0,roll=0,verticalSpeed=0,steering=0;
    double fuel=1,speed=0,airtime=0,travel=0;
    double yawRate=0,pitchRate=0,rollRate=0,steerAngle=0,rpm=1100;
    double steeringVelocity=0,steeringFeedback=0;
    double lateralG=0,longitudinalG=0,slip=0,impactFlash=0,hitAge=10,repairProgress=0;
    double wheelbase=infinity::buggy::wheelbase,track=infinity::buggy::track;
    double shiftTime=0,throttlePedal=0,brakePedal=0;
    int gear=1,groundContacts=4;
    uint64_t impactSerial=0;
    bool grounded=true,boosting=false,repairing=false,hasStepMotion=false;
    Damage damage;
    std::array<Wheel,4> wheels;
    std::vector<glm::dvec3> collisionSamples;
    double sampledCondition=-1,collisionClock=0;
    glm::dvec3 lastCollisionPosition{};
    glm::dvec3 stepStartPosition{};
    glm::dquat stepStartOrientation{1,0,0,0};
    void reset(const Course&,double z=0,double lane=0);
    void settle(const Course&);
    void step(const Course&,const Input&,double dt);
    void impact(double closingSpeed,glm::dvec2 outward,int wheel=-1,bool landing=false);
    void collide(const Course&,glm::dvec3 before);
    void repair(double dt);
    double steeringLimit() const;
    glm::dquat orientation() const;
    void setOrientation(glm::dquat);
    void contactImpulse(glm::dvec2 point,glm::dvec2 normal,double impulse);
    void contactImpulse3D(glm::dvec3 point,glm::dvec3 normal,double impulse);
    infinity::fractal::Vehicle fieldVehicle() const;
};
// Identical mass, friction and angular impulse rules for AI and player contacts.
void resolveContact(Car&,Car&);
enum class State { Menu,Countdown,Racing,Finished,Paused };
struct Race {
    bool combatEnabled=false;
    Course course;
    Car player;
    std::array<Car,3> rivals;
    State state=State::Menu,resumeState=State::Racing;
    double elapsed=0,countdown=3.5,penalty=0,messageTime=0,finishTime=0;
    int nextGate=1,respawns=0,place=4;
    bool missed=false,resultsVisible=true;
    int round=0,lapTarget=3;
    double duration=180,recoveryDelay=0,lapStarted=0,finishDistance=0;
    uint64_t seriesSeed=20261101;
    int baseLaps=3;double baseDuration=180;
    std::array<int,3> rivalNextGate{1,1,1};
    std::array<double,3> rivalDelay{},rivalFinished{};
    std::vector<double> lapTimes;
    std::vector<Pickup> pickupViews;
    std::unordered_map<int,double> pickupRespawn;
    double pickupClock=0,weaponTime=0;
    uint64_t pickupsCollected=0,rutsFired=0,jacksRaised=0,cablesLatched=0;
    explicit Race(uint64_t seed=20261101,RaceFormat kind=RaceFormat::Circuit,int laps=3,double seconds=180);
    void start();
    void regenerate();
    void advance();
    RaceFormat format() const {return course.format;}
    int lap() const;
    int gateInLap() const;
    double distance() const;
    double timeRemaining() const;
    double eventProgress() const;
    std::string recordKey() const;
    bool distanceRecord() const {return format()==RaceFormat::Endless;}
    void recover();
    bool recoverAutopilot();
    void pause();
    void step(const Input&,double dt);
    double gateDistance() const;
    Input ai(const Car&,int index) const;
    Input autopilot() const;
    void stepWeapons(const Input&,double dt,bool ai=true);
    void aimGunnerAt(glm::dvec3 target,double dt);
    void resetWeapons();void updatePickups(double dt);
    void selectWeapon(int car,int direction);void detachCables(int car);
    bool addJack(glm::dvec2 point,glm::dvec2 forward);
    void stitchRuts(glm::dvec2 point,glm::dvec2 forward);
 private:
    void recoverRival(int);
    void updateGates(const Car&,glm::dvec3 before,int& next,bool& invalid);
    double progress(const Car&,int next) const;
};
}
