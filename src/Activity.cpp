#include "infinite/Activity.hpp"
#include "infinite/Planetology.hpp"
#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace infinite {
namespace {
constexpr double pi = std::numbers::pi;
// Activity recipe one owns its stream; the world seed and Feature streams stay intact.
constexpr uint64_t activityStream = 0x4143544956495459ULL;
const auto radial = glm::normalize(glm::dvec3(.3, 1, .4));
const auto along = glm::normalize(glm::cross(radial, glm::dvec3(1, 0, 0)));
const auto across = glm::normalize(glm::cross(along, radial));
glm::dvec3 harbour(const Node& p, unsigned index) {
    const double radius = std::pow(10., p.frame.logRadius);
    const auto a = radial * (radius * 2.2 - 8000) + along * 1500.;
    return index ? a + along * 14000. + across * 3500. + radial * 1200. : a;
}
glm::dquat heading(glm::dvec3 velocity, glm::dvec3 up) {
    if (glm::length(velocity) < 1e-12)
        velocity = along;
    const auto forward = glm::normalize(velocity);
    if (std::abs(glm::dot(forward, up)) > .98)
        up = across;
    return glm::quatLookAtRH(forward, up);
}
double habitation(const Node& p) {
    return smoothstep(170, 260, p.environment.temperature) *
           (1 - smoothstep(330, 600, p.environment.temperature)) * (.35 + .65 * p.environment.waterActivity);
}
} // namespace
std::string ActivityId::str() const {
    return home.str() + "@activity1:" + std::to_string(slot);
}
ActivityId ActivityId::parse(std::string_view text) {
    const auto split = text.find("@activity1:");
    if (split == text.npos)
        throw std::invalid_argument("Missing activity namespace");
    const auto home = Address::parse(text.substr(0, split));
    const auto digits = text.substr(split + 11);
    if (home.kind() != Kind::Planet || digits.empty() ||
        digits.find_first_not_of("0123456789") != digits.npos)
        throw std::invalid_argument("Invalid activity identity");
    const auto value = std::stoull(std::string(digits));
    if (value > 4096)
        throw std::invalid_argument("Activity slot out of range");
    return {home, uint32_t(value)};
}
std::vector<uint32_t> activitySlots(const Node& planet) {
    if (planet.address.kind() != Kind::Planet)
        throw std::invalid_argument("Activity requires an existing planet");
    const double hab = habitation(planet);
    const unsigned freight = 6 + unsigned(hab * 8 + planet.environment.chemistry.metal * 5);
    const unsigned ferries = 8 + unsigned(hab * 12);
    const unsigned debris = 10 + unsigned(planet.environment.impact * 18);
    std::vector<uint32_t> slots{0, 1};
    for (unsigned i = 0; i < freight; ++i)
        slots.push_back(100 + i);
    for (unsigned i = 0; i < ferries; ++i)
        slots.push_back(200 + i);
    for (unsigned i = 0; i < 4; ++i)
        slots.push_back(300 + i);
    for (unsigned i = 0; i < 4; ++i)
        slots.push_back(400 + i);
    for (unsigned i = 0; i < debris; ++i)
        slots.push_back(1000 + i);
    return slots;
}
ActivityBody sampleActivity(const Node& p, uint32_t slot, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::invalid_argument("Invalid activity time");
    const auto slots = activitySlots(p);
    if (std::find(slots.begin(), slots.end(), slot) == slots.end())
        throw std::invalid_argument("Unknown activity slot");
    ActivityBody b;
    b.id = {p.address, slot};
    b.seed = mix64(p.seed ^ activityStream ^ mix64(slot));
    Random r{b.seed};
    b.phase = r.unit(0) * 2 * pi;
    const auto number = std::to_string(b.seed % 10000);
    const double radius = std::pow(10., p.frame.logRadius);
    if (slot < 2) {
        b.kind = ActivityKind::Harbour;
        b.position = harbour(p, slot);
        b.radius = slot ? 240 : 420;
        b.orientation = glm::quatLookAtRH(-across, radial);
        b.tint = slot ? glm::vec3(.65, .82, 1) : glm::vec3(.65, 1, .84);
        b.name = (slot                 ? "SERVICE YARD "
                  : habitation(p) > .2 ? "ORBITAL HARBOUR "
                                       : "RESEARCH HARBOUR ") +
                 number;
        b.status = "STATION KEEPING / BERTHS OPEN";
    } else if (slot < 300) {
        b.kind = slot < 200 ? ActivityKind::Freighter : ActivityKind::Ferry;
        const bool heavy = b.kind == ActivityKind::Freighter;
        b.radius = heavy ? 24 + r.unit(1) * 20 : 7 + r.unit(1) * 5;
        b.tint = heavy ? glm::vec3(1, .72, .4) : glm::vec3(.55, .88, 1);
        const double period = heavy ? 900 + r.unit(2) * 500 : 240 + r.unit(2) * 200;
        const double phase = std::fmod(seconds / period + r.unit(3), 1.);
        const unsigned ordinal = slot - (heavy ? 100 : 200);
        const auto berth = along * (double(ordinal % 8) - 3.5) * (heavy ? 190. : 90.) +
                           across * (heavy ? 320. : -320.) +
                           radial * (800 + (ordinal % 3) * 120. + (ordinal / 8) * 450.);
        const auto a = harbour(p, 0) + berth, z = harbour(p, 1) - berth;
        double t = 0, rate = 0;
        glm::dvec3 from = a, to = z;
        if (phase < .16 || phase >= .96) {
            b.position = a;
            b.status = "LOADING / ORBITAL HARBOUR";
        } else if (phase < .48) {
            t = (phase - .16) / .32;
            rate = 1 / (period * .32);
            b.status = "OUTBOUND / SERVICE YARD";
        } else if (phase < .64) {
            b.position = z;
            b.status = "UNLOADING / SERVICE YARD";
        } else {
            t = (phase - .64) / .32;
            rate = 1 / (period * .32);
            from = z;
            to = a;
            b.status = "RETURNING / ORBITAL HARBOUR";
        }
        if (rate > 0) {
            const double ease = t * t * (3 - 2 * t), derivative = 6 * t * (1 - t);
            // A raised lane has zero endpoint velocity and remains well outside terrain.
            const auto arch = radial * (heavy ? 1800. : 1100.) + across * (heavy ? 900. : -700.);
            const double lift = 16 * t * t * (1 - t) * (1 - t);
            const double liftRate = 32 * t * (1 - t) * (1 - 2 * t);
            b.position = glm::mix(from, to, ease) + arch * lift;
            b.velocity = ((to - from) * derivative + arch * liftRate) * rate;
        }
        b.orientation = heading(rate > 0 ? b.velocity : to - from, radial);
        b.name = (heavy ? "FREIGHTER " : "FERRY ") + number;
    } else if (slot < 400) {
        b.kind = ActivityKind::Patrol;
        const double omega = 2 * pi / (180 + r.unit(1) * 100), angle = seconds * omega + b.phase;
        b.position = harbour(p, 0) + along * (std::cos(angle) * 4400) + across * (std::sin(angle) * 2400) +
                     radial * 1600.;
        b.velocity = (-along * (std::sin(angle) * 4400) + across * (std::cos(angle) * 2400)) * omega;
        b.orientation = heading(b.velocity, radial);
        b.radius = 10;
        b.tint = {.5, 1, .75};
        b.name = "PATROL " + number;
        b.status = "HARBOUR CIRCUIT / CIVILIAN ESCORT";
    } else {
        const bool debris = slot >= 1000;
        b.kind = debris ? ActivityKind::Debris : ActivityKind::Survey;
        const double orbit = radius * (debris ? 1.35 + r.unit(1) * .4 : 1.12 + r.unit(1) * .1);
        const double gm = 4.188790204786391 * 6.67430e-11 * planetDensity(p) * radius * radius * radius;
        const double omega = std::sqrt(gm / (orbit * orbit * orbit));
        const double angle = std::fmod(seconds * omega + b.phase, 2 * pi);
        const auto plane = glm::angleAxis(r.signedUnit(2) * .65, along);
        b.position = plane * (radial * std::cos(angle) + along * std::sin(angle)) * orbit;
        b.velocity = plane * (-radial * std::sin(angle) + along * std::cos(angle)) * (orbit * omega);
        b.orientation = heading(b.velocity, glm::normalize(b.position));
        b.radius = debris ? 2 + r.unit(4) * (5 + p.environment.impact * 28) : 8;
        b.tint = debris ? material(p.environment.mineral).color : glm::vec3(.78, .63, 1);
        b.name = (debris ? "EJECTA " : "SURVEY ") + number;
        b.status = debris ? "BALLISTIC / INHERITED MINERAL" : "ORBITAL SCAN / GEOLOGY TELEMETRY";
    }
    return b;
}
std::vector<ActivityBody> sampleActivity(const Node& planet, double seconds) {
    std::vector<ActivityBody> bodies;
    for (auto slot : activitySlots(planet))
        bodies.push_back(sampleActivity(planet, slot, seconds));
    return bodies;
}
std::string hailActivity(const ActivityBody& b) {
    if (b.kind == ActivityKind::SurfacePort)
        return b.name + " / " + b.status;
    return b.name + " / " + b.status +
           (b.kind == ActivityKind::Harbour ? " / J APPROACH HOLDING POINT" : " / J MATCH COURSE");
}
void ActivityState::advance(double dt) {
    if (std::isfinite(dt) && dt > 0)
        seconds = std::min(1e12, seconds + dt);
}
std::string ActivityState::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "INFINITE_ACTIVITY 1 " << seconds << ' ' << enabled << ' '
        << (selected ? selected->str() : "-");
    return out.str();
}
bool ActivityState::restore(std::string_view text, const Universe& u) {
    try {
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string header, id;
        int version;
        ActivityState candidate;
        if (!(in >> header >> version >> candidate.seconds >> candidate.enabled >> id) ||
            header != "INFINITE_ACTIVITY" || version != 1 || !std::isfinite(candidate.seconds) ||
            candidate.seconds < 0 || candidate.seconds > 1e12)
            return false;
        if (id != "-") {
            candidate.selected = ActivityId::parse(id);
            if (candidate.selected->slot >= surfaceActivitySlot) {
                const auto world = generateSurfaceLife(u, u.describe(candidate.selected->home));
                const auto slot = candidate.selected->slot - surfaceActivitySlot;
                if (!world || std::none_of(world->settlements.begin(), world->settlements.end(),
                                           [&](const auto& s) { return s.slot == slot; }))
                    return false;
            } else
                sampleActivity(u.describe(candidate.selected->home), candidate.selected->slot,
                               candidate.seconds);
        }
        in >> std::ws;
        if (!in.eof())
            return false;
        *this = candidate;
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace infinite
