#include "infinity/Cockpit.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/Piloting.hpp"
#include <iostream>
using namespace infinity;
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
        auto originalBrand = state;
        originalBrand.replace(0, 8, "INFINITE");
        check(restored.restore(originalBrand, s.u) && restored.serialize(s.u.seed()) == state,
              "Original NPC combat save did not load under the new brand");
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
        // An escort order gives the existing raider a civilian target; damage
        // enters the same durable hull ledger used by player weapons.
        Scenario convoy;
        auto civilian = sampleActivity(convoy.u.describe(convoy.planet), 200, 100);
        civilian.position = convoy.raider.position + glm::dvec3(0, 0, 1400);
        civilian.velocity = {};
        convoy.player.position = convoy.raider.position + glm::dvec3(1200, 0, 1400);
        convoy.raider.orientation =
            glm::quatLookAtRH(glm::normalize(civilian.position - convoy.raider.position),
                              glm::normalize(convoy.raider.position));
        WeaponSystem convoyGuns;
        NpcCombat convoyAi;
        convoyAi.setEscort(civilian.id);
        unsigned civilianHits = 0;
        const std::array<ActivityBody, 2> convoyBodies{convoy.raider, civilian};
        for (unsigned i = 0; i < 600; ++i) {
            convoy.player.seconds += 1. / 60;
            auto report = convoyAi.update(convoy.u, convoy.player, convoyBodies, convoyGuns, 1. / 60);
            for (const auto& hit : report.civilianHits) {
                ++civilianHits;
                convoyGuns.receiveNpcDamage(hit, 4, convoy.player.seconds);
            }
        }
        auto damagedCivilian = civilian;
        convoyGuns.apply(damagedCivilian);
        check(civilianHits > 0 && damagedCivilian.hull < 100, "Raider never attacked protected civilian");
        WeaponSystem loadedConvoy;
        check(loadedConvoy.restore(convoyGuns.serialize(), convoy.u), "Civilian NPC damage did not persist");
        auto loadedCivilian = civilian;
        loadedConvoy.apply(loadedCivilian);
        check(loadedCivilian.hull == damagedCivilian.hull && loadedCivilian.id == civilian.id,
              "Restored escort target changed hull or identity");
        const auto originalRaider = convoy.raider.hull;
        convoyGuns.receiveNpcDamage(convoy.raider, 100, convoy.player.seconds);
        convoyGuns.apply(convoy.raider);
        check(convoy.raider.hull == originalRaider, "Civilian damage hook could forge a bounty kill");
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
        Scenario raid;
        const auto cargo = sampleCombatActivity(raid.u, {raid.planet, cargoRaiderSlot}, raid.player.seconds);
        raid.player.position = cargo.position + glm::dvec3(0, 0, 1600);
        NpcCombat cargoAi;
        const auto cargoBodies = std::span(&cargo, 1);
        advance(raid, cargoAi, clean, cargoBodies, 1);
        check(!cargoAi.demand(), "Cargo raider demanded payment from an empty hold");
        raid.player.valuableCargo = true;
        advance(raid, cargoAi, clean, cargoBodies, 1);
        check(cargoAi.demand() && cargoAi.demand()->id == cargo.id &&
                  cargoAi.agents().at(cargo.id).shots == 0,
              "Cargo raider did not wait for its demand");
        auto guard = sampleActivity(raid.u.describe(raid.planet), 300, raid.player.seconds);
        guard.position = raid.player.position + glm::dvec3(0, 0, 500);
        const std::array guarded{cargo, guard};
        advance(raid, cargoAi, clean, guarded, 1);
        check(!cargoAi.demand(), "Cargo demand remained active under patrol protection");
        raid.player.driveDisabled = true;
        advance(raid, cargoAi, clean, cargoBodies, 1);
        check(!cargoAi.demand(), "Cargo raider kept demanding from a disabled ship");
        raid.player.driveDisabled = false;
        advance(raid, cargoAi, clean, cargoBodies, 1);
        check(cargoAi.demand().has_value(), "Cargo demand did not resume");
        NpcCombat unpaid;
        check(unpaid.restore(cargoAi.serialize(raid.u.seed()), raid.u), "Open demand failed to persist");
        cargoAi.acceptTruce(cargo.id, raid.player.seconds, raid.player.seconds + 150);
        check(cargoAi.restore(cargoAi.serialize(raid.u.seed()), raid.u), "Paid truce failed to persist");
        raid.player.seconds += 13;
        advance(raid, cargoAi, clean, cargoBodies, 1);
        advance(raid, unpaid, clean, cargoBodies, 1);
        check(!cargoAi.demand() && cargoAi.agents().at(cargo.id).mode != NpcMode::Attack &&
                  unpaid.agents().at(cargo.id).mode == NpcMode::Attack,
              "Paid and expired demands did not produce different behavior");

        Scenario interdiction;
        const auto wing = sampleCombatActivity(interdiction.u, {interdiction.planet, missileWingSlot}, 100);
        auto cutoff = sampleCombatActivity(interdiction.u, {interdiction.planet, cutoffRaiderSlot}, 100);
        interdiction.player.position = wing.position + glm::dvec3(0, 0, 1800);
        NpcCombat pairAi;
        const std::array pair{wing, cutoff};
        advance(interdiction, pairAi, clean, pair, 1);
        check(pairAi.agents().at(wing.id).mode == NpcMode::Attack &&
                  pairAi.agents().at(cutoff.id).mode == NpcMode::Attack,
              "Both interdiction wings did not acquire the target");
        cutoff.hull = 0;
        const std::array brokenPair{wing, cutoff};
        advance(interdiction, pairAi, clean, brokenPair, 1);
        check(pairAi.agents().at(wing.id).mode == NpcMode::Retreat,
              "Missile wing did not break off after losing the cutoff wing");

        Scenario pursuit;
        WeaponSystem pursuitGuns;
        NpcCombat pursuitAi;
        const auto hunter = sampleCombatActivity(pursuit.u, {pursuit.planet, factionHunterSlot}, 100);
        pursuit.player.position = hunter.position + glm::dvec3(0, 0, 1600);
        const auto hunterBodies = std::span(&hunter, 1);
        advance(pursuit, pursuitAi, pursuitGuns, hunterBodies, 1);
        check(!pursuitAi.demand(), "Hunter accused a player without a recorded attack");
        pursuit.player.position = pursuit.raider.position + glm::dvec3(0, 0, 1000);
        hit(pursuit, pursuitGuns, pursuit.raider);
        pursuit.player.position = hunter.position + glm::dvec3(0, 0, 1600);
        advance(pursuit, pursuitAi, pursuitGuns, hunterBodies, 1);
        check(pursuitAi.demand() && pursuitAi.demand()->restitution > 0 &&
                  pursuitAi.demand()->reason.find(pursuit.raider.name) != std::string::npos,
              "Hunter did not identify the recorded attack when offering restitution");
        Scenario enemyTypes;
        const auto spawn = [&](uint32_t slot) {
            return sampleCombatActivity(enemyTypes.u, {enemyTypes.planet, slot}, enemyTypes.player.seconds);
        };
        auto skirmisher = spawn(skirmisherSlot), siege = spawn(siegeRaiderSlot);
        auto saboteur = spawn(saboteurSlot), scavenger = spawn(scavengerSlot);
        for (const auto& enemy : {skirmisher, siege, saboteur, scavenger}) {
            const auto again = spawn(enemy.id.slot);
            check(isRaider(enemy.id) && enemy.kind == ActivityKind::Raider &&
                      again.id == enemy.id && again.position == enemy.position &&
                      again.seed == enemy.seed && again.name == enemy.name,
                  "New role did not keep its canonical combat identity");
        }
        NpcCombat scavenging;
        const auto fragment = sampleActivity(enemyTypes.u.describe(enemyTypes.planet), 1000,
                                             enemyTypes.player.seconds);
        check(glm::length(scavenger.position - fragment.position) < 3000,
              "Scavenger was not placed near the debris it shadows");
        enemyTypes.player.position = scavenger.position + glm::dvec3(0, 0, 1600);
        const auto scavengers = std::span(&scavenger, 1);
        check(scavenging.update(enemyTypes.u, enemyTypes.player, scavengers, clean, .05).hostiles == 0,
              "Scavenger hunted an intact ship");
        enemyTypes.player.hull = 64;
        check(scavenging.update(enemyTypes.u, enemyTypes.player, scavengers, clean, .05).hostiles == 1 &&
                  scavenging.agents().at(scavenger.id).mode == NpcMode::Attack,
              "Scavenger ignored a damaged ship");

        const auto facePlayer = [&](ActivityBody& attacker, double distance) {
            const auto up = glm::normalize(attacker.position);
            const auto forward = glm::normalize(glm::cross(up, glm::dvec3(1, 0, 0)));
            attacker.orientation = glm::quatLookAtRH(forward, up);
            attacker.velocity = {};
            enemyTypes.player.position = attacker.position + forward * distance;
        };
        facePlayer(siege, 2750);
        NpcCombat artillery;
        auto volley = artillery.update(enemyTypes.u, enemyTypes.player, std::span(&siege, 1), clean, .05);
        check(volley.missileLaunches.size() == 1 && volley.playerHits.empty(),
              "Siege ship failed to fire a missile outside ordinary laser range");
        check(artillery.restore(artillery.serialize(enemyTypes.u.seed()), enemyTypes.u),
              "New enemy actor failed to survive an NPC save round trip");

        facePlayer(skirmisher, 450);
        NpcCombat fighter;
        const auto burst = fighter.update(enemyTypes.u, enemyTypes.player, std::span(&skirmisher, 1), clean, .05);
        check(burst.firing == 1 && fighter.agents().at(skirmisher.id).cooldown == .20,
              "Skirmisher failed to begin a close-range burst");
        facePlayer(saboteur, 210);
        NpcCombat sabotage;
        const auto strike = sabotage.update(enemyTypes.u, enemyTypes.player, std::span(&saboteur, 1), clean, .05);
        check(strike.playerHits.size() == 1 && strike.playerHits.front().first == ShipSystem::Thrusters,
              "Saboteur failed to target the ship's manoeuvring thrusters");
        Scenario relay;
        const auto scout = sampleCombatActivity(relay.u, {relay.planet, raiderScoutSlot}, 100);
        const auto corsair = sampleCombatActivity(relay.u, {relay.planet, pursuitRaiderSlot}, 100);
        relay.player.position = scout.position + glm::dvec3(0, 0, 1200);
        const auto scouts = std::span(&scout, 1);
        NpcCombat signal;
        const auto warning = signal.update(relay.u, relay.player, scouts, clean, .05);
        check(warning.signalRemaining == 10 && warning.firing == 0,
              "Scout did not warn the player and flee before calling reinforcements");
        NpcCombat interrupted, lostContact;
        const auto relaySave = signal.serialize(relay.u.seed());
        check(interrupted.restore(relaySave, relay.u) && lostContact.restore(relaySave, relay.u),
              "Relay countdown failed to persist");
        relay.player.seconds = 111;
        auto disabledScout = scout;
        disabledScout.systems[size_t(ShipSystem::Engines)] = 0;
        interrupted.update(relay.u, relay.player, std::span(&disabledScout, 1), clean, .05);
        check(!interrupted.reinforcementActive(relay.planet, relay.player.seconds),
              "Disabled scout sent a pursuit signal");
        lostContact.update(relay.u, relay.player, {}, clean, .05);
        lostContact.update(relay.u, relay.player, scouts, clean, .05);
        check(!lostContact.reinforcementActive(relay.planet, relay.player.seconds),
              "Scout completed a signal after contact had been lost");
        signal.update(relay.u, relay.player, scouts, clean, .05);
        check(signal.reinforcementActive(relay.planet, relay.player.seconds),
              "Uninterrupted scout failed to call in a pursuit ship");
        relay.player.position = corsair.position + glm::dvec3(0, 0, 1400);
        const auto corsairs = std::span(&corsair, 1);
        check(signal.update(relay.u, relay.player, corsairs, clean, .05).pursuitIncoming,
              "Pursuit ship failed to acquire its target");
        relay.player.seconds += 91;
        check(!signal.update(relay.u, relay.player, corsairs, clean, .05).pursuitIncoming,
              "Pursuit did not expire");
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
