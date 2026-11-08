#define GLM_FORCE_INLINE
#include "infinity/MicroView.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
MicroView extractMicroView(const Universe& u, const Navigation& nav, double hullMetres,
                           const Geometry* visible) {
    MicroView v;
    if (nav.anchor.kind() < Kind::Rock || !std::isfinite(hullMetres) || hullMetres <= 0)
        return v;
    const auto n = u.describe(nav.anchor);
    const double units = std::pow(10., nav.logDistance), radius = std::pow(10., n.frame.logRadius);
    const auto eye = nav.cameraPosition() * units;
    if (!std::isfinite(glm::length(eye)))
        return v;
    v.active = true;
    v.atomic = n.address.kind() == Kind::Lattice;
    v.address = n.address;
    v.name = n.label();
    const auto& m = material(n.environment.mineral);
    v.mineral = m.name;
    v.formula = m.formula;
    const auto cell = crystalCell(n.environment.mineral, n.environment);
    v.phase = processName(cell.phase);
    v.cellMetres = glm::length(cell.basisMetres[0]);
    // Vertical field at the addressed object's centre for the existing 70°
    // ship lens. Unlike render units, this does not change on a pure rebase.
    v.fieldMetres = 2 * glm::length(eye) * std::tan(35. * std::numbers::pi / 180.);
    const double cellScale = std::max({glm::length(cell.basisMetres[0]), glm::length(cell.basisMetres[1]),
                                       glm::length(cell.basisMetres[2]), 1e-30});
    for (unsigned i = 0; i < 8; ++i) {
        const auto p = glm::dvec3(i & 1, (i >> 1) & 1, (i >> 2) & 1) - .5;
        v.cellCorners[i] = glm::vec3(cell.basisMetres * p / cellScale);
    }
    v.cellSites.reserve(cell.sites.size());
    for (const auto& s : cell.sites)
        v.cellSites.push_back({glm::vec3(cell.basisMetres * (s.fractional - .5) / cellScale), s.element});
    if (!v.atomic) {
        const auto p = eye / radius;
        const double field = n.address.kind() == Kind::Rock ? u.rockSdf(n, p) : u.grainSdf(n, p);
        if (std::isfinite(field)) {
            v.proximityKnown = true;
            v.insideMaterial = field < 0;
            v.proximityMetres = std::max(0., field * radius - hullMetres / 6.);
        }
    } else if (visible && visible->address == n.address && visible->referenceFrame == n.address &&
               visible->descriptor == descriptorFingerprint(n) && !visible->lattice.atoms.empty()) {
        const double atomUnits = std::pow(10., visible->logUnits);
        const auto p = eye / atomUnits;
        double gap = 1e100;
        for (const auto& atom : visible->lattice.atoms)
            gap = std::min(gap, (glm::length(p - glm::dvec3(atom.position)) - atom.radius) * atomUnits);
        v.atomsKnown = v.proximityKnown = true;
        v.atoms = visible->lattice.atoms.size();
        v.bonds = visible->lattice.bonds.size();
        v.insideMaterial = gap < 0;
        v.proximityMetres = std::max(0., gap - hullMetres / 6.);
    }
    return v;
}
} // namespace infinity
