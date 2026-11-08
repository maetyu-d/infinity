#pragma once
#include "Activity.hpp"
#include "Piloting.hpp"

namespace infinity {
struct EconomySnapshot;
enum class ArrivalKind { Distress, Hostile, Shortage, Mining, Patrol, Traffic };
struct ArrivalEncounter {
    ActivityId target;
    ArrivalKind kind = ArrivalKind::Traffic;
    std::string title, detail;
    double distance = -1;
    uint64_t order = 0;
};
// Encounters select canonical actors and live ledger events. They create no
// duplicate ships, jobs, rewards or world objects.
std::vector<ArrivalEncounter> proceduralArrivals(const Universe&, const Address& destination,
                                               const EconomySnapshot*, std::span<const ActivityContact>);
class ArrivalDirector {
  public:
    bool update(const Universe&, const FlightInstruments&, const EconomySnapshot*,
                std::span<const ActivityContact>, double seconds, const Address* observerFrame = nullptr);
    const ArrivalEncounter* current() const;
    const ArrivalEncounter* select(std::optional<ActivityId> alreadySelected);
    void annotate(FlightInstruments&) const;
    const std::vector<ArrivalEncounter>& encounters() const { return encounters_; }
  private:
    uint64_t observed_ = 0;
    std::optional<Address> pending_, active_;
    double pendingAt_ = 0, expires_ = 0, refreshAt_ = 0;
    std::vector<ArrivalEncounter> encounters_;
    size_t selected_ = 0;
};
} // namespace infinity
