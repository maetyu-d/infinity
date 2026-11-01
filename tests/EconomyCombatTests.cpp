#include "infinite/CombatActors.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* reason) {
    ++checks;
    if (!value)
        throw std::runtime_error(reason);
}
std::shared_ptr<EconomicNetwork> fixture() {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->fingerprint = 510051;
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite s;
        s.id = {n->system.child(Kind::Planet, i), 0};
        s.name = "SECURITY PORT " + std::to_string(i);
        s.orbital = true;
        s.workers = s.power = 20;
        s.capacityKg = 1000000;
        s.initial.fill(1000);
        n->sites.push_back(s);
    }
    return n;
}
void audit(const Economy& e, const EconomyPlayer& p, int64_t money) {
    check(e.moneyTotal() + p.credits == money, "Security contract created or lost money");
    auto a = e.initialTotal(), d = e.externalGoodsDelta(), v = e.inventoryTotal();
    for (size_t g = 0; g < goodCount; ++g)
        check(a[g] + d[g] == v[g], "Security salvage lost ledger materials");
    Economy restored(std::make_shared<EconomicNetwork>(e.network()));
    check(restored.restore(e.serialize()) && restored.serialize() == e.serialize(),
          "Security ledger save round trip failed");
    EconomyPlayer wallet;
    check(wallet.restore(p.serialize()) && wallet.serialize() == p.serialize(),
          "Security wallet save round trip failed");
    check(restored.reconcilePlayer(wallet), "Security ownership did not reconcile");
}
EconomicCommand command(const Economy& e, const EconomyPlayer& p, const CombatContract& j,
                        EconomicAction action, double time) {
    EconomicCommand c;
    c.sequence = p.lastCommand + 1;
    c.port = e.network().sites[j.site].id;
    c.action = action;
    c.contract = j.id;
    c.combatTarget = j.target;
    c.impactSeconds = time;
    c.targetHull = 100;
    c.targetDistance = 1200;
    c.combatPresent = true;
    c.serviceUnits = 100;
    return c;
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, uint64_t seq, uint64_t tick) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < until) {
        auto s = r.snapshot();
        if (s && s->network && !s->catchingUp && s->tick >= tick && s->player.lastCommand >= seq)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Security runtime timed out");
}
} // namespace
int main(int argc, char** argv) {
    try {
        auto n = fixture();
        Economy e(n);
        EconomyPlayer p;
        const auto money = e.moneyTotal() + p.credits;
        e.advanceTo(12);
        check(e.combatContracts().size() == 2, "No funded escort and bounty offers");
        const auto escort = e.combatContracts()[0], bounty = e.combatContracts()[1];
        check(escort.target.slot == 200 && isRaider(bounty.target),
              "Contract targets replaced existing activity identities");
        audit(e, p, money);
        auto c = command(e, p, escort, EconomicAction::AcceptCombat, 60);
        check(e.execute(c, p).starts_with("ESCORT ACCEPTED"), "Escort acceptance failed");
        auto second = command(e, p, bounty, EconomicAction::AcceptCombat, 60);
        check(e.execute(second, p).find("ACTIVE SECURITY") != std::string::npos,
              "Multiple active contracts accepted");
        // Invalid / duplicate / skipped telemetry cannot manufacture protected time.
        c = command(e, p, escort, EconomicAction::ReportCombat, 61);
        c.targetDistance = std::numeric_limits<double>::quiet_NaN();
        check(e.execute(c, p).starts_with("INVALID"), "Non-finite telemetry accepted");
        for (unsigned sec = 61; sec <= 105; ++sec) {
            e.advanceTo(sec / 5);
            c = command(e, p, escort, EconomicAction::ReportCombat, sec);
            e.execute(c, p);
        }
        check(e.combatContracts()[0].protectedSeconds == 45,
              "Escort time not measured from contiguous physical reports");
        audit(e, p, money);
        Economy resumed(n);
        check(resumed.restore(e.serialize()), "Mid-flight restore failed");
        e = std::move(resumed);
        const auto snapshot = e.serialize();
        check(e.execute(c, p) == "ORDER ALREADY PROCESSED" && e.serialize() == snapshot,
              "Replayed report changed progress");
        for (unsigned sec = 106; sec <= 150; ++sec) {
            e.advanceTo(sec / 5);
            c = command(e, p, escort, EconomicAction::ReportCombat, sec);
            e.execute(c, p);
        }
        check(e.combatContracts()[0].phase == CombatJobPhase::Claimable,
              "90-second escort did not unlock claim");
        const auto credits = p.credits;
        check(credits == 250000, "Reward paid before return to issuer");
        c = command(e, p, escort, EconomicAction::ClaimCombat, 150);
        c.port = n->sites[1].id;
        check(e.execute(c, p).starts_with("COMPLETE OBJECTIVE"), "Wrong port paid escort");
        c = command(e, p, escort, EconomicAction::ClaimCombat, 150);
        check(e.execute(c, p).starts_with("SECURITY PAID") && p.credits == credits + escort.reward &&
                  !p.combatJob,
              "Escort claim did not settle escrow");
        check(e.outcomes().back().kind == OutcomeKind::Escort, "Escort outcome not recorded");
        audit(e, p, money);
        c = command(e, p, bounty, EconomicAction::AcceptCombat, 150);
        c.targetHull = 0;
        check(e.execute(c, p).starts_with("TARGET ALREADY"), "Pre-existing wreck accepted as new bounty");
        c = command(e, p, bounty, EconomicAction::AcceptCombat, 150);
        check(e.execute(c, p).starts_with("BOUNTY ACCEPTED"), "Bounty acceptance failed");
        c = command(e, p, bounty, EconomicAction::ReportCombat, 151);
        c.targetHull = 0;
        c.combatPresent = false;
        check(e.execute(c, p).starts_with("BOUNTY ACTIVE"), "Unattributed kill earned bounty");
        c = command(e, p, bounty, EconomicAction::ReportCombat, 152);
        c.targetHull = 0;
        check(e.execute(c, p).starts_with("OBJECTIVE COMPLETE"), "Named kill receipt failed");
        p.capacityKg = p.massKg();
        c = command(e, p, bounty, EconomicAction::ClaimCombat, 152);
        check(e.execute(c, p).starts_with("SECURITY PAID") && p.cargo[size_t(Good::Alloy)] == 0 &&
                  p.cargo[size_t(Good::Electronics)] == 0 && p.combatCompleted == 2,
              "New bounty failed to pay without automatically teleporting wreck salvage");
        p.capacityKg = holdCapacityKg(0);
        audit(e, p, money);
        check(e.execute(c, p) == "ORDER ALREADY PROCESSED", "Bounty paid twice");
        // Abandonment and actual civilian loss fail with a full issuer refund.
        for (unsigned failure = 0; failure < 3; ++failure) {
            Economy failed(n);
            EconomyPlayer owner;
            const auto cash = failed.moneyTotal() + owner.credits;
            failed.advanceTo(12);
            auto j = failed.combatContracts()[0];
            auto order = command(failed, owner, j, EconomicAction::AcceptCombat, 60);
            failed.execute(order, owner);
            order = command(failed, owner, j, EconomicAction::ReportCombat, 61);
            failed.execute(order, owner);
            if (failure == 0) {
                order = command(failed, owner, j, EconomicAction::ReportCombat, 62);
                order.targetHull = 0;
            }
            if (failure == 1) {
                failed.advanceTo(20);
                check(failed.reconcilePlayer(owner), "Abandonment reconcile failed");
            }
            if (failure == 2) {
                order = command(failed, owner, j, EconomicAction::ReportCombat, 62);
                order.serviceUnits = 0;
            }
            if (failure != 1)
                failed.execute(order, owner);
            check(!owner.combatJob && owner.combatFailed == 1 && failed.combatContracts()[0].escrow == 0,
                  "Failed security job retained authority or issuer funds");
            audit(failed, owner, cash);
        }
        // Version-eight accepted bounty bonuses remain payable, and their
        // reserved scrap allocation cannot also be taken from the same wreck.
        Economy oldReward(n);
        oldReward.advanceTo(12);
        auto oldText = oldReward.serialize();
        const auto security = oldText.rfind("\nSECURITY ") + 1;
        const auto wreckTail = oldText.find("\nWRECK_RECOVERY ") + 1;
        std::istringstream securityRows(oldText.substr(security, wreckTail - security));
        std::string line, oldRows;
        std::getline(securityRows, line);
        oldRows = line + "\n";
        while (std::getline(securityRows, line))
            if (!line.empty()) {
                line.erase(line.rfind(' '));
                oldRows += line + "\n";
            }
        oldText.erase(security);
        oldText += oldRows;
        oldText.replace(17, 1, "8");
        check(oldReward.restore(oldText), "Version-eight bounty ledger failed migration");
        EconomyPlayer legacyPilot;
        const auto legacyBounty = oldReward.combatContracts()[1];
        auto legacyOrder = command(oldReward, legacyPilot, legacyBounty, EconomicAction::AcceptCombat, 60);
        oldReward.execute(legacyOrder, legacyPilot);
        legacyOrder = command(oldReward, legacyPilot, legacyBounty, EconomicAction::ReportCombat, 61);
        legacyOrder.targetHull = 0;
        oldReward.execute(legacyOrder, legacyPilot);
        legacyOrder = command(oldReward, legacyPilot, legacyBounty, EconomicAction::ClaimCombat, 61);
        check(oldReward.execute(legacyOrder, legacyPilot).starts_with("SECURITY PAID") &&
                  legacyPilot.massKg() == 22,
              "Migration removed an existing bounty salvage entitlement");
        legacyOrder = command(oldReward, legacyPilot, legacyBounty, EconomicAction::RegisterWreck, 61);
        WeaponDamage legacyWreck;
        legacyWreck.id = legacyBounty.target;
        legacyWreck.hull = 0;
        legacyWreck.hitSeconds = 61;
        legacyOrder.wreck = legacyWreck;
        check(oldReward.execute(legacyOrder, legacyPilot).starts_with("WRECK REGISTERED"),
              "Legacy wreck did not register");
        check(std::all_of(oldReward.wrecks().back().scrap.begin(), oldReward.wrecks().back().scrap.end(),
                          [](auto kg) { return kg == 0; }),
              "Legacy bounty created duplicate physical salvage");
        // Old ledger and wallet formats remain readable before security exists.
        Economy legacy(n);
        auto old = legacy.serialize();
        old.erase(old.find("SECURITY "));
        old.replace(17, 1, "7");
        Economy migrated(n);
        check(migrated.restore(old), "Version-seven ledger migration failed");
        EconomyPlayer empty;
        auto wallet = empty.serialize();
        wallet.replace(16, 1, "7");
        wallet.erase(wallet.size() - 14);
        EconomyPlayer oldPlayer;
        check(oldPlayer.restore(wallet), "Version-seven wallet migration failed");
        auto malformed = e.serialize();
        malformed += "trailing";
        const auto good = e.serialize();
        check(!e.restore(malformed) && e.serialize() == good, "Malformed save changed live ledger");
        if (argc > 1) {
            const auto path = std::filesystem::path(argv[1]);
            if (path.filename() != "runtime-test" || path.parent_path().filename() != "security-contracts")
                throw std::runtime_error("Security fixture requires its isolated runtime-test directory");
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
            Universe u;
            uint64_t id = 0;
            double progress = 0;
            {
                EconomyRuntime r(u, path);
                r.request(n->system, 60);
                auto s = wait(r, 0, 12);
                check(!s->combatContracts.empty(), "Runtime security board empty");
                auto j = s->combatContracts.front();
                id = j.id;
                EconomicCommand c;
                c.sequence = s->player.lastCommand + 1;
                c.port = s->network->sites[j.site].id;
                c.action = EconomicAction::AcceptCombat;
                c.contract = id;
                c.combatTarget = j.target;
                c.impactSeconds = 60;
                check(r.command(c), "Runtime rejected security order");
                s = wait(r, c.sequence, 12);
                check(s->player.combatJob.has_value(), "Runtime acceptance not published");
                c.sequence++;
                c.action = EconomicAction::ReportCombat;
                c.impactSeconds = 61;
                c.targetDistance = 1000;
                c.combatPresent = true;
                c.serviceUnits = 100;
                r.request(n->system, 61);
                check(r.command(c), "Runtime report rejected");
                s = wait(r, c.sequence, 12);
                progress = s->combatContracts.front().protectedSeconds;
                r.flush();
            }
            EconomyRuntime reopened(u, path);
            reopened.request(n->system, 61);
            auto s = wait(reopened, 0, 12);
            check(s->player.combatJob && s->player.combatJob->id == id &&
                      s->combatContracts.front().protectedSeconds == progress,
                  "SQLite restart lost security ownership or progress");
            reopened.flush();
        }
        std::cout << "PASS: " << checks
                  << " funded security, physical escort, bounty attribution, salvage, conservation, "
                     "migrations and persistence checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
