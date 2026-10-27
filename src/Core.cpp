#include "infinite/Core.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace infinite {
glm::dvec3 cellDisplacement(const std::array<int64_t, 3>& object, const std::array<int64_t, 3>& anchor) {
    glm::dvec3 out;
    for (unsigned i = 0; i < 3; ++i)
        out[i] = (object[i] >= anchor[i] ? static_cast<double>(uint64_t(object[i]) - uint64_t(anchor[i]))
                                         : -static_cast<double>(uint64_t(anchor[i]) - uint64_t(object[i]))) *
                 rootCellWidth;
    return out;
}
std::optional<std::array<int64_t, 3>> adjacentCell(std::array<int64_t, 3> cell, glm::i64vec3 delta) {
    for (unsigned i = 0; i < 3; ++i) {
        if ((delta[i] > 0 && cell[i] > INT64_MAX - delta[i]) ||
            (delta[i] < 0 && cell[i] < INT64_MIN - delta[i]))
            return {};
        cell[i] += delta[i];
    }
    return cell;
}
std::string_view kindName(Kind k) {
    constexpr std::array names{"COSMIC WEB", "GALAXY", "STAR SYSTEM",   "PLANET",         "SURFACE",
                               "MOUNTAIN",   "ROCK",   "MINERAL GRAIN", "CRYSTAL LATTICE"};
    return names.at(static_cast<size_t>(k));
}
uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t childSeed(uint64_t parent, Kind k, uint64_t id) {
    return mix64(parent ^ mix64(static_cast<uint64_t>(k) + 0x100) ^ mix64(id));
}
uint64_t featureSeed(uint64_t objectSeed, Feature feature, uint64_t id) {
    return mix64(mix64(objectSeed ^ 0x726566696e656d65ULL) + mix64(static_cast<uint64_t>(feature)) +
                 mix64(id));
}
uint64_t seedOf(const Address& a, uint64_t seed) {
    for (auto c : a.cell)
        seed = mix64(seed ^ mix64(static_cast<uint64_t>(c)));
    for (auto s : a.path)
        seed = childSeed(seed, s.kind, s.id);
    return seed;
}
Address Address::child(Kind k, uint64_t id) const {
    auto a = *this;
    a.path.push_back({k, id});
    a.validate();
    return a;
}
Address Address::parent() const {
    auto a = *this;
    if (!a.path.empty())
        a.path.pop_back();
    return a;
}
bool Address::ancestorOf(const Address& a) const {
    return cell == a.cell && path.size() <= a.path.size() &&
           std::equal(path.begin(), path.end(), a.path.begin());
}
std::string Address::str() const {
    std::ostringstream o;
    o << cell[0] << ',' << cell[1] << ',' << cell[2];
    for (auto s : path)
        o << '/' << static_cast<int>(s.kind) << ':' << s.id;
    return o.str();
}
void Address::validate() const {
    if (path.size() > static_cast<size_t>(Kind::Lattice))
        throw std::invalid_argument("Address exceeds supported refinement hierarchy");
    for (size_t i = 0; i < path.size(); ++i) {
        if (static_cast<size_t>(path[i].kind) != i + 1)
            throw std::invalid_argument("Non-canonical refinement hierarchy");
        if (path[i].kind == Kind::Surface)
            (void)decodeTile(path[i].id);
    }
}
Address Address::parse(std::string_view s) {
    Address a;
    size_t pos = 0;
    auto number = [&]<typename T>(T& n, char delimiter, bool required) {
        size_t end = s.find(delimiter, pos);
        if (end == std::string_view::npos) {
            if (required)
                throw std::invalid_argument("Missing address delimiter");
            end = s.size();
        }
        auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + end, n);
        if (ec != std::errc{} || ptr != s.data() + end || pos == end)
            throw std::invalid_argument("Invalid hierarchical address");
        pos = end < s.size() ? end + 1 : end;
    };
    number(a.cell[0], ',', true);
    number(a.cell[1], ',', true);
    number(a.cell[2], '/', false);
    if (s.empty() || s.back() == '/')
        throw std::invalid_argument("Incomplete address");
    while (pos < s.size()) {
        unsigned k;
        uint64_t id;
        number(k, ':', true);
        number(id, '/', false);
        if (k == 0 || k > static_cast<unsigned>(Kind::Lattice))
            throw std::invalid_argument("Invalid address kind");
        a.path.push_back({static_cast<Kind>(k), id});
    }
    a.validate();
    if (a.str() != s)
        throw std::invalid_argument("Non-canonical address spelling");
    return a;
}
double smoothstep(double a, double b, double x) {
    x = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
double valueNoise(glm::dvec3 p, uint64_t seed) {
    auto q = glm::floor(p);
    auto f = p - q;
    f = f * f * (3.0 - 2.0 * f);
    auto sample = [&](int x, int y, int z) {
        uint64_t h = seed;
        h = mix64(h ^ static_cast<uint64_t>(static_cast<int64_t>(q.x) + x));
        h = mix64(h ^ static_cast<uint64_t>(static_cast<int64_t>(q.y) + y));
        h = mix64(h ^ static_cast<uint64_t>(static_cast<int64_t>(q.z) + z));
        return Random{h}.signedUnit(0);
    };
    double a = glm::mix(sample(0, 0, 0), sample(1, 0, 0), f.x),
           b = glm::mix(sample(0, 1, 0), sample(1, 1, 0), f.x);
    double c = glm::mix(sample(0, 0, 1), sample(1, 0, 1), f.x),
           d = glm::mix(sample(0, 1, 1), sample(1, 1, 1), f.x);
    return glm::mix(glm::mix(a, b, f.y), glm::mix(c, d, f.y), f.z);
}
double fractalNoise(glm::dvec3 p, uint64_t seed, int octaves) {
    double a = .5, sum = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += a * valueNoise(p, seed);
        p = p * 2.03 + glm::dvec3(17.1, 3.7, 9.2);
        a *= .5;
    }
    return sum;
}
glm::dvec3 cubeDirection(int face, double u, double v) {
    switch (face) {
    case 0:
        return glm::normalize(glm::dvec3(1, v, -u));
    case 1:
        return glm::normalize(glm::dvec3(-1, v, u));
    case 2:
        return glm::normalize(glm::dvec3(u, 1, -v));
    case 3:
        return glm::normalize(glm::dvec3(u, -1, v));
    case 4:
        return glm::normalize(glm::dvec3(u, v, 1));
    case 5:
        return glm::normalize(glm::dvec3(-u, v, -1));
    default:
        throw std::invalid_argument("Cube face");
    }
}
glm::dquat tangentFrame(glm::dvec3 n) {
    auto east =
        glm::normalize(glm::cross(std::abs(n.y) > .95 ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 1, 0), n));
    return glm::quat_cast(glm::dmat3(east, n, glm::cross(east, n)));
}
uint64_t encodeTile(Tile t) {
    if (t.face < 0 || t.face > 5 || t.level > 24 || t.x >= (1u << t.level) || t.y >= (1u << t.level))
        throw std::invalid_argument("Tile range");
    return static_cast<uint64_t>(t.face) | (static_cast<uint64_t>(t.level) << 3) |
           (static_cast<uint64_t>(t.x) << 8) | (static_cast<uint64_t>(t.y) << 32);
}
Tile decodeTile(uint64_t id) {
    Tile t{static_cast<int>(id & 7), static_cast<unsigned>((id >> 3) & 31),
           static_cast<uint32_t>((id >> 8) & 0xffffff), static_cast<uint32_t>((id >> 32) & 0xffffff)};
    if (encodeTile(t) != id)
        throw std::invalid_argument("Non-canonical tile ID");
    return t;
}
glm::dvec3 tileDirection(Tile t, double u, double v) {
    double n = static_cast<double>(1u << t.level);
    return cubeDirection(t.face, 2 * (t.x + u) / n - 1, 2 * (t.y + v) / n - 1);
}
Tile tileAt(glm::dvec3 p, unsigned level) {
    if (level > 24 || !std::isfinite(glm::length(p)) || glm::length(p) == 0)
        throw std::invalid_argument("Invalid spherical tile lookup");
    const auto a = glm::abs(p);
    int face;
    double u, v;
    if (a.x >= a.y && a.x >= a.z) {
        face = p.x >= 0 ? 0 : 1;
        u = (p.x >= 0 ? -p.z : p.z) / a.x;
        v = p.y / a.x;
    } else if (a.y >= a.z) {
        face = p.y >= 0 ? 2 : 3;
        u = p.x / a.y;
        v = (p.y >= 0 ? -p.z : p.z) / a.y;
    } else {
        face = p.z >= 0 ? 4 : 5;
        u = (p.z >= 0 ? p.x : -p.x) / a.z;
        v = p.y / a.z;
    }
    const auto count = 1u << level;
    auto index = [&](double q) {
        return uint32_t(std::clamp(std::floor((q + 1) * .5 * count), 0., double(count - 1)));
    };
    return {face, level, index(u), index(v)};
}
namespace {
size_t commonDepth(const std::vector<LocalFrame>& ob, const Address& oa, const std::vector<LocalFrame>& an,
                   const Address& aa, double log) {
    if (ob.size() != oa.path.size() + 1 || an.size() != aa.path.size() + 1 || !std::isfinite(log))
        throw std::invalid_argument("Incompatible coordinate frames");
    if (oa.cell != aa.cell)
        return 0;
    size_t common = 0;
    while (common < std::min(oa.path.size(), aa.path.size()) && oa.path[common] == aa.path[common])
        ++common;
    return common;
}
glm::dquat branchOrientation(const std::vector<LocalFrame>& frames, size_t common) {
    glm::dquat q{1, 0, 0, 0};
    for (size_t i = common + 1; i < frames.size(); ++i)
        q = glm::normalize(q * frames[i].orientation);
    return q;
}
} // namespace
glm::dvec3 relativePoint(const std::vector<LocalFrame>& ob, const Address& oa, glm::dvec3 point,
                         const std::vector<LocalFrame>& an, const Address& aa, double log) {
    size_t common = commonDepth(ob, oa, an, aa, log);
    // Subtract the first divergent translations BEFORE scaling. In particular,
    // a galaxy point belonging to the focused star cancels exactly here, before
    // any planetary, microscopic or atomic offsets are added.
    auto first = [&](const auto& frames, glm::dvec3 local) {
        return frames.size() > common + 1 ? frames[common + 1].position : local;
    };
    auto separation = first(ob, point) - first(an, {});
    if (oa.cell != aa.cell)
        separation += cellDisplacement(oa.cell, aa.cell);
    glm::dvec3 result = separation * std::pow(10.0, ob[common].logRadius - log);
    auto tail = [&](const auto& frames, glm::dvec3 local, double sign) {
        if (frames.size() == common + 1)
            return;
        glm::dquat q = frames[common + 1].orientation;
        for (size_t i = common + 2; i < frames.size(); ++i) {
            result += sign * (q * frames[i].position) * std::pow(10.0, frames[i - 1].logRadius - log);
            q = glm::normalize(q * frames[i].orientation);
        }
        result += sign * (q * local) * std::pow(10.0, frames.back().logRadius - log);
    };
    tail(ob, point, 1);
    tail(an, {}, -1);
    return glm::inverse(branchOrientation(an, common)) * result;
}
Pose relativePose(const std::vector<LocalFrame>& ob, const Address& oa, const std::vector<LocalFrame>& an,
                  const Address& aa, double log) {
    size_t common = commonDepth(ob, oa, an, aa, log);
    return {relativePoint(ob, oa, {}, an, aa, log),
            glm::normalize(glm::inverse(branchOrientation(an, common)) * branchOrientation(ob, common)),
            std::pow(10.0, ob.back().logRadius - log)};
}
PointTransform pointTransform(const std::vector<LocalFrame>& ob, const Address& oa,
                              const std::vector<LocalFrame>& an, const Address& aa, double log) {
    size_t common = commonDepth(ob, oa, an, aa, log);
    PointTransform out;
    auto rotation = glm::inverse(branchOrientation(an, common));
    out.linear = glm::mat3_cast(glm::normalize(rotation * branchOrientation(ob, common))) *
                 std::pow(10.0, ob.back().logRadius - log);
    if (oa.cell == aa.cell && common == oa.path.size() && common < aa.path.size()) {
        out.pivot = an[common + 1].position;
        glm::dvec3 tail{};
        auto q = an[common + 1].orientation;
        for (size_t i = common + 2; i < an.size(); ++i) {
            tail -= (q * an[i].position) * std::pow(10.0, an[i - 1].logRadius - log);
            q = glm::normalize(q * an[i].orientation);
        }
        out.translation = rotation * tail;
    } else
        out.translation = relativePoint(ob, oa, {}, an, aa, log);
    return out;
}
double LOD::pixels(double r, double d, double h, double f) {
    return r * h / (std::max(d, 1e-12) * std::tan(f * .5));
}
unsigned LOD::detail(double p) {
    return p > 700 ? 4 : p > 250 ? 3 : p > 80 ? 2 : p > 20 ? 1 : 0;
}
unsigned LOD::stableDetail(double pixels, unsigned previous) {
    constexpr std::array thresholds{20.0, 80.0, 250.0, 700.0};
    previous = std::min(previous, 4u);
    while (previous < 4 && pixels > thresholds[previous] * 1.15)
        ++previous;
    while (previous > 0 && pixels < thresholds[previous - 1] * .85)
        --previous;
    return previous;
}
bool LOD::visible(glm::dvec3 p, double r, double aspect, double f) {
    double d = -p.z;
    double v = std::tan(f * .5);
    return d + r > 0.001 && d - r < 1e5 && std::abs(p.y) < d * v + r * 1.5 &&
           std::abs(p.x) < d * v * aspect + r * 1.5;
}
} // namespace infinite
