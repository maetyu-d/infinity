#include "infinity/MiningProbe.hpp"
#include <algorithm>

namespace infinity {
ProbeOperation probeOperation(const Borehole& b, double seconds) {
    ProbeOperation s;
    s.target = b.depth;
    double cursor = b.impactTime();
    s.phase = ProbePhase::Boring;
    const double feed = b.depth / b.drillSeconds;
    auto advance = [&](double until) {
        double dt = std::max(0., until - cursor);
        if (s.phase == ProbePhase::Boring) {
            const double equilibrium = .38 + .4 * s.rate + .12 * b.geology.fractions[1];
            const double heatLimit = equilibrium > .92
                ? 3. * std::log(std::max(1., (equilibrium - s.heat) / (equilibrium - .92))) : 1e30;
            const double finish = std::max(0., s.target - s.depth) / (feed * s.rate);
            const double run = std::min({dt, finish, heatLimit});
            s.depth += run * feed * s.rate;
            s.reached = std::max(s.reached, s.depth);
            s.work += run * s.rate;
            s.heat = equilibrium + (s.heat - equilibrium) * std::exp(-run / 3.);
            cursor += run; dt -= run;
            if (finish <= run + 1e-9) {
                s.phase = ProbePhase::Arming; s.stopped = cursor; s.blastAt = cursor + 1.5;
            } else if (heatLimit <= run + 1e-9) {
                s.phase = ProbePhase::Paused; s.stopped = cursor;
            }
        }
        if (s.phase == ProbePhase::Retracting) {
            s.depth = std::max(0., s.depth - dt * feed * 1.5);
            if (s.depth == 0) s.phase = ProbePhase::Recovered;
        }
        if (s.phase != ProbePhase::Boring) s.heat *= std::exp(-dt / 2.2);
        if (s.phase == ProbePhase::Arming && until >= s.blastAt) s.phase = ProbePhase::Blast;
        if (s.phase == ProbePhase::Blast && until >= s.blastAt + .8) s.phase = ProbePhase::Beacon;
        cursor = std::max(cursor, until);
    };
    for (const auto& instruction : b.controls) {
        if (instruction.seconds > seconds) break;
        advance(std::max(cursor, instruction.seconds));
        switch (instruction.control) {
        case ProbeControl::Depth: s.target = instruction.value; break;
        case ProbeControl::Rate: s.rate = instruction.value; break;
        case ProbeControl::Pause: s.phase = ProbePhase::Paused; s.stopped = cursor; break;
        case ProbeControl::Resume: s.phase = ProbePhase::Boring; s.stopped = 0; break;
        case ProbeControl::Retract: s.phase = ProbePhase::Retracting; s.stopped = cursor; s.blastAt = 1e30; break;
        case ProbeControl::Detonate: s.target = s.depth; s.phase = ProbePhase::Arming;
            s.stopped = cursor; s.blastAt = cursor + 1.5; break;
        }
    }
    if (seconds < b.impactTime()) { s.phase = ProbePhase::Flight; return s; }
    advance(seconds);
    return s;
}
double Borehole::blastTime() const {
    return controls.empty() ? impactTime() + drillSeconds + 1.5 : probeOperation(*this, 1e15).blastAt;
}
double Borehole::finalDepth() const {
    return controls.empty() ? depth : std::max(1., probeOperation(*this, 1e15).reached);
}
double Borehole::roomRadius() const { return std::max(6.,std::min(chamberRadius, finalDepth() * .3)); }
bool probeActive(const Borehole& b, double seconds) {
    const auto phase = probePose(b, seconds).phase;
    return phase != ProbePhase::Beacon && phase != ProbePhase::Recovered;
}
bool controlProbe(Borehole& b, ProbeControl control, double value, double seconds, std::string& status) {
    status = "PROBE CONTROL UNAVAILABLE";
    if (!std::isfinite(seconds) || seconds < b.started || seconds > 1e12 || !std::isfinite(value) ||
        b.controls.size() >= maxProbeControls || (!b.controls.empty() && seconds < b.controls.back().seconds))
        return false;
    if(b.controls.size()>=maxProbeControls-1&&control!=ProbeControl::Retract) {
        status="DRILL CONTROL HISTORY FULL / R SAFE ABORT STILL AVAILABLE";return false;
    }
    const auto s = probeOperation(b, seconds);
    const bool cutting = s.phase == ProbePhase::Boring || s.phase == ProbePhase::Paused;
    if (control == ProbeControl::Depth) {
        if ((!cutting && s.phase != ProbePhase::Flight) || value < std::max(40., s.reached) || value > 120) return false;
        status = "DRILL TARGET " + std::to_string(int(value)) + " M";
        if(std::abs(value-s.target)<1e-8)return true;
    } else if (control == ProbeControl::Rate) {
        if ((!cutting && s.phase != ProbePhase::Flight) || value < .35 || value > 1.5) return false;
        status = "CUTTER FEED " + std::to_string(int(std::lround(value * 100))) + "% / HIGH FEED ADDS HEAT";
        if(std::abs(value-s.rate)<1e-8)return true;
    } else if (control == ProbeControl::Pause) {
        if (s.phase != ProbePhase::Boring) return false;
        status = "DRILL PAUSED / TIP COOLING / P RESUMES";
    } else if (control == ProbeControl::Resume) {
        if (s.phase != ProbePhase::Paused) return false;
        if (s.heat > .65) { status = "TIP TOO HOT / WAIT FOR COOLING BELOW 65%"; return false; }
        status = "DRILL RESUMED";
    } else if (control == ProbeControl::Retract) {
        if (!cutting && s.phase != ProbePhase::Arming) return false;
        status = "ABORT / CHARGE DISARMED / PROBE RETRACTING / AMMUNITION SPENT";
    } else if (control == ProbeControl::Detonate) {
        if (!cutting || s.depth < 20) { status = "CHARGE REQUIRES AT LEAST 20 M OF COVER"; return false; }
        status = "EARLY CHARGE / CUTTER STOWING / 1.5 SECOND FUSE";
    } else return false;
    b.controls.push_back({seconds, control, value});
    return true;
}
uint64_t probeGeometryKey(const Borehole& b) {
    return mix64(b.id ^ mix64(uint64_t(std::llround(b.finalDepth() * 1000000))) ^
                 (uint64_t(b.geologyVersion) << 48));
}
double probeBlastDamage(const Borehole& b,glm::dvec3 point) {
    if(b.blastTime()>1e20)return 0;
    const auto centre=b.entry-glm::normalize(b.entry)*b.finalDepth();
    return std::max(0.,1-glm::length(point-centre)/(b.roomRadius()*2.5))*160;
}
} // namespace infinity
