#pragma once
#include "MiningProbe.hpp"

namespace infinity {
struct FlightInstruments;
void annotateCavern(FlightInstruments&,const Universe&,const Navigation&,const Borehole&,unsigned find,
                    std::string_view work = {});
void annotateDrillCamera(FlightInstruments&,const Borehole&,double seconds,double density);
// Pure, bounded presentation samples. They never mutate the saved bore,
// procedural geometry, mining reserves, or the player ship.
struct ProbeVisual {
    ProbePhase phase = ProbePhase::Flight;
    double cutterAngle = 0, rpm = 0, load = 0, retract = 0;
    double fuse = 0, flash = 0, dust = 0, blastAge = -1;
    double vibration = 0;
    double rollerAngle = 0, driveAngle = 0, heat = 0, feedStroke = 0;
};
ProbeVisual probeVisual(const Borehole&, double seconds, double density);
std::string probeToolReadout(const ProbeVisual&);
struct ProbeRig { Mesh housing, cutter, lamps, roller, drive, actuator, hotTeeth; };
ProbeRig probeRig();
struct ProbeMechanism {
    glm::dmat4 carrier{1}, head{1}, drive{1};
    std::array<glm::dmat4,3> rollers{};
    std::array<glm::dmat4,2> barrels{}, rods{};
};
ProbeMechanism probeMechanism(const ProbeVisual&);
struct ProbeMote {
    glm::dvec3 position{}; // metres in the bore's tangent frame, entry at origin
    glm::vec3 colour{};
    double radius = 0;
    float opacity = 0;
    bool fragment = false;
    bool spark = false;
    glm::dvec3 stretch{1,1,1};
};
struct ProbeMotes {
    std::array<ProbeMote, 128> items{};
    size_t count = 0;
};
ProbeMotes probeMotes(const Borehole&, const ProbeVisual&, double seconds, glm::vec3 mineral);
glm::vec3 probeWallLight(const Borehole&, const ProbeVisual&, glm::dvec3 wall,
                         glm::dvec3 eye, glm::dvec3 forward, glm::vec3 albedo, bool complete);
struct ProbeWallLighting {
    glm::dvec3 eye{}, forward{};
    double depth=0, join=0, charge=0;
    bool complete=false;
    bool operator==(const ProbeWallLighting&) const = default;
    glm::vec3 shade(glm::dvec3 wall,glm::vec3 albedo) const;
};
ProbeWallLighting probeLighting(const Borehole&,const ProbeVisual&,glm::dvec3 eye,
                                glm::dvec3 forward,bool complete);
// Conservative portal visibility in planet metres. A false result always
// retains the exterior; only a ready, enclosing cavity can hide it.
bool boreEnclosesView(const Borehole&,double seconds,glm::dvec3 eye,glm::dvec3 forward,
                      double fov,double aspect,double portalRadius,double nearMetres);
} // namespace infinity
