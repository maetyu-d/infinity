#include "infinity/Economy.hpp"
#include "infinity/Expedition.hpp"
#include "infinity/Navigation.hpp"
#include "infinity/Piloting.hpp"
#include <algorithm>
namespace infinity {
bool SpecimenProbe::enter(const Universe& u, Navigation& nav, Pilot& pilot, const MaterialExpedition& j) {
    if (active() || j.phase != ExpeditionPhase::Assay || !j.lab || pilot.ship().shrinking ||
        pilot.ship().cruise || glm::length(pilot.ship().velocity) > 3)
        return false;
    u.describe(j.source);
    navigation_ = nav.serialize();
    ship_ = pilot.serialize();
    cargo_ = pilot.cargoMass();
    parkedFrame_ = nav.anchor;
    source_ = j.source;
    nav.anchor = j.source;
    nav.selected = j.grain();
    nav.logDistance = nav.targetLogDistance = u.describe(j.source).frame.logRadius;
    nav.orientation = {1, 0, 0, 0};
    nav.offset = {0, 0, 2.6};
    nav.stopMotion();
    pilot.deploySpecimenProbe(j.source);
    return true;
}
bool SpecimenProbe::focus(const Universe& u, Navigation& nav, Pilot& pilot, const Address& target) {
    if (!contains(target))
        return false;
    nav.anchor = target;
    nav.selected.reset();
    nav.logDistance = nav.targetLogDistance = u.describe(target).frame.logRadius;
    nav.orientation = {1, 0, 0, 0};
    nav.offset = {0, 0, 2.6};
    nav.stopMotion();
    pilot.deploySpecimenProbe(target);
    return true;
}
bool SpecimenProbe::leave(Navigation& nav, Pilot& pilot) {
    if (!active())
        return false;
    if (!nav.restore(navigation_) || !pilot.restore(ship_))
        throw std::runtime_error("Unable to restore parked specimen ship");
    pilot.setCargoMass(cargo_);
    source_.reset();
    return true;
}
void expeditionMarketView(MarketView& v, const EconomySnapshot& s, size_t selected) {
    v.ready = true;
    v.role = "MATERIAL EXPEDITION";
    v.site = "COLONY / CORE / CRYSTAL / ATOMS / REPAIR";
    v.selected = s.expeditions.jobs.empty() ? 0 : std::min(selected, s.expeditions.jobs.size() - 1);
    v.bottleneck = "F4 NEXT DESTINATION / SHIFT-F4 COLONY / ENTER CURRENT ACTION / BACKSPACE CANCEL";
    v.summary = "SHIFT-F11 EXPEDITIONS / IN LAB: SHIFT-F11 RETURN TO PARKED SHIP / UP-DOWN EXPEDITIONS";
    const auto* j = selectedExpedition(s.expeditions, v.selected);
    if (!j) {
        v.detail = {"NO COLONY EXPEDITIONS HERE YET", "VISIT AN INHABITED SYSTEM WITH SOLID SURFACE PORTS"};
        return;
    }
    const auto i = s.siteIndex(j->colony);
    v.site = j->colonyName;
    v.detail = {"THERMAL EXCHANGER / " + std::to_string(v.selected + 1) + " OF " +
                    std::to_string(s.expeditions.jobs.size()),
                expeditionObjective(*j),
                "REWARD " + std::to_string(j->reward / 100) + " CR / FUNDED " +
                    std::to_string(j->escrow / 100) + " CR",
                j->carrying() ? "SEALED CORE ABOARD / 1 KG / SOURCE ID " + std::to_string(j->descriptor)
                              : "CORE CUSTODY: NOT ABOARD",
                "ASSAYS: ROCK " + std::string(j->assays & 1 ? "OK" : "--") + " / MINERAL " +
                    (j->assays & 2 ? "OK" : "--") + " / ATOMS " + (j->assays & 4 ? "OK" : "--"),
                "CORE IS A SAMPLE; THE LAB RECONSTRUCTS ITS ORIGINAL SOURCE FIELD",
                "COLONY FUNDS PARTS AND CREW / FINISHING ADDS 25% BASE POWER"};
    if (j->phase >= ExpeditionPhase::Core && j->phase <= ExpeditionPhase::Complete) {
        v.detail.push_back(j->materialDescription);
    }
    if (i) {
        const auto& state = s.sites[*i];
        v.detail.push_back("COLONY CONDITION " + std::to_string(state.condition / 10) + "% / POWER LEVEL " +
                           std::to_string(state.modules[size_t(FacilityModule::Power)]));
    }
    if (j->phase == ExpeditionPhase::Complete)
        v.detail.push_back("COMPLETED / CORE CONSUMED IN REPAIR / OUTCOME " + std::to_string(j->outcome));
    if (j->phase == ExpeditionPhase::Survey)
        v.detail.push_back("COLONY-LOANED CORE SAMPLER / LAND WITHIN 80 M / F12 THEN CTRL-SPACE");
}
} // namespace infinity
