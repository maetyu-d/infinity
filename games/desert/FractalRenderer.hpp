#pragma once
#include "infinity/Fractal.hpp"
#include "Tactical.hpp"
#include "infinity/Ellipsoid.hpp"
#include <memory>
namespace desert {
class FractalRenderer {
 public:
    FractalRenderer();~FractalRenderer();
    bool available() const;
    void beginDisplay(int width,int height,bool crtEffects=true);
    void finishDisplay(int width,int height);
    glm::dvec2 displayToBeam(glm::dvec2 uv) const;
    void draw(const infinity::ellipsoid::Camera&,infinity::fractal::World,const std::array<infinity::fractal::Vehicle,4>&,int width,int height,double time,bool native,bool quality,int debug,int hiddenHead,double pixelDensity=1,double frameWorkSeconds=1./60.,const infinity::ellipsoid::Camera* gunner=nullptr,const std::array<glm::vec4,8>& shots={},const std::vector<glm::vec4>& pickups={},const GunnerMap* tactical=nullptr);
    const std::vector<uint32_t>& pixels() const;
    int frameWidth() const;int frameHeight() const;
 private:struct Impl;std::unique_ptr<Impl> p;
};
}
