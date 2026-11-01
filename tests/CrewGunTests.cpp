#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
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
void tick(ShipCrew& crew, unsigned count = 100) {
    for (unsigned i = 0; i < count; ++i)
        crew.advance(.25);
}
void staff(WeaponSystem& w, const ShipCrew& c) {
    w.setCrewReadiness(c.weaponReadiness());
    for (unsigned i = 1; i < maxWeaponMounts; ++i)
        w.setMountReadiness(i, c.gunnerReadiness(i));
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->universeSeed = 49;
    n->fingerprint = 490049;
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    EconomicSite d;
    d.id = {n->system.child(Kind::Planet, 0), 0};
    d.name = "GUN STATION YARD";
    d.orbital = true;
    d.capacityKg = 2000000;
    d.initial.fill(5000);
    n->sites.push_back(d);
    return n;
}
EconomySnapshot snapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.sites = e.sites();
    s.player = p;
    s.tick = e.tick();
    return s;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Mount trade lost or created money");
    const auto initial = e.initialTotal(), delta = e.externalGoodsDelta(), actual = e.inventoryTotal();
    for (size_t i = 0; i < goodCount; ++i)
        check(initial[i] + delta[i] == actual[i], "Mount trade lost material accounting");
    EconomyPlayer copy;
    check(copy.restore(p.serialize()) && copy.serialize() == p.serialize(),
          "Multi-mount wallet did not round trip");
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& system, uint64_t seq = 0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    do {
        auto s = r.snapshot();
        if (s && s->network && s->network->system == system && !s->catchingUp && s->player.lastCommand >= seq)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("Gun station worker timed out");
}
std::string legacyCrew2() {
    ShipCrew c(49);
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITE_CREW 2 49 0 0 0 0 30 12 -1 0 1 0 6";
    for (const auto& m : c.members()) {
        o << ' ' << m.id << ' ' << std::quoted(m.name) << ' ' << m.trait << ' ' << unsigned(m.duty) << ' '
          << unsigned(m.room) << ' ' << unsigned(m.target) << ' ' << unsigned(m.walkTo) << ' '
          << unsigned(m.order) << ' ' << m.health << ' ' << m.fatigue << ' ' << m.morale << ' ' << m.travel
          << ' ' << m.travelDuration << ' ' << m.automatic;
        for (auto xp : m.experience)
            o << ' ' << xp;
    }
    for (const auto& r : c.rooms())
        o << ' ' << r.integrity << ' ' << r.oxygen << ' ' << r.fire << ' ' << r.breach << ' ' << r.vented;
    o << " 0";
    return o.str();
}
} // namespace
int main(int argc, char** argv) {
    try {
        ShipCrew c(49);
        c.toggleAutomatic();
        c.reconcile({2, 0, 0});
        check(c.manWeapon(6, 1).find("EMPTY") != std::string::npos, "Crew could man a nonexistent extra gun");
        c.setWeaponLevels({1, 2, 3});
        c.manWeapon(6, 1);
        c.manWeapon(7, 2);
        check(c.members()[6].weaponMount == 1 && c.members()[7].weaponMount == 2,
              "Separate gun consoles did not retain crew assignments");
        check(c.gunnerReadiness(1) == 0 && c.gunnerReadiness(2) == 0,
              "Crew operated guns before reaching the weapons bay");
        tick(c);
        check(c.gunnerReadiness(1) > 0 && c.gunnerReadiness(2) > 0, "Gunners failed to reach their consoles");
        auto copy = c.serialize();
        ShipCrew restored(49);
        restored.setWeaponLevels({1, 2, 3});
        check(restored.restore(copy) && restored.serialize() == copy && restored.gunnerReadiness(2) > 0,
              "Gun crew bindings lost on reload");
        check(restored.restore(legacyCrew2()) && restored.members()[2].weaponMount == 0 &&
                  restored.members()[0].weaponMount == noWeaponMount,
              "Legacy crew v2 migration changed duties");
        const auto xp0 = c.members()[2].experience[2], xp1 = c.members()[6].experience[2],
                   xp2 = c.members()[7].experience[2];
        c.advance(.25, 0, false, true, 2);
        check(c.members()[6].experience[2] > xp1 && c.members()[2].experience[2] == xp0 &&
                  c.members()[7].experience[2] == xp2,
              "Inactive gun consoles farmed gunnery experience");
        c.order(6, CrewOrder::Rest, CrewStation::Bunks);
        c.advance(.25);
        check(c.gunnerReadiness(1) == 0 && c.gunnerReadiness(2) > 0,
              "Resting one gunner disabled the wrong gun");
        auto walking = c.members()[6];
        c.manWeapon(6, 1);
        check(c.members()[6].travel == walking.travel && c.members()[6].room == walking.room,
              "Gunner reassignment teleported a crew member");
        tick(c);
        c.assign(6, CrewStation::Navigation);
        check(c.members()[1].weaponMount == 1 && c.members()[1].duty == CrewStation::Weapons &&
                  c.members()[6].weaponMount == noWeaponMount,
              "Station swap lost or duplicated a gun console");
        c.bestStations();
        std::set<unsigned> occupied;
        for (const auto& m : c.members())
            if (m.duty == CrewStation::Weapons)
                check(occupied.insert(m.weaponMount).second, "Two crew assigned the same gun");
        check(occupied.size() == 3, "Best Stations ignored installed extra guns");
        tick(c);
        Universe u;
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        auto body = sampleActivity(u.describe(home), 300, 12);
        WeaponRay ray;
        ray.reference = home;
        ray.origin = body.position + glm::dvec3(0, 0, 600);
        ray.seconds = 12;
        WeaponSystem w;
        w.setLoadout({1, 2, 3});
        check(w.fireBattery(u, ray, std::span(&body, 1)) == 1, "Unmanned auxiliary guns fired");
        w.advance(1);
        staff(w, c);
        check(w.fireBattery(u, ray, std::span(&body, 1)) == 7, "A manned gun failed linked fire");
        auto hit = body;
        w.apply(hit);
        check(hit.hull == 40 && w.damage().size() == 1, "Gun mounts did not share cumulative target damage");
        check(w.shots().size() == 3 && w.shots()[0].mount == 0 && w.shots()[1].mount == 1 &&
                  w.shots()[2].mount == 2,
              "Pulse effects lost mount identities");
        check(glm::length(w.shots()[1].from - w.shots()[2].from) > 4,
              "Side gun muzzles used identical positions");
        auto combat = w.serialize();
        WeaponSystem wcopy;
        wcopy.setLoadout({1, 2, 3});
        check(wcopy.restore(combat, u) && wcopy.serialize() == combat,
              "Independent gun heat and damage did not persist");
        check(!wcopy.fireMount(1, u, ray, std::span(&body, 1)),
              "Loaded gun fired without restored crew staffing");
        check(wcopy.restore("INFINITE_COMBAT 1 35 0.2 0 10 0", u) && wcopy.heat() == 35 &&
                  wcopy.mount(1).heat == 0,
              "Legacy combat migration changed the original weapon");
        WeaponSystem hot;
        hot.setLoadout({0, 1, 1});
        hot.setMountReadiness(1, 1);
        hot.setMountReadiness(2, 1);
        for (unsigned i = 0; i < 100 && !hot.mount(1).overheated; ++i) {
            hot.fireMount(1, u, ray, {});
            if (!hot.mount(1).overheated)
                hot.advance(.28);
        }
        check(hot.mount(1).overheated && hot.mount(2).heat == 0 && hot.fireMount(2, u, ray, {}),
              "One gun's heat blocked another gun");
        c.vent(CrewStation::Weapons);
        tick(c, 250);
        staff(w, c);
        w.advance(1);
        const auto decompressedFire = w.fireBattery(u, ray, {});
        check(c.gunnerReadiness(1) == 0 && c.gunnerReadiness(2) == 0 && !(decompressedFire & 6),
              "Crew operated guns in a decompressed weapons bay");
        check(decompressedFire & 1, "Extra crew requirements removed the original manual gun");
        auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        p.credits = 10000000;
        const auto money = e.moneyTotal() + p.credits;
        uint64_t seq = 0;
        for (unsigned mount = 1; mount < maxWeaponMounts; ++mount)
            for (unsigned level = 1; level <= 3; ++level) {
                const auto q = economicWeaponQuote(snapshot(e, p), 0, level, false, mount);
                EconomicCommand cmd;
                cmd.sequence = ++seq;
                cmd.port = n->sites[0].id;
                cmd.action = EconomicAction::FitWeapon;
                cmd.weaponMount = mount;
                cmd.serviceUnits = level;
                cmd.limitCents = q.totalCents();
                check(e.execute(cmd, p).starts_with("WEAPON FITTED"), "Extra gun fitting or upgrade failed");
                check(p.weaponAt(mount) == level && p.weaponAt(0) == 1,
                      "Fitting an auxiliary replaced the primary gun");
                audit(e, p, money);
                const auto wallet = p.serialize();
                e.execute(cmd, p);
                check(p.serialize() == wallet, "Repeated mount command charged twice");
            }
        EconomicCommand sell;
        sell.port = n->sites[0].id;
        sell.action = EconomicAction::SellWeapon;
        sell.weaponMount = 1;
        sell.serviceUnits = 3;
        sell.limitCents = economicWeaponQuote(snapshot(e, p), 0, 3, true, 1).sellCents;
        sell.sequence = ++seq;
        check(e.execute(sell, p).starts_with("WEAPON SOLD") && p.weaponAt(1) == 0 && p.weaponAt(2) == 3 &&
                  p.weaponAt(0) == 1,
              "Selling one mount removed another gun");
        audit(e, p, money);
        auto poor = snapshot(e, p);
        poor.player.credits = 0;
        check(!economicWeaponQuote(poor, 0, 1, false, 1).available, "Unfunded extra weapon offered");
        check(!economicWeaponQuote(poor, 0, 1, false, 3).available, "Invalid mount accepted");
        MarketContext context;
        context.weaponMount = 2;
        auto market = economicMarketView(&poor, n->sites[0].id, 0, 1, true, MarketPage::Weapons, context);
        check(market.weaponMount == 2 && market.summary.find("STBD") != std::string::npos,
              "Weapon shop lost mount selection");
        std::ostringstream legacy;
        legacy << "INFINITE_WALLET 6 250000 0";
        for (size_t i = 0; i < goodCount; ++i)
            legacy << " 0";
        legacy << " 0 0 0 0 0 0 0 1 0 0 0 0 0 0";
        EconomyPlayer old;
        check(old.restore(legacy.str()) && old.weaponLoadout() == std::array<unsigned, 3>{1, 0, 0},
              "Legacy wallet gained free extra weapons");
        // The economic worker commits the two selected mounts across SQLite restart.
        const auto data = std::filesystem::path(argc > 1 ? argv[1] : "build/crew-guns/runtime-test");
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        ActivityId port;
        {
            EconomyRuntime runtime(u, data);
            runtime.request(n->system, 0);
            auto s = wait(runtime, n->system);
            size_t i = 0;
            for (; i < s->sites.size(); ++i) {
                auto q = economicWeaponQuote(*s, i, 1, false, 1);
                if (q.available && q.totalCents() * 3 < s->player.credits)
                    break;
            }
            check(i < s->sites.size(), "No affordable real two-gun armourer");
            port = s->network->sites[i].id;
            for (unsigned mount = 1; mount < 3; ++mount) {
                auto q = economicWeaponQuote(*s, i, 1, false, mount);
                EconomicCommand cmd;
                cmd.sequence = s->player.lastCommand + 1;
                cmd.port = port;
                cmd.action = EconomicAction::FitWeapon;
                cmd.weaponMount = mount;
                cmd.serviceUnits = 1;
                cmd.limitCents = q.totalCents();
                check(runtime.command(cmd), "Worker rejected extra gun fitting");
                s = wait(runtime, n->system, cmd.sequence);
                check(s->player.weaponAt(mount) == 1, "Worker committed wrong weapon mount");
            }
            runtime.flush();
        }
        {
            EconomyRuntime runtime(u, data);
            runtime.request(n->system, 0);
            auto s = wait(runtime, n->system, 2);
            check(s->player.weaponLoadout() == std::array<unsigned, 3>{1, 1, 1},
                  "Restart lost independent mount equipment");
        }
        check(c.residentBytes() < 32768, "Extra gun crew state exceeded its memory budget");
        std::cout
            << checks
            << " crewed weapon mount, manning, heat, damage, trading, migration and restart checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
