#pragma once
#include "SurfaceHazards.hpp"

namespace infinity {
struct EconomySnapshot;
enum class SurfaceMachine : uint8_t { CargoRover, CraneBase, CraneBoom, Drill, Count };
enum class SurfaceDuty : uint8_t { Idle, LoadFreight, ReceiveFreight, Production, Maintenance };
struct SurfaceWorkOrder {
    uint64_t id = 0;
    SurfaceDuty duty = SurfaceDuty::Idle;
    unsigned good = 0;
    int64_t kg = 0;
};
struct SettlementWork {
    bool synchronized = false, outdoorHold = true, damaged = false;
    double power = 0, activity = 0;
    unsigned loading = 0, deliveries = 0;
    double animationSeconds = -1; // optional presentation clock; never economic production time
    double machineryActivity = 0, machinerySeconds = -1;
    std::string status;
    std::array<SurfaceWorkOrder,6> orders{};
    unsigned orderCount = 0;
};
struct SurfaceWorkClock {
    double seconds = 0, last = -1;
    void advance(double activity, double observedSeconds);
};
struct SurfaceWorkActor {
    uint64_t id = 0, building = 0;
    SurfaceMachine kind = SurfaceMachine::CargoRover;
    glm::dvec3 position{}, dimensions{1};
    glm::dquat orientation{1,0,0,0};
    glm::vec3 tint{1};
    SurfaceWorkOrder order;
};
SettlementWork settlementWork(const SurfaceWorld&, const SurfaceSettlement&, const WeatherSample&,
                              const EconomySnapshot*);
// A bounded presentation overlay, addressed by retained building/street IDs.
std::vector<SurfaceWorkActor> sampleSurfaceWork(const SurfaceWorld&, const SurfaceSettlement&,
                                              const SettlementWork&, double seconds);
Mesh surfaceMachineMesh(SurfaceMachine, unsigned detail);
} // namespace infinity
