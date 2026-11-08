#include "infinity/Cockpit.hpp"
#include "infinity/CockpitContext.hpp"
#include "infinity/SurfaceRecon.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace infinity;
static size_t checks = 0;
static void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
static bool containsText(const CockpitOverlay& h,std::string_view value){
    return std::any_of(h.textRuns.begin(),h.textRuns.end(),[&](const auto& r){return r.value.find(value)!=r.value.npos;});
}
static void contextChecks(){
    FlightInstruments t;t.hull=100;t.hullMetres=12;t.altitude=1630000;t.gear=true;
    ShipCrew ship;const auto normal=ship.view();
    check(cockpitAlerts(t,&normal,{}).empty(),"Normal crew status became a persistent warning");
    check(cockpitFocus(t).mode==CockpitMode::Travel,"Gear alone enabled landing guidance in orbit");
    t.warp=WarpPhase::Manual;t.landing.phase=LandingPhase::Align;t.docking.active=true;
    check(cockpitFocus(t).mode==CockpitMode::Travel&&cockpitFocus(t).warp,"Warp must suppress stale landing and docking UI");
    const auto warp=cockpitOverlay(t,16./9,"HYPERION",{});
    check(!containsText(warp,"LANDING DIRECTOR")&&!containsText(warp,"DOCKING DIRECTOR")&&!containsText(warp,"ALTITUDE"),"Warp shows irrelevant surface guidance");
    t={};t.altitude=12000;t.navigating=true;
    check(cockpitFocus(t).mode==CockpitMode::Approach,"Approach has no cockpit context");
    t.navigating=false;t.altitude=80;t.gear=true;
    check(cockpitFocus(t).mode==CockpitMode::Landing,"Low gear approach has no landing director");
    t.landed=true;t.landing.phase=LandingPhase::Landed;t.landing.settle=0;
    check(cockpitFocus(t).mode==CockpitMode::Surface,"Landed ship did not become a surface workspace");
    t={};t.docking.active=true;
    check(cockpitFocus(t).mode==CockpitMode::Docking,"Docking director missing");
    t={};t.hostileContacts=2;
    check(cockpitFocus(t).mode==CockpitMode::Combat,"Threats do not enable tactical instruments");
    t={};t.shrinking=true;t.hullMetres=1.2e-11;
    check(cockpitFocus(t).mode==CockpitMode::Surface&&cockpitFocus(t).micro,"Atomic ship lost micro context");
    t.phase="MINING PROBE CAMERA";
    check(cockpitFocus(t).mode==CockpitMode::Probe,"Probe feed lost its dedicated context");
    t={};t.destination="MOUNTAIN 2690 / J NAVIGATE";t.targetVisible=true;
    check(cockpitDestination(t)=="MOUNTAIN 2690","Instruction suffix leaked into destination name");
    t.hull=20;t.incomingFire=1;t.localMissile={0,0,-1};t.altitude=20;t.descent=100;t.stoppingDistance=500;
    t.surfaceSeverity=.9;t.surfaceWarning="ELECTRICAL STORM";t.surfaceAction="LEAVE THE CELL";
    auto alerts=cockpitAlerts(t,&normal,"SALE COMPLETE / 120 CR CREDITED");
    check(alerts.front().key=="missile"&&alerts.front().level==CockpitAlertLevel::Critical,"A receipt masked a missile warning");
    check(std::count_if(alerts.begin(),alerts.end(),[](const auto& a){return a.key=="terrain";})==1,"Terrain warning duplicated or disappeared");
    check(std::any_of(alerts.begin(),alerts.end(),[](const auto& a){return a.detail=="SALE COMPLETE / 120 CR CREDITED";}),"Full transaction receipt was discarded");
    const auto warning=cockpitOverlay(t,16./9,"HYPERION","SALE COMPLETE / 120 CR CREDITED",{}, {},false,1,nullptr,{1,0,0,0},&normal);
    check(warning.popups.size()==1&&warning.popups.front().critical&&containsText(warning,"SALE COMPLETE"),"Alerts must share one prioritized channel without losing the receipt");

    const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9);
    std::vector<CelestialContact> worlds(20);std::vector<ActivityContact> actors(20);
    for(unsigned i=0;i<20;++i){auto& w=worlds[i];w.address=system.child(Kind::Planet,i);w.label="PLANET "+std::to_string(i);w.distanceMetres=10000+i;w.life=LifeDetection::Detected;w.population=5000;
        auto& a=actors[i];a.body.id={worlds[0].address,i};a.body.name="FREIGHTER "+std::to_string(i);a.body.status="LOADING";a.distance=1000+i;}
    worlds[17].selected=true;actors[15].body.hostile=true;
    const auto rows=cockpitSummaryRows(worlds,actors);
    check(rows.size()==3&&!rows[0].activity&&rows[0].index==17&&rows[1].activity&&rows[1].index==15,"Summary must prioritize selected objects and hostiles using canonical indices");
    t={};t.targetVisible=true;t.destination=worlds[17].label;t.localTarget={0,0,-1};t.range=10000;
    for(double aspect:{1.,1.2,4./3,16./9,2.4})for(bool expanded:{false,true}){
        t.contactsExpanded=expanded;const auto h=cockpitOverlay(t,aspect,"HYPERION",{},worlds,actors);
        check(cockpitTextConflicts(h).empty(),"Contextual cockpit has overlapping text");
        size_t picked=0;for(const auto& hit:h.contactHits){
            picked+=bool(hit.world)||bool(hit.activity);
            if(hit.world)check(std::any_of(worlds.begin(),worlds.end(),[&](const auto& w){return w.address==hit.world;}),"Drawn world hit lost its identity");
            if(hit.activity)check(std::any_of(actors.begin(),actors.end(),[&](const auto& a){return a.body.id==hit.activity;}),"Drawn activity hit lost its identity");
        }
        check(picked==(expanded?14:3),"Hidden catalogue rows remain clickable, or visible rows have no hit target");
        check(std::count_if(h.contactHits.begin(),h.contactHits.end(),[](const auto& hit){return hit.life;})==1,"Life targeting badge missing or duplicated");
        check(!containsText(h,"WEAPON INHIBITED"),"Idle armament panel consumes the windscreen");
        for(const auto& run:h.textRuns)check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=900*aspect+.01&&run.bounds.y+run.bounds.h<=900.01,"Contextual text outside screen");
        if(!expanded){size_t chars=0;for(const auto& r:h.textRuns)chars+=r.value.size();check(chars<800,"Flight text was not substantially reduced");}
    }
    for(double aspect:{1.,16./9}){
        const auto guide=cockpitOverlay(t,aspect,"HYPERION",{},worlds,actors,true);
        check(containsText(guide,"SURFACE MAP / NAV ASSESS / ENGINEER WATCH")&&containsText(guide,"SHIFT-F5 NEXT / F4 ROUTE / HOLD 0 FIELD WORK"),"F1 hides the complete surface controls on a small screen");
        check(containsText(guide,"HOSTILE / GUNNERS HOLD / DEFEND / FOCUS"),"Gunner fire policies are inaccessible from the guide");
    }
    for(unsigned i=0;i<20;++i)check(worlds[i].address==system.child(Kind::Planet,i)&&actors[i].body.id.slot==i,"UI reordered or regenerated canonical contacts");
}
int main() {
    try {
        contextChecks();
        check(flightQuantity(.012, true) == "12.0 mm/s", "Millimetre units are ambiguous");
        check(flightQuantity(1.2e-11) == "12.0 pm", "Atomic hull units are wrong");
        check(flightQuantity(85000, true) == "85.0 km/s", "Flight speed units are wrong");
        std::vector<ActivityContact> traffic(80);
        const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
        for (size_t i = 0; i < traffic.size(); ++i) {
            auto& c = traffic[i];
            c.body.id = {system.child(Kind::Planet, 0), uint32_t(i)};
            c.body.name = "ORBITAL HARBOUR " + std::to_string(i);
            c.body.status = "STATION KEEPING / BERTHS OPEN";
            c.body.kind = ActivityKind::Harbour;
            c.localDirection = glm::normalize(glm::dvec3((int(i % 12) - 6) * .035, .18, -1));
            c.distance = 1000 + i * 100;
        }
        traffic[77].selected = true;
        const auto rows = cockpitActivityRows(traffic);
        check(rows.size() == 6 && rows.back() == 77, "Selected distant craft is missing from the display");
        std::set<size_t> unique(rows.begin(), rows.end());
        check(unique.size() == rows.size(), "A contact row is duplicated");
        for (size_t i = 0; i < traffic.size(); ++i)
            check(traffic[i].body.id.slot == i, "Display changed stable K-cycle order");
        check(cockpitActivityRows({}).empty(), "Empty contact display is invalid");
        std::vector<CelestialContact> worlds(70);
        for (size_t i = 0; i < worlds.size(); ++i) {
            auto& c = worlds[i];
            c.address = system.child(Kind::Planet, i);
            c.localDirection = glm::normalize(glm::dvec3((int(i % 10) - 5) * .06, -.14, -1));
            c.distanceMetres = 1e7 + i;
            c.angularRadius = .0001;
            c.label = "MOON " + std::to_string(i);
            c.moon = true;
        }
        FlightInstruments t;
        worlds[54].life = worlds[61].life = LifeDetection::Detected;
        worlds[54].population = 34000;
        worlds[61].population = 5100;
        worlds[61].occluded = true;
        worlds[63].life = LifeDetection::Scanning;
        worlds[68].selected = true;
        worlds[69].life = LifeDetection::Detected; // No population: never a positive signal.
        const auto livingRows = cockpitCelestialRows(worlds);
        check(livingRows.size() == 8 && livingRows[0] == 68 && livingRows[1] == 54 && livingRows[2] == 61,
              "Life-bearing distant worlds or the selected world disappeared behind nearby moons");
        check(std::set<size_t>(livingRows.begin(), livingRows.end()).size() == 8,
              "Prioritized celestial rows contain duplicates");
        check(nextLivingWorld(worlds, {}) == worlds[54].address &&
                  nextLivingWorld(worlds, worlds[54].address) == worlds[61].address &&
                  nextLivingWorld(worlds, worlds[61].address) == worlds[54].address,
              "Life targeting does not wrap through confirmed worlds");
        check(!hasSurfaceLife(worlds[63]) && !hasSurfaceLife(worlds[69]) &&
                  cockpitCelestialRows({}).empty() && !nextLivingWorld({}, {}),
              "Pending, empty or zero-population observations produced a life target");
        for (size_t i = 0; i < worlds.size(); i++)
            check(worlds[i].address == system.child(Kind::Planet, i),
                  "HUD prioritization mutated authoritative contact order or identity");
        t.speed = 85000;
        t.localVelocity = {0, 0, -85000};
        t.targetVisible = true;
        t.localTarget = {.13, .11, -1};
        t.range = 2e10;
        t.destination = "Moon 13145 / J NAVIGATE";
        for (double aspect : {1., 4. / 3, 16. / 9, 2.4}) {
            const CockpitLayout l(aspect);
            for (auto r : {l.traffic, l.celestial, l.life, l.navigation, l.flight, l.scope, l.systems}) {
                check(r.x >= 0 && r.y >= 0 && r.x + r.w <= l.width && r.y + r.h <= 900,
                      "Instrument panel escaped the viewport");
                check(r.w > 100 && r.h > 0, "Instrument collapsed in a narrow window");
            }
            for (auto a : {l.navigation, l.flight, l.scope, l.systems})
                for (auto b : {l.navigation, l.flight, l.scope, l.systems})
                    if (a.x != b.x)
                        check(!a.overlaps(b), "Lower-deck instruments overlap");
            check(l.trafficRow({l.traffic.x + 20, 217}, 80) == 0, "First traffic row cannot be clicked");
            check(l.celestialRow({l.celestial.x + 20, 217}, 70) == 0, "First world row cannot be clicked");
            check(!l.trafficRow({l.traffic.x + l.traffic.w + 1, 217}, 80), "Outside click selected traffic");
            check(!l.trafficRow({l.traffic.x + 20, 422}, 80), "Footer click selected hidden traffic");
            check(!l.celestialRow({l.celestial.x + 20, 217}, 0), "Empty list accepted a click");
            const auto projected = cockpitTargetMarker({.1, .05, -1}, aspect);
            check(!projected.offscreen && !projected.aft &&
                      glm::length(projected.point - glm::dvec2(l.width * .5 + .1 * 450 / .7002075,
                                                               450 - .05 * 450 / .7002075)) < 1e-9,
                  "Onscreen target does not match the camera projection");
            for (auto bearing : {glm::dvec3(0, 0, 1), glm::dvec3(1, 2, -.0001), glm::dvec3(-3, -8, 1),
                                 glm::dvec3(0, 0, -1)}) {
                const auto marker = cockpitTargetMarker(bearing, aspect);
                check(l.windscreen.contains(marker.point), "Offscreen cue escaped the readable windscreen");
                check(marker.aft == (bearing.z >= 0), "Offscreen cue lost front/back direction");
            }
            for (int regime = 0; regime < 4; ++regime) {
                t.altitude = regime == 1 ? 32 : regime == 2 ? 1200 : -1;
                t.gear = regime == 1;
                t.roll = regime == 1 ? 1.57079632679 : 0;
                t.pitch = regime == 1 ? .15 : 0;
                t.descent = regime == 1 ? 1 : 800;
                t.shrinking = regime == 3;
                t.hullMetres = regime == 3 ? 1.2e-11 : 12;
                t.logMetres = regime == 3 ? -10 : 7;
                t.localTarget = regime % 2 ? glm::dvec3(0, 0, 1) : glm::dvec3(0, 0, -1);
                const auto h = cockpitOverlay(t, aspect, "HYPERION", "DESTINATION SELECTED", worlds, traffic);
                check(h.worldLabels.size() <= 4, "Crowded scene produced too many labels");
                for (size_t i = 0; i < h.worldLabels.size(); ++i) {
                    const auto r = h.worldLabels[i];
                    check(l.windscreen.contains({r.x, r.y}) &&
                              r.x + r.w <= l.windscreen.x + l.windscreen.w + 1e-6 &&
                              r.y + r.h <= l.windscreen.y + l.windscreen.h + 1e-6,
                          "World label escaped the windscreen");
                    check(!r.overlaps({l.width * .5 - 42, 423, 84, 54}), "World label covered the reticle");
                    for (size_t j = 0; j < i; ++j)
                        check(!r.overlaps(h.worldLabels[j]), "World labels overlap");
                }
                for (const auto* mesh : {&h.panels, &h.strokes, &h.lettering})
                    for (auto v : mesh->vertices)
                        check(std::isfinite(v.x) && std::isfinite(v.y) && v.x >= 0 && v.x <= l.width + .01 &&
                                  v.y >= 0 && v.y <= 900,
                              "Cockpit drawing is clipped or non-finite");
                const auto guide = cockpitOverlay(t, aspect, "HYPERION", {}, worlds, traffic, true);
                check(guide.worldLabels.empty(), "Guide permits distracting world labels");
            }
        }
        t.shrinking = false;
        t.hullMetres = 12;
        t.hull = 100;
        t.gear = true;
        t.altitude = 35;
        t.weather = "HEAVY SNOW / 260K / 0.66BAR / WIND 12M/S";
        for (auto size : {glm::ivec2(800, 500), glm::ivec2(600, 900), glm::ivec2(601, 1001),
                          glm::ivec2(1920, 1200), glm::ivec2(3840, 2160)})
            for (double inset : {0., 24., 48.}) {
                const CockpitRect safe{inset, inset, size.x - inset * 1.5, size.y - inset * 1.25};
                const CockpitViewport v(size.x, size.y, safe);
                check(v.x >= safe.x && v.y >= safe.y && v.x + v.width <= safe.x + safe.w &&
                          v.y + v.height <= safe.y + safe.h && v.aspect() >= 1.,
                      "Fitted UI escaped the safe area or became too narrow");
                const CockpitLayout layout(v.aspect());
                for (bool help : {false, true}) {
                    const auto hud = cockpitOverlay(t, v.aspect(), "HYPERION", "DESTINATION SELECTED", worlds,
                                                    traffic, help, v.projectionScale);
                    for (const auto* mesh : {&hud.panels, &hud.strokes, &hud.lettering})
                        for (auto vertex : mesh->vertices) {
                            const auto p = v.toPixels({vertex.x, vertex.y});
                            check(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= safe.x - .01 &&
                                      p.x <= safe.x + safe.w + .01 && p.y >= safe.y - .01 &&
                                      p.y <= safe.y + safe.h + .01,
                                  "HUD or controls guide is clipped after fitting");
                        }
                }
                const auto worldPixel =
                    glm::dvec2(size) * .5 + glm::dvec2(.05, -.03) * (size.y * .5 / .7002075);
                const auto marker = cockpitTargetMarker({.05, .03, -1}, v.aspect(), v.projectionScale);
                check(!marker.offscreen && glm::length(v.toPixels(marker.point) - worldPixel) < 1e-7,
                      "Fitted HUD target no longer aligns with its world-space bearing");
                const auto row = glm::dvec2(layout.celestial.x + 20, 217);
                check(layout.celestialRow(v.toCanvas(v.toPixels(row)), 70) == 0,
                      "Retina/narrow-window coordinate conversion lost the clickable contact row");
                check(glm::length(v.toPixels(v.toCanvas(worldPixel)) - worldPixel) < 1e-9,
                      "Fitted hit testing does not invert the drawing transform");
            }
        std::cout
            << "PASS: " << checks
            << " cockpit layout, target projection, decluttering, stable selection and SI unit checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
