#include "infinite/Cockpit.hpp"
#include "infinite/NpcCombat.hpp"
#include "infinite/Piloting.hpp"
#include <iostream>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
struct Scenario {
    Universe u;
    Address planet = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    ActivityBody raider = sampleRaider(u, planet, 100);
    NpcPlayer player;
    Scenario() {
        raider.velocity = {};
        player.reference = planet;
        player.position = raider.position + glm::dvec3(0, 0, 1600);
        player.forward = {0, 1, 0};
        player.seconds = 100;
        raider.orientation = glm::quatLookAtRH(glm::normalize(player.position - raider.position),
                                               glm::normalize(raider.position));
    }
};
void advance(Scenario& s, NpcCombat& ai, const WeaponSystem& w, std::span<const ActivityBody> bodies,
             unsigned ticks, double* damage = nullptr) {
    for (unsigned i = 0; i < ticks; ++i) {
        s.player.seconds += 1. / 60;
        const auto report = ai.update(s.u, s.player, bodies, w, 1. / 60);
        if (damage)
            *damage += report.playerDamage;
        check(ai.residentBytes() < 2 * 1024 * 1024, "Combat director exceeded bounded memory");
    }
}
void hit(Scenario& s, WeaponSystem& w, ActivityBody body) {
    WeaponRay r;
    r.reference = s.planet;
    r.origin = s.player.position;
    r.direction = glm::normalize(body.position - r.origin);
    r.seconds = s.player.seconds;
    w.advance(1);
    check(w.fire(s.u, r, std::span(&body, 1)), "Controlled player shot failed");
}
} // namespace
int main() {
    try {
        Scenario s;
        WeaponSystem weapons;
        NpcCombat ai;
        double damage = 0;
        advance(s, ai, weapons, std::span(&s.raider, 1), 240, &damage);
        check(damage > 0 && ai.agents().at(s.raider.id).shots > 0,
              "Hostile did not acquire and fire on player");
        check(ai.agents().at(s.raider.id).mode == NpcMode::Attack, "Hostile did not enter attack state");
        const auto state = ai.serialize(s.u.seed());
        NpcCombat restored;
        check(restored.restore(state, s.u) && restored.serialize(s.u.seed()) == state,
              "NPC state failed exact save round trip");
        auto before = s.raider, after = s.raider;
        ai.apply(before);
        restored.apply(after);
        check(before.id == after.id && before.position == after.position && before.velocity == after.velocity,
              "Reload changed actor identity or coordinates");
        check(!restored.restore(state + " corrupt", s.u) && restored.serialize(s.u.seed()) == state,
              "Corrupt AI save changed live state");
        auto target = s.raider;
        ai.apply(target);
        hit(s, weapons, target);
        advance(s, ai, weapons, std::span(&s.raider, 1), 1);
        check(ai.agents().at(s.raider.id).mode == NpcMode::Evade, "Incoming damage did not trigger evasion");
        const auto drift = ai.agents().at(s.raider.id).drift;
        advance(s, ai, weapons, std::span(&s.raider, 1), 35);
        check(glm::length(ai.agents().at(s.raider.id).drift - drift) > 1, "Evasion did not steer the ship");
        // Eight further starter hits leave ten hull, triggering a genuine escape course.
        for (unsigned i = 0; i < 8; ++i) {
            target = s.raider;
            ai.apply(target);
            hit(s, weapons, target);
        }
        advance(s, ai, weapons, std::span(&s.raider, 1), 600);
        target = s.raider;
        ai.apply(target);
        check(ai.agents().at(s.raider.id).mode == NpcMode::Retreat &&
                  glm::dot(target.velocity, target.position - s.player.position) > 0,
              "Damaged hostile did not turn and retreat");
        const auto retreatShots = ai.agents().at(s.raider.id).shots;
        advance(s, ai, weapons, std::span(&s.raider, 1), 120);
        check(ai.agents().at(s.raider.id).shots == retreatShots, "Retreating hostile continued firing");
        target = s.raider;
        ai.apply(target);
        s.player.position = target.position + glm::dvec3(0, 0, 500);
        hit(s, weapons, target);
        advance(s, ai, weapons, std::span(&s.raider, 1), 1);
        check(ai.agents().at(s.raider.id).mode == NpcMode::Disabled, "Wreck continued combat decisions");
        check(weapons.restore(weapons.serialize(), s.u),
              "Raider damage ID did not persist through weapon save");
        ActivityState activity;
        activity.seconds = s.player.seconds;
        activity.selected = s.raider.id;
        check(activity.restore(activity.serialize(), s.u) && activity.selected == s.raider.id,
              "Selected raider failed activity save migration");
        Scenario safe;
        NpcCombat protectedAi;
        WeaponSystem clean;
        auto harbour = sampleActivity(safe.u.describe(safe.planet), 0, safe.player.seconds);
        safe.player.position = harbour.position + glm::normalize(harbour.position) * 1000.;
        std::array<ActivityBody, 2> protectedBodies{safe.raider, harbour};
        double protectedDamage = 0;
        advance(safe, protectedAi, clean, protectedBodies, 240, &protectedDamage);
        check(protectedDamage == 0, "Raider fired into protected harbour space");
        Scenario tiny;
        tiny.player.size = 1e-9;
        NpcCombat tinyAi;
        double tinyDamage = 0;
        advance(tiny, tinyAi, clean, std::span(&tiny.raider, 1), 120, &tinyDamage);
        check(tinyDamage == 0, "NPC retained an impossible nanoscale targeting lock");
        Scenario cruise;
        cruise.player.field = true;
        NpcCombat cruiseAi;
        double cruiseDamage = 0;
        advance(cruise, cruiseAi, clean, std::span(&cruise.raider, 1), 120, &cruiseDamage);
        check(cruiseDamage == 0, "NPC attacked a player using the cruise field");
        Scenario civilianCase;
        auto neutral = sampleActivity(civilianCase.u.describe(civilianCase.planet), 100, 100);
        neutral.position =
            glm::normalize(neutral.position) *
            (std::pow(10., civilianCase.u.describe(civilianCase.planet).frame.logRadius) + 20.);
        auto berth = sampleActivity(civilianCase.u.describe(civilianCase.planet), 0, 100);
        berth.position = neutral.position + glm::dvec3(20, 0, 0);
        civilianCase.player.position = neutral.position + glm::dvec3(0, 0, 100);
        std::array<ActivityBody, 2> berthed{neutral, berth};
        NpcCombat quiet;
        advance(civilianCase, quiet, clean, berthed, 60);
        auto unchanged = neutral;
        quiet.apply(unchanged);
        check(unchanged.position == neutral.position && unchanged.velocity == neutral.velocity &&
                  quiet.agents().empty(),
              "Combat avoidance displaced an unprovoked civilian from its canonical berth");
        auto freighter =
            sampleActivity(civilianCase.u.describe(civilianCase.planet), 100, civilianCase.player.seconds);
        civilianCase.player.position = freighter.position + glm::dvec3(0, 0, 1200);
        auto patrol =
            sampleActivity(civilianCase.u.describe(civilianCase.planet), 300, civilianCase.player.seconds);
        patrol.position = freighter.position + glm::dvec3(500, 500, 500);
        patrol.velocity = {};
        std::array<ActivityBody, 2> civilians{freighter, patrol};
        NpcCombat law;
        WeaponSystem aggressor;
        advance(civilianCase, law, aggressor, civilians, 10);
        check(law.agents().empty() ||
                  std::none_of(law.agents().begin(), law.agents().end(),
                               [](const auto& a) { return a.second.mode == NpcMode::Attack; }),
              "Friendly patrol attacked without aggression");
        hit(civilianCase, aggressor, freighter);
        advance(civilianCase, law, aggressor, civilians, 1);
        check(law.agents().at(freighter.id).mode == NpcMode::Evade &&
                  law.agents().at(patrol.id).mode == NpcMode::Attack,
              "Distress did not trigger civilian evasion and patrol response");
        const auto identity = freighter.id;
        law.apply(freighter);
        check(freighter.id == identity, "Evasion replaced an economic vessel identity");
        civilianCase.player.seconds += 90;
        advance(civilianCase, law, aggressor, civilians, 1);
        check(law.agents().at(patrol.id).mode != NpcMode::Attack, "Patrol aggression never expired");
        // Identical canonical inputs produce identical decisions and steering.
        Scenario repeat;
        NpcCombat a, b;
        for (unsigned i = 0; i < 120; ++i) {
            repeat.player.seconds += 1. / 60;
            a.update(repeat.u, repeat.player, std::span(&repeat.raider, 1), clean, 1. / 60);
            b.update(repeat.u, repeat.player, std::span(&repeat.raider, 1), clean, 1. / 60);
        }
        check(a.serialize(repeat.u.seed()) == b.serialize(repeat.u.seed()),
              "AI depends on nondeterministic random state");
        Universe otherSeed(17);
        check(!b.restore(a.serialize(repeat.u.seed()), otherSeed), "NPC state crossed universe seeds");
        FlightInstruments instruments;
        a.annotate(instruments);
        check(instruments.hostileContacts > 0, "Combat HUD omitted visible threat");
        for (auto aspect : {16. / 10, .75}) {
            instruments.weapon = "PULSE LASER I";
            instruments.weaponStatus = "SPACE FIRE";
            auto hud = cockpitOverlay(instruments, aspect, "", {}, {}, {}, false, 1, nullptr);
            check(!hud.lettering.vertices.empty(), "Combat HUD failed fitted layout");
        }
        const auto seed = s.u.describe(s.planet).seed;
        auto raider = sampleRaider(s.u, s.planet, 300);
        auto again = sampleRaider(s.u, s.planet, 300);
        check(raider.id == again.id && raider.seed == again.seed && raider.position == again.position &&
                  s.u.describe(s.planet).seed == seed,
              "Raider sampling changed underlying generation");
        std::cout << checks
                  << " NPC attack, evasion, retreat, safety, patrol, persistence and deterministic steering "
                     "checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
