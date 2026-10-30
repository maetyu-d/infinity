#pragma once
#include "Universe.hpp"
namespace infinite {
inline constexpr uint32_t stellarRecipeVersion = 1;
// New stars never reuse IDs from the existing resolved/LOD catalogue.
inline constexpr uint64_t stellarIdFlag = uint64_t(1) << 62;
inline constexpr unsigned stellarPopulationCount = 192;
inline constexpr double solarRadiusMetres = 6.957e8;
enum class StellarKind : uint8_t {
    MainSequence,
    Protostar,
    RedGiant,
    RedSupergiant,
    BlueSupergiant,
    WolfRayet,
    WhiteDwarf,
    NeutronStar,
    BrownDwarf,
    CarbonStar
};
struct StellarProcess {
    StellarKind kind = StellarKind::MainSequence;
    double initialMassSolar = 1, massSolar = 1, ageGyr = 0, mainSequenceGyr = 10;
    double radiusMetres = solarRadiusMetres, temperatureK = 5772, luminositySolar = 1;
    double coolingAgeGyr = 0, rotationSeconds = 2160000;
    double convectionScale = 58, spots = .1, wind = .2, survivorInnerAu = 0;
    bool maturePlanets = true;
};
bool isDiverseStar(uint64_t id);
StellarProcess stellarEvolution(uint64_t seed, unsigned index, double galaxyAgeGyr, bool starForming);
double mainSequenceLifetime(double initialMassSolar);
std::string_view stellarKindName(StellarKind);
std::string spectralClass(double temperatureK, bool brownDwarf = false);
glm::vec3 stellarRadiance(const Node&);
std::array<glm::vec4, 2> stellarPresentation(const Node&);
double stellarDiscInnerAu(const Node&);
} // namespace infinite
