#include "infinite/Crew.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
namespace infinite {
namespace {
constexpr std::array<std::pair<unsigned, unsigned>, 9> corridors{
    {{0, 1}, {0, 2}, {1, 2}, {1, 3}, {2, 4}, {3, 4}, {3, 5}, {4, 6}, {5, 6}}};
bool valid(CrewStation s) {
    return size_t(s) < crewRoomCount;
}
double skill(const CrewMember& m, CrewStation s) {
    const double xp = size_t(s) < 6 ? m.experience[size_t(s)] : 0;
    const double rank = xp >= 360 ? 2 : xp >= 120 ? 1 : 0;
    return (1 + .2 * rank) * (.25 + .75 * m.health / 100) * (1 - .55 * m.fatigue / 100) *
           (.75 + .25 * m.morale / 100);
}
} // namespace
std::span<const std::pair<unsigned, unsigned>> crewCorridors() {
    return corridors;
}
void writeCrewPerson(std::ostream& o, const CrewMember& m) {
    o << std::setprecision(17) << m.id << ' ' << std::quoted(m.name) << ' ' << m.trait << ' ' << m.health
      << ' ' << m.fatigue << ' ' << m.morale;
    for (auto xp : m.experience)
        o << ' ' << xp;
}
bool readCrewPerson(std::istream& in, CrewMember& person) {
    CrewMember m;
    if (!(in >> m.id >> std::quoted(m.name) >> m.trait >> m.health >> m.fatigue >> m.morale) || !m.id ||
        m.name.empty() || m.name.size() > 48 || m.trait > 2 ||
        m.name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-") != std::string::npos)
        return false;
    for (auto x : {m.health, m.fatigue, m.morale})
        if (!std::isfinite(x) || x < 0 || x > 100)
            return false;
    for (auto& xp : m.experience)
        if (!(in >> xp) || !std::isfinite(xp) || xp < 0 || xp > 3600)
            return false;
    person = std::move(m);
    return true;
}
std::string_view crewStationName(CrewStation s) {
    constexpr std::array<std::string_view, 7> names{"BRIDGE",       "NAVIGATION", "WEAPONS", "ENGINEERING",
                                                    "LIFE SUPPORT", "MEDBAY",     "BUNKS"};
    return valid(s) ? names[size_t(s)] : "UNKNOWN";
}
std::string_view crewOrderName(CrewOrder o) {
    constexpr std::array<std::string_view, 5> names{"ON STATION", "REPAIR", "FIGHT FIRE", "FIRST AID",
                                                    "REST"};
    return size_t(o) < names.size() ? names[size_t(o)] : "UNKNOWN";
}
glm::dvec2 crewRoomPosition(CrewStation s) {
    const std::array<glm::dvec2, 7> positions{
        {{0, -4}, {-2, -1.5}, {2, -1.5}, {-2, 1.5}, {2, 1.5}, {-1, 4}, {1, 4}}};
    return valid(s) ? positions[size_t(s)] : glm::dvec2(0);
}
ShipCrew::ShipCrew(uint64_t seed) : seed_(seed) {
    for (unsigned i = 0; i < 6; ++i) {
        auto m = recruit(10001 + i);
        m.id = i + 1;
        m.duty = m.room = m.target = m.walkTo = CrewStation(i);
        m.experience[i] = 120;
        members_.push_back(m);
    }
}
CrewMember ShipCrew::recruit(uint64_t ordinal) const {
    constexpr std::array<std::string_view, 12> first{"ADA",  "JUN",  "MARA", "IVO", "SAM", "NIA",
                                                     "THEO", "LENA", "OMAR", "KIT", "ELI", "SORA"};
    constexpr std::array<std::string_view, 12> last{"VALE",    "CHEN", "OKAFOR", "REYES", "SINGH", "WARD",
                                                    "IBRAHIM", "PARK", "SILVA",  "ROSS",  "SHAH",  "ITO"};
    auto h = mix64(seed_ ^ 0x4352455753484950ULL ^ mix64(ordinal));
    CrewMember m;
    m.id = ordinal + 100;
    const auto names = mix64(seed_ ^ 0x435245574e414d45ULL);
    m.name = std::string(first[(names % first.size() + ordinal) % first.size()]) + " " +
             std::string(last[((names >> 16) % last.size() + ordinal / first.size()) % last.size()]);
    m.trait = unsigned((h >> 32) % 3);
    for (size_t i = 0; i < 6; ++i)
        m.experience[i] = double((h >> (i * 7)) % 60);
    m.experience[ordinal % 6] += 120;
    return m;
}
size_t ShipCrew::living() const {
    return size_t(
        std::count_if(members_.begin(), members_.end(), [](const auto& m) { return m.health > 0; }));
}
void ShipCrew::event(std::string s) {
    if (events_.size() == 8)
        events_.erase(events_.begin());
    events_.push_back(std::move(s));
}
std::string ShipCrew::assign(size_t i, CrewStation s) {
    if (i >= members_.size() || !valid(s) || members_[i].health <= 0)
        return "NO FIT CREW SELECTED";
    auto& m = members_[i];
    if (s != CrewStation::Bunks)
        for (size_t j = 0; j < members_.size(); ++j)
            if (j != i && members_[j].health > 0 && members_[j].duty == s) {
                members_[j].duty = m.duty;
                members_[j].order = CrewOrder::Duty;
                members_[j].target = m.duty;
                members_[j].automatic = false;
            }
    m.duty = s;
    return order(i, CrewOrder::Duty, s);
}
std::string ShipCrew::order(size_t i, CrewOrder o, CrewStation s) {
    if (i >= members_.size() || !valid(s) || unsigned(o) > unsigned(CrewOrder::Rest) ||
        members_[i].health <= 0)
        return "NO FIT CREW SELECTED";
    auto& m = members_[i];
    m.order = o;
    m.target = o == CrewOrder::Duty ? m.duty : o == CrewOrder::Rest ? CrewStation::Bunks : s;
    m.automatic = false;
    return m.name + " / " + std::string(crewOrderName(o)) + " / " + std::string(crewStationName(m.target));
}
void ShipCrew::recall() {
    for (size_t i = 0; i < members_.size(); ++i)
        order(i, CrewOrder::Duty, members_[i].duty);
    event("CREW RETURNING TO ASSIGNED STATIONS");
}
void ShipCrew::vent(CrewStation s) {
    if (valid(s)) {
        rooms_[size_t(s)].vented = !rooms_[size_t(s)].vented;
        event(std::string(crewStationName(s)) +
              (rooms_[size_t(s)].vented ? " / VENT OPEN" : " / VENT SEALED"));
    }
}
bool ShipCrew::dismiss(size_t i) {
    if (i >= members_.size())
        return false;
    event(members_[i].name + " / LEFT SHIP AT PORT");
    members_.erase(members_.begin() + ptrdiff_t(i));
    return true;
}
void ShipCrew::observeHull(double hull) {
    if (!std::isfinite(hull) || hull < 0 || hull > 100)
        return;
    const double damage = previousHull_ < 0 ? 0 : std::max(0., previousHull_ - hull);
    previousHull_ = hull;
    if (damage <= 0)
        return;
    const auto h = mix64(seed_ ^ mix64(++impacts_));
    const size_t r = size_t(h % 6);
    auto& room = rooms_[r];
    room.integrity = std::max(0., room.integrity - damage * 1.5);
    if (damage >= 15 || ((h >> 8) & 3) == 0)
        room.fire = std::min(100., room.fire + damage * 1.8);
    if (damage >= 6 && ((h >> 12) & 1))
        room.breach = std::min(100., room.breach + damage);
    for (auto& m : members_)
        if (m.health > 0) {
            m.morale = std::max(0., m.morale - damage * .3);
            if (size_t(m.room) == r) {
                m.health = std::max(0., m.health - damage * (m.trait == 2 ? .55 : .8));
                if (m.health == 0) {
                    m.travel = 0;
                    event(m.name + " / LOST ABOARD SHIP");
                }
            }
        }
    event(std::string(crewStationName(CrewStation(r))) + " / IMPACT / " +
          (room.fire > 0 ? "FIRE" : "SYSTEM DAMAGE"));
}
double ShipCrew::efficiency(CrewStation s) const {
    if (!valid(s))
        return 0;
    double best = 0;
    for (const auto& m : members_)
        if (m.health >= 20 && m.travel <= 0 && m.room == s && m.order == CrewOrder::Duty)
            best = std::max(best, skill(m, s));
    return best * rooms_[size_t(s)].integrity / 100.;
}
double ShipCrew::stationFit(size_t i, CrewStation s) const {
    return i < members_.size() && valid(s) && members_[i].health >= 20 ? skill(members_[i], s) : 0;
}
double ShipCrew::weaponReadiness() const {
    return std::clamp((.65 + .35 * rooms_[size_t(CrewStation::Weapons)].integrity / 100.) *
                          (1 + .12 * efficiency(CrewStation::Weapons)),
                      .65, 1.2);
}
CrewStation ShipCrew::nextRoom(CrewStation from, CrewStation to) const {
    std::array<double, crewRoomCount> distance;
    distance.fill(1e20);
    std::array<unsigned, crewRoomCount> previous{};
    std::array<bool, crewRoomCount> done{};
    distance[size_t(from)] = 0;
    for (size_t k = 0; k < crewRoomCount; ++k) {
        size_t a = crewRoomCount;
        for (size_t i = 0; i < crewRoomCount; ++i)
            if (!done[i] && (a == crewRoomCount || distance[i] < distance[a]))
                a = i;
        if (a == crewRoomCount || a == size_t(to))
            break;
        done[a] = true;
        for (auto [x, y] : corridors) {
            const size_t b = x == a ? y : y == a ? x : crewRoomCount;
            if (b == crewRoomCount)
                continue;
            const auto& room = rooms_[b];
            const double cost =
                glm::length(crewRoomPosition(CrewStation(a)) - crewRoomPosition(CrewStation(b))) *
                (1 + room.fire * .015 + (room.oxygen < 20 ? 3 : 0));
            if (distance[a] + cost < distance[b]) {
                distance[b] = distance[a] + cost;
                previous[b] = unsigned(a);
            }
        }
    }
    auto b = size_t(to);
    while (previous[b] != size_t(from) && b != size_t(from))
        b = previous[b];
    return CrewStation(b);
}
void ShipCrew::advance(double seconds, double g, bool combat, bool firing) {
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 10 || !std::isfinite(g))
        return;
    accumulator_ += seconds;
    while (accumulator_ + 1e-10 >= .25) {
        accumulator_ = std::max(0., accumulator_ - .25);
        step(std::clamp(g, 0., 20.), combat, firing);
    }
    if (accumulator_ < 1e-9)
        accumulator_ = 0;
}
void ShipCrew::step(double g, bool combat, bool firing) {
    constexpr double dt = .25;
    const auto life = efficiency(CrewStation::LifeSupport);
    const auto coordination =
        1 + .1 * (efficiency(CrewStation::Bridge) + efficiency(CrewStation::Navigation));
    for (auto& r : rooms_) {
        r.oxygen = std::clamp(r.oxygen + dt * ((r.vented ? -8. : (1.2 + life) * rooms_[4].integrity / 100.) -
                                               r.breach * .1 - r.fire * .01),
                              0., 100.);
        if (r.fire > 0) {
            r.fire = std::clamp(r.fire + dt * (r.oxygen > 20 ? .35 : -4.), 0., 100.);
            r.integrity = std::max(0., r.integrity - r.fire * .002 * dt);
        }
    }
    unsigned responders = 0;
    for (const auto& m : members_)
        if (m.health >= 20 && (m.order == CrewOrder::Repair || m.order == CrewOrder::Firefight))
            ++responders;
    for (auto& m : members_) {
        if (m.health <= 0)
            continue;
        auto& room = rooms_[size_t(m.room)];
        const double oldHealth = m.health;
        m.health = std::max(
            0., m.health - dt * (room.fire * .012 + (room.oxygen < 18 ? (18 - room.oxygen) * .045 : 0)));
        if (m.health == 0) {
            event(m.name + " / LOST ABOARD SHIP");
            m.travel = 0;
            continue;
        }
        const bool finishedAid =
            m.order == CrewOrder::Aid && std::none_of(members_.begin(), members_.end(), [&](const auto& c) {
                return c.health > 0 && c.health < 99.99 && (m.automatic || c.room == m.target);
            });
        if ((m.automatic && m.order == CrewOrder::Rest && m.fatigue < 20) || finishedAid ||
            (m.order == CrewOrder::Firefight && rooms_[size_t(m.target)].fire < .01) ||
            (m.order == CrewOrder::Repair && rooms_[size_t(m.target)].integrity >= 99.99 &&
             rooms_[size_t(m.target)].breach <= .01)) {
            event(m.name + " / " + std::string(crewOrderName(m.order)) + " COMPLETE / RETURNING TO DUTY");
            m.order = CrewOrder::Duty;
            m.target = m.duty;
            m.automatic = false;
        }
        if (m.automatic && m.order == CrewOrder::Aid) {
            const auto patient =
                std::min_element(members_.begin(), members_.end(), [](const auto& a, const auto& b) {
                    return (a.health > 0 ? a.health : 101) < (b.health > 0 ? b.health : 101);
                });
            if (patient != members_.end() && patient->health < 99.99)
                m.target = patient->room;
        }
        if (automatic_ && m.order == CrewOrder::Duty && m.health >= 20) {
            if (m.duty == CrewStation::Medbay) {
                auto patient =
                    std::min_element(members_.begin(), members_.end(), [](const auto& a, const auto& b) {
                        return (a.health > 0 ? a.health : 101) < (b.health > 0 ? b.health : 101);
                    });
                if (patient != members_.end() && patient->health < 90 && medical_ > 0) {
                    m.order = CrewOrder::Aid;
                    m.target = patient->room;
                    m.automatic = true;
                }
            }
            if ((m.duty == CrewStation::Engineering || m.duty == CrewStation::LifeSupport) &&
                responders < 2) {
                auto worst = std::max_element(rooms_.begin(), rooms_.end(),
                                              [](const auto& a, const auto& b) { return a.fire < b.fire; });
                if (worst->fire > .01) {
                    m.order = CrewOrder::Firefight;
                    m.target = CrewStation(worst - rooms_.begin());
                    m.automatic = true;
                    ++responders;
                } else if (spares_ > 0) {
                    auto broken =
                        std::min_element(rooms_.begin(), rooms_.end(), [](const auto& a, const auto& b) {
                            return a.integrity - a.breach < b.integrity - b.breach;
                        });
                    if (broken->integrity < 99.99 || broken->breach > .01) {
                        m.order = CrewOrder::Repair;
                        m.target = CrewStation(broken - rooms_.begin());
                        m.automatic = true;
                        ++responders;
                    }
                }
            }
            if (m.order == CrewOrder::Duty && m.fatigue > 85) {
                m.order = CrewOrder::Rest;
                m.target = CrewStation::Bunks;
                m.automatic = true;
            }
        }
        m.fatigue =
            std::clamp(m.fatigue + dt * (((m.order == CrewOrder::Rest ||
                                           (m.order == CrewOrder::Duty && m.duty == CrewStation::Bunks)) &&
                                          m.room == CrewStation::Bunks && m.travel <= 0)
                                             ? -.18
                                             : .009 + std::max(0., g - 2) * .02),
                       0., 100.);
        m.morale = std::clamp(m.morale + dt * ((combat ? -.035 : .01) * (m.trait == 0 ? .65 : 1)) -
                                  (oldHealth - m.health) * .2,
                              0., 100.);
        if (m.travel > 0) {
            m.travel = std::max(0., m.travel - dt);
            if (m.travel == 0)
                m.room = m.walkTo;
            continue;
        }
        if (m.health < 20)
            continue;
        if (m.room != m.target) {
            m.walkTo = nextRoom(m.room, m.target);
            m.travelDuration = glm::length(crewRoomPosition(m.room) - crewRoomPosition(m.walkTo)) /
                               (1.2 * (m.trait == 1 ? 1.2 : 1) * coordination);
            m.travel = m.travelDuration;
            continue;
        }
        const double ability = skill(m, m.order == CrewOrder::Aid    ? CrewStation::Medbay
                                        : m.order == CrewOrder::Duty ? m.duty
                                                                     : CrewStation::Engineering);
        bool worked = false;
        if (m.order == CrewOrder::Firefight && room.fire > 0) {
            room.fire = std::max(0., room.fire - 4 * ability * dt);
            worked = true;
        } else if (m.order == CrewOrder::Repair && room.fire < 5 && spares_ > 0) {
            const double work = std::min(spares_ * 5., 2.4 * ability * dt);
            const double patched = std::min(room.breach, work);
            room.breach -= patched;
            const double repaired = std::min(100. - room.integrity, work - patched);
            room.integrity += repaired;
            spares_ = std::max(0., spares_ - (patched + repaired) / 5.);
            worked = patched + repaired > 0;
        } else if (m.order == CrewOrder::Aid && medical_ > 0 && room.oxygen >= 18 && room.fire < 10) {
            for (auto& patient : members_)
                if (patient.health > 0 && patient.health < 100 && patient.room == m.room &&
                    patient.travel <= 0) {
                    const auto heal = std::min({100. - patient.health, medical_ * 20., .8 * ability * dt});
                    patient.health += heal;
                    medical_ = std::max(0., medical_ - heal / 20.);
                    worked |= heal > 0;
                }
        } else if (m.order == CrewOrder::Duty && size_t(m.duty) < 6)
            worked = m.duty != CrewStation::Weapons || firing;
        if (worked) {
            const auto job = m.order == CrewOrder::Aid    ? CrewStation::Medbay
                             : m.order == CrewOrder::Duty ? m.duty
                                                          : CrewStation::Engineering;
            m.experience[size_t(job)] =
                std::min(3600., m.experience[size_t(job)] + dt * (m.order == CrewOrder::Duty ? .03 : .3));
        }
    }
}
void ShipCrew::reconcile(CrewReceipts r) {
    if (r.hired > 1000000 || r.supplied > 1000000 || r.treated > 1000000)
        return;
    while (receipts_.hired < r.hired && living() < maxShipCrew) {
        auto m = recruit(++receipts_.hired);
        if (std::any_of(members_.begin(), members_.end(),
                        [&](const auto& old) { return old.health > 0 && old.name == m.name; }))
            m.name += " " + std::to_string(receipts_.hired);
        auto dead =
            std::find_if(members_.begin(), members_.end(), [](const auto& c) { return c.health <= 0; });
        event(m.name + " / SIGNED ABOARD / RELIEF CREW");
        if (dead != members_.end())
            *dead = m;
        else
            members_.push_back(m);
    }
    if (r.supplied > receipts_.supplied) {
        const auto n = r.supplied - receipts_.supplied;
        spares_ = std::min(100., spares_ + 10 * double(n));
        medical_ = std::min(50., medical_ + 5 * double(n));
        receipts_.supplied = r.supplied;
        event("CREW STORES / SPARES AND MEDICAL RECEIVED");
    }
    if (r.treated > receipts_.treated) {
        for (auto& m : members_)
            if (m.health > 0) {
                m.health = 100;
                m.fatigue = 0;
                m.morale = 100;
            }
        receipts_.treated = r.treated;
        event("SHORE CLINIC / SURVIVORS TREATED AND RESTED");
    }
}
CrewView ShipCrew::view(size_t selected, CrewStation station) const {
    CrewView v;
    v.members = members_;
    for (size_t i = 0; i < members_.size(); ++i)
        v.tasks.push_back(task(i));
    v.rooms = rooms_;
    v.spares = spares_;
    v.medical = medical_;
    v.autoDamageControl = automatic_;
    v.events = events_;
    v.selected = members_.empty() ? 0 : std::min(selected, members_.size() - 1);
    v.station = valid(station) ? station : CrewStation::Bridge;
    v.summary = "CREW " + std::to_string(living()) + "/8 / F9 ORDERS / F8 DECK";
    v.alert = "CREW STATIONS NORMAL / MANUAL FLIGHT LIVE";
    if (std::any_of(rooms_.begin(), rooms_.end(), [](const auto& r) { return r.fire > 1 || r.oxygen < 25; }))
        v.summary = "CREW EMERGENCY / FIRE OR LOW OXYGEN / F9 ORDERS";
    for (size_t i = 0; i < rooms_.size(); ++i)
        if (rooms_[i].fire > 1 || rooms_[i].oxygen < 25) {
            v.alert = std::string(crewStationName(CrewStation(i))) +
                      (rooms_[i].fire > 1 ? " / FIRE " + std::to_string(int(rooms_[i].fire))
                                          : " / OXYGEN " + std::to_string(int(rooms_[i].oxygen))) +
                      " / RESPONSE REQUIRED";
            break;
        }
    if (v.alert == "CREW STATIONS NORMAL / MANUAL FLIGHT LIVE") {
        for (size_t i = 0; i < rooms_.size(); ++i)
            if (rooms_[i].breach > 1 || rooms_[i].integrity < 70) {
                v.alert = std::string(crewStationName(CrewStation(i))) + " / DAMAGE / REPAIR REQUIRED";
                break;
            }
    }
    if (v.alert == "CREW STATIONS NORMAL / MANUAL FLIGHT LIVE") {
        for (const auto& m : members_)
            if (m.health < 80 || m.fatigue > 80) {
                v.alert = m.name + (m.health <= 0   ? " / CREW LOST / PORT REPLACEMENT"
                                    : m.health < 20 ? " / INCAPACITATED / FIRST AID"
                                    : m.health < 80 ? " / INJURED / FIRST AID"
                                                    : " / EXHAUSTED / REST REQUIRED");
                break;
            }
    }
    if (v.summary.find("EMERGENCY") == std::string::npos &&
        v.alert != "CREW STATIONS NORMAL / MANUAL FLIGHT LIVE")
        v.summary = "F9 CREW ALERT / " + v.alert;
    if (!members_.empty()) {
        const auto& m = members_[v.selected];
        v.selectedSkill = (m.trait == 0 ? "CALM" : m.trait == 1 ? "AGILE" : "HARDY");
        for (size_t i = 0; i < 6; ++i)
            v.selectedSkill += " / " + std::to_string(unsigned(m.experience[i] >= 360   ? 2
                                                               : m.experience[i] >= 120 ? 1
                                                                                        : 0));
    }
    return v;
}
void ShipCrew::reconcileMarket(uint64_t revision, std::span<const CrewTitle> titles,
                               std::span<const CrewBoarding> incoming) {
    if (revision < marketRevision_ || revision > 1000000000000ULL)
        return;
    std::erase_if(members_, [&](const auto& m) {
        const auto title =
            std::find_if(titles.begin(), titles.end(), [&](const auto& t) { return t.id == m.id; });
        if (title == titles.end() || title->aboard)
            return false;
        event(m.name + " / TRANSFERRED OFF SHIP");
        return true;
    });
    for (const auto& boarding : incoming) {
        if (boarding.revision <= marketRevision_)
            continue;
        const auto title = std::find_if(titles.begin(), titles.end(), [&](const auto& t) {
            return t.id == boarding.member.id && t.aboard;
        });
        if (title == titles.end())
            continue;
        auto found = std::find_if(members_.begin(), members_.end(),
                                  [&](const auto& m) { return m.id == boarding.member.id; });
        if (found == members_.end() && living() >= maxShipCrew)
            return; // retain the paid boarding receipt until a berth opens
        auto m = boarding.member;
        m.duty = m.room = m.target = m.walkTo = CrewStation::Bunks;
        m.order = CrewOrder::Duty;
        m.travel = m.travelDuration = 0;
        m.automatic = false;
        event(m.name + " / CREW PURCHASE BOARDED");
        if (found != members_.end())
            *found = std::move(m);
        else {
            auto dead =
                std::find_if(members_.begin(), members_.end(), [](const auto& p) { return p.health <= 0; });
            if (dead != members_.end())
                *dead = std::move(m);
            else
                members_.push_back(std::move(m));
        }
        marketRevision_ = boarding.revision;
    }
    marketRevision_ = revision;
}
std::string ShipCrew::serialize() const {
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITE_CREW 2 " << seed_ << ' ' << impacts_ << ' ' << receipts_.hired
      << ' ' << receipts_.supplied << ' ' << receipts_.treated << ' ' << spares_ << ' ' << medical_ << ' '
      << previousHull_ << ' ' << accumulator_ << ' ' << automatic_ << ' ' << marketRevision_ << ' '
      << members_.size();
    for (const auto& m : members_) {
        o << ' ' << m.id << ' ' << std::quoted(m.name) << ' ' << m.trait << ' ' << unsigned(m.duty) << ' '
          << unsigned(m.room) << ' ' << unsigned(m.target) << ' ' << unsigned(m.walkTo) << ' '
          << unsigned(m.order) << ' ' << m.health << ' ' << m.fatigue << ' ' << m.morale << ' ' << m.travel
          << ' ' << m.travelDuration << ' ' << m.automatic;
        for (auto x : m.experience)
            o << ' ' << x;
    }
    for (const auto& r : rooms_)
        o << ' ' << r.integrity << ' ' << r.oxygen << ' ' << r.fire << ' ' << r.breach << ' ' << r.vented;
    o << ' ' << events_.size();
    for (const auto& e : events_)
        o << ' ' << std::quoted(e);
    return o.str();
}
bool ShipCrew::restore(std::string_view text) {
    if (text.size() > 65536)
        return false;
    ShipCrew c(seed_);
    c.members_.clear();
    std::istringstream in{std::string(text)};
    std::string h;
    unsigned v, automatic;
    size_t count;
    auto range = [](double x, double lo, double hi) { return std::isfinite(x) && x >= lo && x <= hi; };
    uint64_t seed;
    if (!(in >> h >> v >> seed >> c.impacts_ >> c.receipts_.hired >> c.receipts_.supplied >>
          c.receipts_.treated >> c.spares_ >> c.medical_ >> c.previousHull_ >> c.accumulator_ >> automatic) ||
        h != "INFINITE_CREW" || (v != 1 && v != 2) || seed != seed_ || automatic > 1 ||
        c.impacts_ > 1000000000000ULL || c.receipts_.hired > 1000000 || c.receipts_.supplied > 1000000 ||
        c.receipts_.treated > 1000000 || !range(c.spares_, 0, 100) || !range(c.medical_, 0, 50) ||
        !range(c.previousHull_, -1, 100) || !range(c.accumulator_, 0, .25) ||
        (c.previousHull_ < 0 && c.previousHull_ != -1))
        return false;
    if (v >= 2 && (!(in >> c.marketRevision_) || c.marketRevision_ > 1000000000000ULL))
        return false;
    if (!(in >> count) || count > maxShipCrew)
        return false;
    c.automatic_ = automatic;
    std::set<uint64_t> ids;
    std::set<unsigned> duties;
    for (size_t i = 0; i < count; ++i) {
        CrewMember m;
        unsigned duty, room, target, walk, order, autoOrder;
        if (!(in >> m.id >> std::quoted(m.name) >> m.trait >> duty >> room >> target >> walk >> order >>
              m.health >> m.fatigue >> m.morale >> m.travel >> m.travelDuration >> autoOrder) ||
            !m.id || m.name.empty() || m.name.size() > 48 ||
            m.name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-") != std::string::npos ||
            m.trait > 2 || duty >= crewRoomCount || room >= crewRoomCount || target >= crewRoomCount ||
            walk >= crewRoomCount || order > unsigned(CrewOrder::Rest) || autoOrder > 1 ||
            !range(m.health, 0, 100) || !range(m.fatigue, 0, 100) || !range(m.morale, 0, 100) ||
            !range(m.travel, 0, 20) || !range(m.travelDuration, 0, 20) || m.travel > m.travelDuration ||
            !ids.insert(m.id).second)
            return false;
        if (m.health > 0 && duty != unsigned(CrewStation::Bunks) && !duties.insert(duty).second)
            return false;
        if (m.travel > 0 &&
            (m.health <= 0 || std::none_of(corridors.begin(), corridors.end(), [&](const auto& edge) {
                 return (edge.first == room && edge.second == walk) ||
                        (edge.first == walk && edge.second == room);
             })))
            return false;
        m.duty = CrewStation(duty);
        m.room = CrewStation(room);
        m.target = CrewStation(target);
        m.walkTo = CrewStation(walk);
        m.order = CrewOrder(order);
        m.automatic = autoOrder;
        for (auto& x : m.experience)
            if (!(in >> x) || !range(x, 0, 3600))
                return false;
        c.members_.push_back(m);
    }
    for (auto& r : c.rooms_) {
        unsigned vent;
        if (!(in >> r.integrity >> r.oxygen >> r.fire >> r.breach >> vent) || !range(r.integrity, 0, 100) ||
            !range(r.oxygen, 0, 100) || !range(r.fire, 0, 100) || !range(r.breach, 0, 100) || vent > 1)
            return false;
        r.vented = vent;
    }
    if (!(in >> count) || count > 8)
        return false;
    c.events_.clear();
    for (size_t i = 0; i < count; ++i) {
        std::string e;
        if (!(in >> std::quoted(e)) || e.size() > 160 ||
            std::any_of(e.begin(), e.end(), [](unsigned char ch) { return ch < 32 || ch > 126; }))
            return false;
        c.events_.push_back(e);
    }
    in >> std::ws;
    if (!in.eof())
        return false;
    *this = std::move(c);
    return true;
}
size_t ShipCrew::residentBytes() const {
    size_t n = sizeof(*this) + members_.capacity() * sizeof(CrewMember);
    for (const auto& m : members_)
        n += m.name.capacity();
    for (const auto& e : events_)
        n += sizeof(std::string) + e.capacity();
    return n;
}
} // namespace infinite
