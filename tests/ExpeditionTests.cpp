#include "infinity/Economy.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/World.hpp"
#include <iostream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool v, const char* why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        Universe u;
        auto planet = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto home = u.describe(planet);
        auto net = std::make_shared<EconomicNetwork>();
        net->system = planet.parent();
        net->universeSeed = u.seed();
        net->fingerprint = 550055;
        EconomicSite site;
        site.id = {planet, 0};
        site.name = "CORE TEST COLONY";
        site.role = "COLONY";
        site.population = 1000;
        site.workers = 20;
        site.power = 20;
        site.radius = 100;
        site.capacityKg = 2000000;
        site.initial.fill(10000);
        auto direction = glm::normalize(glm::dvec3(.37, .74, 1));
        site.metres = direction * (std::pow(10., home.frame.logRadius) + u.terrainHeight(home, direction));
        net->sites.push_back(site);
        Economy e(net);
        EconomyPlayer p;
        ExplorationLedger exploration;
        ExpeditionLedger ledger;
        ledger.offer(u, e);
        check(ledger.state().jobs.size() == 1, "Surface expedition missing");
        ledger.offer(u, e);
        check(ledger.state().jobs.size() == 1, "Duplicate offer");
        const auto job = ledger.state().jobs.front();
        const auto source = u.describe(job.source);
        const auto originalDescriptor = descriptorFingerprint(source);
        GeometryGenerator generator(u);
        const auto rockHash = geometryFingerprint(*generator.generate(source, 1));
        const auto atomHash = geometryFingerprint(*generator.generate(u.describe(job.lattice()), 1));
        const auto beforeMoney = e.moneyTotal() + p.credits;
        SurveyObserver port;
        port.reference = planet;
        port.position = site.metres;
        port.landed = true;
        EconomicCommand c;
        c.contract = job.id;
        c.port = site.id;
        c.observer = port;
        uint64_t seq = 0;
        auto issue = [&](EconomicAction action) {
            c.action = action;
            c.sequence = ++seq;
            return ledger.execute(u, e, exploration, p, c);
        };
        c.observer->landed = false;
        check(!issue(EconomicAction::DiagnoseExpedition).starts_with("EXPEDITION FUNDED"),
              "Remote diagnosis succeeded");
        c.observer = port;
        check(issue(EconomicAction::DiagnoseExpedition).starts_with("EXPEDITION FUNDED"),
              "Physical diagnosis failed");
        const auto escrow = ledger.state().jobs[0].escrow;
        check(e.moneyTotal() + p.credits + escrow == beforeMoney, "Funding minted or lost money");
        check(e.sites()[0].condition == 850, "Diagnosis did not record actual equipment failure");
        Economy savedNetwork(net);
        check(savedNetwork.restore(e.serialize()), "Repair kit broke network reservation restore");
        {
            ExpeditionLedger branch;
            check(branch.restore(ledger.serialize(), u), "Cancellation branch failed to restore");
            Economy cancelledNetwork(net);
            check(cancelledNetwork.restore(e.serialize()), "Cancellation colony failed to restore");
            auto cancelledPlayer = p;
            auto cancel = c;
            cancel.sequence = seq + 1;
            cancel.action = EconomicAction::CancelExpedition;
            check(branch.execute(u, cancelledNetwork, exploration, cancelledPlayer, cancel)
                      .starts_with("EXPEDITION CANCELLED"),
                  "Accepted expedition could not be cancelled");
            check(cancelledNetwork.moneyTotal() + cancelledPlayer.credits == beforeMoney &&
                      cancelledNetwork.sites()[0].stock == net->sites[0].initial,
                  "Cancellation lost escrow or set-aside repair parts");
            check(savedNetwork.restore(cancelledNetwork.serialize()),
                  "Cancelled colony failed strict accounting restore");
        }
        ExpeditionLedger saved;
        check(saved.restore(ledger.serialize(), u), "Accepted expedition did not restore");
        const auto valid = saved.serialize();
        check(!saved.restore(valid + " junk", u) && saved.serialize() == valid,
              "Invalid save partially replaced expedition");
        SurveyObserver ground;
        ground.reference = planet;
        ground.position =
            source.geographicDirection *
            (std::pow(10., home.frame.logRadius) + u.terrainHeight(home, source.geographicDirection) + 10);
        ground.landed = true;
        c.observer = ground;
        c.workSeconds = 6;
        c.combatPresent = true;
        c.surveyTarget = job.source.parent().parent();
        check(!issue(EconomicAction::RecoverCore).starts_with("SEALED CORE"),
              "Unsurveyed source yielded core");
        EconomicCommand scan = c;
        scan.action = EconomicAction::ScanSurvey;
        scan.workSeconds = 8;
        scan.sequence = ++seq;
        check(exploration.execute(u, e, p, scan).starts_with("DISCOVERY"), "Core surface survey failed");
        p.capacityKg = 0;
        check(!issue(EconomicAction::RecoverCore).starts_with("SEALED CORE") &&
                  exploration.state().prospects.empty(),
              "Full hold consumed a core or depleted the prospect");
        p.capacityKg = 4000;
        check(issue(EconomicAction::RecoverCore).starts_with("SEALED CORE"), "Surveyed core recovery failed");
        check(p.massKg() == 1 && p.specimenKg == 1 && exploration.state().minedKg == 1,
              "Core mass or finite depletion missing");
        const auto depletion = exploration.state().prospects[0].extracted;
        issue(EconomicAction::RecoverCore);
        check(exploration.state().prospects[0].extracted == depletion && p.massKg() == 1, "Core duplicated");
        {
            ExpeditionLedger branch;
            check(branch.restore(ledger.serialize(), u), "Core cancellation branch failed to restore");
            Economy cancelledNetwork(net);
            check(cancelledNetwork.restore(e.serialize()), "Core cancellation colony failed to restore");
            auto cancelledPlayer = p;
            auto cancel = c;
            cancel.sequence = seq + 1;
            cancel.action = EconomicAction::CancelExpedition;
            check(branch.execute(u, cancelledNetwork, exploration, cancelledPlayer, cancel)
                      .starts_with("EXPEDITION CANCELLED"),
                  "Carried core could not be cancelled");
            check(cancelledPlayer.massKg() == 1 && !cancelledPlayer.specimenKg &&
                      cancelledNetwork.moneyTotal() + cancelledPlayer.credits == beforeMoney,
                  "Cancelling a recovered core destroyed cargo or created a reward");
            ExpeditionLedger reloaded;
            check(reloaded.restore(branch.serialize(), u), "Cancelled sample custody failed restore");
        }
        c.observer = port;
        check(issue(EconomicAction::OpenSpecimen).starts_with("SPECIMEN LAB READY"),
              "Port lab rejected owned core");
        check(saved.restore(ledger.serialize(), u), "Core custody failed to restart");
        EconomyPlayer restored;
        check(restored.restore(p.serialize()), "Wallet migration failed");
        saved.reconcilePlayer(restored);
        check(restored.massKg() == 1 && restored.specimenKg == 1, "Separate custody ledger lost sample mass");
        Navigation nav(u);
        RegionCache cache(u, 4 * 1024 * 1024, 1);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        pilot.startInSystem();
        pilot.brake();
        pilot.setVelocity({.2, 0, 0});
        pilot.setCargoMass(1);
        pilot.applyCombatDamage(15);
        auto parkedNav = nav.serialize(), parkedShip = pilot.serialize();
        SpecimenProbe probe;
        check(probe.enter(u, nav, pilot, ledger.state().jobs[0]), "Specimen probe failed to deploy");
        check(probe.active() && probe.contains(job.lattice()) && !probe.contains(planet),
              "Probe escaped sample identity");
        check(probe.parkedNavigation() == parkedNav && probe.parkedShip() == parkedShip &&
                  pilot.cargoMass() == 0,
              "Probe moved or shrank physical cargo");
        check(probe.focus(u, nav, pilot, job.grain()) && nav.anchor == job.grain(),
              "Lab could not magnify the original mineral");
        check(probe.focus(u, nav, pilot, job.lattice()) && nav.anchor == job.lattice(),
              "Lab could not magnify the original atomic lattice");
        check(!probe.focus(u, nav, pilot, planet), "Lab magnification escaped its source material");
        c.specimenLab = site.id;
        c.workSeconds = 8;
        auto assay = [&](const Address& a) {
            SurveyObserver o;
            o.reference = a;
            o.position = {0, 0, 3 * std::pow(10., u.describe(a).frame.logRadius)};
            o.size = 1e-12;
            c.observer = o;
            c.surveyTarget = a;
            return issue(EconomicAction::AssaySpecimen);
        };
        check(!assay(job.lattice()).starts_with("ATOMIC ASSAY COMPLETE"),
              "Atomic assay skipped material structure");
        check(assay(job.source).starts_with("SOURCE ASSAY"), "Source rock assay failed");
        check(assay(job.grain()).starts_with("SOURCE ASSAY"), "Mineral assay failed");
        c.specimenLab.reset();
        check(!assay(job.lattice()).starts_with("ATOMIC ASSAY COMPLETE"), "Assay bypassed lab custody");
        c.specimenLab = site.id;
        check(assay(job.lattice()).starts_with("ATOMIC ASSAY COMPLETE"), "Atomic assay failed");
        check(saved.restore(ledger.serialize(), u), "Assayed core did not restore");
        check(probe.leave(nav, pilot) && nav.serialize() == parkedNav && pilot.serialize() == parkedShip &&
                  pilot.cargoMass() == 1,
              "Probe round trip changed physical ship");
        c.observer = ground;
        check(!issue(EconomicAction::RepairExpedition).starts_with("EXCHANGER FITTED"),
              "Remote repair succeeded");
        c.observer = port;
        check(issue(EconomicAction::RepairExpedition).starts_with("EXCHANGER FITTED"),
              "Colony repair failed");
        check(p.massKg() == 0 && e.sites()[0].modules[size_t(FacilityModule::Power)] == 1,
              "Core was not consumed or power upgrade missing");
        check(e.moneyTotal() + p.credits == beforeMoney, "Completed expedition minted or lost money");
        const auto credits = p.credits;
        issue(EconomicAction::RepairExpedition);
        check(p.credits == credits, "Repair paid twice");
        check(savedNetwork.restore(e.serialize()), "Completed network failed to restore");
        check(saved.restore(ledger.serialize(), u), "Completed expedition failed to restore");
        check(!e.outcomes().empty() && e.outcomes().back().after.condition == 1000,
              "Colony repair outcome missing");
        EconomySnapshot report;
        report.network = net;
        report.sites = e.sites();
        report.expeditions = ledger.state();
        const auto signals = economicPortSignals(report);
        check(signals[0].report[0].find("ADAPTED EXCHANGER") != std::string::npos && signals[0].lighting > 1,
              "Repaired colony has no visible or reported persistent benefit");
        check(descriptorFingerprint(u.describe(job.source)) == originalDescriptor &&
                  geometryFingerprint(*generator.generate(source, 1)) == rockHash &&
                  geometryFingerprint(*generator.generate(u.describe(job.lattice()), 1)) == atomHash,
              "Expedition changed source generation");
        std::cout << checks << " material expedition assertions passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
