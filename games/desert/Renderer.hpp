#pragma once
#include "Desert.hpp"
#include "Tactical.hpp"
#include "infinity/Geometry.hpp"
#include "infinity/Ellipsoid.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
namespace desert {
enum class UiAction {None,Continue,Pause,Retry,NewTerrain,Controls,CloseControls,FreeRoam,Audio,Fullscreen,Quit};
class Renderer {
 public:
    explicit Renderer(SDL_Window*);
    ~Renderer();
    void draw(const Race&, double visualTime, double best, bool muted, bool help);
    void capture(const std::string&);
    UiAction actionAt(float windowX,float windowY) const;
    bool tacticalGunner=true;
    glm::dvec2 gunnerCursor{.65,.5};
    GunnerAim gunnerAim;
    std::array<bool,3> gunnerVisible{true,true,true};double contactClock=0;
    void moveGunnerCursor(double dx,double dy);
    void pointGunnerAt(const Race&,glm::dvec3);
    void resetGunnerAim(const Race&);
    bool updateGunnerAim(Race&,double dt);
    double lookYaw = 0, lookPitch = 0, frameWorkSeconds=1./60.;
    bool fractalGraphics=true, exhibit=false, orbit=false, walking=true, highResolution=true,gpuEnabled=true;
    bool chase=false,vista=false,retinaTerrain=false,recordSaveFailed=false,crtEffects=true;
    int fractalDebug=0;
    double orbitYaw=.55,orbitPitch=.25,orbitDistance=10;
 private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
}
