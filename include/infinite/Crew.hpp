#pragma once
#include "Core.hpp"
#include <array>
#include <iosfwd>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace infinite {
enum class CrewStation : uint8_t {
    Bridge,
    Navigation,
    Weapons,
    Engineering,
    LifeSupport,
    Medbay,
    Bunks,
    Count
};
enum class CrewOrder : uint8_t { Duty, Repair, Firefight, Aid, Rest };
enum class CrewCommand : uint8_t {
    Assign,
    Repair,
    Firefight,
    Aid,
    Rest,
    Return,
    BestStations,
    Emergency,
    Automatic
};
inline constexpr size_t crewRoomCount = size_t(CrewStation::Count), maxShipCrew = 8;
std::string_view crewStationName(CrewStation);
std::string_view crewOrderName(CrewOrder);
glm::dvec2 crewRoomPosition(CrewStation);
std::span<const std::pair<unsigned, unsigned>> crewCorridors();
struct CrewMember {
    uint64_t id = 0;
    std::string name;
    unsigned trait = 0;
    CrewStation duty = CrewStation::Bunks, room = CrewStation::Bunks, target = CrewStation::Bunks,
                walkTo = CrewStation::Bunks;
    CrewOrder order = CrewOrder::Duty;
    double health = 100, fatigue = 0, morale = 85, travel = 0, travelDuration = 0;
    std::array<double, 6> experience{};
    bool automatic = false;
};
// Tradable person data excludes ship-local duty, orders and walking state.
void writeCrewPerson(std::ostream&, const CrewMember&);
bool readCrewPerson(std::istream&, CrewMember&);
struct CrewTitle {
    uint64_t id = 0;
    bool aboard = false;
};
struct CrewBoarding {
    uint64_t revision = 0;
    CrewMember member;
};
struct CrewRoom {
    double integrity = 100, oxygen = 100, fire = 0, breach = 0;
    bool vented = false;
};
struct CrewTaskView {
    std::string status, detail;
    double travelSeconds = 0;
    bool blocked = false;
};
struct CrewView {
    bool open = false, flightOpen = false, autoDamageControl = true, connected = false, pending = false;
    size_t selected = 0;
    CrewStation station = CrewStation::Bridge;
    std::vector<CrewMember> members;
    std::vector<CrewTaskView> tasks;
    std::array<CrewRoom, crewRoomCount> rooms;
    double spares = 0, medical = 0;
    std::string summary, alert, message, port, recruit, sell, supply, care, selectedSkill;
    std::vector<std::string> events;
};
struct CrewReceipts {
    uint64_t hired = 0, supplied = 0, treated = 0;
};
// Mutable ship-only simulation. No world, coordinates or procedural generator writes.
class ShipCrew {
  public:
    explicit ShipCrew(uint64_t seed = 0);
    const std::vector<CrewMember>& members() const {
        return members_;
    }
    const std::array<CrewRoom, crewRoomCount>& rooms() const {
        return rooms_;
    }
    const CrewReceipts& receipts() const {
        return receipts_;
    }
    size_t living() const;
    double spares() const {
        return spares_;
    }
    double medical() const {
        return medical_;
    }
    bool automatic() const {
        return automatic_;
    }
    void toggleAutomatic() {
        automatic_ = !automatic_;
    }
    std::string assign(size_t member, CrewStation);
    std::string order(size_t member, CrewOrder, CrewStation);
    std::string command(CrewCommand, size_t member, CrewStation);
    std::string bestStations();
    std::string respond();
    CrewTaskView task(size_t member) const;
    double stationFit(size_t member, CrewStation) const;
    void recall();
    void vent(CrewStation);
    bool dismiss(size_t); // caller enforces port access
    void observeHull(double hull);
    void advance(double seconds, double properG = 0, bool combat = false, bool firing = false);
    void reconcile(CrewReceipts);
    void reconcileMarket(uint64_t revision, std::span<const CrewTitle>, std::span<const CrewBoarding>);
    double efficiency(CrewStation) const;
    double weaponReadiness() const;
    CrewMember recruit(uint64_t ordinal) const;
    CrewView view(size_t selected = 0, CrewStation station = CrewStation::Bridge) const;
    std::string serialize() const;
    bool restore(std::string_view);
    size_t residentBytes() const;

  private:
    uint64_t seed_ = 0, impacts_ = 0, marketRevision_ = 0;
    CrewReceipts receipts_;
    std::vector<CrewMember> members_;
    std::array<CrewRoom, crewRoomCount> rooms_{};
    std::vector<std::string> events_;
    double spares_ = 30, medical_ = 12, previousHull_ = -1, accumulator_ = 0;
    bool automatic_ = true;
    void step(double properG, bool combat, bool firing);
    void event(std::string);
    CrewStation nextRoom(CrewStation from, CrewStation to) const;
};
} // namespace infinite
