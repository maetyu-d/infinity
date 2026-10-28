#include "infinite/Cockpit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace infinite {
namespace {
const glm::vec3 ink{.018, .035, .052}, muted{.43, .60, .66}, ice{.49, .85, .93}, mint{.44, .93, .74},
    amber{1, .65, .28}, pearl{.84, .92, .91}, trafficInk{.90, .61, .76};
void triangle(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color, float alpha = 1) {
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color, alpha)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void rect(Mesh& m, double x, double y, double w, double h, glm::vec3 c, float alpha = 1) {
    triangle(m, {x, y, 0}, {x + w, y, 0}, {x, y + h, 0}, c, alpha);
    triangle(m, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}, c, alpha);
}
void line(Mesh& m, double x, double y, double a, double b, glm::vec3 c) {
    addLine(m, {x, y, 0}, {a, b, 0}, c);
}
// Small native-resolution instrument type. No font library or atlas allocation.
const std::map<char, std::string_view> glyphs{
    {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"},
    {'C', "01111100001000010000100001000001111"}, {'D', "11110100011000110001100011000111110"},
    {'E', "11111100001000011110100001000011111"}, {'F', "11111100001000011110100001000010000"},
    {'G', "01111100001000010111100011000101111"}, {'H', "10001100011000111111100011000110001"},
    {'I', "11111001000010000100001000010011111"}, {'J', "00111000100001000010000101001001100"},
    {'K', "10001100101010011000101001001010001"}, {'L', "10000100001000010000100001000011111"},
    {'M', "10001110111010110101100011000110001"}, {'N', "10001110011010110011100011000110001"},
    {'O', "01110100011000110001100011000101110"}, {'P', "11110100011000111110100001000010000"},
    {'Q', "01110100011000110001101011001001101"}, {'R', "11110100011000111110101001001010001"},
    {'S', "01111100001000001110000010000111110"}, {'T', "11111001000010000100001000010000100"},
    {'U', "10001100011000110001100011000101110"}, {'V', "10001100011000110001100010101000100"},
    {'W', "10001100011000110101101011101110001"}, {'X', "10001100010101000100010101000110001"},
    {'Y', "10001100010101000100001000010000100"}, {'Z', "11111000010001000100010001000011111"},
    {'0', "01110100011001110101110011000101110"}, {'1', "00100011000010000100001000010001110"},
    {'2', "01110100010000100010001000100011111"}, {'3', "11110000010000101110000010000111110"},
    {'4', "00010001100101010010111110001000010"}, {'5', "11111100001000011110000010000111110"},
    {'6', "01110100001000011110100011000101110"}, {'7', "11111000010001000100010000100001000"},
    {'8', "01110100011000101110100011000101110"}, {'9', "01110100011000101111000010000101110"},
    {'/', "00001000010001000100010001000010000"}, {'-', "00000000000000011111000000000000000"},
    {'+', "00000001000010011111001000010000000"}, {',', "00000000000000000000001100010001000"},
    {'.', "00000000000000000000000000011000110"}, {':', "00000011000110000000011000110000000"},
    {'%', "11001110100001000100010001001100011"}, {'m', "00000000001101010101101011010110101"},
    {'k', "10000100001001010100110001010010010"}, {'n', "00000000001111010001100011000110001"},
    {'p', "00000000001111010001111101000010000"}, {'u', "00000000001000110001100011001101101"},
    {'s', "00000000000111110000011100000111110"}, {'e', "00000000000111010001111111000001111"}};
void text(Mesh& m, std::string_view s, double x, double y, double size, glm::vec3 color,
          size_t maxChars = 100, bool literal = false) {
    const double pixel = size / 7.;
    size_t count = 0;
    for (char c : s) {
        if (count++ >= maxChars)
            break;
        if (!literal && c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
        if (auto it = glyphs.find(c); it != glyphs.end())
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5;) {
                    if (it->second[size_t(row * 5 + col)] != '1') {
                        ++col;
                        continue;
                    }
                    const int start = col++;
                    while (col < 5 && it->second[size_t(row * 5 + col)] == '1')
                        ++col;
                    rect(m, x + start * pixel, y + row * pixel, (col - start) * pixel, pixel, color);
                }
        x += pixel * 6;
    }
}
std::string elide(std::string_view value, double width, double size) {
    const size_t count = size_t(std::max(0., std::floor(width / (size * 6 / 7))));
    if (value.size() <= count)
        return std::string(value);
    return count > 3 ? std::string(value.substr(0, count - 3)) + "..." : std::string{};
}
void fitted(Mesh& m, std::string_view s, double x, double y, double width, double size, glm::vec3 color,
            bool literal = false) {
    text(m, elide(s, width, size), x, y, size, color, 200, literal);
}
void card(CockpitOverlay& h, CockpitRect r, glm::vec3 tint) {
    h.hitRegions.push_back(r);
    rect(h.panels, r.x, r.y, r.w, r.h, ink, .94f);
    rect(h.panels, r.x, r.y, 2, r.h, tint * .55f);
    line(h.strokes, r.x + 2, r.y, r.x + r.w, r.y, tint * .38f);
    line(h.strokes, r.x + 2, r.y + r.h, r.x + r.w, r.y + r.h, tint * .15f);
}
void diamond(Mesh& m, glm::dvec2 p, double radius, glm::vec3 color) {
    for (int i = 0; i < 4; ++i) {
        const double a = i * 1.570796326795, b = (i + 1) * 1.570796326795;
        line(m, p.x + std::cos(a) * radius, p.y + std::sin(a) * radius, p.x + std::cos(b) * radius,
             p.y + std::sin(b) * radius, color);
    }
}
void bar(Mesh& m, double x, double y, double w, double value, glm::vec3 color) {
    rect(m, x, y, w, 3, muted * .22f);
    rect(m, x, y, w * std::clamp(value, 0., 1.), 3, color);
}
} // namespace
std::string flightQuantity(double v, bool speed) {
    char b[64];
    const double a = std::abs(v);
    double divisor = 1;
    const char* suffix = "m";
    if (a >= 9.4607304725808e15) {
        divisor = 9.4607304725808e15;
        suffix = "LY";
    } else if (a >= 1.495978707e10) {
        divisor = 1.495978707e11;
        suffix = "AU";
    } else if (a >= 1000) {
        divisor = 1000;
        suffix = "km";
    } else if (a > 0 && a < 1e-9) {
        divisor = 1e-12;
        suffix = "pm";
    } else if (a > 0 && a < 1e-6) {
        divisor = 1e-9;
        suffix = "nm";
    } else if (a > 0 && a < .001) {
        divisor = 1e-6;
        suffix = "um";
    } else if (a > 0 && a < 1) {
        divisor = .001;
        suffix = "mm";
    }
    const double n = v / divisor;
    if (std::abs(n) > 99999)
        std::snprintf(b, sizeof(b), "%.2e %s%s", n, suffix, speed ? "/s" : "");
    else
        std::snprintf(b, sizeof(b), std::abs(n) < 100 ? "%.1f %s%s" : "%.0f %s%s", n, suffix,
                      speed ? "/s" : "");
    return b;
}
bool CockpitRect::contains(glm::dvec2 p) const {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}
bool CockpitRect::overlaps(const CockpitRect& r) const {
    return x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y;
}
CockpitLayout::CockpitLayout(double aspect)
    : width(900 * aspect), traffic{24, 176, 274, 263}, celestial{width - 310, 176, 286, 331},
      life{width - 310, 550, 286, 48}, navigation{24, 758, std::clamp((width - 48) * .31, 240., 430.), 124},
      flight{}, scope{}, systems{width - 24 - std::clamp((width - 48) * .22, 185., 290.), 758,
                                 std::clamp((width - 48) * .22, 185., 290.), 124},
      windscreen{traffic.x + traffic.w + 18, 166, celestial.x - traffic.x - traffic.w - 36, 472} {
    const double mid = systems.x - navigation.x - navigation.w - 36;
    flight = {navigation.x + navigation.w + 12, 758, mid * .48, 124};
    scope = {flight.x + flight.w + 12, 758, mid * .52, 124};
}
namespace {
std::optional<size_t> contactRow(glm::dvec2 p, CockpitRect r, size_t count, size_t capacity) {
    if (!r.contains(p) || p.y < 205 || p.y >= 205 + 34 * std::min(count, capacity))
        return {};
    return size_t((p.y - 205) / 34);
}
} // namespace
std::optional<size_t> CockpitLayout::trafficRow(glm::dvec2 p, size_t count) const {
    return contactRow(p, traffic, count, 6);
}
std::optional<size_t> CockpitLayout::celestialRow(glm::dvec2 p, size_t count) const {
    return contactRow(p, celestial, count, 8);
}
std::vector<size_t> cockpitActivityRows(std::span<const ActivityContact> contacts) {
    std::vector<size_t> rows;
    for (size_t i = 0; i < std::min(size_t(6), contacts.size()); ++i)
        rows.push_back(i);
    for (size_t i = rows.size(); i < contacts.size(); ++i)
        if (contacts[i].selected) {
            rows.back() = i;
            break;
        }
    return rows;
}
std::vector<size_t> cockpitCelestialRows(std::span<const CelestialContact> contacts) {
    std::vector<size_t> rows;
    for (unsigned priority = 0; priority < 3; priority++)
        for (size_t i = 0; i < contacts.size() && rows.size() < 8; i++) {
            const auto& c = contacts[i];
            const unsigned rank = c.selected ? 0 : hasSurfaceLife(c) ? 1 : 2;
            if (rank == priority)
                rows.push_back(i);
        }
    return rows;
}
std::optional<Address> nextLivingWorld(std::span<const CelestialContact> contacts,
                                       const std::optional<Address>& current) {
    std::vector<Address> worlds;
    for (const auto& c : contacts)
        if (hasSurfaceLife(c))
            worlds.push_back(c.address);
    if (worlds.empty())
        return {};
    std::sort(worlds.begin(), worlds.end());
    const auto next = current ? std::upper_bound(worlds.begin(), worlds.end(), *current) : worlds.begin();
    return next == worlds.end() ? worlds.front() : *next;
}
CockpitTargetMarker cockpitTargetMarker(glm::dvec3 d, double aspect) {
    CockpitLayout l(aspect);
    const glm::dvec2 center{l.width * .5, 450};
    CockpitTargetMarker result;
    result.point = center;
    const double length = glm::length(d);
    if (!std::isfinite(length) || length < 1e-30)
        return result;
    d /= length;
    result.aft = d.z >= 0;
    glm::dvec2 offset{d.x, -d.y};
    if (!result.aft) {
        const auto projected = center + offset * (450 / .7002075 / -d.z);
        if (l.windscreen.contains(projected)) {
            result.point = projected;
            return result;
        }
    }
    result.offscreen = true;
    if (glm::length(offset) < 1e-12)
        offset = {0, 1};
    offset = glm::normalize(offset);
    result.direction = offset;
    const double limitX =
        (offset.x < 0 ? center.x - l.windscreen.x : l.windscreen.x + l.windscreen.w - center.x) - 12;
    const double limitY =
        (offset.y < 0 ? center.y - l.windscreen.y : l.windscreen.y + l.windscreen.h - center.y) - 12;
    const double distance =
        std::min(limitX / std::max(1e-12, std::abs(offset.x)), limitY / std::max(1e-12, std::abs(offset.y)));
    result.point = center + offset * distance;
    return result;
}
Mesh cockpitMesh(double aspect) {
    Mesh m;
    auto point = [&](glm::dvec2 p, double depth) {
        return glm::dvec3((p.x * 2 - 1) * aspect * .7002075 * depth, (1 - p.y * 2) * .7002075 * depth,
                          -depth);
    };
    auto tri = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 col) {
        auto n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(n, -(a + b + c)) < 0)
            n = -n;
        const float diffuse =
            float(.48 + .52 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.6, .7, 1)))));
        triangle(m, a, b, c, col * diffuse);
    };
    auto panel = [&](std::initializer_list<glm::dvec2> points, glm::vec3 color, double front = .76,
                     double bevel = .10) {
        std::vector<glm::dvec2> outer(points), inner;
        glm::dvec2 center{};
        for (auto p : outer)
            center += p;
        center /= outer.size();
        for (auto p : outer)
            inner.push_back(glm::mix(p, center, bevel));
        auto depth = [&](glm::dvec2 p) { return front / (1 + (p.y - .74) * .28); };
        for (size_t i = 1; i + 1 < inner.size(); i++)
            tri(point(inner[0], depth(inner[0])), point(inner[i], depth(inner[i])),
                point(inner[i + 1], depth(inner[i + 1])), color);
        for (size_t i = 0; i < outer.size(); i++) {
            const size_t j = (i + 1) % outer.size();
            auto a = point(inner[i], depth(inner[i])), b = point(inner[j], depth(inner[j]));
            auto c = point(outer[i], depth(outer[i]) + .035), d = point(outer[j], depth(outer[j]) + .035);
            tri(a, c, b, color * 1.5f + glm::vec3(.04f));
            tri(b, c, d, color * 1.5f + glm::vec3(.04f));
            auto e = point(outer[i], depth(outer[i]) + .18), f = point(outer[j], depth(outer[j]) + .18);
            tri(c, e, d, ink);
            tri(d, e, f, ink);
        }
    };
    const glm::vec3 titanium{.176, .255, .306}, dark{.067, .114, .149}, copper{.357, .255, .173};
    constexpr double top = .052, deck = .842;
    panel({{0, top - .05}, {.24, top - .025}, {.21, top + .009}, {0, top + .035}}, titanium, .86);
    panel({{1, top - .05}, {.76, top - .025}, {.79, top + .009}, {1, top + .035}}, titanium, .86);
    panel({{0, top}, {.032, top + .032}, {.085, deck - .045}, {.054, deck + .03}, {0, deck - .005}}, titanium,
          .7, .16);
    panel({{1, top}, {.968, top + .032}, {.915, deck - .045}, {.946, deck + .03}, {1, deck - .005}}, titanium,
          .7, .16);
    panel({{0, deck - .005}, {.13, deck - .055}, {.28, deck + .025}, {0, 1}}, titanium, .65);
    panel({{1, deck - .005}, {.87, deck - .055}, {.72, deck + .025}, {1, 1}}, titanium, .65);
    panel({{.13, deck}, {.87, deck}, {1, 1}, {0, 1}}, dark, .68, .035);
    panel({{.11, deck - .041}, {.14, deck - .043}, {.232, deck + .005}, {.216, deck + .011}}, copper, .62,
          .22);
    panel({{.89, deck - .041}, {.86, deck - .043}, {.768, deck + .005}, {.784, deck + .011}}, copper, .62,
          .22);
    for (int side : {0, 1})
        for (int i = 0; i < 6; i++) {
            double x = side ? 1 - .018 - i * .012 : .018 + i * .012;
            panel({{x, deck - .016}, {x + .006, deck - .016}, {x + .006, deck - .012}, {x, deck - .012}},
                  i == 5 ? amber : ice * .7f, .58, 0);
        }
    for (double y : {.25, .43, .61})
        for (int side : {0, 1}) {
            double x = .023 + (y - top) * .077;
            if (side)
                x = 1 - x;
            panel({{x - .003, y - .004}, {x + .003, y - .004}, {x + .003, y + .004}, {x - .003, y + .004}},
                  muted, .66, .35);
        }
    m.compact();
    return m;
}
CockpitOverlay cockpitOverlay(const FlightInstruments& t, double aspect, std::string_view mode,
                              std::string_view notice, std::span<const CelestialContact> contacts,
                              std::span<const ActivityContact> activity, bool help) {
    CockpitOverlay h;
    const CockpitLayout l(aspect);
    const double w = l.width, center = w * .5;
    const double ratio = std::max(1e-12, t.hullMetres / 12);
    const bool surface = t.altitude >= 0 && (t.gear || t.altitude < 5000 * ratio);
    const bool catalogues = !t.shrinking || t.hullMetres >= .001;
    const auto living = std::count_if(contacts.begin(), contacts.end(), hasSurfaceLife);
    const auto scanning = std::count_if(contacts.begin(), contacts.end(), [](const auto& c) {
        return c.address.kind() == Kind::Planet &&
               (c.life == LifeDetection::Scanning || c.life == LifeDetection::Unknown);
    });
    const auto population = [](uint64_t people) {
        char value[40];
        if (people >= 1000000)
            std::snprintf(value, sizeof(value), "%.1fM", double(people) / 1000000);
        else if (people >= 1000)
            std::snprintf(value, sizeof(value), "%.1fK", double(people) / 1000);
        else
            std::snprintf(value, sizeof(value), "%llu", (unsigned long long)people);
        return std::string(value);
    };
    auto label = [&](std::string_view s, double x, double y, double width, glm::vec3 color = muted) {
        fitted(h.lettering, s, x, y, width, 10.5, color);
    };
    auto quantity = [&](std::string_view s, double x, double y, double width, double size = 14,
                        glm::vec3 color = pearl) {
        // Unit case is intentional: mm and m must remain distinguishable.
        size = std::min(size, width * 7 / (6 * std::max(size_t(1), s.size())));
        fitted(h.lettering, s, x, y, width, size, color, true);
    };
    auto duration = [](double seconds) {
        if (!(seconds >= 0) || !std::isfinite(seconds))
            return std::string("--");
        const auto total = uint64_t(std::min(seconds, 1e15));
        char b[64];
        std::snprintf(b, sizeof(b), "%llu:%02llu:%02llu", (unsigned long long)(total / 3600),
                      (unsigned long long)(total / 60 % 60), (unsigned long long)(total % 60));
        return std::string(b);
    };
    char value[80];
    rect(h.panels, 0, 0, w, 46, ink, .98f);
    h.hitRegions.push_back({0, 0, w, 46});
    text(h.lettering, "INFINITE", 26, 15, 17.5, pearl);
    fitted(h.lettering, t.drive, center - 160, 19, 320, 10.5, t.shrinking ? amber : ice);
    label(mode, w - 155, 19, 130, mint);
    line(h.strokes, 24, 45, w - 24, 45, muted * .3f);

    // Scale changes and physical ship size share one backed instrument. They
    // remain separate quantities; reference-frame refinement never resizes it.
    const CockpitRect scale{24, 64, 274, t.shrinking ? 96. : 76.};
    card(h, scale, t.shrinking ? amber : ice);
    label(t.shrinking ? "SHRINK FIELD / HULL" : "SHIP / FULL SIZE", 38, 75, 245, t.shrinking ? amber : muted);
    quantity(flightQuantity(t.hullMetres), 38, 94, 245, 17.5, t.shrinking ? amber : pearl);
    quantity("VIEW " + flightQuantity(std::pow(10., t.logMetres)), 38, 120, 166, 10.5, muted);
    label(std::abs(t.scaleRate) <= .02 ? "STABLE"
          : t.scaleRate < 0            ? "REFINING"
                                       : "WIDENING",
          211, 120, 77, std::abs(t.scaleRate) > .02 ? amber : mint);
    if (t.shrinking)
        quantity((t.growthBlocked ? "BLOCKED / "
                  : t.autoShrink  ? "AUTO / "
                                  : "MANUAL / ") +
                     flightQuantity(t.targetHullMetres),
                 38, 145, 245, 10.5, t.growthBlocked ? amber : muted);

    if (t.altitude >= 0) {
        card(h, {w - 310, 64, 286, 96}, surface ? mint : ice);
        label(t.shrinking ? "PARENT TERRAIN / AGL"
              : surface   ? "SURFACE / ALTITUDE AGL"
                          : "ALTITUDE AGL",
              w - 296, 75, 258);
        quantity(flightQuantity(std::max(0., t.altitude)), w - 296, 94, 258, 17.5, pearl);
        if (surface) {
            quantity(std::string(t.descent < 0 ? "UP " : "DOWN ") + flightQuantity(std::abs(t.descent), true),
                     w - 296, 121, 155, 10.5, t.descent > 2 * ratio ? amber : ice);
            const auto status = t.landed ? "LANDED" : t.safe ? "STABLE" : !t.gear ? "GEAR UP" : "ALIGN";
            label(status, w - 131, 121, 107, t.landed || t.safe ? mint : amber);
            quantity("DRIFT " + flightQuantity(t.drift, true), w - 296, 142, 258, 10.5, muted);
        } else {
            label(t.shrinking ? "SHRINK FIELD ACTIVE" : "ORBITAL FLIGHT", w - 296, 125, 258, muted);
        }
    }
    if (!t.weather.empty() && !t.shrinking) {
        card(h, {w - 310, 514, 286, 29}, ice);
        h.hitRegions.push_back({w - 310, 514, 286, 29});
        fitted(h.lettering, t.weather, w - 296, 524, 258, 8.4, ice);
    }

    // Hyperion's separated navigation, instrument, scope and systems rails.
    card(h, l.navigation, mint);
    card(h, l.flight, ice);
    card(h, l.scope, ice);
    card(h, l.systems, mint);
    const auto nav = l.navigation, flight = l.flight, scope = l.scope, systems = l.systems;
    label("NAVIGATION", nav.x + 14, nav.y + 12, nav.w - 28, mint);
    auto destination = t.destination;
    if (auto pos = destination.find(" / J NAVIGATE"); pos != std::string::npos)
        destination.resize(pos);
    if (!t.targetVisible && destination.starts_with("SELECT A DESTINATION"))
        destination = "SELECT A DESTINATION";
    fitted(h.lettering, destination, nav.x + 14, nav.y + 33, nav.w - 28, 17.5, pearl);
    if (t.targetVisible) {
        quantity(flightQuantity(t.range), nav.x + 14, nav.y + 60, nav.w * .53, 10.5, ice);
        label("ETA " + duration(t.etaSeconds), nav.x + nav.w * .57, nav.y + 60, nav.w * .40, muted);
    }
    const auto phase = t.phase.starts_with("PLANETARY FLIGHT / Y") ? "PLANETARY FLIGHT" : t.phase;
    label(phase, nav.x + 14, nav.y + 81, nav.w - 28, t.landed ? mint : muted);
    const auto primary = t.landed          ? "ENTER  LAUNCH"
                         : t.navigating    ? "B  TAKE HELM"
                         : t.targetVisible ? "J  APPROACH"
                                           : "Y  NEXT WORLD";
    label(primary, nav.x + 14, nav.y + 105, nav.w * .52, mint);
    label(t.landed          ? ""
          : surface         ? "L  LAND"
          : t.targetVisible ? "Y  WORLDS"
                            : "K  TRAFFIC",
          nav.x + nav.w * .59, nav.y + 105, nav.w * .37, ice);

    label("VELOCITY", flight.x + 14, flight.y + 12, flight.w - 28);
    quantity(flightQuantity(t.speed, true), flight.x + 14, flight.y + 36, flight.w - 28,
             flight.w < 210 ? 17.5 : 21, pearl);
    quantity("STOP " + flightQuantity(t.stoppingDistance), flight.x + 14, flight.y + 70, flight.w - 28, 10.5,
             ice);
    std::snprintf(value, sizeof(value), "ROLL %+.0f DEG/S / U-O", t.rollRate * 57.2957795);
    fitted(h.lettering, value, flight.x + 14, flight.y + 88, flight.w - 28, 8.4,
           std::abs(t.rollRate) > .02 ? ice : muted);
    label(t.assist ? "X  ASSIST ON" : "X  INERTIAL", flight.x + 14, flight.y + 105, flight.w - 28,
          t.assist ? mint : amber);
    const double gaugeWidth = (systems.w - 42) * .5;
    for (int i = 0; i < 2; ++i) {
        const double x = systems.x + 14 + i * (gaugeWidth + 14);
        label(i ? "HULL" : "THROTTLE", x, systems.y + 12, gaugeWidth);
        std::snprintf(value, sizeof(value), "%.0f%%", i ? t.hull : t.throttle * 100);
        text(h.lettering, value, x, systems.y + 36, 21, i && t.hull < 30 ? amber : pearl);
        bar(h.panels, x, systems.y + 70, gaugeWidth, i ? t.hull / 100 : t.throttle,
            i ? (t.hull < 30 ? amber : mint) : ice);
    }
    label(t.gear ? "G  GEAR DOWN" : "G  GEAR UP", systems.x + 14, systems.y + 105, systems.w - 28,
          t.gear ? mint : muted);
    const double radarX = scope.x + scope.w * .5, radarY = scope.y + 72;
    const double radarRadius = std::min(90., (scope.w - 28) * .5);
    label(surface ? "LANDING DIRECTOR" : "CONTACT SCOPE", scope.x + 14, scope.y + 12, scope.w - 28,
          surface ? mint : ice);
    for (double r : {.5, 1.})
        for (int i = 0; i < 64; ++i) {
            const double a = i * 6.283185307 / 64, b = (i + 1) * 6.283185307 / 64;
            line(h.strokes, radarX + std::cos(a) * radarRadius * r, radarY + std::sin(a) * 25 * r,
                 radarX + std::cos(b) * radarRadius * r, radarY + std::sin(b) * 25 * r,
                 muted * float(r == 1 ? .7 : .25));
        }
    line(h.strokes, radarX - radarRadius, radarY, radarX + radarRadius, radarY, muted * .22f);
    line(h.strokes, radarX, radarY - 25, radarX, radarY + 25, muted * .22f);
    auto scopePoint = [&](glm::dvec3 v, glm::vec3 color, bool selected, bool world) {
        const double x = radarX + v.x * radarRadius * .88, y = radarY + v.z * 22;
        const glm::dvec2 tip{x, y - v.y * 20};
        line(h.strokes, x, y, tip.x, tip.y, color * .48f);
        if (selected || world)
            diamond(h.strokes, tip, selected ? 5 : 2.5, color);
        else
            rect(h.panels, tip.x - 1.5, tip.y - 1.5, 3, 3, color);
    };
    if (catalogues && !surface) {
        for (const auto& contact : contacts)
            if (!contact.selected)
                scopePoint(contact.localDirection,
                           hasSurfaceLife(contact) ? mint
                           : contact.occluded      ? muted * .6f
                                                   : ice,
                           false, true);
        for (size_t i = 0; i < activity.size(); ++i)
            if (i < 32 || activity[i].selected)
                scopePoint(activity[i].localDirection, activity[i].selected ? amber : trafficInk,
                           activity[i].selected, false);
    }
    if (!surface && t.targetVisible && glm::length(t.localTarget) > 1e-30)
        scopePoint(glm::normalize(t.localTarget), amber, true, true);
    if (surface) {
        const double shift = std::clamp(t.pitch * 38, -18., 18.);
        for (int side : {-1, 1}) {
            const double a = side * 12., b = side * radarRadius * .8;
            line(h.strokes, radarX + a * std::cos(t.roll), radarY + shift - a * .28 * std::sin(t.roll),
                 radarX + b * std::cos(t.roll), radarY + shift - b * .28 * std::sin(t.roll),
                 t.safe ? mint : amber);
        }
        diamond(h.strokes, {radarX, radarY}, 4, pearl);
        label(t.landed ? "LANDED"
              : t.safe ? "STABLE"
                       : "LEVEL SHIP",
              radarX - 44, scope.y + 31, 115, t.safe || t.landed ? mint : amber);
        std::snprintf(value, sizeof(value), scope.w < 240 ? "R %.0f / P %.0f" : "ROLL %.0f / PITCH %.0f",
                      t.roll * 57.2957795, t.pitch * 57.2957795);
        label(value, scope.x + 14, scope.y + 105, scope.w - 28);
    } else {
        line(h.strokes, radarX, radarY - 5, radarX - 4, radarY + 4, mint);
        line(h.strokes, radarX - 4, radarY + 4, radarX + 4, radarY + 4, mint);
        line(h.strokes, radarX + 4, radarY + 4, radarX, radarY - 5, mint);
        label("FORWARD", radarX - 31, scope.y + 31, 80, muted);
        label(scope.w < 240 ? "WORLDS / SHIPS" : "WORLD / TRAFFIC / TARGET", scope.x + 14, scope.y + 105,
              scope.w - 28);
    }

    // Only a compact hint remains at the bottom; the full guide is opt-in.
    label("F1 CONTROLS", 26, 889, 145, muted);
    quantity("FLIGHT " + duration(t.flightSeconds) + " / " + flightQuantity(t.travelMetres),
             std::max(178., center - 180), 889, std::max(180., w - 490), 9, muted);
    label("H RENDER / TAB HUD", w - 200, 889, 178, muted);
    if (help) {
        const CockpitRect guide{center - std::min(700., w - 80) * .5, 205, std::min(700., w - 80), 418};
        card(h, guide, mint);
        fitted(h.lettering, "FLIGHT CONTROLS", guide.x + 22, guide.y + 22, guide.w - 44, 17.5, pearl);
        const std::pair<const char*, const char*> controls[]{{"W / S", "THROTTLE"},
                                                             {"A / D   Q / E", "STRAFE / LIFT"},
                                                             {"C / MOUSE / ARROWS", "HELM / LOOK"},
                                                             {"U / O  ALT+A / D", "ROLL"},
                                                             {"B / X", "BRAKE / ASSIST"},
                                                             {"Y / SHIFT-Y", "PLANETS / MOONS"},
                                                             {"CTRL-Y", "LIFE-DETECTED WORLDS"},
                                                             {"K / I / J", "TRAFFIC / HAIL / APPROACH"},
                                                             {"SHIFT-K", "SURFACE PORTS"},
                                                             {"L / G / ENTER", "LAND / GEAR / LAUNCH"},
                                                             {"N / - / +", "LONG CRUISE / DRIVE SPEED"},
                                                             {"Z / , / .", "SHRINK / SMALLER / LARGER"},
                                                             {"SHIFT-Z / V", "FULL SIZE / EXPLORER"},
                                                             {"F6 / H / TAB", "TRAFFIC / RENDER / HUD"},
                                                             {"F1 / ESC", "CLOSE GUIDE / RELEASE HELM"}};
        for (size_t i = 0; i < std::size(controls); ++i) {
            label(controls[i].first, guide.x + 22, guide.y + 65 + i * 24, guide.w * .42, ice);
            label(controls[i].second, guide.x + guide.w * .46, guide.y + 65 + i * 24, guide.w * .50, pearl);
        }
        return h;
    }

    if (catalogues && !activity.empty()) {
        card(h, l.traffic, trafficInk);
        label("PORTS / TRAFFIC", 38, 188, 160, trafficInk);
        label("K / I", 238, 188, 56, muted);
        const auto rows = cockpitActivityRows(activity);
        for (size_t row = 0; row < rows.size(); ++row) {
            const auto& c = activity[rows[row]];
            const double y = 211 + row * 34;
            if (c.selected)
                rect(h.panels, 28, y - 5, 266, 33, amber * .16f, .8f);
            fitted(h.lettering, (c.selected ? "+ " : "  ") + c.body.name, 36, y, 246, 10.5,
                   c.selected   ? amber
                   : c.occluded ? muted
                                : pearl);
            auto status = c.body.status.substr(0, c.body.status.find(" / "));
            if (c.occluded)
                status = "OCCULTED";
            quantity(flightQuantity(c.distance) + " / " + status, 52, y + 16, 234, 9,
                     c.selected ? amber : muted);
        }
        label(std::to_string(activity.size()) + " CONTACTS / K SELECT", 38, 421, 246, muted);
    }
    if (catalogues && !contacts.empty()) {
        card(h, l.celestial, ice);
        const auto moons =
            std::count_if(contacts.begin(), contacts.end(), [](const auto& c) { return c.moon; });
        const auto planets = std::count_if(contacts.begin(), contacts.end(), [](const auto& c) {
            return c.address.kind() == Kind::Planet && !c.moon;
        });
        label("SYSTEM " + std::to_string(planets) + "P / " + std::to_string(moons) + "M", w - 296, 188, 258,
              ice);
        const auto rows = cockpitCelestialRows(contacts);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& c = contacts[rows[i]];
            const double x = w - 296, y = 211 + i * 34;
            const bool inhabited = hasSurfaceLife(c);
            if (c.selected)
                rect(h.panels, w - 306, y - 5, 278, 33, amber * .16f, .8f);
            else if (inhabited)
                rect(h.panels, w - 306, y - 5, 278, 33, mint * .10f, .9f);
            const auto color = c.selected ? amber : inhabited ? mint : c.occluded ? muted : pearl;
            fitted(h.lettering, (c.selected ? "+ " : "  ") + c.label, x, y, inhabited ? 207 : 258, 10.5,
                   color);
            if (inhabited) {
                rect(h.panels, x + 214, y - 1, 44, 13, mint * .20f);
                fitted(h.lettering, "LIFE", x + 219, y + 1, 37, 9, mint);
            }
            const auto direction = c.occluded ? "HIDDEN" : c.localDirection.z < 0 ? "AHEAD" : "AFT";
            const auto status =
                inhabited ? population(c.population) + " PEOPLE" + (c.occluded ? " / HIDDEN" : "")
                : c.life == LifeDetection::Scanning                                 ? "SCANNING"
                : c.life == LifeDetection::None && c.address.kind() == Kind::Planet ? "NO SURFACE LIFE"
                                                                                    : direction;
            quantity(flightQuantity(c.distanceMetres) + " / " + status, x + 17, y + 16, 243, 9,
                     inhabited    ? mint
                     : c.selected ? amber
                                  : muted);
        }
        label("CTRL-Y LIFE / Y WORLDS", w - 296, 490, 258, living ? mint : muted);
        card(h, l.life, living ? mint : scanning ? ice : muted);
        const bool paused = t.shrinking && scanning;
        label(living     ? "LIFE DETECTED / " + std::to_string(living) + " WORLDS"
              : paused   ? "LIFE SURVEY PAUSED / SHRUNK"
              : scanning ? "SCANNING FOR SURFACE LIFE"
                         : "NO SURFACE LIFE DETECTED",
              l.life.x + 14, l.life.y + 10, l.life.w - 28,
              living     ? mint
              : scanning ? ice
                         : muted);
        fitted(h.lettering,
               living     ? (paused     ? "CLICK / CTRL-Y / SURVEY PAUSED"
                             : scanning ? "CLICK / CTRL-Y SELECT / SCANNING"
                                        : "CLICK / CTRL-Y SELECT / J APPROACH")
               : paused   ? "RESTORE SHIP SIZE TO CONTINUE"
               : scanning ? std::to_string(scanning) + " WORLDS REMAINING"
                          : "Y WORLDS / SHIFT-Y MOONS",
               l.life.x + 14, l.life.y + 30, l.life.w - 28, 9, living ? mint : muted);
    }

    // Readable world labels reserve rectangles, avoid all fixed panels and the
    // reticle, and cap at three secondary labels. Geometry and picking stay full.
    const CockpitRect aim{center - 42, 423, 84, 54};
    auto worldLabel = [&](glm::dvec2 anchor, std::string_view name, std::string_view range, glm::vec3 color) {
        const double width = std::min(184., l.windscreen.w - 12);
        for (int placement : {0, 1, 2, 3}) {
            const int side = placement == 1 ? -1 : 1;
            CockpitRect r{side > 0 ? anchor.x + 18 : anchor.x - width - 18,
                          anchor.y + (placement == 2   ? 32
                                      : placement == 3 ? -68
                                                       : -16),
                          width, 39};
            r.x = std::clamp(r.x, l.windscreen.x, l.windscreen.x + l.windscreen.w - width);
            r.y = std::clamp(r.y, l.windscreen.y, l.windscreen.y + l.windscreen.h - r.h);
            if (r.overlaps(aim) || std::any_of(h.worldLabels.begin(), h.worldLabels.end(),
                                               [&](auto other) { return r.overlaps(other); }))
                continue;
            h.worldLabels.push_back(r);
            rect(h.panels, r.x, r.y, r.w, r.h, ink, .94f);
            line(h.strokes, anchor.x, anchor.y, side > 0 ? r.x : r.x + r.w, r.y + 18, color * .5f);
            fitted(h.lettering, name, r.x + 7, r.y + 6, r.w - 14, 9, color);
            quantity(range, r.x + 7, r.y + 22, r.w - 14, 9, muted);
            return true;
        }
        return false;
    };
    if (t.targetVisible) {
        const auto marker = cockpitTargetMarker(t.localTarget, aspect);
        const auto p = marker.point;
        if (marker.offscreen) {
            const auto d = marker.direction, across = glm::dvec2(-d.y, d.x);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 + across.x * 6, p.y - d.y * 12 + across.y * 6, amber);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 - across.x * 6, p.y - d.y * 12 - across.y * 6, amber);
        } else {
            for (int side : {-1, 1}) {
                line(h.strokes, p.x + side * 10, p.y - 12, p.x + side * 17, p.y - 12, amber);
                line(h.strokes, p.x + side * 17, p.y - 12, p.x + side * 17, p.y + 12, amber);
                line(h.strokes, p.x + side * 17, p.y + 12, p.x + side * 10, p.y + 12, amber);
            }
        }
        const auto lifeTarget = std::find_if(contacts.begin(), contacts.end(),
                                             [](const auto& c) { return c.selected && hasSurfaceLife(c); });
        worldLabel(p,
                   lifeTarget != contacts.end() ? (marker.aft ? "LIFE DETECTED / AFT" : "LIFE DETECTED")
                   : marker.aft                 ? "TARGET AFT"
                                                : "TARGET",
                   flightQuantity(t.range), lifeTarget != contacts.end() ? mint : amber);
    }
    for (int side : {-1, 1}) {
        line(h.strokes, center + side * 9, 450, center + side * 23, 450, ice);
        line(h.strokes, center + side * 23, 450, center + side * 23, 454, ice);
    }
    rect(h.panels, center - 1, 449, 2, 2, pearl);
    if (t.speed > 1e-15 && -t.localVelocity.z > t.speed * .12) {
        const glm::dvec2 p{center + t.localVelocity.x / -t.localVelocity.z * 450 / .7002075,
                           450 - t.localVelocity.y / -t.localVelocity.z * 450 / .7002075};
        if (l.windscreen.contains(p)) {
            for (int i = 0; i < 24; ++i) {
                const double a = i * 6.283185307 / 24, b = (i + 1) * 6.283185307 / 24;
                line(h.strokes, p.x + std::cos(a) * 7, p.y + std::sin(a) * 7, p.x + std::cos(b) * 7,
                     p.y + std::sin(b) * 7, mint);
            }
            line(h.strokes, p.x - 16, p.y, p.x - 9, p.y, mint);
            line(h.strokes, p.x + 9, p.y, p.x + 16, p.y, mint);
        }
    }
    size_t secondary = 0;
    if (catalogues) {
        auto marker = [&](glm::dvec3 d, std::string_view name, double range, glm::vec3 color) {
            if (d.z >= -.01 || secondary >= 3)
                return;
            const glm::dvec2 p{center + d.x / -d.z * 450 / .7002075, 450 - d.y / -d.z * 450 / .7002075};
            if (!l.windscreen.contains(p) || aim.contains(p))
                return;
            if (worldLabel(p, name, flightQuantity(range), color)) {
                diamond(h.strokes, p, 4, color);
                ++secondary;
            }
        };
        for (const auto& c : contacts)
            if (!c.selected && !c.occluded && hasSurfaceLife(c))
                marker(c.localDirection, "LIFE / " + c.label, c.distanceMetres, mint);
        for (const auto& c : activity)
            if (!c.selected && !c.occluded && c.body.kind == ActivityKind::Harbour && c.distance < 2e7)
                marker(c.localDirection, c.body.name, c.distance, trafficInk);
        for (const auto& c : contacts)
            if (!c.selected && !c.occluded && !hasSurfaceLife(c) && c.angularRadius < .008)
                marker(c.localDirection, c.label, c.distanceMetres, ice);
    }
    if (surface) {
        // Local terrain normal drives the horizon; it is unrelated to galaxy up.
        const double cs = std::cos(t.roll), sn = std::sin(t.roll);
        for (int pitch : {-20, -10, 0, 10, 20}) {
            const double y = (t.pitch - pitch * .01745329252) * 220;
            if (std::abs(y) > 140)
                continue;
            for (int side : {-1, 1}) {
                const double a = side * (pitch ? 57. : 46.), b = side * (pitch ? 83. : 108.);
                line(h.strokes, center + a * cs + y * sn, 450 - a * sn + y * cs, center + b * cs + y * sn,
                     450 - b * sn + y * cs, pitch ? muted * .5f : mint * .7f);
            }
        }
        const bool danger = t.descent > 2 * ratio && t.stoppingDistance > std::max(0., t.altitude);
        if (danger || t.growthBlocked) {
            const std::string warning =
                t.growthBlocked ? "GROWTH BLOCKED / MOVE CLEAR" : "BRAKE / TERRAIN APPROACH";
            const double width = std::min(300., l.windscreen.w);
            card(h, {center - width * .5, 664, width, 29}, amber);
            fitted(h.lettering, warning, center - width * .5 + 10, 674, width - 20, 10.5, amber);
        }
    }
    if (!notice.empty()) {
        const double width = std::min(650., w - 80);
        card(h, {center - width * .5, 714, width, 29}, ice);
        fitted(h.lettering, notice, center - width * .5 + 12, 723, width - 24, 10.5, ice);
    }
    return h;
}
} // namespace infinite
