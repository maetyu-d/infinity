#include "infinity/Cockpit.hpp"
#include "infinity/Economy.hpp"
#include "infinity/OrbitalDebris.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace infinity;
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
        uint64_t expenseJob=0;int64_t savedRepairCost=0;
        {
            EconomyRuntime r(u, root);
            r.request(system, 100);
            auto s = wait(r, system);
            check(s->exploration.scannerLevel == 1 && s->exploration.journal.empty(),
                  "Legacy runtime did not initialise a starter scanner");
            check(s->exploration.contracts.size() == 4, "Background worker did not publish survey and cavern assay offers");
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
            const auto assay=*std::find_if(s->exploration.contracts.begin(),s->exploration.contracts.end(),[](const auto& j){return j.cavern;});
            c={};c.action=EconomicAction::AcceptSurvey;c.port=assay.issuer;c.contract=assay.id;c.observer=linked;c.sequence=++sequence;
            check(r.command(c),"Scoped assay acceptance not queued");s=wait(r,system,sequence);
            check(s->message.starts_with("SURVEY ACCEPTED"),"Real assay issuer could not reserve its reward");expenseJob=assay.id;
            std::optional<size_t> repairSite;
            for(size_t i=0;i<s->sites.size();++i)if(economicServiceQuote(*s,i,ShipService::Repair,1).available){repairSite=i;break;}
            check(bool(repairSite),"Fixture has no funded repair service");
            c={};c.action=EconomicAction::RepairHull;c.port=s->network->sites[*repairSite].id;c.serviceUnits=1;c.sequence=++sequence;c.surveyAccount=expenseJob;
            c.limitCents=economicServiceQuote(*s,*repairSite,ShipService::Repair,1).totalCents();
            const auto repairCredits=s->player.credits;
            check(r.command(c),"Scoped repair not queued");s=wait(r,system,sequence);
            savedRepairCost=repairCredits-s->player.credits;
            auto report=std::find_if(s->exploration.contracts.begin(),s->exploration.contracts.end(),[&](const auto& j){return j.id==expenseJob;});
            check(savedRepairCost>0&&report->accounting.repairs==savedRepairCost,"Worker did not attach the actual paid repair to its expedition");
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
            const auto report=std::find_if(s->exploration.contracts.begin(),s->exploration.contracts.end(),[&](const auto& j){return j.id==expenseJob;});
            check(report!=s->exploration.contracts.end()&&report->accounting.repairs==savedRepairCost,"SQLite restart lost the expedition repair receipt");
            check(r.residentBytes() > 0, "Exploration runtime memory not accounted");
            r.flush();
        }
        Address seamSource;
        glm::dvec3 savedEntry{};
        {
            EconomyRuntime r(u, root);
            r.request(system, 200);
            auto s = wait(r, system, sequence);
            const auto planet = system.child(Kind::Planet, 0);
            const auto p = u.describe(planet);
            const auto sites = generateProspects(u, p, tileAt(glm::normalize(glm::dvec3(.37, .74, 1)), 7));
            check(!sites.empty(), "Worker seam fixture has no physical outcrop");
            const auto& site = sites.front();
            seamSource = site.rocks.front().address;
            SurveyObserver ground;
            ground.reference = planet;
            ground.position = site.metres + site.direction * 16.;
            ground.seconds = 200;
            ground.attitude = glm::quatLookAtRH(
                glm::normalize(u.relative(seamSource, planet, 0).position - ground.position), site.direction);
            EconomicCommand c;
            auto submit = [&](EconomicAction action, Address target, SurveyObserver observer) {
                c = {};
                c.sequence = ++sequence;
                c.action = action;
                c.surveyTarget = target;
                c.observer = observer;
                c.workSeconds = 8;
                check(r.command(c), "Worker seam command queue rejected valid work");
                s = wait(r, system, sequence);
            };
            submit(EconomicAction::ScanSurvey, site.surface, ground);
            submit(EconomicAction::BeginSeam, seamSource, ground);
            check(s->exploration.seams.size() == 1, "Worker did not begin seam expedition");
            savedEntry = s->exploration.seams.front().entryMetres;
            auto micro = ground;
            micro.reference = seamSource;
            micro.position = {0, 0, std::pow(10., u.describe(seamSource).frame.logRadius) * 1.1};
            micro.size = .001;
            micro.attitude = {1, 0, 0, 0};
            submit(EconomicAction::ScanSurvey, seamSource, micro);
            submit(EconomicAction::MarkSeam, seamSource, micro);
            check(s->exploration.seams.front().marked, "Worker did not durably mark inspected material");
            check(s->player.massKg() == 10, "Marking awarded material before physical extraction");
            r.flush();
        }
        {
            EconomyRuntime r(u, root);
            r.request(system, 200);
            auto s = wait(r, system, sequence);
            check(s->exploration.seams.size() == 1 && s->exploration.seams.front().marked &&
                      s->exploration.seams.front().source == seamSource &&
                      s->exploration.seams.front().entryMetres == savedEntry,
                  "SQLite restart lost close survey, mark, source or entry beacon");
            SurveyObserver full;
            full.reference = seamSource.parent().parent().parent();
            full.position = savedEntry;
            full.seconds = 200;
            full.attitude = glm::quatLookAtRH(
                glm::normalize(u.relative(seamSource, full.reference, 0).position - full.position),
                glm::normalize(full.position));
            EconomicCommand c;
            c.sequence = ++sequence;
            c.action = EconomicAction::ExtractProspect;
            c.surveyTarget = seamSource;
            c.observer = full;
            c.weaponMount = 1;
            c.workSeconds = 6;
            c.combatPresent = true;
            check(r.command(c), "Saved marked extraction could not be queued");
            s = wait(r, system, sequence);
            check(s->player.massKg() == 30 && s->exploration.seams.front().minedKg == 20,
                  "Worker did not commit marked cargo and depletion together");
            r.flush();
        }
        {
            EconomyRuntime r(u, root);
            r.request(system, 200);
            auto s = wait(r, system, sequence);
            check(s->player.massKg() == 30 && s->exploration.seams.front().minedKg == 20,
                  "Second SQLite restart lost the completed seam outcome");
            r.flush();
        }
        std::string savedProbe;
        {
            EconomyRuntime r(u,root);r.request(system,300);auto s=wait(r,system,sequence);
            std::optional<size_t> fitting;
            for(size_t i=0;i<s->sites.size();++i)
                if(economicProbeQuote(*s,i,2,ProbeService::Fit).available){fitting=i;break;}
            check(bool(fitting),"No stocked port for the probe worker test");
            const auto& port=s->network->sites[*fitting];
            SurveyObserver o;o.reference=port.id.home;o.position=port.metres;o.landed=!port.orbital;o.seconds=300;
            EconomicCommand c;c.sequence=++sequence;c.action=EconomicAction::FitProbeLauncher;c.port=port.id;
            c.weaponMount=2;c.observer=o;c.limitCents=economicProbeQuote(*s,*fitting,2,ProbeService::Fit).totalCents();
            check(r.command(c),"Probe refit command queue rejected work");s=wait(r,system,sequence);
            check(s->exploration.probes.launchers[2]&&s->exploration.probes.rounds[2]==1,
                  "Worker did not commit launcher and included ammunition");
            const auto planet=seamSource.parent().parent().parent();const auto p=u.describe(planet);
            const auto up=u.describe(seamSource).geographicDirection;
            WeaponRay ray;ray.reference=planet;ray.origin=up*(std::pow(10.,p.frame.logRadius)+u.terrainHeight(p,up)+500.);
            ray.direction=-up;ray.seconds=300;
            o.reference=planet;o.position=ray.origin;o.landed=false;
            c={};c.sequence=++sequence;c.action=EconomicAction::LaunchProbe;c.weaponMount=2;
            c.observer=o;c.probeRay=ray;c.surveyTarget=planet;c.serviceUnits=100;
            check(r.command(c),"Probe launch command queue rejected work");s=wait(r,system,sequence);
            check(s->exploration.probes.holes.size()==1&&s->exploration.probes.rounds[2]==0,
                  "Worker did not atomically store trajectory and consume ammunition");
            check(s->player.massKg()==30&&s->exploration.miningLevel==1&&s->player.weaponAt(0)==1,
                  "Probe launch changed cargo, another weapon or mining equipment");
            savedProbe=serializeProbes(s->exploration.probes);r.flush();
        }
        ActivityId minedFragment;
        {
            EconomyRuntime r(u,root);r.request(system,301);const auto s=wait(r,system,sequence);
            check(serializeProbes(s->exploration.probes)==savedProbe,"SQLite restart changed in-flight camera, ammunition or future cavern");
            const auto& b=s->exploration.probes.holes.front();
            check(boreClearance(b,b.entry-glm::normalize(b.entry)*b.depth,b.blastTime()+2)>10,
                  "Reloaded probe did not create the same physical cavern at the saved time");
            const auto planet = u.describe(system.child(Kind::Planet, 13));
            const auto fragments = sampleOrbitalDebris(planet, 301);
            const auto rock = std::find_if(fragments.begin(), fragments.end(), landableDebris);
            check(rock != fragments.end(), "Runtime fixture lacks landable debris");
            minedFragment = rock->id;
            const auto pad = debrisPad(*rock, 1);
            SurveyObserver parked;
            parked.reference = planet.address;
            parked.position = pad.eye;
            parked.velocity = pad.velocity;
            parked.attitude = glm::quatLookAtRH(pad.forward, pad.up);
            parked.seconds = 301;
            parked.landed = true;
            EconomicCommand cut;
            cut.sequence = ++sequence;
            cut.action = EconomicAction::ExtractOrbitalDebris;
            cut.port = minedFragment;
            cut.observer = parked;
            cut.weaponMount = 1;
            cut.workSeconds = 6;
            cut.combatPresent = true;
            check(r.command(cut), "Orbital extraction did not enter the worker queue");
            const auto mined = wait(r, system, sequence);
            check(mined->message.starts_with("DEBRIS CUT") && mined->player.massKg() == 40 &&
                      mined->exploration.debris.size() == 1,
                  "Worker did not commit orbital cargo and depletion together");
            r.flush();
        }
        {
            EconomyRuntime r(u, root);
            r.request(system, 301);
            const auto s = wait(r, system, sequence);
            check(s->player.massKg() == 40 && s->exploration.debris.size() == 1 &&
                      s->exploration.debris.front().id == minedFragment &&
                      s->exploration.debris.front().extracted[0] == 10,
                  "SQLite restart lost the mined fragment or its cargo");
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
