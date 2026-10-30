#include "infinite/StellarEvolution.hpp"
#include <algorithm>
#include <cmath>
namespace infinite {
bool isDiverseStar(uint64_t id) {
    return (id & stellarIdFlag) != 0;
}
double mainSequenceLifetime(double m) {
    // Bounded population proxy in Gyr, not a stellar-structure solver.
    return 10 * std::pow(m, -2.5) + .003;
}
StellarProcess stellarEvolution(uint64_t seed, unsigned index, double galaxyAge, bool starForming) {
    const Random r{featureSeed(seed, Feature::StellarEvolution, stellarRecipeVersion)};
    StellarProcess s;
    const auto u = [&](unsigned i) { return r.unit(i); };
    const unsigned channel = index % 20;
    if (channel < 10) {
        // Stratification exposes rare hot stars; old populations enforce survival.
        const double lo[] = {.09, .15, .3, .55, .8, 1.05, 1.4, 2.1, 5, 18};
        const double hi[] = {.15, .3, .55, .8, 1.05, 1.4, 2.1, 5, 18, 45};
        s.massSolar = lo[channel] * std::pow(hi[channel] / lo[channel], u(0));
        if (!starForming)
            s.massSolar = std::min(s.massSolar, std::pow(10 / std::max(.01, galaxyAge * 1.1), .4));
        s.initialMassSolar = s.massSolar;
        s.mainSequenceGyr = mainSequenceLifetime(s.massSolar);
        s.ageGyr = starForming ? std::min(galaxyAge, s.mainSequenceGyr * (.05 + .8 * u(1)))
                               : galaxyAge * (.8 + .19 * u(1));
        const double m = s.massSolar;
        const double lum = m < .43 ? .23 * std::pow(m, 2.3) : m < 2 ? std::pow(m, 4) : 1.5 * std::pow(m, 3.5);
        s.radiusMetres = solarRadiusMetres * std::pow(m, m < 1 ? .8 : .57);
        s.temperatureK = 5772 * std::pow(lum / std::pow(s.radiusMetres / solarRadiusMetres, 2), .25);
        s.spots = m < .6 ? .28 : .08;
        s.wind = std::clamp(m / 15., .1, .9);
        s.rotationSeconds = (m < .6 ? 12 : m > 2 ? 2 : 25) * 86400 * (.6 + u(2));
        s.maturePlanets = s.ageGyr >= .01;
    } else {
        const StellarKind kinds[] = {StellarKind::Protostar,     StellarKind::RedGiant,
                                     StellarKind::RedSupergiant, StellarKind::BlueSupergiant,
                                     StellarKind::WolfRayet,     StellarKind::WhiteDwarf,
                                     StellarKind::NeutronStar,   StellarKind::BrownDwarf,
                                     StellarKind::CarbonStar,    StellarKind::WhiteDwarf};
        s.kind = kinds[channel - 10];
        if (!starForming && (s.kind == StellarKind::Protostar || s.kind == StellarKind::RedSupergiant ||
                             s.kind == StellarKind::BlueSupergiant || s.kind == StellarKind::WolfRayet))
            s.kind = StellarKind::WhiteDwarf;
        s.ageGyr = std::max(.00001, galaxyAge * (.6 + .39 * u(1)));
        switch (s.kind) {
        case StellarKind::Protostar:
            s.initialMassSolar = s.massSolar = .15 + 1.8 * u(0);
            s.ageGyr = std::min(galaxyAge, .00005 + .001 * u(1));
            s.radiusMetres = solarRadiusMetres * (2 + 3 * u(2));
            s.temperatureK = 2800 + 1800 * u(3);
            s.convectionScale = 14;
            s.spots = .35;
            s.wind = .6;
            s.maturePlanets = false;
            break;
        case StellarKind::RedGiant:
        case StellarKind::CarbonStar: {
            const double phase = s.kind == StellarKind::CarbonStar ? 1.16 : 1.06;
            s.initialMassSolar =
                std::clamp(std::pow(10 / std::max(.01, s.ageGyr / phase - .003), .4), .8, 7.5);
            s.ageGyr = std::min(galaxyAge, mainSequenceLifetime(s.initialMassSolar) * phase);
            s.massSolar = s.initialMassSolar * (s.kind == StellarKind::CarbonStar ? .7 : .94);
            s.radiusMetres =
                solarRadiusMetres * (s.kind == StellarKind::CarbonStar ? 140 + 180 * u(2) : 18 + 70 * u(2));
            s.temperatureK = s.kind == StellarKind::CarbonStar ? 2400 + 700 * u(3) : 3400 + 1400 * u(3);
            s.convectionScale = s.kind == StellarKind::CarbonStar ? 6 : 10;
            s.spots = .08;
            s.wind = s.kind == StellarKind::CarbonStar ? .8 : .4;
            s.survivorInnerAu = s.radiusMetres / 1.495978707e11 * 12;
            break;
        }
        case StellarKind::RedSupergiant:
        case StellarKind::BlueSupergiant:
        case StellarKind::WolfRayet:
            s.initialMassSolar = s.kind == StellarKind::WolfRayet ? 28 + 38 * u(0) : 10 + 15 * u(0);
            s.massSolar = s.initialMassSolar * (s.kind == StellarKind::WolfRayet ? .38 : .84);
            s.ageGyr = mainSequenceLifetime(s.initialMassSolar) * (1.03 + .05 * u(1));
            s.radiusMetres = solarRadiusMetres * (s.kind == StellarKind::RedSupergiant ? 350 + 600 * u(2)
                                                  : s.kind == StellarKind::WolfRayet   ? 1.5 + 5 * u(2)
                                                                                       : 18 + 30 * u(2));
            s.temperatureK = s.kind == StellarKind::RedSupergiant ? 3200 + 900 * u(3)
                             : s.kind == StellarKind::WolfRayet   ? 50000 + 90000 * u(3)
                                                                  : 14000 + 15000 * u(3);
            s.convectionScale = s.kind == StellarKind::RedSupergiant ? 4 : 35;
            s.spots = .02;
            s.wind = s.kind == StellarKind::WolfRayet ? 1.5 : .8;
            s.maturePlanets = false;
            break;
        case StellarKind::WhiteDwarf: {
            const double turnoff = std::pow(10 / std::max(.001, s.ageGyr * .7 - .003), .4);
            s.initialMassSolar = std::clamp(turnoff + .2 + 2.5 * u(0), 1., 7.5);
            s.massSolar = std::clamp(.109 * s.initialMassSolar + .394, .5, 1.25);
            s.coolingAgeGyr = std::max(.00001, s.ageGyr - mainSequenceLifetime(s.initialMassSolar) * 1.15);
            const double ratio = s.massSolar / 1.44;
            s.radiusMetres =
                solarRadiusMetres * .0112 * std::sqrt(std::pow(ratio, -2. / 3) - std::pow(ratio, 2. / 3));
            s.temperatureK = std::clamp(11000 * std::pow(s.coolingAgeGyr + .03, -.27), 3500., 100000.);
            s.survivorInnerAu = 2 * s.initialMassSolar / s.massSolar;
            s.convectionScale = 160;
            s.spots = 0;
            s.wind = 0;
            break;
        }
        case StellarKind::NeutronStar:
            s.initialMassSolar = 8 + 15 * u(0);
            s.massSolar = 1.2 + .8 * u(2);
            s.coolingAgeGyr = std::max(.00001, s.ageGyr - mainSequenceLifetime(s.initialMassSolar) * 1.1);
            s.radiusMetres = 11000 + 2500 * u(3);
            s.temperatureK = std::clamp(150000 * std::pow(s.coolingAgeGyr + .0001, -.2), 50000., 1500000.);
            s.rotationSeconds = .03 + 2 * u(4);
            s.convectionScale = 0;
            s.spots = 0;
            s.wind = .1;
            s.maturePlanets = false;
            break;
        case StellarKind::BrownDwarf:
            if (starForming)
                s.ageGyr = .01 + (galaxyAge - .01) * std::pow(u(1), 2);
            s.initialMassSolar = s.massSolar = .015 + .059 * u(0);
            s.coolingAgeGyr = s.ageGyr;
            s.radiusMetres = solarRadiusMetres * (.085 + .02 * std::exp(-s.ageGyr));
            s.temperatureK = std::clamp(
                1600 * std::pow(s.massSolar / .05, .65) * std::pow(s.ageGyr + .03, -.28), 280., 2400.);
            s.rotationSeconds = (2 + 10 * u(4)) * 3600;
            s.convectionScale = 12;
            s.spots = .4;
            s.wind = 0;
            break;
        default:
            break;
        }
        s.mainSequenceGyr = mainSequenceLifetime(s.initialMassSolar);
    }
    s.luminositySolar = std::pow(s.radiusMetres / solarRadiusMetres, 2) * std::pow(s.temperatureK / 5772, 4);
    if (s.massSolar > 8) {
        // Electron-scattering Eddington envelope: helium-rich stripped stars
        // permit a higher L/M than hydrogen-rich envelopes. Reconcile radius
        // with temperature rather than retaining an impossible random L/M.
        const double ceiling = (s.kind == StellarKind::WolfRayet ? 60000 : 32000) * s.massSolar;
        if (s.luminositySolar > ceiling) {
            s.luminositySolar = ceiling;
            s.radiusMetres = solarRadiusMetres * std::sqrt(ceiling) / std::pow(s.temperatureK / 5772, 2);
        }
    }
    // Bound the equatorial speed below centrifugal breakup. Expanded giants
    // cannot retain the default dwarf rotation period after envelope growth.
    const double breakupSeconds =
        6.283185307179586 * std::sqrt(std::pow(s.radiusMetres, 3) / (1.32712440018e20 * s.massSolar));
    s.rotationSeconds = std::max(s.rotationSeconds, breakupSeconds * 2);
    return s;
}
std::string_view stellarKindName(StellarKind kind) {
    constexpr std::string_view names[] = {"Main sequence",   "Protostar",   "Red giant",   "Red supergiant",
                                          "Blue supergiant", "Wolf-Rayet",  "White dwarf", "Neutron star",
                                          "Brown dwarf",     "Carbon giant"};
    return names[unsigned(kind)];
}
std::string spectralClass(double t, bool brown) {
    const double bounds[] = {30000, 10000, 7500, 6000, 5200, 3700, 0};
    if (brown)
        return t >= 1300 ? "L" : t >= 600 ? "T" : "Y";
    constexpr char classes[] = "OBAFGKM";
    for (unsigned i = 0; i < 7; i++)
        if (t >= bounds[i])
            return std::string(1, classes[i]);
    return "M";
}
glm::vec3 stellarRadiance(const Node& n) {
    if (!n.stellar)
        return stellarColor(n.temperatureK);
    // Approximate blackbody RGB under cockpit HDR exposure; not a spectrum solver.
    const double t = std::clamp(n.temperatureK, 1000., 40000.) / 100;
    double r = t <= 66 ? 255 : 329.698727446 * std::pow(t - 60, -.1332047592);
    double g = t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661
                       : 288.1221695283 * std::pow(t - 60, -.0755148492);
    double b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307;
    glm::vec3 c(std::clamp(r / 255, 0., 1.), std::clamp(g / 255, 0., 1.), std::clamp(b / 255, 0., 1.));
    if (n.stellar->kind == StellarKind::CarbonStar)
        c *= glm::vec3(1, .64f, .38f);
    if (n.stellar->kind == StellarKind::BrownDwarf)
        c *= float(std::clamp(std::pow(n.temperatureK / 2400, 2), .025, 1.));
    return c;
}
std::array<glm::vec4, 2> stellarPresentation(const Node& n) {
    if (!n.stellar)
        return {};
    const auto& s = *n.stellar;
    return {glm::vec4(unsigned(s.kind) + 1, float(s.convectionScale), float(s.spots), float(s.wind)),
            glm::vec4(float(Random{n.seed}.unit(3)), float(1 / s.rotationSeconds), float(s.temperatureK), 1)};
}
double stellarDiscInnerAu(const Node& n) {
    const double original = std::max(.25, .04 * std::sqrt(n.luminositySolar));
    if (!n.stellar)
        return original;
    return std::max({original, n.stellarRadiusMetres * 8 / 1.495978707e11, n.stellar->survivorInnerAu});
}
} // namespace infinite
