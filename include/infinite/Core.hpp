#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infinite {
enum class Kind : uint8_t { Cosmic, Galaxy, System, Planet, Surface, Mountain, Rock, Grain, Lattice };
std::string_view kindName(Kind);
struct Segment {
    Kind kind;
    uint64_t id;
    auto operator<=>(const Segment&) const = default;
};
struct Address {
    std::array<int64_t, 3> cell{};
    std::vector<Segment> path;
    Address child(Kind kind, uint64_t id) const;
    Address parent() const;
    Kind kind() const {
        return path.empty() ? Kind::Cosmic : path.back().kind;
    }
    uint64_t id() const {
        return path.empty() ? 0 : path.back().id;
    }
    std::string str() const;
    static Address parse(std::string_view);
    bool ancestorOf(const Address&) const;
    void validate() const;
    auto operator<=>(const Address&) const = default;
};
uint64_t mix64(uint64_t);
uint64_t childSeed(uint64_t parent, Kind, uint64_t id);
// Additional feature streams never alter the existing hierarchical seed recipe.
enum class Feature : uint64_t {
    AtomicVacancy = 0x61746f6d766163ULL,
    CosmicWeb = 0x776562,
    GalaxyStructure = 0x67616c617879,
    StellarPopulation = 0x7374617273,
    PlateMotion = 0x706c617465,
    Impacts = 0x696d70616374,
    RockFabric = 0x666162726963,
    CrystalGrowth = 0x6372797374616c,
    ThermalMotion = 0x746865726d616c,
    Substitution = 0x7375627374
};
uint64_t featureSeed(uint64_t objectSeed, Feature, uint64_t featureId);
uint64_t seedOf(const Address&, uint64_t universeSeed = 0x494e46494e495445ULL);
// Every sample is a pure function of key and counter; no mutable RNG state.
struct Random {
    uint64_t key;
    uint64_t bits(uint64_t counter) const {
        return mix64(key ^ mix64(counter));
    }
    double unit(uint64_t counter) const {
        return static_cast<double>(bits(counter) >> 11) * 0x1.0p-53;
    }
    double signedUnit(uint64_t counter) const {
        return unit(counter) * 2.0 - 1.0;
    }
};
double smoothstep(double a, double b, double x);
double valueNoise(glm::dvec3, uint64_t seed);
double fractalNoise(glm::dvec3, uint64_t seed, int octaves = 5);
glm::dvec3 cubeDirection(int face, double u, double v);
glm::dquat tangentFrame(glm::dvec3 normal);
struct Tile {
    int face;
    unsigned level;
    uint32_t x, y;
};
uint64_t encodeTile(Tile);
Tile decodeTile(uint64_t);
glm::dvec3 tileDirection(Tile, double u = 0.5, double v = 0.5);
struct LocalFrame {
    double logRadius = 0;  // log10 metres per local unit
    glm::dvec3 position{}; // parent-local units, never absolute universe metres
    glm::dquat orientation{1, 0, 0, 0};
};
struct Pose {
    glm::dvec3 position{};
    glm::dquat orientation{1, 0, 0, 0};
    double scale = 1;
};
// Solve using only the path AFTER the common ancestor. Shared large translations
// never enter the subtraction. Result is in anchor axes, in 10^renderLog metres.
Pose relativePose(const std::vector<LocalFrame>& object, const Address& objectAddress,
                  const std::vector<LocalFrame>& anchor, const Address& anchorAddress, double renderLog);
glm::dvec3 relativePoint(const std::vector<LocalFrame>& object, const Address& objectAddress,
                         glm::dvec3 localPoint, const std::vector<LocalFrame>& anchor,
                         const Address& anchorAddress, double renderLog);
// Prepared once for a batch. Ancestor points subtract the first divergent
// anchor translation in source units, before any astronomical scaling.
struct PointTransform {
    glm::dmat3 linear{1};
    glm::dvec3 pivot{}, translation{};
    glm::dvec3 operator()(glm::dvec3 p) const {
        return linear * (p - pivot) + translation;
    }
};
PointTransform pointTransform(const std::vector<LocalFrame>& object, const Address&,
                              const std::vector<LocalFrame>& anchor, const Address&, double renderLog);
struct LOD {
    static double pixels(double radius, double distance, double height, double fovRadians);
    static unsigned detail(double pixels);
    static unsigned stableDetail(double pixels, unsigned previous);
    static bool visible(glm::dvec3 viewCenter, double radius, double aspect, double fov);
};
} // namespace infinite
