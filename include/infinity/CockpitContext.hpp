#pragma once
#include "Piloting.hpp"
#include "Presentation.hpp"
#include "Crew.hpp"

namespace infinity {
enum class CockpitMode { Travel, Approach, Landing, Docking, Combat, Surface, Probe };
struct CockpitFocus {
    CockpitMode mode=CockpitMode::Travel;
    bool warp=false,nearGround=false,micro=false;
    std::string title,nextAction;
};
CockpitFocus cockpitFocus(const FlightInstruments&);
std::string cockpitDestination(const FlightInstruments&);
struct CockpitContactRow { bool activity=false; size_t index=0; };
std::vector<CockpitContactRow> cockpitSummaryRows(std::span<const CelestialContact>,std::span<const ActivityContact>);
enum class CockpitAlertLevel { Information, Advisory, Warning, Critical };
struct CockpitAlert {
    std::string key,title,action,detail;
    CockpitAlertLevel level=CockpitAlertLevel::Information;
    int priority=0;
};
std::vector<CockpitAlert> cockpitAlerts(const FlightInstruments&,const CrewView*,std::string_view notice);
} // namespace infinity
