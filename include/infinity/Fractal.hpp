#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
namespace infinity::fractal {
using float2=glm::vec2;using float3=glm::vec3;using float4=glm::vec4;using uint=uint32_t;
using glm::abs;using glm::floor;using glm::min;using glm::max;using glm::clamp;using glm::mix;
using glm::log;using glm::length;using glm::dot;using glm::normalize;using glm::sin;using glm::cos;using glm::sqrt;
inline float atan2(float y,float x) {return glm::atan(y,x);}
#include "infinity/FractalField.inl"
class Deformation {
 public:
    static constexpr int tileSize=32;
    RutSample sample(glm::vec2) const;
    void stamp(glm::dvec2 from,glm::dvec2 to,glm::dvec2 forward,double width,double load,double rolling,double shearing);
    void clear();
    uint64_t revision() const {return serial;}
    size_t tileCount() const {return tiles.size();}
    uint64_t tileRevision(int x,int z) const;
    bool occupied(int x,int z) const {return tiles.contains(key(x,z));}
    void copyTile(int x,int z,std::span<glm::vec2>) const;
 private:
    struct Tile {std::array<glm::vec2,tileSize*tileSize> nodes{};uint64_t revision=0;};
    std::unordered_map<uint64_t,Tile> tiles;
    uint64_t serial=1;
    static uint64_t key(int x,int z) {return (uint64_t(uint32_t(x))<<32)|uint32_t(z);}
};
struct Contact {glm::vec3 point{},normal{};float distance=0;int material=0;};
std::optional<Contact> cast(World,glm::vec3 origin,glm::vec3 direction,float maximum,float tolerance=.001f);
glm::vec3 normal(World,glm::vec3 point);
float signedDistance(World,glm::vec3 point);
std::vector<glm::dvec3> vehicleSamples(const Vehicle&);
std::optional<Contact> castVehicle(const Vehicle&,glm::vec3 origin,glm::vec3 direction,float maximum,bool wheels=true);
}
