#include "infinity/SurfaceHazards.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
SurfaceExposure surfaceExposure(const Node& n, const WeatherSample& w, double altitude, std::optional<glm::dvec3> direction) {
    SurfaceExposure e;
    if (!hasSolidSurface(n) || !std::isfinite(altitude) || altitude < -5 || altitude > 30000) return e;
    const double low = 1-smoothstep(1500.,30000.,std::max(0.,altitude));
    e.heat = smoothstep(430.,850.,w.temperatureK)*low;
    if (direction && n.geology->landform)
        e.heat = std::max(e.heat,geologySample(*n.geology,*direction).majorHeat *
                          (1-smoothstep(100.,2000.,std::max(0.,altitude))));
    if (w.pressureBar > .003) {
        e.electrical = w.type==WeatherType::Thunderstorm ? smoothstep(.4,.95,w.storm)*low : 0;
        const double dust = w.type==WeatherType::Dust ?
            std::max(w.dust,1-smoothstep(150.,3000.,w.visibilityMetres)) : 0;
        e.scanQuality = std::clamp(1-low*(dust*.78+e.electrical*.85),.12,1.);
    }
    return e;
}
double SurfaceExposureState::advance(const SurfaceExposure& e, double size, double dt) {
    if (!std::isfinite(dt)||dt<=0||dt>1||!std::isfinite(size)||size<=0||
        !std::isfinite(e.heat)||!std::isfinite(e.electrical)) return 0;
    const double vulnerability=1+std::clamp(-std::log10(size),0.,3.)*.35;
    const double target=std::clamp(e.heat,0.,1.)*1.3;
    heat=std::clamp(target+(heat-target)*std::exp(-dt*(target>heat?vulnerability/55.:1./18.)),0.,1.3);
    const double storm=std::clamp(e.electrical,0.,1.);
    charge=std::clamp(charge+dt*(storm>.1?storm*vulnerability/24.:-.18),0.,1.);
    double damage=std::max(0.,heat-.8)*3.*dt;
    if(charge>=1){charge=.22;damage+=1.8*storm;}
    return damage;
}
SurfaceConditions surfaceConditions(const Node& n, glm::dvec3 direction, double slope, const WeatherSample& w) {
    SurfaceConditions result;
    if (!hasSolidSurface(n) || !std::isfinite(slope)) return result;
    const auto offer = [&](SurfaceHazard kind, double severity, const char* label, const char* action,
                           bool landing, bool outdoor) {
        severity = std::clamp(severity, 0., 1.);
        result.landingHold |= landing; result.outdoorHold |= outdoor;
        if (severity > result.severity) {
            result.kind = kind; result.severity = severity; result.label = label; result.action = action;
        }
    };
    const double gust = w.pressureBar > .003 ? std::max(glm::length(w.wind), w.gustMetresPerSecond) : 0;
    if (gust > 18) offer(SurfaceHazard::Gusts, smoothstep(18., 65., gust), "DANGEROUS CROSSWIND",
                         "WAIT FOR WIND TO EASE / SEEK SHELTER", gust > 48, gust > 30);
    if (w.pressureBar > .003 && w.visibilityMetres < 1800)
        offer(SurfaceHazard::Whiteout, 1 - smoothstep(120., 1800., w.visibilityMetres),
              w.type == WeatherType::Dust ? "DUST OBSCURES THE GROUND" : "LOW SURFACE VISIBILITY",
              "HOLD ABOVE TERRAIN / WAIT FOR CLEARANCE", w.visibilityMetres < 300, true);
    if (w.pressureBar > .003 && w.type == WeatherType::Thunderstorm && w.storm > .4)
        offer(SurfaceHazard::ElectricalStorm,w.storm,"ELECTRICAL STORM / LIGHTNING RISK",
              "HOLD ABOVE WEATHER / WAIT FOR CLEARANCE",w.storm>.72,w.storm>.58);
    const auto geology = geologySample(*n.geology, direction);
    if (geology.majorHeat > .08)
        offer(SurfaceHazard::Geothermal,geology.majorHeat,"ACTIVE GEOTHERMAL VENT",
              "AVOID THE VENT / SAMPLE WITH A PROBE",geology.majorHeat>.35,true);
    if (geology.majorIce > .1 && geology.majorFracture > .18)
        offer(SurfaceHazard::Crevasses,geology.majorFracture,"GLACIER CREVASSE FIELD",
              "SEEK UNBROKEN ICE / SURVEY BEFORE LANDING",geology.majorFracture>.4,true);
    const double degrees = slope * 57.295779513;
    if (degrees > 22) {
        const bool ice = n.geology->type == GeologyType::IceShell || n.environment.mineral == Mineral::Ice;
        const double weakness = std::clamp(geology.erosion + geology.shear + n.environment.strain * .6, .1, 1.);
        offer(ice ? SurfaceHazard::FracturedIce : SurfaceHazard::LooseGround,
              smoothstep(22., 39., degrees) * (.65 + weakness * .35),
              ice ? "STEEP FRACTURED ICE" : "LOOSE SCREE / STEEP GROUND",
              "CHOOSE GENTLER GROUND / SURVEY BY PROBE", degrees > 32, degrees > 28);
    }
    if (w.temperatureK > 430)
        offer(SurfaceHazard::Heat, smoothstep(430., 850., w.temperatureK), "EXTREME SURFACE HEAT",
              "USE A PROBE / KEEP CREW SEALED", w.temperatureK > 700, true);
    if (result.severity < .12 && !result.landingHold && !result.outdoorHold) return {};
    return result;
}
} // namespace infinity
