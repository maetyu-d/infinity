#pragma once
#include "Exploration.hpp"

namespace infinity {
class Navigation;
class Pilot;
enum class ExpeditionPhase : uint8_t { Offered, Survey, Core, Assay, Return, Complete, Cancelled };
struct MaterialExpedition {
    uint64_t id = 0, acceptedRevision = 0, outcome = 0;
    ActivityId colony;
    Address source; // Canonical rock; a core is carried, its surrounding source field is reference context.
    std::optional<ActivityId> lab;
    ExpeditionPhase phase = ExpeditionPhase::Offered;
    int64_t reward = 90000, escrow = 0;
    unsigned assays = 0, resource = 0, powerLevel = 1;
    uint64_t descriptor = 0;
    std::string colonyName, materialDescription;
    bool carrying() const {
        return phase >= ExpeditionPhase::Core && phase <= ExpeditionPhase::Return;
    }
    Address grain() const {
        return source.child(Kind::Grain, 0);
    }
    Address lattice() const {
        return grain().child(Kind::Lattice, 0);
    }
};
struct ExpeditionState {
    std::vector<MaterialExpedition> jobs;
};
inline constexpr size_t maxMaterialExpeditions = 64;
class ExpeditionLedger {
  public:
    const ExpeditionState& state() const {
        return state_;
    }
    void offer(const Universe&, Economy&);
    void reconcilePlayer(EconomyPlayer&) const;
    std::string execute(const Universe&, Economy&, ExplorationLedger&, EconomyPlayer&,
                        const EconomicCommand&);
    std::string serialize() const;
    bool restore(std::string_view, const Universe&);
    size_t residentBytes() const;

  private:
    ExpeditionState state_;
};
const MaterialExpedition* activeExpedition(const ExpeditionState&, const Address& system);
const MaterialExpedition* selectedExpedition(const ExpeditionState&, size_t);
std::string expeditionObjective(const MaterialExpedition&);
void expeditionMarketView(MarketView&, const EconomySnapshot&, size_t);

// A lab's probe uses canonical source coordinates; the actual ship never travels to that source.
// Leaving or saving restores the exact parked ship, including its momentum and size.
class SpecimenProbe {
  public:
    bool enter(const Universe&, Navigation&, Pilot&, const MaterialExpedition&);
    bool focus(const Universe&, Navigation&, Pilot&, const Address&);
    bool leave(Navigation&, Pilot&);
    bool active() const {
        return source_.has_value();
    }
    const std::optional<Address>& source() const {
        return source_;
    }
    const Address& parkedFrame() const {
        return parkedFrame_;
    }
    const std::string& parkedNavigation() const {
        return navigation_;
    }
    const std::string& parkedShip() const {
        return ship_;
    }
    bool contains(const Address& a) const {
        return source_ && source_->ancestorOf(a);
    }

  private:
    std::optional<Address> source_;
    Address parkedFrame_;
    std::string navigation_, ship_;
    double cargo_ = 0;
};
} // namespace infinity
