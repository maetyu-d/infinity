#include "infinite/CombatActors.hpp"
#include <numbers>
namespace infinite {
bool isRaider(const ActivityId& id) {
    return id.slot == raiderActivitySlot && id.home.kind() == Kind::Planet;
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
ActivityBody sampleCombatActivity(const Universe& u, const ActivityId& id, double seconds) {
    return isRaider(id) ? sampleRaider(u, id.home, seconds)
                        : sampleActivity(u.describe(id.home), id.slot, seconds);
}
} // namespace infinite
