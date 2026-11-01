#include "infinite/Crew.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace infinite {
CrewTaskView ShipCrew::task(size_t i) const {
    CrewTaskView v;
    if (i >= members_.size()) {
        v.status = "NO CREW";
        v.blocked = true;
        return v;
    }
    const auto& m = members_[i];
    if (m.health < 20) {
        v.status = m.health <= 0 ? "LOST" : "INCAPACITATED";
        v.detail = m.health <= 0 ? "RELEASE OR REPLACE AT PORT" : "FIRST AID REQUIRED / CANNOT MOVE";
        v.blocked = true;
        return v;
    }
    const auto& room = rooms_[size_t(m.target)];
    v.status = std::string(crewOrderName(m.order));
    v.detail = std::string(crewStationName(m.target));
    if (m.id == reliefHelmId_ && m.order == CrewOrder::Duty && m.target == CrewStation::Bridge) {
        v.status = "RELIEF HELM";
        v.detail = "RELIEF HELM / " + std::string(weaponMountName(playerGun_)) + " TO CAPTAIN";
    } else if (m.duty == CrewStation::Weapons && m.order == CrewOrder::Duty) {
        v.detail = std::string(weaponMountName(m.weaponMount)) + " / " + gunnerStatus(m.weaponMount);
        v.blocked =
            m.weaponMount >= maxWeaponMounts || !weaponLevels_[m.weaponMount] ||
            (gunnerStatus(m.weaponMount) == "BAY UNSAFE" || gunnerStatus(m.weaponMount) == "GUN OFFLINE");
    }
    if (m.order == CrewOrder::Repair && (spares_ <= 0 || room.fire >= 5)) {
        v.detail = spares_ <= 0 ? "SPARES EMPTY / RESUPPLY AT PORT" : "FIRE TOO HOT / EXTINGUISH FIRST";
        v.blocked = true;
    } else if (m.order == CrewOrder::Aid && (medical_ <= 0 || room.oxygen < 18 || room.fire >= 10)) {
        v.detail = medical_ <= 0      ? "MEDICAL EMPTY / RESUPPLY AT PORT"
                   : room.oxygen < 18 ? "NO SAFE AIR / SEAL VENT OR PATCH BREACH"
                                      : "FIRE TOO HOT FOR FIRST AID";
        v.blocked = true;
    }
    const double coordination =
        1 + .1 * (efficiency(CrewStation::Bridge) + efficiency(CrewStation::Navigation));
    const double speed = 1.2 * (m.trait == 1 ? 1.2 : 1) * coordination;
    auto from = m.travel > 0 ? m.walkTo : m.room;
    v.travelSeconds = m.travel;
    for (size_t hop = 0; from != m.target && hop < crewRoomCount; ++hop) {
        const auto to = nextRoom(from, m.target);
        v.travelSeconds += glm::length(crewRoomPosition(from) - crewRoomPosition(to)) / speed + .25;
        from = to;
    }
    if (v.travelSeconds > 0) {
        v.status = "MOVING " + std::to_string(int(std::ceil(v.travelSeconds))) + "S";
        if (!v.blocked && m.id != reliefHelmId_)
            v.detail = std::string(crewOrderName(m.order)) + " / " +
                       std::string(m.order == CrewOrder::Duty ? crewDutyName(m) : crewStationName(m.target));
    } else if (!v.blocked) {
        if (m.order == CrewOrder::Repair)
            v.detail = "SYSTEM " + std::to_string(int(room.integrity)) + "% / BREACH " +
                       std::to_string(int(room.breach));
        else if (m.order == CrewOrder::Firefight)
            v.detail = "FIRE " + std::to_string(int(room.fire)) + "% / OXYGEN " +
                       std::to_string(int(room.oxygen)) + "%";
        else if (m.order == CrewOrder::Rest)
            v.detail = "FATIGUE " + std::to_string(int(m.fatigue)) + "% / RECOVERING";
        else if (m.order == CrewOrder::Aid)
            v.detail = "TREATING SURVIVORS / " + std::string(crewStationName(m.room));
    }
    return v;
}
std::string ShipCrew::bestStations() {
    // Working rooms plus installed gun consoles, with at most eight people.
    std::vector<std::pair<CrewStation, unsigned>> roles;
    for (unsigned s = 0; s < 6; ++s)
        roles.push_back({CrewStation(s), s == 2 ? 0u : noWeaponMount});
    for (unsigned gun = 1; gun < maxWeaponMounts; ++gun)
        if (weaponLevels_[gun])
            roles.push_back({CrewStation::Weapons, gun});
    constexpr size_t masks = size_t(1) << maxShipCrew;
    std::array<double, masks> score, next;
    std::array<std::array<unsigned, masks>, maxShipCrew + 1> parent{};
    std::array<std::array<int, masks>, maxShipCrew + 1> worker{};
    score.fill(-1e20);
    score[0] = 0;
    for (size_t station = 0; station < roles.size(); ++station) {
        next = score;
        for (size_t mask = 0; mask < masks; ++mask) {
            parent[station + 1][mask] = unsigned(mask);
            worker[station + 1][mask] = -1;
        }
        for (size_t mask = 0; mask < masks; ++mask)
            if (score[mask] > -1e19)
                for (size_t i = 0; i < members_.size(); ++i) {
                    const auto fit = stationFit(i, roles[station].first);
                    if ((mask & (size_t(1) << i)) || fit <= 0)
                        continue;
                    const auto target = mask | (size_t(1) << i);
                    const auto value = score[mask] + fit + (station < 6 ? 2 : 0) +
                                       (members_[i].duty == roles[station].first &&
                                                members_[i].weaponMount == roles[station].second
                                            ? .02
                                            : 0);
                    if (value > next[target] + 1e-12) {
                        next[target] = value;
                        parent[station + 1][target] = unsigned(mask);
                        worker[station + 1][target] = int(i);
                    }
                }
        score = next;
    }
    auto mask = size_t(std::max_element(score.begin(), score.end()) - score.begin());
    std::array<CrewStation, maxShipCrew> duties;
    duties.fill(CrewStation::Bunks);
    std::array<unsigned, maxShipCrew> guns;
    guns.fill(noWeaponMount);
    unsigned assigned = 0;
    for (size_t station = roles.size(); station > 0; --station) {
        const auto i = worker[station][mask];
        if (i >= 0) {
            duties[size_t(i)] = roles[station - 1].first;
            guns[size_t(i)] = roles[station - 1].second;
            ++assigned;
        }
        mask = parent[station][mask];
    }
    for (size_t i = 0; i < members_.size(); ++i)
        if (members_[i].health > 0) {
            members_[i].duty = duties[i];
            members_[i].weaponMount = guns[i];
            order(i, duties[i] == CrewStation::Bunks ? CrewOrder::Rest : CrewOrder::Duty, duties[i]);
        }
    const auto result = "BEST STATIONS / " + std::to_string(assigned) + "/" + std::to_string(roles.size()) +
                        " STAFFED / RELIEF TO BUNKS";
    event(result);
    return result;
}
std::string ShipCrew::respond() {
    std::array<bool, maxShipCrew> used{};
    unsigned dispatched = 0;
    auto dispatch = [&](CrewOrder job, CrewStation room, CrewStation skill) {
        if (std::any_of(members_.begin(), members_.end(),
                        [&](const auto& m) { return m.health >= 20 && m.order == job && m.target == room; }))
            return;
        size_t best = members_.size();
        double value = -1e20;
        for (size_t i = 0; i < members_.size(); ++i) {
            const auto& m = members_[i];
            if (used[i] || m.health < 20 || (m.order != CrewOrder::Duty && m.order != CrewOrder::Rest))
                continue;
            const double fit = stationFit(i, skill) * 10 + (m.duty == skill ? 4 : 0) +
                               (m.duty == CrewStation::Bunks ? 3 : 0) -
                               ((m.duty == CrewStation::Weapons || m.duty == CrewStation::Bridge ||
                                 m.duty == CrewStation::Navigation)
                                    ? 3
                                    : 0) -
                               glm::length(crewRoomPosition(m.room) - crewRoomPosition(room)) * .08;
            if (fit > value) {
                best = i;
                value = fit;
            }
        }
        if (best < members_.size()) {
            order(best, job, room);
            used[best] = true;
            ++dispatched;
        }
    };
    const auto fire = std::max_element(rooms_.begin(), rooms_.end(),
                                       [](const auto& a, const auto& b) { return a.fire < b.fire; });
    if (fire->fire > .01)
        dispatch(CrewOrder::Firefight, CrewStation(fire - rooms_.begin()), CrewStation::Engineering);
    if (spares_ > 0) {
        size_t broken = crewRoomCount;
        double damage = .01;
        for (size_t i = 0; i < rooms_.size(); ++i) {
            const auto& r = rooms_[i];
            const auto need = 100 - r.integrity + r.breach * 2;
            if (r.fire < 5 && need > damage) {
                broken = i;
                damage = need;
            }
        }
        if (broken != crewRoomCount)
            dispatch(CrewOrder::Repair, CrewStation(broken), CrewStation::Engineering);
    }
    if (medical_ > 0) {
        const CrewMember* worst = nullptr;
        for (const auto& m : members_) {
            const auto& r = rooms_[size_t(m.room)];
            if (m.health > 0 && m.health < 99.99 && r.oxygen >= 18 && r.fire < 10 &&
                (!worst || m.health < worst->health))
                worst = &m;
        }
        if (worst)
            dispatch(CrewOrder::Aid, worst->room, CrewStation::Medbay);
    }
    const auto result = dispatched ? "EMERGENCY / " + std::to_string(dispatched) + " CREW DISPATCHED"
                                   : "NO NEW SAFE TASK / CHECK ACTIVE ORDERS AND STORES";
    event(result);
    return result;
}
std::string ShipCrew::command(CrewCommand c, size_t i, CrewStation room) {
    if (c == CrewCommand::BestStations)
        return bestStations();
    if (c == CrewCommand::Emergency)
        return respond();
    if (c == CrewCommand::Automatic) {
        toggleAutomatic();
        return automatic_ ? "AUTOMATIC DAMAGE CONTROL ON" : "AUTOMATIC DAMAGE CONTROL OFF";
    }
    if (i >= members_.size() || members_[i].health < 20)
        return "NO FIT CREW / FIRST AID REQUIRED";
    switch (c) {
    case CrewCommand::Assign:
        return assign(i, room);
    case CrewCommand::Repair:
        return order(i, CrewOrder::Repair, room);
    case CrewCommand::Firefight:
        return order(i, CrewOrder::Firefight, room);
    case CrewCommand::Aid:
        return order(i, CrewOrder::Aid, room);
    case CrewCommand::Rest:
        return order(i, CrewOrder::Rest, CrewStation::Bunks);
    case CrewCommand::Return:
        return order(i, CrewOrder::Duty, members_[i].duty);
    default:
        return "UNKNOWN CREW COMMAND";
    }
}
} // namespace infinite
