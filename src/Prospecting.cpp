#include "infinity/Prospecting.hpp"
#include "infinity/SurfaceProvince.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
namespace infinity {
std::string_view exposureName(ExposureType t) {
    switch (t) {
    case ExposureType::Vein:
        return "FRACTURED MINERAL VEINS";
    case ExposureType::Ejecta:
        return "IMPACT EJECTA";
    case ExposureType::CarbonSeam:
        return "CARBONACEOUS SEAMS";
    case ExposureType::IceFracture:
        return "EXPOSED ICE FRACTURES";
    default:
        return "BEDROCK OUTCROPS";
    }
}
ResourceExposure resourceExposure(const Node& n, glm::dvec3 d, const GeologySample& g) {
    ResourceExposure out;
    if (!hasSolidSurface(n))
        return out;
    auto c = n.environment.chemistry;
    c.normalize();
    const double fracture = std::clamp(g.convergence + g.shear + g.divergence * .6, 0., 2.);
    const double impact = std::clamp(g.cratering * 2000, 0., 1.);
    const double water = n.geology->liquidWater;
    const double cold = 1 - smoothstep(190, 260, n.environment.temperature);
    // Transport and concentration amplify existing constituents, never create a missing element.
    const double vein = fracture * (.25 + water) + g.volcanism * 2.5;
    const double sediment = g.sediment + g.erosion * 400;
    const double fabric =
        .5 + .5 * fractalNoise(glm::normalize(d) * 64., mix64(n.planetSeed ^ 0x4558504f53555245ULL), 2);
    out.fractions = {c.ice * cold * (1 + 5 * fracture + 4 * n.geology->ice + 3 * g.majorIce),
                     c.metal * (.5 + 4 * vein + 3 * impact + fabric),
                     c.silicate * (.7 + 2 * sediment + .8 * fracture),
                     c.carbon * (.4 + 4 * sediment + 2 * n.environment.waterActivity) *
                         (1 - smoothstep(650, 1500, n.environment.temperature))};
    const double total = std::accumulate(out.fractions.begin(), out.fractions.end(), 0.);
    if (total > 0)
        for (auto& f : out.fractions)
            f /= total;
    const auto primary =
        unsigned(std::max_element(out.fractions.begin(), out.fractions.end()) - out.fractions.begin());
    out.type = primary == 0 && cold > .5                                      ? ExposureType::IceFracture
               : (primary == 3 || (out.fractions[3] > .12 && sediment > .15)) ? ExposureType::CarbonSeam
               : impact > .28                                                 ? ExposureType::Ejecta
               : vein > .32                                                   ? ExposureType::Vein
                                                                              : ExposureType::Layered;
    out.concentration = std::clamp(.25 + vein * .25 + impact * .3 + sediment * .3 + fabric * .15, .15, 1.);
    out.exposure =
        std::clamp(.12 + fracture * .22 + impact * .6 + g.volcanism * .35 + sediment * .18, 0., .75);
    constexpr std::array<glm::vec3, 4> colors{glm::vec3(.30, .78, .91), glm::vec3(.79, .38, .19),
                                              glm::vec3(.62, .51, .74), glm::vec3(.12, .16, .23)};
    for (unsigned k = 0; k < 4; ++k)
        out.color += colors[k] * float(out.fractions[k]);
    return out;
}
LocalCatchment localCatchment(const Node& planet, glm::dvec3 direction) {
    LocalCatchment result;
    if (!hasSolidSurface(planet))
        return result;
    const auto up = glm::normalize(direction);
    const auto frame = tangentFrame(up);
    const double radius = std::pow(10., planet.frame.logRadius);
    const double reach = std::clamp(radius * .009, 3000., 75000.) / radius;
    constexpr std::array<glm::dvec2, 9> offsets{{{0,0}, {1,0}, {-1,0}, {0,1}, {0,-1},
                                                   {.42,.42}, {-.42,.42}, {.42,-.42}, {-.42,-.42}}};
    double weight = 0;
    for (const auto& offset : offsets) {
        const auto d = glm::normalize(up + frame * glm::dvec3(offset.x * reach, 0, offset.y * reach));
        const auto g = geologySample(*planet.geology, d);
        if (planet.geology->liquidWater > .1 && g.heightMetres / radius < -.0008)
            continue;
        const auto exposed = resourceExposure(planet, d, g);
        const double nearby = 1 - glm::length(offset) * .45;
        const double access = .35 + .65 * exposed.exposure;
        for (unsigned k = 0; k < result.grade.size(); ++k)
            result.grade[k] += nearby * exposed.fractions[k] * exposed.concentration * access;
        result.water += nearby * planet.geology->liquidWater * (.3 + std::clamp(g.sediment, 0., 1.) * .7);
        weight += nearby;
    }
    if (weight > 0) {
        for (auto& grade : result.grade)
            grade /= weight;
        result.water /= weight;
    }
    // Silicate is widespread. Valuable accessible ice, metals and carbon
    // can define a catchment even when they form a smaller part of its rock.
    const std::array value{result.grade[0] * 1.5, result.grade[1] * 1.35,
                           result.grade[2] * .6, result.grade[3] * 1.4};
    result.dominant = unsigned(std::max_element(value.begin(), value.end()) - value.begin());
    return result;
}
glm::vec3 exposureColor(const Node& n, glm::dvec3 direction, const GeologySample& field, glm::vec3 base) {
    const double log = n.address.kind() == Kind::Planet ? n.frame.logRadius : n.planetLogRadius;
    if (!hasSolidSurface(n))
        return base;
    const double height = field.heightMetres / std::pow(10., log);
    const double exposedGround = n.geology->liquidWater > .1 ? smoothstep(-.0008, -.0005, height) : 1.;
    if (exposedGround == 0)
        return base;
    const auto province = surfaceProvince(*n.geology, direction, field);
    const auto rock = provinceAlbedo(province, base);
    const auto exposure = resourceExposure(n, direction, field);
    return glm::mix(base, glm::mix(rock, exposure.color, float(exposure.exposure * .55)),
                    float(exposedGround));
}

namespace {
Address surfaceAncestor(Address a) {
    while (a.kind() > Kind::Surface)
        a = a.parent();
    return a;
}
unsigned siteLevel(const Node& p) {
    return unsigned(std::clamp(int(p.frame.logRadius / std::log10(2.)) - 8, 8, 24));
}
bool waterAt(const Node& p, glm::dvec3 d) {
    return p.geology->liquidWater > .1 &&
           geologySample(*p.geology, d).heightMetres / std::pow(10., p.frame.logRadius) < -.0008;
}
double slope(const Universe& u, const Node& p, glm::dvec3 d) {
    return std::acos(std::clamp(glm::dot(u.terrainNormal(p, d), glm::normalize(d)), -1., 1.)) * 57.295779513;
}
Address rockAt(const Universe& u, const Node& p, glm::dvec3 d, unsigned ordinal = 0) {
    auto a = p.address.child(Kind::Surface, encodeTile(tileAt(d, siteLevel(p))))
                 .child(Kind::Mountain, ordinal % 12)
                 .child(Kind::Rock, exposureRockIdFlag | (ordinal % 3));
    u.describe(a);
    return a;
}
double footing(const Universe& u, const Node& p, glm::dvec3 d) {
    const auto frame = tangentFrame(d);
    const double r = std::pow(10., p.frame.logRadius), h = u.terrainHeight(p, d);
    double relief = 0;
    for (auto offset : {glm::dvec3(-8, 0, 0), glm::dvec3(8, 0, 0), glm::dvec3(0, 0, -8), glm::dvec3(0, 0, 8)})
        relief = std::max(relief, std::abs(u.terrainHeight(p, glm::normalize(d + frame * offset / r)) - h));
    return relief;
}
} // namespace
ResourceStock legacyProspectResources(const Universe& u, const Address& target) {
    if (target.kind() < Kind::Surface)
        return {};
    auto s = surfaceAncestor(target);
    auto p = u.describe(s.parent());
    if (!hasSolidSurface(p))
        return {};
    const auto key = p.address.child(Kind::Surface, encodeTile(tileAt(u.describe(s).geographicDirection, 7)));
    const auto n = u.describe(key);
    auto c = n.environment.chemistry;
    c.normalize();
    const auto mass = int64_t(std::clamp(material(n.environment.mineral).density * .5, 100., 5000.));
    ResourceStock out{};
    const std::array f{c.ice, c.metal, c.silicate, c.carbon};
    int64_t left = mass;
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = i == 3 ? left : int64_t(std::floor(mass * f[i]));
        left -= out[i];
    }
    return out;
}
std::vector<ProspectSite> generateProspects(const Universe& u, const Node& p, Tile region,
                                            const std::atomic<bool>* cancel) {
    std::vector<ProspectSite> out;
    out.reserve(maxRegionalProspects);
    if (!hasSolidSurface(p) || p.address.kind() != Kind::Planet)
        return out;
    if (region.level != 7)
        throw std::invalid_argument("Prospecting requires a canonical level-7 region");
    const auto pool = p.address.child(Kind::Surface, encodeTile(region));
    const Random r{mix64(seedOf(pool, u.seed()) ^ 0x50524f5350454354ULL)};
    const auto centre = tileDirection(region);
    const auto tangent = tangentFrame(centre);
    const double radius = std::pow(10., p.frame.logRadius), span = radius / 128.;
    std::set<Address> used;
    for (unsigned slot = 0; slot < maxRegionalProspects; ++slot) {
        if (cancel && cancel->load())
            return {};
        double best = -1e30;
        std::optional<Node> selected;
        for (unsigned attempt = 0; attempt < 5; ++attempt) {
            const auto k = slot * 32 + attempt * 3;
            const double x = (double(slot % 4) - 1.5 + r.signedUnit(k) * .35) * span * .46;
            const double z = (double(slot / 4) - 1 + r.signedUnit(k + 1) * .35) * span * .6;
            const auto d = glm::normalize(centre + tangent * glm::dvec3(x, 0, z) / radius);
            const auto rock = u.describe(rockAt(u, p, d, attempt));
            const auto point = rock.geographicDirection;
            if (encodeTile(tileAt(point, 7)) != pool.id() || waterAt(p, point))
                continue;
            const auto exposure = resourceExposure(p, point, geologySample(*p.geology, point));
            const auto score =
                exposure.concentration * 12 - slope(u, p, point) * .9 - footing(u, p, point) * 1.5;
            if (score > best) {
                best = score;
                selected = rock;
            }
        }
        if (!selected || !used.insert(selected->address.parent().parent()).second)
            continue;
        auto rock = *selected;
        const auto d = rock.geographicDirection;
        ProspectSite s;
        s.surface = rock.address.parent().parent();
        s.pool = pool;
        s.direction = d;
        s.outcropMetres = d * (radius + u.terrainHeight(p, d));
        // Put the ship's approach point outside the physical formation, on the best nearby footing.
        auto clearing = d;
        double bestGround = 1e30;
        const auto frame = tangentFrame(d);
        const double standOff = std::pow(10., rock.frame.logRadius) * 3 + 35.;
        for (unsigned k = 0; k < 8; ++k) {
            const double a = k * .7853981633974483;
            const auto q = glm::normalize(
                d + frame * glm::dvec3(std::cos(a) * standOff, 0, std::sin(a) * standOff) / radius);
            if (encodeTile(tileAt(q, 7)) != pool.id() || waterAt(p, q))
                continue;
            const double score = slope(u, p, q) + footing(u, p, q) * 2;
            if (score < bestGround) {
                bestGround = score;
                clearing = q;
            }
        }
        s.direction = clearing;
        s.metres = clearing * (radius + u.terrainHeight(p, clearing));
        s.slopeDegrees = slope(u, p, clearing);
        s.clearanceMetres = footing(u, p, clearing);
        s.easy = s.slopeDegrees < 12 && s.clearanceMetres < 2;
        const auto e = resourceExposure(p, d, geologySample(*p.geology, d));
        s.type = e.type;
        s.color = e.color;
        s.concentration = e.concentration;
        s.resource = unsigned(std::max_element(e.fractions.begin(), e.fractions.end()) - e.fractions.begin());
        s.rocks.reserve(4);
        s.rocks.push_back(rock);
        // Retain real geological objects at their exact physical sizes and positions.
        for (unsigned i = 1; i < 4; ++i)
            s.rocks.push_back(u.describe(rock.address.parent().child(
                Kind::Rock,
                i < 3 ? (exposureRockIdFlag | ((rock.address.id() + i) % 3)) : (rock.address.id() % 24))));
        s.parentFrames = u.frames(rock.address.parent());
        s.name = std::string(exposureName(s.type)) + " " +
                 std::to_string((mix64(seedOf(s.surface, u.seed())) % 9000) + 1000);
        out.push_back(std::move(s));
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.easy != b.easy ? a.easy > b.easy : a.concentration > b.concentration;
    });
    return out;
}
Address samplingSourceNear(const Universe& u, const Node& p, glm::dvec3 metres, double exclusion) {
    const auto up = glm::normalize(metres);
    const auto frame = tangentFrame(up);
    const double radius = std::pow(10., p.frame.logRadius);
    const unsigned sampleLevel = unsigned(std::clamp(int(p.frame.logRadius / std::log10(2.)) - 4, 8, 24));
    auto sampleAt = [&](glm::dvec3 d, unsigned i) {
        return p.address.child(Kind::Surface, encodeTile(tileAt(d, sampleLevel)))
            .child(Kind::Mountain, i % 12)
            .child(Kind::Rock, i % 24);
    };
    const double range = std::max(950., exclusion + 700.);
    double best = -1e30;
    std::optional<Node> chosen;
    for (unsigned i = 0; i < 16; ++i) {
        const double angle = i * 2.399963229728653;
        const auto d = glm::normalize(
            up + frame * glm::dvec3(std::cos(angle) * range, 0, std::sin(angle) * range) / radius);
        const auto rock = u.describe(sampleAt(d, i));
        const auto q = rock.geographicDirection;
        if (waterAt(p, q))
            continue;
        const double score = -slope(u, p, q) - footing(u, p, q) * 2;
        if (score > best) {
            best = score;
            chosen = rock;
        }
    }
    if (chosen)
        return chosen->address;
    return sampleAt(glm::normalize(up + frame * glm::dvec3(range, 0, 0) / radius), 0);
}
} // namespace infinity
