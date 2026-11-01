#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <sstream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 440044;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite d;
        d.id = {n->system.child(Kind::Planet, i), 0};
        d.name = "ARMOURER";
        d.orbital = true;
        d.capacityKg = 2000000;
        d.systemMetres = {double(i) * 1e10, 1000, 0};
        d.initial.fill(5000);
        if (i) {
            d.initial[size_t(Good::Alloy)] = 0;
            d.target[size_t(Good::Alloy)] = 1000;
        }
        n->sites.push_back(d);
    }
    n->vessels.push_back({n->sites[0].id.home, 100});
    return n;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.tick = e.tick();
    s.player = p;
    return s;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Weapon trade lost money");
    auto a = e.initialTotal(), d = e.externalGoodsDelta(), n = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(a[g] + d[g] == n[g], "Weapon trade lost materials");
    Economy restored(std::make_shared<EconomicNetwork>(e.network()));
    check(restored.restore(e.serialize()) && restored.serialize() == e.serialize(),
          "Weapon/wreck ledger failed save round trip");
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& system, uint64_t sequence = 0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        const auto s = r.snapshot();
        if (s && s->network && s->network->system == system && !s->catchingUp &&
            s->player.lastCommand >= sequence)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Weapon worker did not publish");
}
} // namespace
int main(int argc, char** argv) {
    try {
        Universe u;
        const auto planet = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        auto body = sampleActivity(u.describe(planet), 300, 12);
        WeaponRay ray;
        ray.reference = planet;
        ray.origin = body.position + glm::dvec3(0, 0, 600);
        ray.seconds = 12;
        WeaponSystem w;
        check(w.level() == 1 && w.fire(u, ray, std::span(&body, 1)), "Starter laser unavailable");
        auto hit = body;
        w.apply(hit);
        check(hit.hull == 90 && !hit.disabled, "Laser did not damage nearest craft");
        check(!w.fire(u, ray, std::span(&body, 1)) && w.shotsFired() == 1, "Laser bypassed cooldown");
        const auto before = w.serialize();
        check(!w.restore(before + " corrupt", u) && w.serialize() == before,
              "Corrupt combat save mutated state");
        WeaponSystem reload;
        check(reload.restore(before, u) && reload.heat() == w.heat(), "Combat heat/damage did not persist");
        for (unsigned i = 0; i < 9; ++i) {
            w.advance(.3);
            check(w.fire(u, ray, std::span(&body, 1)), "Laser burst failed");
        }
        hit = body;
        w.apply(hit);
        check(hit.disabled && hit.hull == 0 && glm::length(hit.velocity) == 0,
              "Disabled target still flying");
        auto moved = body;
        moved.position += glm::dvec3(100, 200, 300);
        w.apply(moved);
        check(moved.position == body.position && moved.id == body.id, "Wreck position or identity changed");
        check(w.restore(w.serialize(), u), "Wreck failed persistence");
        w.advance(30);
        check(w.heat() == 0 && w.shots().empty(), "Weapon failed to cool or evict effects");
        w.setLevel(0);
        check(!w.fire(u, ray, std::span(&body, 1)), "Unarmed ship fired");
        WeaponSystem heat;
        for (unsigned i = 0; i < 100; ++i) {
            heat.advance(.28);
            heat.fire(u, ray, {});
        }
        check(heat.shotsFired() < 100 && heat.heat() <= 100, "Sustained fire ignored heat");
        heat.advance(10);
        check(heat.fire(u, ray, {}), "Overheated laser never recovered");
        // A nearer body blocks the farther contact and a station absorbs fire without becoming a wreck.
        WeaponSystem nearest;
        auto far = body;
        far.id.slot = 301;
        far.position.z -= 100;
        std::array<ActivityBody, 2> targets{far, body};
        nearest.fire(u, ray, targets);
        check(nearest.damage().size() == 1 && nearest.damage()[0].id == body.id,
              "Hit order followed input order rather than depth");
        WeaponSystem station;
        auto wall = body;
        wall.kind = ActivityKind::Harbour;
        wall.id.slot = 0;
        station.fire(u, ray, std::span(&wall, 1));
        check(station.damage().empty() && station.shots()[0].hit, "Station did not absorb fire");
        // Equivalent nanoscale ray in an atomic frame; no scene or seed mutation.
        WeaponSystem nano;
        WeaponRay nr;
        nr.reference = planet;
        nr.scale = 1e-12;
        nr.origin = {0, 0, 0};
        auto atomTarget = body;
        atomTarget.position = {0, 0, -1e-9};
        atomTarget.radius = 1e-11;
        check(nano.fire(u, nr, std::span(&atomTarget, 1)) && nano.damage().size() == 1,
              "Scale-adjusted SI weapon missed nanoscale target");
        WeaponSystem blocked;
        Geometry barrier;
        barrier.referenceFrame = planet;
        barrier.logUnits = 0;
        barrier.solid.vertices.resize(3);
        barrier.solid.indices = {0, 1, 2};
        barrier.precisePositions = {ray.origin + glm::dvec3(-100, -100, -100),
                                    ray.origin + glm::dvec3(100, -100, -100),
                                    ray.origin + glm::dvec3(0, 100, -100)};
        blocked.fire(u, ray, std::span(&body, 1), &barrier);
        check(blocked.damage().empty() && blocked.shots()[0].hit &&
                  glm::length(blocked.shots()[0].to - (ray.origin + glm::dvec3(0, 0, -100))) < 1e-5,
              "Opaque world geometry did not stop fire");
        WeaponSystem atoms;
        Geometry atomic;
        atomic.referenceFrame = planet;
        atomic.logUnits = 0;
        atomic.solid.indices.resize(60003);
        auto model = glm::scale(glm::translate(glm::mat4(1), glm::vec3(0, 0, -1e-9f)), glm::vec3(1e-11f));
        atomic.atomInstances.push_back({model, glm::vec4(1)});
        check(atoms.fire(u, nr, {}, &atomic) && atoms.shots()[0].hit &&
                  glm::length(atoms.shots()[0].to) < 1.01e-9,
              "Large atomic geometry failed compact instance hit testing");
        auto net = fixture();
        Economy e(net);
        EconomyPlayer p;
        const auto money = e.moneyTotal() + p.credits;
        auto s = snapshot(e, p);
        check(p.weaponLevel == 1, "New ship has no starter weapon");
        auto q = economicWeaponQuote(s, 0, 2);
        check(q.available, "Stocked armourer cannot upgrade");
        EconomicCommand c{1, net->sites[0].id, EconomicAction::FitWeapon};
        c.serviceUnits = 2;
        c.limitCents = q.totalCents();
        check(e.execute(c, p).starts_with("WEAPON FITTED") && p.weaponLevel == 2, "Weapon upgrade failed");
        audit(e, p, money);
        const auto ledger = e.serialize(), wallet = p.serialize();
        e.execute(c, p);
        check(e.serialize() == ledger && p.serialize() == wallet, "Replay duplicated weapon purchase");
        check(p.restore(wallet) && p.weaponLevel == 2, "Weapon grade lost on wallet reload");
        c.sequence++;
        c.serviceUnits = 3;
        c.limitCents = 0;
        check(e.execute(c, p).starts_with("WEAPON QUOTE CHANGED") && p.weaponLevel == 2,
              "Stale weapon quote accepted");
        s = snapshot(e, p);
        q = economicWeaponQuote(s, 0, 2, true);
        c.sequence++;
        c.action = EconomicAction::SellWeapon;
        c.serviceUnits = 2;
        c.limitCents = q.sellCents;
        check(e.execute(c, p).starts_with("WEAPON SOLD") && !p.weaponLevel, "Fitted weapon cannot be sold");
        audit(e, p, money);
        s = snapshot(e, p);
        q = economicWeaponQuote(s, 0, 3);
        c.sequence++;
        c.action = EconomicAction::FitWeapon;
        c.serviceUnits = 3;
        c.limitCents = q.totalCents();
        check(e.execute(c, p).starts_with("WEAPON FITTED") && p.weaponLevel == 3,
              "Sold mount cannot buy a better weapon");
        audit(e, p, money);
        s = snapshot(e, p);
        for (auto aspect : {16. / 10, .75}) {
            auto v = economicMarketView(&s, net->sites[0].id, 2, 10, true, MarketPage::Weapons);
            auto hud = cockpitOverlay({}, aspect, "", {}, {}, {}, false, 1, &v);
            check(v.detail.size() == 15 && !hud.lettering.vertices.empty(),
                  "Weapon shop does not fit/render");
        }
        // Historical v3 fixture, independent of future wallet trailers.
        std::ostringstream old;
        old << "INFINITE_WALLET 3 " << p.credits << ' ' << p.lastCommand;
        for (size_t g = 0; g < goodCount; ++g)
            old << " 0";
        old << " 0 0 0 0 0 0 0";
        EconomyPlayer migrated;
        check(migrated.restore(old.str()) && migrated.weaponLevel == 1 && migrated.credits == p.credits,
              "Legacy wallet lost cargo/cash or lacked starter laser");
        // Killing a reserved carrier refunds its buyer and releases physical stock.
        Economy waiting(net);
        EconomyPlayer owner;
        waiting.advanceTo(3);
        auto f = waiting.freight()[0];
        check(f.phase == FreightPhase::Loading, "No reserved freight fixture");
        const auto total = waiting.moneyTotal() + owner.credits;
        EconomicCommand disable{1, f.id, EconomicAction::DisableVessel};
        disable.impactSeconds = 15;
        check(waiting.execute(disable, owner).starts_with("FREIGHTER DISABLED") &&
                  waiting.freight()[0].kg == 0,
              "Disabled loading carrier retained reservation");
        audit(waiting, owner, total);
        waiting.advanceTo(30);
        check(waiting.freight()[0].disabledAt == 15, "Disabled carrier was redispatched");
        audit(waiting, owner, total);
        Economy transit(net);
        EconomyPlayer tp;
        transit.advanceTo(5);
        const auto cargo = transit.freight()[0].kg;
        check(transit.freight()[0].phase == FreightPhase::Transit && cargo > 0, "No transit cargo fixture");
        disable.sequence = 1;
        disable.impactSeconds = 25;
        const auto tm = transit.moneyTotal() + tp.credits;
        transit.execute(disable, tp);
        transit.advanceTo(80, 100);
        check(transit.freight()[0].kg == cargo && transit.freight()[0].disabledAt == 25,
              "Wreck cargo vanished or arrived at destination");
        audit(transit, tp, tm);
        // The authoritative mount and a disabled freighter survive worker LRU eviction and SQLite restart.
        auto data = std::filesystem::path(argc > 1 ? argv[1] : "build/economy/weapons-test");
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        ActivityId port, vessel;
        int64_t credits = 0;
        {
            EconomyRuntime runtime(u, data);
            runtime.request(net->system, 0);
            auto live = wait(runtime, net->system);
            size_t index = 0;
            for (; index < live->sites.size(); ++index)
                if (economicWeaponQuote(*live, index, 2).available)
                    break;
            check(index < live->sites.size(), "Real universe has no stocked armourer");
            port = live->network->sites[index].id;
            auto offer = economicWeaponQuote(*live, index, 2);
            EconomicCommand fit{1, port, EconomicAction::FitWeapon};
            fit.serviceUnits = 2;
            fit.limitCents = offer.totalCents();
            runtime.command(fit);
            live = wait(runtime, net->system, 1);
            check(live->player.weaponLevel == 2, "Worker did not fit weapon atomically");
            credits = live->player.credits;
            vessel = live->flights[0].id;
            EconomicCommand wreck{2, vessel, EconomicAction::DisableVessel};
            wreck.impactSeconds = 0;
            runtime.command(wreck);
            live = wait(runtime, net->system, 2);
            check(live->flights[0].disabledAt == 0, "Worker did not commit disabled vessel");
            auto a = sampleEconomicFreight(u, *live->network, live->flights[0], 0);
            auto b = sampleEconomicFreight(u, *live->network, live->flights[0], 60);
            check(a.disabled && a.position == b.position && glm::length(b.velocity) == 0,
                  "Economic wreck drifted after being disabled");
            for (unsigned star : {0u, 1u, 2u}) {
                auto other = Address{}.child(Kind::Galaxy, 0).child(Kind::System, star);
                runtime.request(other, 0);
                auto remote = wait(runtime, other, 2);
                check(remote->player.weaponLevel == 2 && remote->player.credits == credits,
                      "Cache eviction lost weapon or credits");
            }
            check(runtime.residentBytes() < 4 * 1024 * 1024, "Weapons exceeded worker cache budget");
            runtime.flush();
        }
        {
            EconomyRuntime runtime(u, data);
            runtime.request(net->system, 0);
            auto live = wait(runtime, net->system, 2);
            check(live->player.weaponLevel == 2 && live->player.credits == credits &&
                      live->flights[0].id == vessel && live->flights[0].disabledAt == 0,
                  "Restart lost installed weapon, payment or disabled vessel identity");
        }
        std::cout << checks
                  << " weapon, damage, heat, trade, upgrade, migration and wreck accounting checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
