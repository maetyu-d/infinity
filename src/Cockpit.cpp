#include "infinite/Cockpit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace infinite {
namespace {
const glm::vec3 ink{.018, .035, .052}, muted{.35, .53, .59}, ice{.49, .85, .93}, mint{.44, .93, .74},
    amber{1, .65, .28}, pearl{.84, .92, .91};
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
    {'%', "11001110100001000100010001001100011"}};
void text(Mesh& m, std::string_view s, double x, double y, double size, glm::vec3 color,
          size_t maxChars = 100) {
    const double pixel = size / 7.;
    size_t count = 0;
    for (char c : s) {
        if (count++ >= maxChars)
            break;
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
        if (auto it = glyphs.find(c); it != glyphs.end())
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5; col++)
                    if (it->second[size_t(row * 5 + col)] == '1')
                        rect(m, x + col * pixel, y + row * pixel, pixel, pixel, color);
        x += pixel * 6;
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
    const char* suffix = "M";
    if (a >= 9.4607304725808e15) {
        divisor = 9.4607304725808e15;
        suffix = "LY";
    } else if (a >= 1.495978707e10) {
        divisor = 1.495978707e11;
        suffix = "AU";
    } else if (a >= 1000) {
        divisor = 1000;
        suffix = "KM";
    } else if (a > 0 && a < 1e-9) {
        divisor = 1e-12;
        suffix = "PM";
    } else if (a > 0 && a < 1e-6) {
        divisor = 1e-9;
        suffix = "NM";
    } else if (a > 0 && a < .001) {
        divisor = 1e-6;
        suffix = "UM";
    } else if (a > 0 && a < 1) {
        divisor = .001;
        suffix = "MM";
    }
    const double n = v / divisor;
    if (std::abs(n) > 99999)
        std::snprintf(b, sizeof(b), "%.2e %s%s", n, suffix, speed ? "/S" : "");
    else
        std::snprintf(b, sizeof(b), std::abs(n) < 100 ? "%.1f %s%s" : "%.0f %s%s", n, suffix,
                      speed ? "/S" : "");
    return b;
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
    constexpr double top = .052, deck = .865;
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
                              std::span<const ActivityContact> activity) {
    CockpitOverlay h;
    const double w = 900 * aspect, center = w * .5, deck = 790;
    rect(h.panels, 0, 0, w, 40, ink, .98f);
    text(h.lettering, "INFINITE", 25, 15, 15, pearl);
    text(h.lettering, mode, w - 180, 16, 11, mint);
    text(h.lettering, t.drive, center - 128, 16, 11, ice);
    rect(h.panels, 24, deck, w - 48, 92, ink, .96f);
    line(h.strokes, 24, deck, w - 24, deck, muted * .5f);
    const double left = 44, right = w - 298;
    text(h.lettering, t.destination, left, deck + 15, 12, pearl,
         size_t(std::max(12., (center - 180 - left) / 10.3)));
    text(h.lettering, t.phase, left, deck + 39, 10, t.landed ? mint : ice, 43);
    text(h.lettering, t.assist ? "ASSIST ON" : "ASSIST OFF", left, deck + 64, 9, muted);
    text(h.lettering, t.gear ? "GEAR DOWN" : "GEAR UP", left + 120, deck + 64, 9, t.gear ? mint : muted);
    text(h.lettering, "VELOCITY", center - 158, deck + 12, 9, muted);
    text(h.lettering, flightQuantity(t.speed, true), center - 158, deck + 29, 15, pearl, 24);
    text(h.lettering, "STOP " + flightQuantity(t.stoppingDistance), center - 158, deck + 60, 9, ice, 27);
    text(h.lettering, "THROTTLE", right, deck + 12, 9, muted);
    char value[80];
    std::snprintf(value, sizeof(value), "%.0f%%", t.throttle * 100);
    text(h.lettering, value, right, deck + 29, 15, pearl);
    bar(h.panels, right, deck + 59, 100, t.throttle, ice);
    text(h.lettering, "HULL", right + 135, deck + 12, 9, muted);
    std::snprintf(value, sizeof(value), "%.0f%%", t.hull);
    text(h.lettering, value, right + 135, deck + 29, 15, t.hull > 30 ? pearl : amber);
    bar(h.panels, right + 135, deck + 59, 100, t.hull / 100, mint);
    // Hyperion's flattened contact scope with real target and velocity bearings.
    const double radarX = center + 115, radarY = deck + 48;
    for (int i = 0; i < 48; i++) {
        double a = i * 6.283185307 / 48, b = (i + 1) * 6.283185307 / 48;
        line(h.strokes, radarX + std::cos(a) * 60, radarY + std::sin(a) * 19, radarX + std::cos(b) * 60,
             radarY + std::sin(b) * 19, muted * .65f);
    }
    line(h.strokes, radarX, radarY - 5, radarX - 4, radarY + 5, mint);
    line(h.strokes, radarX - 4, radarY + 5, radarX + 4, radarY + 5, mint);
    line(h.strokes, radarX + 4, radarY + 5, radarX, radarY - 5, mint);
    if (!t.shrinking || t.hullMetres >= .001) {
        const glm::vec3 trafficInk{1, .64, .78};
        size_t shown = 0;
        for (const auto& c : activity) {
            const auto v = c.localDirection;
            if (shown < 24) {
                const double x = radarX + v.x * 52, y = radarY + v.z * 15 - v.y * 17;
                rect(h.panels, x - 1, y - 1, c.selected ? 4 : 2, c.selected ? 4 : 2,
                     c.selected ? amber : trafficInk);
            }
            if (shown < 6) {
                const double y = 211 + shown * 34;
                text(h.lettering, (c.selected ? "+ " : "  ") + c.body.name, 28, y, 10,
                     c.selected ? amber : trafficInk, 28);
                text(h.lettering, flightQuantity(c.distance) + " / " + c.body.status, 45, y + 15, 8, muted,
                     36);
            }
            ++shown;
            if (!c.occluded && c.localDirection.z < 0 &&
                (c.selected || c.body.kind == ActivityKind::Harbour)) {
                const double x = center + v.x / -v.z * 450 / .7002075, y = 450 - v.y / -v.z * 450 / .7002075;
                if (x > 350 && x < w - 345 && y > 180 && y < 660) {
                    for (int side : {-1, 1})
                        line(h.strokes, x + side * 9, y - 6, x + side * 9, y + 6, trafficInk);
                    text(h.lettering, c.body.name, x + 14, y - 13, 8, trafficInk, 25);
                    text(h.lettering, flightQuantity(c.distance), x + 14, y + 2, 8, muted, 20);
                }
            }
        }
        if (!activity.empty())
            text(h.lettering, "LOCAL TRAFFIC / K SELECT / I HAIL", 28, 189, 10, trafficInk);
    }
    for (const auto& contact : contacts) {
        if (contact.selected)
            continue; // The active target is drawn in amber below.
        const auto v = contact.localDirection;
        const double x = radarX + v.x * 49, y = radarY + v.z * 14;
        const auto color = contact.address.kind() == Kind::System ? pearl : ice;
        line(h.strokes, x, y, x, y - v.y * 17, color * .6f);
        rect(h.panels, x - 1.4, y - v.y * 17 - 1.4, 2.8, 2.8, color);
    }
    if (t.targetVisible) {
        auto v = glm::normalize(t.localTarget);
        double x = radarX + v.x * 49, y = radarY + v.z * 14;
        line(h.strokes, x, y, x, y - v.y * 17, amber);
        rect(h.panels, x - 2, y - v.y * 17 - 2, 4, 4, amber);
    }
    // Fixed reticle and a separate prograde marker make lateral drift visible.
    const double cy = 450;
    for (int side : {-1, 1}) {
        line(h.strokes, center + side * 9, cy, center + side * 24, cy, ice);
        line(h.strokes, center + side * 24, cy, center + side * 24, cy + 4, ice);
    }
    if (t.speed > 1e-15 && -t.localVelocity.z > t.speed * .12) {
        double x = center + t.localVelocity.x / -t.localVelocity.z * 450 / .7002075,
               y = cy - t.localVelocity.y / -t.localVelocity.z * 450 / .7002075;
        if (x > 60 && x < w - 60 && y > 70 && y < 750) {
            for (int i = 0; i < 20; i++) {
                double a = i * 6.283185307 / 20, b = (i + 1) * 6.283185307 / 20;
                line(h.strokes, x + std::cos(a) * 7, y + std::sin(a) * 7, x + std::cos(b) * 7,
                     y + std::sin(b) * 7, mint);
            }
            line(h.strokes, x - 15, y, x - 8, y, mint);
            line(h.strokes, x + 8, y, x + 15, y, mint);
        }
    }
    if (t.targetVisible) {
        auto d = t.localTarget;
        double x = center, y = 450;
        bool ahead = d.z < 0;
        if (ahead) {
            x += d.x / -d.z * 450 / .7002075;
            y -= d.y / -d.z * 450 / .7002075;
        } else {
            x += std::copysign(w * .38, d.x);
            y -= std::copysign(250., d.y);
        }
        x = std::clamp(x, 80., w - 80.);
        y = std::clamp(y, 90., 710.);
        for (int side : {-1, 1}) {
            line(h.strokes, x + side * 10, y - 10, x + side * 16, y - 10, amber);
            line(h.strokes, x + side * 16, y - 10, x + side * 16, y + 10, amber);
            line(h.strokes, x + side * 16, y + 10, x + side * 10, y + 10, amber);
        }
        text(h.lettering, flightQuantity(t.range), x + 22, y - 4, 10, amber, 22);
    }
    if (t.altitude >= -1e-3) {
        text(h.lettering, "AGL " + flightQuantity(std::max(0., t.altitude)), w - 300, 98, 12, ice);
        text(h.lettering, "DOWN " + flightQuantity(t.descent, true), w - 300, 120, 10,
             t.descent > 6 ? amber : muted);
        text(h.lettering, t.safe ? "APPROACH STABLE" : "CHECK GEAR / DRIFT / ATTITUDE", w - 300, 142, 9,
             t.safe ? mint : muted);
        const auto cs = std::cos(t.roll), sn = std::sin(t.roll);
        double yy = cy + t.pitch * 220;
        for (int side : {-1, 1})
            line(h.strokes, center + side * 45 * cs, yy - side * 45 * sn, center + side * 95 * cs,
                 yy - side * 95 * sn, muted);
    }
    if (!contacts.empty() && (!t.shrinking || t.hullMetres >= .001)) {
        const double x = w - 310;
        const auto moons =
            std::count_if(contacts.begin(), contacts.end(), [](const auto& c) { return c.moon; });
        const auto planets = std::count_if(contacts.begin(), contacts.end(), [](const auto& c) {
            return c.address.kind() == Kind::Planet && !c.moon;
        });
        text(h.lettering, "Y PLANETS / SHIFT-Y MOONS", x, 171, 8, muted);
        text(h.lettering, "SYSTEM " + std::to_string(planets) + "P / " + std::to_string(moons) + "M", x, 189,
             10, ice);
        size_t index = 0;
        for (const auto& contact : contacts) {
            if (index >= 8)
                break;
            const auto color = contact.selected ? amber : (contact.occluded ? muted * .8f : pearl);
            const double y = 211 + index * 34;
            text(h.lettering, (contact.selected ? "+ " : "  ") + contact.label, x, y, 10, color, 25);
            text(h.lettering,
                 flightQuantity(contact.distanceMetres) + " / " +
                     (contact.occluded               ? "OCCULTED"
                      : contact.localDirection.z < 0 ? "AHEAD"
                                                     : "AFT"),
                 x + 17, y + 15, 8, contact.selected ? amber : muted, 33);
            ++index;
            if (!contact.occluded && contact.localDirection.z < 0 && contact.angularRadius < .008) {
                const auto d = contact.localDirection;
                const double px = center + d.x / -d.z * 450 / .7002075;
                const double py = 450 - d.y / -d.z * 450 / .7002075;
                if (px > 70 && px < w - 70 && py > 180 && py < 660 && (px < w - 350 || py > 570)) {
                    for (int side : {-1, 1}) {
                        line(h.strokes, px + side * 6, py - 4, px + side * 6, py + 4,
                             contact.selected ? amber : ice * .6f);
                    }
                    if (!contact.selected) {
                        text(h.lettering, contact.label, px + 10, py - 10, 8, ice, 20);
                        text(h.lettering, flightQuantity(contact.distanceMetres), px + 10, py + 4, 8, muted,
                             20);
                    }
                }
            }
        }
    }
    std::snprintf(value, sizeof(value), "VIEW SCALE %s / %s",
                  flightQuantity(std::pow(10., t.logMetres)).c_str(),
                  std::abs(t.scaleRate) > .02 ? (t.scaleRate < 0 ? "REFINING" : "WIDENING") : "STABLE");
    text(h.lettering, value, 28, 61, 9, t.shrinking ? amber : muted);
    text(h.lettering,
         "SHIP SIZE " + flightQuantity(t.hullMetres) + (t.shrinking ? " / SHRINK FIELD" : " / FULL SIZE"), 28,
         84, 11, t.shrinking ? amber : ice);
    if (t.shrinking) {
        text(h.lettering,
             "TARGET " + flightQuantity(t.targetHullMetres) + (t.autoShrink ? " / AUTO" : " / MANUAL"), 28,
             105, 9, muted);
        text(h.lettering,
             t.growthBlocked ? "GROWTH BLOCKED / MOVE CLEAR" : ", SMALLER  . LARGER  SHIFT-Z FULL SIZE", 28,
             124, 9, t.growthBlocked ? amber : muted);
    }
    auto duration = [](double seconds) {
        char b[64];
        if (seconds < 0)
            return std::string("--");
        const auto total = uint64_t(std::min(seconds, 1e15));
        std::snprintf(b, sizeof(b), "%llu:%02llu:%02llu", (unsigned long long)(total / 3600),
                      (unsigned long long)(total / 60 % 60), (unsigned long long)(total % 60));
        return std::string(b);
    };
    text(h.lettering, "FLIGHT " + duration(t.flightSeconds) + " / " + flightQuantity(t.travelMetres), 28,
         t.shrinking ? 148 : 108, 9, muted);
    if (t.navigating)
        text(h.lettering, "ETA " + duration(t.etaSeconds), 28, t.shrinking ? 167 : 128, 9, ice);
    text(h.lettering, "W/S THROTTLE  A/D STRAFE  Q/E LIFT  U/O ROLL  B BRAKE  X ASSIST", 30, 721, 9, muted);
    text(h.lettering,
         "Y PLANETS  SHIFT-Y MOONS  J NAVIGATE  L LAND  ENTER LAUNCH  N LONG CRUISE  Z SHRINK  V EXPLORER",
         30, 740, 9, muted);
    if (!notice.empty())
        text(h.lettering, notice, 30, 690, 10, ice, size_t(std::max(30., (w - 60) / 8.6)));
    return h;
}
} // namespace infinite
