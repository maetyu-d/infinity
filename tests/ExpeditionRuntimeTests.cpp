#include "infinity/Economy.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& system, uint64_t command = 0) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    while (std::chrono::steady_clock::now() < end) {
        auto s = r.snapshot();
        if (s && !s->message.empty() && s->catchingUp && s->message.find("INVALID") != std::string::npos)
            throw std::runtime_error(s->message);
        if (s && s->network && s->network->system == system && !s->catchingUp &&
            s->player.lastCommand >= command)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Expedition worker did not acknowledge its command");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Isolated expedition runtime directory required");
        const auto root = std::filesystem::absolute(argv[1]);
        check(root.filename() == "runtime-test" && root.parent_path().filename() == "expedition",
              "Unsafe expedition fixture path");
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        Universe u;
        const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
        uint64_t seq = 0;
        MaterialExpedition job;
        SurveyObserver docked;
        int64_t startingCredits = 0;
        for (unsigned boot = 0; boot < 4; ++boot) {
            EconomyRuntime r(u, root);
            r.request(system, 0);
            auto s = wait(r, system, seq);
            check(!s->expeditions.jobs.empty(), "Real inhabited system has no material expedition");
            job = s->expeditions.jobs[0];
            auto issue = [&](EconomicAction action, std::optional<Address> target, SurveyObserver observer,
                             double seconds = 8) {
                EconomicCommand c;
                c.sequence = ++seq;
                c.action = action;
                c.contract = job.id;
                c.port = job.colony;
                c.surveyTarget = target;
                c.observer = observer;
                c.workSeconds = seconds;
                c.combatPresent = true;
                if (action == EconomicAction::AssaySpecimen)
                    c.specimenLab = job.colony;
                check(r.command(c), "Expedition command was not queued");
                s = wait(r, system, seq);
                return s->message;
            };
            const auto index = s->siteIndex(job.colony);
            check(bool(index), "Expedition colony not a real network site");
            const auto& site = s->network->sites[*index];
            check(!site.orbital && site.population > 0, "Expedition attached to a nonlandable or empty site");
            docked.reference = site.id.home;
            docked.position = site.metres;
            docked.landed = true;
            if (boot == 0) {
                startingCredits = s->player.credits;
                check(issue(EconomicAction::DiagnoseExpedition, {}, docked).starts_with("EXPEDITION FUNDED"),
                      "Real colony could not fund and set aside its repair kit");
                const auto p = u.describe(job.colony.home), source = u.describe(job.source);
                SurveyObserver ground;
                ground.reference = p.address;
                ground.landed = true;
                ground.position =
                    source.geographicDirection *
                    (std::pow(10., p.frame.logRadius) + u.terrainHeight(p, source.geographicDirection) + 10);
                check(issue(EconomicAction::ScanSurvey, job.source.parent().parent(), ground)
                          .starts_with("DISCOVERY"),
                      "Worker could not survey source terrain");
                check(issue(EconomicAction::RecoverCore, job.source.parent().parent(), ground, 6)
                          .starts_with("SEALED CORE"),
                      "Worker could not recover canonical sample");
                check(s->player.specimenKg == 1 && s->player.massKg() == 1, "Worker custody mass missing");
            } else if (boot == 1) {
                check(job.phase == ExpeditionPhase::Core && s->player.specimenKg == 1,
                      "Restart lost core or mass");
                check(issue(EconomicAction::OpenSpecimen, {}, docked).starts_with("SPECIMEN LAB READY"),
                      "Worker refused physically linked lab");
                SurveyObserver probe;
                probe.reference = job.source;
                probe.position = {0, 0, 3 * std::pow(10., u.describe(job.source).frame.logRadius)};
                probe.size = 1e-12;
                check(issue(EconomicAction::AssaySpecimen, job.source, probe).starts_with("SOURCE ASSAY"),
                      "Worker source assay failed");
            } else if (boot == 2) {
                check(job.phase == ExpeditionPhase::Assay && job.assays == 1 && job.lab == job.colony,
                      "Restart lost lab permit or partial assay");
                for (const auto& target : {job.grain(), job.lattice()}) {
                    SurveyObserver probe;
                    probe.reference = target;
                    probe.position = {0, 0, 3 * std::pow(10., u.describe(target).frame.logRadius)};
                    probe.size = 1e-12;
                    const auto message = issue(EconomicAction::AssaySpecimen, target, probe);
                    check(message.starts_with("SOURCE ASSAY") || message.starts_with("ATOMIC ASSAY COMPLETE"),
                          "Worker microscopic assay failed");
                }
                check(issue(EconomicAction::RepairExpedition, {}, docked).starts_with("EXCHANGER FITTED"),
                      "Worker physical colony repair failed");
            } else {
                check(job.phase == ExpeditionPhase::Complete && !s->player.specimenKg &&
                          s->player.credits == startingCredits + job.reward,
                      "Restart lost repaired colony reward or consumed core");
                check(s->sites[*index].modules[size_t(FacilityModule::Power)] == job.powerLevel &&
                          !s->outcomes.empty(),
                      "Worker restart lost real utility upgrade or report");
                check(!issue(EconomicAction::RepairExpedition, {}, docked).starts_with("EXCHANGER FITTED") &&
                          s->player.credits == startingCredits + job.reward,
                      "Completed job paid again after restart");
            }
            r.flush();
        }
        std::cout << checks << " expedition worker/restart assertions passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
