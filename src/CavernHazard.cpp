#include "infinity/CavernHazard.hpp"
#include <algorithm>

namespace infinity {
std::optional<IceFault> iceFault(const Borehole& bore) {
    const auto& geology = bore.geology;
    if (!bore.geologyVersion || geology.temperature >= 250 || geology.fractions[0] <= .08)
        return std::nullopt;
    const auto faces = cavernFinds(bore);
    const auto face = std::find_if(faces.rbegin(), faces.rend(), [](const CavernFind& find) {
        return find.mineral == Mineral::Ice;
    });
    if (face == faces.rend())
        return std::nullopt;
    const double weakness = std::clamp(.45 * face->concentration + .40 * geology.fractureStrength +
                                       .15 * geology.sediment, 0., 1.);
    return IceFault{*face, weakness};
}

double IceStress::update(uint64_t hole, const IceFault& fault, glm::dvec3 localEye, double speed,
                         double shipScale, bool cutting, double dt) {
    if (bore != hole) {
        *this = {};
        bore = hole;
    }
    if (falling()) {
        fallAge += dt;
        if (fallAge >= 2.4)
            fallAge = -1;
        return 0;
    }
    const double distance = glm::length(localEye - fault.face.local);
    const double closeFlight = shipScale < .1 && distance < 3.5
        ? std::clamp((speed / shipScale - 6.) / 18., 0., 1.) : 0.;
    // An uninterrupted cut always gets several seconds of warning. Tiny
    // craft can inspect safely at assay speed, or coast away to unload the face.
    const double forcing = cutting ? .072 + .029 * fault.weakness : closeFlight * (.25 + .07 * fault.weakness);
    load = std::clamp(load + (forcing > 0 ? forcing : -.33) * dt, 0., 1.);
    if (load < 1)
        return 0;
    load = 0;
    fallAge = 0;
    return (1.4 + fault.weakness) * std::clamp(1. - distance / 12., 0., 1.);
}

std::array<IceCrack, 24> iceCracks(const Borehole& bore, const IceFault& fault) {
    const auto& face = fault.face;
    const glm::dvec3 centre(0, -bore.finalDepth(), 0);
    const auto frame = tangentFrame(glm::normalize(bore.entry));
    const auto fracture = glm::inverse(frame) * bore.geology.fracture;
    auto across = fracture - face.inward * glm::dot(fracture, face.inward);
    if (glm::length(across) < 1e-4)
        across = tangentFrame(face.inward) * glm::dvec3(1, 0, 0);
    across = glm::normalize(across);
    const auto side = glm::cross(face.inward, across);
    const auto wall = [&](double x, double y) {
        const auto ray = glm::normalize(face.local - centre + across * x + side * y);
        return centre + ray * (boreRoomRadius(bore, ray) - .06);
    };
    const Random random{face.id};
    std::array<IceCrack, 24> cracks;
    // Branches grow away from the same weak spot along the inherited fault.
    for (unsigned branch = 0; branch < 4; ++branch) {
        const double sign = branch & 1 ? 1. : -1.;
        const double slope = branch < 2 ? .12 : -.65;
        auto last = wall(0, 0);
        for (unsigned step = 0; step < 6; ++step) {
            const auto index = branch * 6 + step;
            const double x = sign * (step + 1.) * (branch < 2 ? .53 : .40);
            const auto next = wall(x, x * slope + random.signedUnit(index) * .22);
            const double threshold = step < 2 && branch < 2 ? 0. : .26 + step * .115;
            cracks[index] = {last, next, threshold};
            last = next;
        }
    }
    return cracks;
}

ProbeMotes iceFallMotes(const Borehole& bore, const IceFault& fault, double age) {
    ProbeMotes out;
    if (age < 0 || age >= 2.4)
        return out;
    const auto& face = fault.face;
    const auto frame = tangentFrame(face.inward);
    const glm::dvec3 centre(0, -bore.finalDepth(), 0);
    const Random random{face.id ^ 0x49434546414c4cULL};
    for (unsigned i = 0; i < 28; ++i) {
        const double t = age - random.unit(i * 4) * .42;
        if (t <= 0)
            continue;
        auto point = face.local + face.inward * (.18 + t * (1 + random.unit(i * 4 + 1))) +
            frame * glm::dvec3(random.signedUnit(i * 4 + 2) * t, 0, random.signedUnit(i * 4 + 3) * t);
        point.y -= 2.1 * t * t;
        const auto radial = point - centre;
        if (glm::length(radial) >= boreRoomRadius(bore, radial))
            continue;
        const bool chip = i < 20;
        out.items[out.count++] = {point, {.50f, .83f, 1.f}, chip ? .12 : .55 + t * .28,
                                  float(1 - age / 2.4), chip, false, {1, .5, 1}};
    }
    return out;
}
} // namespace infinity
