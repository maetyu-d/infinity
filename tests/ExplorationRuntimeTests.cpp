#include "infinite/Cockpit.hpp"
#include "infinite/Economy.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
std::shared_ptr<const EconomySnapshot> wait(EconomyRuntime& r, const Address& system, uint64_t sequence = 0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(50);
    while (std::chrono::steady_clock::now() < end) {
        const auto s = r.snapshot();
        if (s && s->network && s->network->system == system && !s->catchingUp &&
            s->player.lastCommand >= sequence)
            return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Exploration worker failed to acknowledge command");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("isolated exploration runtime-test directory required");
        const auto root = std::filesystem::absolute(argv[1]);
        check(root.filename() == "runtime-test" && root.parent_path().filename() == "exploration",
              "Unsafe fixture path");
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        Universe u;
        const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
        uint64_t sequence = 0;
        int64_t savedCredits = 0;
        uint64_t savedRevision = 0;
        {
            EconomyRuntime r(u, root);
            r.request(system, 100);
            auto s = wait(r, system);
            check(s->exploration.scannerLevel == 1 && s->exploration.journal.empty(),
                  "Legacy runtime did not initialise a starter scanner");
            check(s->exploration.contracts.size() == 3, "Background worker did not publish survey offers");
            const auto job = s->exploration.contracts.front();
            EconomicCommand c;
            c.sequence = ++sequence;
            c.port = job.issuer;
            c.contract = job.id;
            c.action = EconomicAction::AcceptSurvey;
            check(r.command(c), "Survey acceptance not queued");
            s = wait(r, system, sequence);
            check(s->exploration.contracts.front().phase == SurveyPhase::Accepted &&
                      s->exploration.contracts.front().escrow == job.reward,
                  "Background contract reward not escrowed");
            SurveyObserver o;
            o.reference = job.target;
            o.position = {0, 0, std::pow(10., u.describe(job.target).frame.logRadius) * 3};
            o.seconds = 100;
            c.action = EconomicAction::ScanSurvey;
            c.surveyTarget = job.target;
            c.observer = o;
            c.workSeconds = 0;
            c.sequence = ++sequence;
            check(r.command(c), "Invalid scan not queued for rejection");
            s = wait(r, system, sequence);
            check(s->exploration.journal.empty(), "Unfinished scan reached journal");
            c.workSeconds = 8;
            c.sequence = ++sequence;
            check(r.command(c), "Valid scan not queued");
            s = wait(r, system, sequence);
            check(s->exploration.journal.size() == 1 && s->exploration.journal.front().address == job.target,
                  "Worker journal lost surveyed identity");
            const auto revision = s->exploration.revision;
            check(r.command(c), "Replay command not accepted for acknowledgement");
            r.request(system, 105);
            s = wait(r, system, sequence);
            check(s->exploration.revision == revision, "Replayed scan command duplicated discovery");
            const auto port = s->siteIndex(job.issuer);
            check(bool(port), "Survey issuer is not a real port");
            const auto& issuer = s->network->sites[*port];
            SurveyObserver docked;
            docked.reference = issuer.id.home;
            docked.position = issuer.metres;
            docked.landed = !issuer.orbital;
            docked.seconds = 105;
            c.action = EconomicAction::ClaimSurvey;
            c.observer = docked;
            c.sequence = ++sequence;
            check(r.command(c), "Claim not queued");
            s = wait(r, system, sequence);
            check(s->exploration.contracts.front().phase == SurveyPhase::Complete,
                  "Completed observed contract did not pay at real issuer");
            check(s->exploration.surveysDelivered == 1 && s->exploration.surveyEarnings == job.reward,
                  "Survey completion statistics lost payment");
            c.action = EconomicAction::SellSurvey;
            c.sequence = ++sequence;
            check(r.command(c), "Data sale not queued");
            s = wait(r, system, sequence);
            check(s->exploration.journal.front().paidGrade == 1, "First-survey data sale was not retained");
            savedCredits = s->player.credits;
            savedRevision = s->exploration.revision;
            c.sequence = ++sequence;
            check(r.command(c), "Repeated data sale not queued");
            s = wait(r, system, sequence);
            check(s->player.credits == savedCredits, "Same survey paid twice through worker");
            std::optional<size_t> outfitter;
            for (size_t i = 0; i < s->sites.size(); ++i)
                if (economicExplorationQuote(*s, i, true, 1).available) {
                    outfitter = i;
                    break;
                }
            check(bool(outfitter), "Real fixture system has no stocked mining outfitter");
            const auto& equipmentPort = s->network->sites[*outfitter];
            SurveyObserver linked;
            linked.reference = equipmentPort.id.home;
            linked.position = equipmentPort.metres;
            linked.landed = !equipmentPort.orbital;
            linked.seconds = 105;
            c = {};
            c.sequence = ++sequence;
            c.action = EconomicAction::FitMiningHead;
            c.port = equipmentPort.id;
            c.weaponMount = 1;
            c.observer = linked;
            c.limitCents = economicExplorationQuote(*s, *outfitter, true, 1).totalCents();
            check(r.command(c), "Mining fit not queued");
            s = wait(r, system, sequence);
            check(s->exploration.miningLevel == 1 && s->exploration.miningMount == 1 &&
                      s->player.weaponAt(1) == 0,
                  "Worker lost mining station ownership");
            const auto home = system.child(Kind::Planet, 0);
            const auto p = u.describe(home);
            const auto direction = glm::normalize(glm::dvec3(.37, .74, 1));
            const auto patch = home.child(Kind::Surface, encodeTile(tileAt(direction, 7)));
            SurveyObserver ground;
            ground.reference = home;
            ground.position =
                direction * (std::pow(10., p.frame.logRadius) + u.terrainHeight(p, direction) + 10);
            ground.landed = true;
            ground.seconds = 105;
            c = {};
            c.sequence = ++sequence;
            c.action = EconomicAction::ScanSurvey;
            c.surveyTarget = patch;
            c.observer = ground;
            c.workSeconds = 8;
            check(r.command(c), "Surface survey not queued");
            s = wait(r, system, sequence);
            c.action = EconomicAction::ExtractProspect;
            c.sequence = ++sequence;
            c.workSeconds = 6;
            c.weaponMount = 1;
            c.combatPresent = true;
            check(r.command(c), "Extraction not queued");
            s = wait(r, system, sequence);
            check(s->player.massKg() == 10 && s->exploration.minedKg == 10 &&
                      s->exploration.prospects.size() == 1,
                  "Worker extraction did not commit cargo and depletion together");
            savedCredits = s->player.credits;
            savedRevision = s->exploration.revision;
            for (auto size : {glm::ivec2(600, 1000), glm::ivec2(1440, 900)}) {
                CockpitViewport viewport(size.x, size.y, {0, 0, double(size.x), double(size.y)});
                for (auto page : {MarketPage::Survey, MarketPage::Journal}) {
                    auto market = economicMarketView(s.get(), job.issuer, 0, 10, true, page);
                    FlightInstruments t;
                    t.explorationActive = true;
                    t.exploration = {"SCANNING", "OBSERVATION COMPLETE", "JOURNAL 1024 / MINED 5000 KG",
                                     "F11 JOURNAL / F2 SURVEY"};
                    auto overlay = cockpitOverlay(t, viewport.aspect(), "", {}, {}, {}, false,
                                                  viewport.projectionScale, &market);
                    for (const auto& v : overlay.lettering.vertices)
                        check(std::isfinite(v.x) && std::isfinite(v.y) && v.x >= 0 &&
                                  v.x <= 900 * viewport.aspect() && v.y >= 0 && v.y <= 900,
                              "Survey/journal HUD exceeds fitted viewport");
                }
            }
            r.flush();
        }
        {
            EconomyRuntime r(u, root);
            r.request(system, 105);
            auto s = wait(r, system, sequence);
            check(s->player.credits == savedCredits && s->exploration.revision == savedRevision &&
                      s->exploration.journal.size() == 2,
                  "Restart lost atomic wallet/journal commit");
            check(s->exploration.contracts.front().phase == SurveyPhase::Complete &&
                      s->exploration.contracts.front().escrow == 0,
                  "Restart replenished a paid survey reward");
            check(s->exploration.journal.front().paidGrade == 1, "Restart allowed resale of paid data");
            check(s->player.massKg() == 10 && s->exploration.miningLevel == 1 &&
                      s->exploration.prospects.size() == 1,
                  "SQLite restart lost mined cargo, tool ownership or depletion");
            check(r.residentBytes() > 0, "Exploration runtime memory not accounted");
            r.flush();
        }
        std::cout << "PASS: " << checks
                  << " asynchronous survey transactions, funded jobs, replay, fitted HUD and SQLite restart "
                     "checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
