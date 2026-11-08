#define GLM_FORCE_INLINE
#include "infinity/MajorLandforms.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
std::string_view majorLandformName(MajorLandformType type) {
    constexpr std::array names{"Supervolcanic Caldera", "Giant Shield Volcano", "Continental Glacier",
                               "Great Rift", "Multi-ring Impact Basin"};
    return names.at(size_t(type));
}
std::shared_ptr<const MajorLandform> generateMajorLandform(const PlanetProcess& p, const Environment& e) {
    if (p.type == GeologyType::GasEnvelope)
        return {};
    const double radius = std::pow(10., p.logRadius);
    if (radius < 180000)
        return {};
    // Frozen society admission has probability at most .96. Use only its
    // rejected tail (or physically excluded worlds), never move an old town.
    const double gravity = 4.188790204786391 * 6.67430e-11 * material(e.mineral).density * radius;
    if (e.temperature >= 85 && e.temperature <= 650 && gravity <= 32 &&
        Random{featureSeed(p.seed, Feature::SurfaceSociety, 1)}.unit(0) <= .96)
        return {};
    const Random r{featureSeed(p.seed, Feature::LocalRelief, 0x6d616a6f720001ULL)};
    if (r.unit(0) >= .30)
        return {};
    auto f = std::make_shared<MajorLandform>();
    f->seed = r.key;
    f->radiusMetres = radius;
    const bool frozen = p.type == GeologyType::IceShell && e.temperature < 245 && p.ice > .25;
    const bool volcanic = p.activity > .045 && e.chemistry.silicate > .25 && e.peakTemperature > 1250;
    if (frozen)
        f->type = MajorLandformType::IceSheet;
    else if (volcanic && r.unit(1) < .65)
        f->type = r.unit(2) < .55 ? MajorLandformType::Supercaldera : MajorLandformType::ShieldVolcano;
    else
        f->type = r.unit(3) < .5 ? MajorLandformType::Rift : MajorLandformType::ImpactBasin;
    // Start from inherited geological structures, then choose the highest dry
    // candidate. Sampling the unmodified process cannot recurse into this layer.
    double best = -1e100;
    for (unsigned i = 0; i < 12; ++i) {
        glm::dvec3 d;
        if (frozen) {
            const auto pole = glm::normalize(p.plates[0].angularVelocity) * (r.unit(4) < .5 ? -1. : 1.);
            const auto frame = tangentFrame(pole);
            const double a = i * 2.399963229728653;
            d = glm::normalize(pole + frame * glm::dvec3(.3 * std::cos(a), 0, .3 * std::sin(a)));
        } else if (f->type == MajorLandformType::Supercaldera || f->type == MajorLandformType::ShieldVolcano)
            d = p.hotspots[i % p.hotspots.size()];
        else if (f->type == MajorLandformType::ImpactBasin)
            d = p.impacts[i].centre;
        else
            d = glm::normalize(p.plates[i].centre + p.plates[(i + 1) % p.plates.size()].centre);
        const auto g = geologySample(p, d);
        const double score =
            g.heightMetres +
            (f->type == MajorLandformType::ImpactBasin ? p.impacts[i].survival * radius * .005 : 0);
        if (score > best) {
            best = score;
            f->centre = d;
        }
    }
    if (p.liquidWater > .1 && geologySample(p, f->centre).heightMetres < -radius * .0005)
        return {};
    auto along = p.plates[0].angularVelocity - f->centre * glm::dot(p.plates[0].angularVelocity, f->centre);
    if (glm::length(along) < 1e-8)
        along = glm::cross(f->centre, glm::dvec3(1, .1, 0));
    if (frozen) {
        // Route the outlet ice down the inherited catchment gradient.
        const auto frame = tangentFrame(f->centre);
        const auto east = frame * glm::dvec3(.0005, 0, 0);
        const auto north = frame * glm::dvec3(0, 0, .0005);
        const double dx = geologySample(p, glm::normalize(f->centre + east)).heightMetres -
                          geologySample(p, glm::normalize(f->centre - east)).heightMetres;
        const double dz = geologySample(p, glm::normalize(f->centre + north)).heightMetres -
                          geologySample(p, glm::normalize(f->centre - north)).heightMetres;
        const auto downhill = -east * dx - north * dz;
        if (glm::length(downhill) > 1e-8)
            along = downhill;
    }
    f->along = glm::normalize(along);
    f->across = glm::normalize(glm::cross(f->centre, f->along));
    const double scale = .7 + .6 * r.unit(5);
    const double gravityLimit = std::clamp(10. / std::max(1., gravity), .35, 2.5);
    switch (f->type) {
    case MajorLandformType::Supercaldera:
        f->widthMetres = std::min(radius * .035, 35000. * scale);
        f->lengthMetres = f->widthMetres * (.8 + .4 * r.unit(6));
        f->reliefMetres = std::min(f->widthMetres * .13, 4500. * gravityLimit);
        break;
    case MajorLandformType::ShieldVolcano:
        f->widthMetres = std::min(radius * .10, 220000. * scale);
        f->lengthMetres = f->widthMetres * .85;
        f->reliefMetres = std::min(f->widthMetres * .085, 17000. * gravityLimit);
        break;
    case MajorLandformType::IceSheet:
        f->widthMetres = std::min(radius * .09, 350000. * scale);
        f->lengthMetres = f->widthMetres * 1.4;
        f->reliefMetres = std::min(f->widthMetres * .025, 4000. * gravityLimit);
        break;
    case MajorLandformType::Rift:
        f->widthMetres = std::min(radius * .018, 50000. * scale);
        f->lengthMetres = std::min(radius * .12, f->widthMetres * 7.);
        f->reliefMetres = std::min(f->widthMetres * .18, 7000. * gravityLimit);
        break;
    case MajorLandformType::ImpactBasin:
        f->widthMetres = std::min(radius * .08, 180000. * scale);
        f->lengthMetres = f->widthMetres;
        f->reliefMetres = std::min(f->widthMetres * .045, 7000. * gravityLimit);
        break;
    }
    f->activity = volcanic ? std::clamp(p.activity * 1.8, 0., 1.) : 0;
    return f;
}
MajorLandformSample sampleMajorLandform(const MajorLandform& f, glm::dvec3 direction) {
    const auto d = glm::normalize(direction);
    if (glm::dot(d, f.centre) < .9)
        return {};
    const double x = glm::dot(d, f.along) * f.radiusMetres / f.lengthMetres;
    const double z = glm::dot(d, f.across) * f.radiusMetres / f.widthMetres;
    const double q = std::sqrt(x * x + z * z);
    if (q >= 2.3)
        return {};
    const double taper = 1 - smoothstep(1.8, 2.3, q);
    const auto bell = [](double a, double width) { return std::exp(-a * a / (width * width)); };
    MajorLandformSample s;
    s.coverage = (1 - smoothstep(1., 2.3, q));
    double h = 0;
    switch (f.type) {
    case MajorLandformType::Supercaldera: {
        const double floor = 1 - smoothstep(.68, .92, q);
        h = .24 * bell(q, 1.4) - floor * .8 + .44 * bell(q - .98, .11) + .24 * bell(q, .22);
        s.fracture = .8 * bell(q - .88, .15) * taper;
        s.heat = f.activity * bell(q, .24);
        break;
    }
    case MajorLandformType::ShieldVolcano: {
        const double dome = std::pow(std::max(0., 1 - q / 1.8), 2);
        h = dome - .38 * bell(q, .13) + .055 * bell(q - .20, .055);
        // Lobate apron and channels remain fixed to the edifice, never time noise.
        h += .045 * std::sin(std::atan2(z, x) * 11 + q * 8) * smoothstep(.2, .5, q) * dome;
        s.heat = f.activity * bell(q, .12);
        s.fracture = .3 * bell(q - .2, .07);
        break;
    }
    case MajorLandformType::IceSheet: {
        const double dome = std::sqrt(std::max(0., 1 - q * q));
        // Accumulation dome feeds three outlet tongues and scoured U troughs.
        const double outlet = smoothstep(-.35, .65, x) * (1 - smoothstep(1.2, 2.1, x));
        const double channels =
            bell(z - .34 - .05 * std::sin(x * 4), .13) + bell(z + .37, .16) + bell(z, .10);
        h = dome + outlet * channels * .22;
        const double margin = bell(q - .98, .08);
        const double cracks = (.5 + .5 * std::sin(x * 115 + std::sin(z * 32) * 2));
        s.fracture = std::clamp(margin * .85 + outlet * channels * cracks * .45, 0., 1.) * taper;
        h -= s.fracture * .055;
        h += bell(q - 1.13, .055) * .07; // terminal moraine
        s.ice = std::clamp(dome + outlet * channels, 0., 1.) * taper;
        break;
    }
    case MajorLandformType::Rift: {
        const double edge = 1 - smoothstep(.68, 1.05, std::abs(x));
        const double bend = z + .10 * std::sin(x * 4);
        h = edge * (-.85 * bell(bend, .38) - .28 * bell(bend, .17) + .18 * bell(std::abs(bend) - .65, .15));
        s.fracture = edge * bell(std::abs(bend) - .4, .2) * taper;
        break;
    }
    case MajorLandformType::ImpactBasin:
        h = -.8 * bell(q, .62) + .38 * bell(q - .82, .10) + .21 * bell(q - 1.25, .09) +
            .12 * bell(q - 1.65, .08) + .12 * bell(q, .18);
        s.impact = s.coverage;
        s.fracture = .6 * bell(q - .82, .15) * taper;
        break;
    }
    s.reliefMetres = h * f.reliefMetres * taper;
    return s;
}
Address majorLandformAddress(const Node& planet) {
    const auto& f = *planet.geology->landform;
    const auto level =
        unsigned(std::clamp(std::ceil(std::log2(f.radiusMetres / f.widthMetres * 16.)), 14., 18.));
    auto address = planet.address;
    while (address.kind() > Kind::Planet)
        address = address.parent();
    return address.child(Kind::Surface, encodeTile(tileAt(f.centre, level)));
}
} // namespace infinity
