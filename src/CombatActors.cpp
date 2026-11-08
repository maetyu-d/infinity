#include "infinity/CombatActors.hpp"
#include <numbers>
namespace infinity {
bool isRaider(const ActivityId& id) {
    return id.slot >= raiderActivitySlot && id.slot <= pursuitRaiderSlot && id.home.kind() == Kind::Planet;
}
ActivityBody sampleRaider(const Universe& u, const Address& address, double seconds) {
    const auto planet = u.describe(address);
    auto harbour = sampleActivity(planet, 0, seconds);
    const auto up = glm::normalize(harbour.position);
    auto along = glm::cross(up, glm::dvec3(0, 1, 0));
    if (glm::length(along) < .01)
        along = glm::cross(up, glm::dvec3(1, 0, 0));
    along = glm::normalize(along);
    const auto across = glm::cross(up, along);
    ActivityBody b;
    b.id = {address, raiderActivitySlot};
    b.kind = ActivityKind::Raider;
    b.seed = mix64(planet.seed ^ 0x5241494445523031ULL);
    const double phase = double(b.seed & 65535) / 65536 * 2 * std::numbers::pi;
    const double omega = 2 * std::numbers::pi / 650;
    const double angle = phase + seconds * omega;
    b.position =
        harbour.position + along * (std::cos(angle) * 8500) + across * (std::sin(angle) * 8500) + up * 1800.;
    b.velocity = (-along * std::sin(angle) + across * std::cos(angle)) * (8500 * omega);
    b.orientation = glm::quatLookAtRH(glm::normalize(b.velocity), up);
    b.radius = 12;
    b.phase = phase;
    b.tint = {1, .25, .18};
    b.hostile = true;
    b.name = "RAIDER " + std::to_string(b.seed % 10000);
    b.status = "HOSTILE / PATROLLING OUTER LANES";
    return b;
}
RaiderRole raiderRole(const ActivityId& id) {
    return isRaider(id) ? RaiderRole(id.slot - raiderActivitySlot) : RaiderRole::Legacy;
}
std::string_view raiderFaction(uint64_t seed) {
    constexpr std::array names{"CINDER CELL", "RIM CARTEL", "ASH CORSAIRS", "VOID LEAGUE"};
    return names[mix64(seed) % names.size()];
}
ActivityBody sampleCombatActivity(const Universe& u, const ActivityId& id, double seconds) {
    if (!isRaider(id))
        return sampleActivity(u.describe(id.home), id.slot, seconds);
    auto body = sampleRaider(u, id.home, seconds);
    if (id.slot == raiderActivitySlot)
        return body;
    body.id = id;
    body.seed = mix64(body.seed ^ id.slot);
    const auto planet = u.describe(id.home);
    const auto harbour = sampleActivity(planet, 0, seconds);
    const auto up = glm::normalize(harbour.position);
    const auto along = glm::normalize(sampleActivity(planet, 1, seconds).position - harbour.position);
    const auto flank = glm::normalize(glm::cross(up, along));
    const double angle = seconds * (2 * std::numbers::pi / 640.) +
        double(seedOf(id.home, u.seed()) % 1000) * (.002 * std::numbers::pi);
    const double reach = id.slot == cargoRaiderSlot ? 11500. : id.slot == factionHunterSlot ? 13800. : 12900.;
    const double phase = angle + (id.slot == cutoffRaiderSlot ? .17 :
                                  id.slot == raiderScoutSlot ? .38 :
                                  id.slot == pursuitRaiderSlot ? .62 : 0.);
    body.position = harbour.position + up * 2200. +
        reach * (along * std::cos(phase) + flank * std::sin(phase));
    body.velocity = reach * (2 * std::numbers::pi / 640.) *
        (-along * std::sin(phase) + flank * std::cos(phase));
    body.orientation = glm::quatLookAtRH(glm::normalize(body.velocity), up);
    switch (raiderRole(id)) {
    case RaiderRole::Skirmisher:
        body.radius = 8;
        body.tint = {1, .48, .12};
        break;
    case RaiderRole::Siege:
        body.radius = 23;
        body.tint = {1, .17, .47};
        break;
    case RaiderRole::Saboteur:
        body.radius = 10;
        body.tint = {.79, .33, 1};
        break;
    case RaiderRole::Scavenger: {
        // Shadow a canonical fragment, leaving mining and its orbit untouched.
        const auto debris = sampleActivity(planet, 1000, seconds);
        const auto radial = glm::normalize(debris.position);
        const auto tangent = glm::normalize(debris.velocity);
        body.position = debris.position + radial * 1600. - tangent * 1900.;
        body.velocity = debris.velocity;
        body.orientation = debris.orientation;
        body.radius = 15;
        body.tint = {1, .70, .26};
        break;
    }
    case RaiderRole::Scout:
        body.radius = 7;
        body.tint = {.98, .76, .20};
        break;
    case RaiderRole::Pursuit:
        body.radius = 13;
        body.tint = {1, .27, .31};
        break;
    default:
        break;
    }
    const auto faction = raiderFaction(seedOf(id.home.parent(), u.seed()));
    constexpr std::array roles{"RAIDER", "CARGO RAIDER", "MISSILE WING", "CUTOFF WING", "FACTION HUNTER",
                               "SKIRMISHER", "SIEGE SHIP", "SABOTEUR", "SCAVENGER",
                               "RELAY SCOUT", "PURSUIT CORSAIR"};
    body.name = std::string(roles[size_t(raiderRole(id))]) + " " + std::to_string(body.seed % 10000);
    body.hostile = id.slot == missileWingSlot || id.slot == cutoffRaiderSlot ||
                   id.slot == skirmisherSlot || id.slot == siegeRaiderSlot || id.slot == saboteurSlot;
    body.status = std::string(faction) + " / " + roles[size_t(raiderRole(id))];
    return body;
}
} // namespace infinity
