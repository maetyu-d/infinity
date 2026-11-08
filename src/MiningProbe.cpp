#include "infinity/SaveTag.hpp"
#include "infinity/MiningProbe.hpp"
#include "infinity/ProbePresentation.hpp"
#include "infinity/Navigation.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
namespace infinity {
namespace {
const auto sectionNormals=[] {
    std::array<glm::dvec2,boreSectors> values{};
    for(unsigned k=0;k<boreSectors;++k){const double a=(k+.5)*6.283185307179586/boreSectors;
        values[k]={std::cos(a),std::sin(a)};}
    return values;
}();
bool finite(glm::dvec3 p) { return std::isfinite(glm::length(p)); }
glm::dvec3 axis(const Borehole& b) { return glm::normalize(b.entry); }
double roomRadius(const Borehole& b, glm::dvec3 local) {
    const double radius = b.roomRadius();
    const double r = glm::length(local);
    if (r < 1e-12) return radius;
    const double t = std::acos(std::clamp(local.y / r, -1., 1.));
    const double phi = std::atan2(local.z, local.x), top = std::asin(std::min(.99,b.shaftRadius / radius));
    const double fade = std::pow(std::sin(std::clamp((t - top) / (3.141592653589793 - top), 0., 1.) *
                                          3.141592653589793), 2);
    return radius * (1 + .07 * fade * std::sin(phi * 5 + double(b.id % 1024)) * std::sin(t * 4));
}
}
size_t MiningProbeState::residentBytes() const {
    size_t n = sizeof(*this) + holes.capacity() * sizeof(Borehole);
    for (const auto& h : holes) n += h.planet.path.capacity() * sizeof(Segment) + h.controls.capacity()*sizeof(ProbeInstruction);
    return n;
}
double boreRoomRadius(const Borehole& b, glm::dvec3 p) { return roomRadius(b, p); }
ProbePlan planMiningProbe(const Universe& u, const Address& a, const WeaponRay& ray, unsigned mount,
                          uint64_t id) {
    ProbePlan result;
    result.status = "PROBE / FACE SOLID LAND ON A PLANET OR MOON / RANGE 1500 KM";
    if (!id || mount >= maxWeaponMounts || a.kind() != Kind::Planet || !finite(ray.origin) ||
        !finite(ray.direction) || std::abs(glm::length(ray.direction) - 1) > 1e-6 ||
        !std::isfinite(ray.seconds) || ray.seconds < 0 || ray.seconds > 1e12) return result;
    const auto planet = u.describe(a);
    if (!hasSolidSurface(planet)) { result.status = "PROBE / GAS WORLDS HAVE NO SOLID LANDING SURFACE"; return result; }
    const auto pose = u.relative(ray.reference, a, 0);
    const double units = std::pow(10., u.describe(ray.reference).frame.logRadius);
    const auto origin = u.pointRelative(ray.reference, ray.origin / units, a, 0);
    const auto direction = glm::normalize(pose.orientation * ray.direction);
    const double radius = std::pow(10., planet.frame.logRadius);
    auto height = [&](glm::dvec3 p) {
        return glm::length(p) - radius - u.terrainHeight(planet, glm::normalize(p));
    };
    if (height(origin) < .2) { result.status = "PROBE / LAUNCH ABOVE SOLID GROUND"; return result; }
    double distance = 0, previous = 0;
    bool hit = false;
    for (unsigned i = 0; i < 768 && distance <= 1500000; ++i) {
        const auto p = origin + direction * distance;
        const double h = height(p);
        if (h <= 0) { hit = true; break; }
        if (glm::dot(p, direction) >= 0 && distance > 0) break;
        previous = distance;
        distance += std::clamp(h * .18, .15, 15000.);
    }
    if (!hit) return result;
    for (unsigned i = 0; i < 32; ++i) {
        const double mid = (previous + distance) * .5;
        (height(origin + direction * mid) > 0 ? previous : distance) = mid;
    }
    const auto entry = origin + direction * distance, up = glm::normalize(entry);
    if (glm::dot(direction, -up) < .25) { result.status = "PROBE / USE A STEEPER SURFACE APPROACH"; return result; }
    if (planet.geology->liquidWater > .1 && u.terrainHeight(planet, up) / radius < -.0008) {
        result.status = "PROBE / SELECT EXPOSED SOLID LAND, NOT OPEN WATER"; return result;
    }
    Borehole b;
    b.id = id; b.planet = a; b.mount = mount; b.launch = origin; b.entry = entry; b.started = ray.seconds;
    b.flightSeconds = std::clamp(distance / 2000., .6, 12.);
    b.depth = std::clamp(radius * .0001, 40., 80.);
    b.chamberRadius = std::min(18., b.depth * .3);
    const double density = material(planet.environment.mineral).density;
    b.drillSeconds = b.depth / std::clamp(10 * std::sqrt(2200 / density), 4., 14.);
    b.geologyVersion = 1; b.geology = cavernRecipe(u,b);
    result.hole = b; result.status = "PROBE LAUNCHED / REMOTE CAMERA AVAILABLE / ALT-P RETURNS TO SHIP";
    return result;
}
ProbePose probePose(const Borehole& b, double seconds) {
    ProbePose p;
    const auto up = axis(b);
    if (seconds < b.impactTime()) {
        p.progress = std::clamp((seconds - b.started) / b.flightSeconds, 0., 1.);
        p.forward = glm::normalize(b.entry - b.launch);
        p.position = glm::mix(b.launch, b.entry, p.progress)-p.forward*2.4;
    } else {
        const double drill = std::clamp((seconds - b.impactTime()) / b.drillSeconds, 0., 1.);
        p.progress = drill; p.depth = b.depth * drill;
        p.forward = glm::normalize(glm::mix(glm::normalize(b.entry-b.launch),-up,
                                           smoothstep(0.,.5,seconds-b.impactTime())));
        p.position = b.entry-up*p.depth-p.forward*2.4;
        p.phase = seconds < b.impactTime() + b.drillSeconds ? ProbePhase::Boring
                    : seconds < b.blastTime() ? ProbePhase::Arming
                    : seconds < b.blastTime() + .8 ? ProbePhase::Blast : ProbePhase::Beacon;
        if (p.phase == ProbePhase::Blast || p.phase == ProbePhase::Beacon) {
            p.blast = std::max(0., 1 - (seconds - b.blastTime()) / .8);
            const double settled = smoothstep(0., 2., seconds - b.blastTime());
            p.position = b.entry - up * (b.depth - glm::mix(2.4, b.chamberRadius * .25, settled));
            p.forward = glm::normalize(-up + tangentFrame(up) * glm::dvec3(.5 * settled, 0, 0));
        }
    }
    if (!b.controls.empty()) {
        const auto operation=probeOperation(b,seconds);
        p.phase=operation.phase; p.depth=operation.depth;
        if (p.phase!=ProbePhase::Flight) {
            p.progress=std::clamp(operation.reached/b.finalDepth(),0.,1.);
            p.forward=glm::normalize(glm::mix(glm::normalize(b.entry-b.launch),-up,smoothstep(0.,.5,seconds-b.impactTime())));
            p.position=b.entry-up*p.depth-p.forward*2.4;
            if (p.phase==ProbePhase::Recovered) p.position=b.entry+up*3.;
            if (p.phase==ProbePhase::Blast||p.phase==ProbePhase::Beacon) {
                p.blast=std::max(0.,1-(seconds-b.blastTime())/.8);
                const double settled=smoothstep(0.,2.,seconds-b.blastTime());
                p.position=b.entry-up*(b.finalDepth()-glm::mix(2.4,b.roomRadius()*.25,settled));
                p.forward=glm::normalize(-up+tangentFrame(up)*glm::dvec3(.5*settled,0,0));
            }
        }
    }
    auto cameraUp = tangentFrame(up) * glm::dvec3(0, 0, -1);
    if (std::abs(glm::dot(cameraUp, p.forward)) > .95) cameraUp = tangentFrame(up) * glm::dvec3(1, 0, 0);
    p.attitude = glm::quatLookAtRH(p.forward, cameraUp);
    return p;
}
std::string_view probePhaseName(ProbePhase p) {
    constexpr std::array names{"SURFACE APPROACH", "BORING / CUTTER ACTIVE", "CHARGE ARMED", "DETONATION", "CAVERN SURVEY BEACON",
        "DRILL PAUSED / TIP COOLING", "ABORT / RETRACTING", "ABORTED / CHARGE SAFE"};
    return names[size_t(p)];
}
void frameProbeCamera(Navigation& camera,const Universe& u,const Borehole& b,double seconds,glm::dvec2 look) {
    const auto sample=probePose(b,seconds);
    camera.anchor=b.planet.child(Kind::Surface,encodeTile(tileAt(glm::normalize(b.entry),16)));
    const double view=sample.phase==ProbePhase::Flight
        ?std::clamp(std::log10(std::max(10.,glm::length(sample.position-b.entry)*.3)),1.,5.):1.;
    camera.logDistance=camera.targetLogDistance=view;camera.transitioning=false;camera.selected.reset();
    const auto frame=u.relative(b.planet,camera.anchor,view);
    const auto planet=u.describe(b.planet);
    const double shake=probeVisual(b,seconds,material(planet.environment.mineral).density).vibration;
    camera.orientation=glm::normalize(frame.orientation*sample.attitude*
        glm::angleAxis(look.x+std::sin(seconds*27)*shake,glm::dvec3(0,1,0))*
        glm::angleAxis(look.y+std::sin(seconds*19)*shake,glm::dvec3(1,0,0)));
    const auto point=u.pointRelative(b.planet,sample.position/std::pow(10.,planet.frame.logRadius),camera.anchor,view);
    camera.offset=point-camera.orientation*glm::dvec3(0,0,1);
}
const Borehole* latestProbe(const MiningProbeState& s, const Address& system) {
    for (auto i = s.holes.rbegin(); i != s.holes.rend(); ++i) if (i->planet.parent() == system) return &*i;
    return nullptr;
}
double boreClearance(const Borehole& b, glm::dvec3 point, double seconds) {
    if (seconds < b.impactTime()) return -1e30;
    const auto q = glm::inverse(tangentFrame(axis(b))) * (point - b.entry);
    const bool open = seconds >= b.blastTime();
    const double r = open ? b.shaftRadius : 1.3;
    const double length = b.controls.empty() ? b.depth * std::clamp((seconds - b.impactTime()) / b.drillSeconds, 0., 1.)
                                            : probeOperation(b,seconds).reached;
    // Overlap the outside air generously: a finite cap at the surface would
    // form a false collision ceiling for a full-size hull entering the shaft.
    double tube = std::min(20. - q.y, q.y + length);
    for (const auto n:sectionNormals)
        tube = std::min(tube, r * std::cos(3.141592653589793 / boreSectors) - q.x*n.x - q.z*n.y);
    const auto roomPoint = q + glm::dvec3(0, b.finalDepth(), 0);
    const double room = open ? (roomRadius(b, roomPoint) - glm::length(roomPoint)) * .65 : -1e30;
    double clearance=std::max(tube,room);
    if (open&&b.geologyVersion&&room>tube) {
        const auto crystals=cavernCrystals(b);
        for(size_t i=0;i<crystals.count;++i)
            clearance=std::min(clearance,cavernCrystalClearance(crystals.items[i],q));
    }
    return clearance;
}
double excavationClearance(const MiningProbeState& s, const Address& a, glm::dvec3 p, double seconds) {
    double value = -1e30;
    for (const auto& b : s.holes)
        if (b.planet == a && glm::length(p - b.entry) < b.finalDepth() + b.chamberRadius * 2 + 30)
            value = std::max(value, boreClearance(b, p, seconds));
    return value;
}
glm::dvec3 excavationNormal(const MiningProbeState& s, const Address& a, glm::dvec3 p, double seconds) {
    glm::dvec3 n{};
    for (unsigned k = 0; k < 3; ++k) {
        glm::dvec3 e{}; e[k] = .001;
        n[k] = excavationClearance(s, a, p + e, seconds) - excavationClearance(s, a, p - e, seconds);
    }
    return glm::length(n) > 1e-12 ? glm::normalize(n) : glm::normalize(p);
}
bool nearExcavation(const MiningProbeState& s, const Address& a, glm::dvec3 p, double seconds) {
    for (const auto& b : s.holes)
        if (b.planet == a && seconds >= b.impactTime() &&
            glm::length(p - b.entry) < b.finalDepth() + b.chamberRadius * 2 + 30) return true;
    return false;
}
std::string serializeProbes(const MiningProbeState& s) {
    std::ostringstream out; out << std::setprecision(17) << "INFINITY_PROBES 2 ";
    for (unsigned k = 0; k < maxWeaponMounts; ++k) out << s.launchers[k] << ' ' << s.rounds[k] << ' ';
    out << s.holes.size() << '\n';
    for (const auto& b : s.holes) {
        out << b.id << ' ' << std::quoted(b.planet.str()) << ' ' << b.mount << ' '
            << b.launch.x << ' ' << b.launch.y << ' ' << b.launch.z << ' '
            << b.entry.x << ' ' << b.entry.y << ' ' << b.entry.z << ' ' << b.started << ' '
            << b.flightSeconds << ' ' << b.drillSeconds << ' ' << b.depth << ' '
            << b.chamberRadius << ' ' << b.shaftRadius << ' ' << b.geologyVersion << ' ' << b.controls.size() << '\n';
        for (const auto& c:b.controls) out<<c.seconds<<' '<<unsigned(c.control)<<' '<<c.value<<'\n';
        for (const auto& f:b.finds) out<<f.grade<<' '<<f.paidGrade<<' '<<f.revision<<' '<<f.minedRevision<<' '<<f.minedKg<<' '<<f.marked<<'\n';
    }
    return out.str();
}
bool restoreProbes(MiningProbeState& s, std::string_view text, const Universe& u) {
    try {
        MiningProbeState next; std::istringstream in{std::string(text)}; std::string h; unsigned version;
        if (!(in >> h >> version) || !matchesSaveTag(h, "INFINITY_PROBES") || version < 1 || version > 2) return false;
        for (unsigned k = 0; k < maxWeaponMounts; ++k) {
            unsigned fitted;
            if (!(in >> fitted >> next.rounds[k]) || fitted > 1 || next.rounds[k] > maxProbeRounds ||
                (!fitted && next.rounds[k])) return false;
            next.launchers[k] = fitted;
        }
        size_t count; if (!(in >> count) || count > maxBoreholes) return false;
        std::set<uint64_t> ids;
        for (size_t i = 0; i < count; ++i) {
            Borehole b; std::string a;
            if (!(in >> b.id >> std::quoted(a) >> b.mount >> b.launch.x >> b.launch.y >> b.launch.z >>
                  b.entry.x >> b.entry.y >> b.entry.z >> b.started >> b.flightSeconds >> b.drillSeconds >>
                  b.depth >> b.chamberRadius >> b.shaftRadius) || !b.id || !ids.insert(b.id).second ||
                b.mount >= maxWeaponMounts || !finite(b.launch) || !finite(b.entry) ||
                !std::isfinite(b.started) || b.started < 0 || b.started > 1e12 ||
                !(b.flightSeconds >= .6 && b.flightSeconds <= 12) ||
                !(b.drillSeconds > 0 && b.drillSeconds <= 30) ||
                !(b.depth >= 40 && b.depth <= 80) || !(b.chamberRadius >= 12 && b.chamberRadius <= 18) ||
                b.shaftRadius != 4.5 || glm::length(b.launch - b.entry) > 1500001) return false;
            b.planet = Address::parse(a);
            const auto p = u.describe(b.planet);
            if (b.planet.kind() != Kind::Planet || !hasSolidSurface(p) ||
                std::abs(glm::length(b.entry) - std::pow(10., p.frame.logRadius) -
                         u.terrainHeight(p, axis(b))) > .02) return false;
            b.geology=cavernRecipe(u,b);
            if(version>=2) {
                size_t controls;
                if(!(in>>b.geologyVersion>>controls)||b.geologyVersion>1||controls>maxProbeControls)return false;
                for(size_t j=0;j<controls;++j){ProbeInstruction c;unsigned action;std::string status;
                    if(!(in>>c.seconds>>action>>c.value)||action>unsigned(ProbeControl::Detonate)||
                       !controlProbe(b,ProbeControl(action),c.value,c.seconds,status))return false;}
                for(auto& f:b.finds){unsigned marked;
                    if(!(in>>f.grade>>f.paidGrade>>f.revision>>f.minedRevision>>f.minedKg>>marked)||
                       f.grade>3||f.paidGrade>f.grade||marked>1||f.revision>1000000000000ULL||
                       f.minedRevision>1000000000000ULL||f.minedKg>1000000000000ULL||
                       (f.grade&&!f.revision)||(marked&&f.grade<2)||(!f.grade&&(f.revision||f.minedKg||f.minedRevision)))return false;
                    f.marked=marked;}
            }
            next.holes.push_back(b);
        }
        in >> std::ws; if (!in.eof()) return false;
        s = std::move(next); return true;
    } catch (...) { return false; }
}
} // namespace infinity
