#pragma once
#include "Activity.hpp"
#include "Crew.hpp"
#include "Piloting.hpp"
#include "Presentation.hpp"
namespace infinity {
struct MarketView;
struct ExpeditionPlan;
struct CockpitRect {
    double x, y, w, h;
    bool contains(glm::dvec2) const;
    bool overlaps(const CockpitRect&) const;
};
// A uniform fit in framebuffer pixels. A centred safe area keeps the reticle
// aligned with the world projection, including notches and portrait windows.
struct CockpitViewport {
    int x = 0, y = 0, width = 1, height = 1;
    double projectionScale = 1;
    CockpitViewport() = default;
    CockpitViewport(int pixelWidth, int pixelHeight, CockpitRect safe);
    double aspect() const {
        return double(width) / height;
    }
    glm::dvec2 toCanvas(glm::dvec2 point) const;
    glm::dvec2 toPixels(glm::dvec2 point) const;
};
struct CockpitLayout {
    double width;
    CockpitRect traffic, celestial, life, navigation, flight, scope, systems, windscreen;
    explicit CockpitLayout(double aspect);
    std::optional<size_t> trafficRow(glm::dvec2, size_t count) const;
    std::optional<size_t> celestialRow(glm::dvec2, size_t count) const;
};
// A short display list keeps a distant selected actor visible without reordering
// the authoritative contact list used by K cycling and stable identity lookup.
std::vector<size_t> cockpitActivityRows(std::span<const ActivityContact>);
std::vector<size_t> cockpitCelestialRows(std::span<const CelestialContact>);
std::optional<Address> nextLivingWorld(std::span<const CelestialContact>, const std::optional<Address>&);
struct CockpitTargetMarker {
    glm::dvec2 point{}, direction{};
    bool offscreen = false, aft = false;
};
struct CockpitTextRun {
    CockpitRect bounds;
    size_t firstVertex=0,vertexCount=0,firstIndex=0,indexCount=0;
    std::string value;
};
struct CockpitPopup {
    CockpitRect bounds;
    bool critical=false;
};
struct CockpitContactHit {
    CockpitRect bounds;
    std::optional<Address> world;
    std::optional<ActivityId> activity;
    bool catalogue=false,life=false;
};
CockpitTargetMarker cockpitTargetMarker(glm::dvec3 bearing, double aspect, double projectionScale = 1);
struct CockpitOverlay {
    Mesh panels, strokes, lettering;
    std::vector<CockpitRect> worldLabels, hitRegions;
    std::vector<CockpitTextRun> textRuns;
    std::vector<CockpitPopup> popups;
    std::vector<CockpitContactHit> contactHits;
};
std::vector<std::pair<size_t,size_t>> cockpitTextConflicts(const CockpitOverlay&);
// Screen-space world markers yield to panels, notifications and label cards.
void maskCockpitMarkers(Mesh&, const CockpitViewport&, const CockpitOverlay&);
struct CrewPanelLayout {
    CockpitRect panel;
    std::array<CockpitRect, crewRoomCount> rooms;
    std::array<CockpitRect, maxShipCrew> roster;
    explicit CrewPanelLayout(double aspect);
};
struct CrewFlightLayout {
    CockpitRect panel, automatic, weapon, notice;
    std::array<CockpitRect, maxShipCrew> roster;
    std::array<CockpitRect, crewRoomCount> rooms;
    std::array<CockpitRect, 8> actions;
    std::array<CockpitRect, maxWeaponMounts> guns;
    explicit CrewFlightLayout(double aspect);
};
CockpitRect weaponStationCard(double aspect, bool flightConsole);
struct PlannerLayout {
    CockpitRect panel,left,right,details,close;
    CockpitRect body{0,0,0,0},status{0,0,0,0},receipt{0,0,0,0};
    CockpitRect debrief{0,0,0,0};
    double footerY = 0;
    CockpitRect section,composition,route;
    std::array<CockpitRect,3> bays;
    std::array<CockpitRect,10> steps;
    std::array<CockpitRect,4> offers;
    std::array<CockpitRect,3> minus,plus;
    std::array<CockpitRect,6> actions;
    explicit PlannerLayout(double aspect,bool expanded=false,const ExpeditionPlan* view=nullptr);
};
CockpitRect surfaceReconPanel(double aspect);
CockpitRect surfaceReconMapRect(double aspect);
CockpitRect cockpitContactsSummary();
CockpitRect cockpitLifeBadge(double aspect,bool expanded);
// Native vector diagrams share the modal's fitted canvas and authoritative view data.
void drawPlannerDiagrams(CockpitOverlay&,const ExpeditionPlan&,const PlannerLayout&);
// Port of Hyperion's CabinMesh / rebuildCockpit beveled screen-framed cabin.
Mesh cockpitMesh(double aspect);
CockpitOverlay cockpitOverlay(const FlightInstruments&, double aspect, std::string_view mode,
                              std::string_view notice = {}, std::span<const CelestialContact> contacts = {},
                              std::span<const ActivityContact> activity = {}, bool help = false,
                              double projectionScale = 1, const MarketView* market = nullptr,
                              glm::dquat headInShip = {1, 0, 0, 0}, const CrewView* crew = nullptr);
CockpitOverlay cockpitOverlay(const FlightInstruments&,double aspect,std::string_view mode,std::string_view notice,
                              std::span<const CelestialContact>,std::span<const ActivityContact>,bool help,
                              double projectionScale,const MarketView*,glm::dquat headInShip,const CrewView*,const ExpeditionPlan*);
std::string flightQuantity(double value, bool speed = false);
} // namespace infinity
