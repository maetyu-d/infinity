#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "infinity/BuggyGeometry.hpp"

namespace infinity::ellipsoid {
using Vec = glm::dvec3;
using Rotation = glm::dquat;
enum class Material : uint8_t { Sand, DarkSand, Ochre, Teal, Rust, Skin, Leather, Cloth,
                                Cactus, Stone, Bone, Sky, Haze, Metal, Red, Gold };
enum class Mode { Analytic, Lookup };
enum class Debug { None, Parts, Depth };
struct Transform { Vec center{}; Rotation orientation{1,0,0,0}; };
// A rigid joint hierarchy never inherits the primitive's non-uniform radii.
struct Joint { int parent=-1; Vec offset{}; Rotation rotation{1,0,0,0}; std::string name; };
struct Ellipsoid {
    Vec center{};
    Rotation orientation{1,0,0,0};
    Vec radii{1};
    Material material=Material::Ochre;
    int parent=-1;
};
struct Model { std::vector<Joint> joints; std::vector<Ellipsoid> parts; };
std::vector<Ellipsoid> resolve(const Model&, Transform root = {});
struct Hit { double t=0; Vec position{},normal{},unitPosition{}; };
std::optional<Hit> intersect(const Ellipsoid&, Vec origin, Vec direction, double near=1e-6);
struct Camera {
    Vec eye{};
    Rotation orientation{1,0,0,0}; // Camera-local +Z is forward, +Y is up.
    double verticalFov=.9, aspect=1.6, near=.05, far=1500;
    Vec ray(double pixelX,double pixelY,int width,int height) const;
    Vec toView(Vec world) const;
};
struct LookupTables {
    static constexpr int size=128;
    std::array<float,size*size> depth{};
    std::array<uint8_t,size*size> shade{};
    std::array<uint8_t,size*size> mask{};
    Vec light=glm::normalize(Vec(-.45,.65,-.7));
    LookupTables();
    void relight(Vec direction);
    float profile(double x,double y) const;
    uint8_t lighting(Vec unitNormal) const;
};
class Rasterizer {
 public:
    Rasterizer(int width=320,int height=200);
    void resize(int width,int height);
    void begin(const Camera&, Vec light={-.6,.47,.75});
    void background(std::span<const uint8_t> paletteIndices,std::span<const double> cameraDepth);
    void draw(std::span<const Ellipsoid>,Mode=Mode::Analytic,Debug=Debug::None);
    // Triangle backgrounds use the same positive camera-Z depth as the volumes.
    void triangle(Vec a,Vec b,Vec c,glm::vec3 ca,glm::vec3 cb,glm::vec3 cc,bool fog=true);
    void finish(Debug=Debug::None);
    void clearDepth();
    int width() const {return w;}
    int height() const {return h;}
    const std::vector<uint32_t>& pixels() const {return colors;}
    const std::vector<double>& depthBuffer() const {return depth;}
    const std::vector<int>& owners() const {return owner;}
    const std::array<uint32_t,256>& palette() const {return shades;}
    LookupTables tables;
    size_t candidates=0,hits=0;
 private:
    int w=0,h=0,nextId=0;
    Camera camera;
    Vec light{};
    std::vector<uint32_t> colors;
    std::vector<uint8_t> indices;
    std::vector<double> depth;
    std::vector<int> owner;
    std::vector<Vec> rays;
    std::array<uint32_t,256> shades{};
    std::array<uint8_t,32768> colorLookup{};
    double projectionX=1,projectionY=1;
    uint8_t quantize(Vec) const;
    void axes(const Ellipsoid&);
    void line(Vec a,Vec b,uint32_t color);
};
Model human(double time,bool walking);
Model creature(double time,bool walking);
struct VehiclePose {
    double steering=0,steerAngle=0,wheelPhase=0,speed=0,rpm=1100;
    double chassis=1,engine=1,suspension=1,alignment=0;
    std::array<double,4> compression{.08,.08,.08,.08},tyres{1,1,1,1},dents{};
    std::array<double,4> wheelRotation{},wheelSteer{};
    bool independentWheels=false;
};
Model buggy(const VehiclePose&,Material paint=Material::Ochre);
Model cockpit(const VehiclePose&,double time);
Model buggy(double steering,double wheelPhase,Material paint=Material::Ochre);
Model cockpit(double steering,double time);
Ellipsoid volume(Vec center,Vec radii,Material,Rotation orientation={1,0,0,0});
Ellipsoid segment(Vec a,Vec b,double radius,Material,double flatten=1.0);
}
