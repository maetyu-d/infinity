#include "infinite/Universe.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>

namespace infinite {
std::string_view processName(CrystalPhase phase) {
    constexpr std::array names{
        "ALPHA QUARTZ / TRIGONAL SiO2",        "ALPHA/DELTA IRON / BCC",    "GAMMA IRON / FCC",
        "OLIVINE / ORTHORHOMBIC (Mg,Fe)2SiO4", "ICE Ih / OXYGEN FRAMEWORK", "GRAPHITE / AB LAYERS"};
    return names.at(static_cast<size_t>(phase));
}
CrystalCell crystalCell(Mineral mineral, const Environment& e) {
    CrystalCell cell;
    auto& motif = cell.sites;
    double a = material(mineral).cellMetres, alpha = 8e-6, bulkGPa = 40;
    glm::dmat3 basis(1);
    switch (mineral) {
    case Mineral::Iron:
        if (e.temperature >= 1185 && e.temperature < 1667) {
            cell.phase = CrystalPhase::FccIron;
            a = 3.647e-10;
            motif = {{{0, 0, 0}, 26}, {{.5, .5, 0}, 26}, {{.5, 0, .5}, 26}, {{0, .5, .5}, 26}};
        } else {
            cell.phase = CrystalPhase::BccIron;
            motif = {{{0, 0, 0}, 26}, {{.5, .5, .5}, 26}};
        }
        alpha = 12e-6;
        bulkGPa = 170;
        break;
    case Mineral::Quartz: {
        cell.phase = CrystalPhase::AlphaQuartz;
        basis = glm::dmat3({1, 0, 0}, {-.5, std::sqrt(3.0) * .5, 0}, {0, 0, 5.405 / 4.913});
        // P3_121, 3 Si + 6 O. AFLOW hP9 / Wright & Lehmann room-T idealisation.
        constexpr double u = .4697, x = .4133, y = .2672, z = .1188;
        motif = {{{u, 0, 1.0 / 3}, 14}, {{0, u, 2.0 / 3}, 14},         {{-u, -u, 0}, 14},
                 {{x, y, z}, 8},        {{-y, x - y, z + 1.0 / 3}, 8}, {{y - x, -x, z + 2.0 / 3}, 8},
                 {{y, x, -z}, 8},       {{x - y, -y, 2.0 / 3 - z}, 8}, {{-x, y - x, 1.0 / 3 - z}, 8}};
        break;
    }
    case Mineral::Olivine: {
        cell.phase = CrystalPhase::Forsterite;
        // Pnma oP28, transformed from the Pbnm forsterite setting. Eight Mg,
        // four Si and sixteen O; replaces the old seven-site schematic motif.
        basis = glm::dmat3({10.2 / 4.754, 0, 0}, {0, 5.98 / 4.754, 0}, {0, 0, 1});
        motif = {{{0, 0, 0}, 12}, {{.5, 0, .5}, 12}, {{0, .5, 0}, 12}, {{.5, .5, .5}, 12}};
        auto four = [&](double x, double z, uint8_t element) {
            for (auto p : {glm::dvec3(x, .25, z), glm::dvec3(.5 - x, .75, .5 + z), glm::dvec3(-x, .75, -z),
                           glm::dvec3(.5 + x, .25, .5 - z)})
                motif.push_back({p, element});
        };
        four(.277, .991, 12);
        four(.094, .426, 14);
        four(.091, .766, 8);
        four(.447, .221, 8);
        constexpr double x = .163, y = .033, z = .278;
        for (auto p :
             {glm::dvec3(x, y, z), glm::dvec3(.5 - x, -y, .5 + z), glm::dvec3(-x, .5 + y, -z),
              glm::dvec3(.5 + x, .5 - y, .5 - z), glm::dvec3(-x, -y, -z), glm::dvec3(.5 + x, y, .5 - z),
              glm::dvec3(x, .5 - y, z), glm::dvec3(.5 - x, .5 + y, .5 + z)})
            motif.push_back({p, 8});
        bulkGPa = 128;
        break;
    }
    case Mineral::Ice:
        cell.phase = CrystalPhase::IceIh;
        basis = glm::dmat3({1, 0, 0}, {-.5, std::sqrt(3.0) * .5, 0}, {0, 0, 1.633});
        // Oxygen framework only; proton disorder and hydrogen bonds are not MD.
        motif = {
            {{0, 0, 0}, 8}, {{0, 0, .375}, 8}, {{2.0 / 3, 1.0 / 3, .5}, 8}, {{2.0 / 3, 1.0 / 3, .875}, 8}};
        alpha = 45e-6;
        bulkGPa = 9;
        break;
    case Mineral::Graphite:
        cell.phase = CrystalPhase::Graphite;
        basis = glm::dmat3({1, 0, 0}, {-.5, std::sqrt(3.0) * .5, 0}, {0, 0, 6.708 / 2.461});
        motif = {{{0, 0, 0}, 6}, {{1.0 / 3, 2.0 / 3, 0}, 6}, {{0, 0, .5}, 6}, {{2.0 / 3, 1.0 / 3, .5}, 6}};
        alpha = 4e-6;
        bulkGPa = 33;
        break;
    }
    for (auto& site : motif)
        site.fractional -= glm::floor(site.fractional);
    // Linear thermal expansion and isotropic elastic compression, deliberately
    // bounded to the small-strain educational regime of these endmembers.
    double expansion =
        std::clamp(1 + alpha * (e.temperature - 293) - e.pressureGPa / (3 * bulkGPa), .94, 1.06);
    cell.basisMetres = basis * (a * expansion);
    return cell;
}
double vacancyProbability(const Environment& e, Mineral mineral) {
    constexpr double kBoltzmannEv = 8.617333262e-5;
    // Effective formation energies, in eV. Thermal vacancies quenched from the
    // formation temperature are distinguished from collision damage and annealing.
    constexpr std::array energy{2.5, 2.02, 3.0, .55, 7.0};
    constexpr std::array solidLimit{846.0, 1811.0, 2163.0, 273.0, 3900.0};
    double freezeTemperature = std::min(e.peakTemperature, solidLimit.at(static_cast<size_t>(mineral)));
    double formation = std::exp(-energy.at(static_cast<size_t>(mineral)) /
                                (kBoltzmannEv * std::max(1.0, freezeTemperature)));
    double anneal = std::clamp((1 - e.coolingRate) * e.peakTemperature / 2200, 0.0, .98);
    return std::clamp(formation * e.coolingRate + .012 * e.impact * (1 - anneal), 0.0, .08);
}
Lattice generateLattice(const Node& n, LatticeQuery query, const std::atomic<bool>* cancel) {
    if (query.minCell < -8 || query.maxCell > 8 || query.minCell > query.maxCell)
        throw std::invalid_argument("Lattice query exceeds bounded specimen");
    Lattice out;
    const auto& e = n.environment;
    auto cell = crystalCell(e.mineral, e);
    double metres = std::pow(10.0, n.frame.logRadius);
    double unit = material(e.mineral).cellMetres / metres;
    double vacancy = vacancyProbability(e, e.mineral);
    // Site-addressed, bounded static thermal displacement. Re-querying a region
    // does not re-roll positions, damage or Fe/Mg substitution.
    double amplitude = unit * .006 * std::sqrt(std::max(0.0, e.temperature) / 293);
    for (int z = query.minCell; z <= query.maxCell; ++z) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            return {};
        for (int y = query.minCell; y <= query.maxCell; ++y)
            for (int x = query.minCell; x <= query.maxCell; ++x)
                for (unsigned motif = 0; motif < cell.sites.size(); ++motif) {
                    auto site = cell.sites[motif];
                    uint64_t id = atomicSiteId(x, y, z, motif);
                    auto p = cell.basisMetres * (glm::dvec3(x, y, z) + site.fractional) / metres;
                    if (glm::length(p) > .97)
                        continue;
                    if (Random{featureSeed(n.seed, Feature::AtomicVacancy, id)}.unit(0) < vacancy) {
                        ++out.vacancies;
                        out.vacantSites.push_back(id);
                        continue;
                    }
                    if (site.element == 12) {
                        double iron =
                            e.chemistry.metal / std::max(1e-9, e.chemistry.metal + e.chemistry.silicate);
                        if (Random{featureSeed(n.seed, Feature::Substitution, id)}.unit(0) < iron)
                            site.element = 26;
                    }
                    if (p.x > 0)
                        p = glm::angleAxis(n.crystal.boundaryAngle, glm::dvec3(0, 1, 0)) * p;
                    p.y += unit * n.crystal.dislocation * std::atan2(p.z, p.x) / (2 * std::numbers::pi);
                    Random thermal{featureSeed(n.seed, Feature::ThermalMotion, id)};
                    p += glm::dvec3(thermal.signedUnit(0), thermal.signedUnit(1), thermal.signedUnit(2)) *
                         amplitude;
                    out.atoms.push_back({glm::vec3(p), site.element,
                                         static_cast<float>(unit * (site.element == 8   ? .16
                                                                    : site.element == 6 ? .17
                                                                                        : .23)),
                                         id});
                }
    }
    // Local bins avoid a quadratic scan while preserving site/query identity.
    double range = (e.mineral == Mineral::Iron ? 3.0e-10 : 3.1e-10) / metres;
    using Bin = std::array<int, 3>;
    auto bin = [&](glm::dvec3 p) {
        auto q = glm::floor(p / range);
        return Bin{static_cast<int>(q.x), static_cast<int>(q.y), static_cast<int>(q.z)};
    };
    std::map<Bin, std::vector<uint32_t>> bins;
    for (uint32_t i = 0; i < out.atoms.size(); ++i)
        bins[bin(out.atoms[i].position)].push_back(i);
    for (uint32_t i = 0; i < out.atoms.size(); ++i) {
        if ((i & 127) == 0 && cancel && cancel->load(std::memory_order_relaxed))
            return {};
        auto key = bin(out.atoms[i].position);
        for (int z = -1; z <= 1; ++z)
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x) {
                    auto found = bins.find({key[0] + x, key[1] + y, key[2] + z});
                    if (found == bins.end())
                        continue;
                    for (uint32_t j : found->second) {
                        if (j <= i)
                            continue;
                        auto& a = out.atoms[i];
                        auto& b = out.atoms[j];
                        double limit = 0;
                        if (e.mineral == Mineral::Iron)
                            limit = cell.phase == CrystalPhase::BccIron ? 2.65e-10 : 2.85e-10;
                        else if (e.mineral == Mineral::Ice)
                            limit = 3.1e-10;
                        else if (e.mineral == Mineral::Graphite)
                            limit = 1.65e-10;
                        else if ((a.element == 8) != (b.element == 8))
                            limit = a.element == 14 || b.element == 14 ? 1.9e-10 : 2.5e-10;
                        double distance =
                            glm::length(glm::dvec3(a.position) - glm::dvec3(b.position)) * metres;
                        if (distance < limit && distance > .8e-10)
                            out.bonds.push_back({i, j});
                    }
                }
    }
    return out;
}
} // namespace infinite
