#include "infinite/Economy.hpp"
#include "infinite/Planetology.hpp"
#include "infinite/World.hpp"
#include <iostream>
#include <numeric>
using namespace infinite;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) {
    ++checks;
    if (!value)
        throw std::runtime_error(why);
}
std::shared_ptr<EconomicNetwork> network(const Universe& u, const Address& planet) {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = planet.parent();
    n->universeSeed = u.seed();
    n->fingerprint = 530053;
    EconomicSite s;
    s.id = {planet, 0};
    s.name = "SURVEY COOPERATIVE";
    s.role = "SCIENCE";
    s.orbital = true;
    s.radius = 100;
    s.capacityKg = 2000000;
    s.initial.fill(10000);
    const auto p = u.describe(planet);
    s.metres = {0, std::pow(10., p.frame.logRadius) + 10000, 0};
    n->sites.push_back(s);
    return n;
}
SurveyObserver orbit(const Universe& u, const Address& planet) {
    SurveyObserver o;
    o.reference = planet;
    o.position = {0, 0, std::pow(10., u.describe(planet).frame.logRadius) * 3};
    o.seconds = 100;
    return o;
}
} // namespace
int main() {
    try {
        Universe u;
        const auto planet = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto node = u.describe(planet);
        const auto descriptor = descriptorFingerprint(node);
        auto eye = orbit(u, planet);
        SurveyController scan;
        unsigned done = 0;
        for (unsigned i = 0; i < 100; ++i)
            done += scan.update(u, eye, planet, 1, 1, .1, true);
        check(done == 1 && scan.progress() == 8, "Survey did not complete exactly once while held");
        scan.update(u, eye, planet, 1, 1, .1, false);
        check(scan.progress() == 0, "Release retained scan progress");
        auto blocked = eye;
        blocked.field = true;
        check(!scan.update(u, blocked, planet, 3, 1, .1, true), "Travel field allowed scanning");
        blocked = eye;
        blocked.position *= 10;
        check(!scan.update(u, blocked, planet, 3, 1, .1, true), "Remote scan bypassed range");
        blocked = eye;
        blocked.attitude = {0, 0, 1, 0};
        check(!surveyAccess(u, blocked, planet), "Scan ignored view direction");
        auto low = surveyReading(u, planet, 1, 100), high = surveyReading(u, planet, 3, 100);
        check(low.fractions == high.fractions && low.composition != high.composition,
              "Scanner grade changed real composition or failed to improve precision");
        auto chemistry = node.environment.chemistry;
        chemistry.normalize();
        check(low.fractions[0] == chemistry.ice && low.fractions[1] == chemistry.metal,
              "Survey invented resource composition");
        SurveyController slow, fast;
        unsigned slowFrames = 0, fastFrames = 0;
        while (!slow.update(u, eye, planet, 1, 0, .1, true))
            ++slowFrames;
        while (!fast.update(u, eye, planet, 2, 1.3, .1, true))
            ++fastFrames;
        check(fastFrames < slowFrames, "Scanner equipment and crew skill do not improve scan time");
        SurveyController busy;
        for (unsigned i = 0; i < 70; ++i)
            busy.update(u, eye, planet, 1, 1, .1, true);
        const auto partial = busy.progress();
        unsigned premature = 0;
        for (unsigned i = 0; i < 100; ++i)
            premature += busy.update(u, eye, planet, 1, 1, .1, true, false);
        check(!premature && busy.progress() == partial && busy.status().starts_with("SCAN WAITING"),
              "Busy flight computer consumed a scan before its command could be submitted");
        unsigned resumed = 0;
        for (unsigned i = 0; i < 30; ++i)
            resumed += busy.update(u, eye, planet, 1, 1, .1, true, true);
        check(resumed == 1 && busy.progress() == 8,
              "Pending scan failed to resume exactly once after the flight computer became ready");
        busy.update(u, eye, planet, 1, 1, .1, false, false);
        check(busy.progress() == 0,
              "Release failed to cancel an observation while the flight computer was busy");
        auto net = network(u, planet);
        Economy e(net);
        EconomyPlayer player;
        ExplorationLedger ledger;
        ledger.offer(u, e);
        check(ledger.state().contracts.size() == 3, "Real survey offers missing");
        const auto initialMoney = e.moneyTotal() + player.credits;
        uint64_t sequence = 0;
        auto issue = [&](EconomicCommand c) {
            c.sequence = ++sequence;
            return ledger.execute(u, e, player, c);
        };
        EconomicCommand c;
        c.action = EconomicAction::ScanSurvey;
        c.surveyTarget = planet;
        c.observer = eye;
        c.workSeconds = 8;
        check(issue(c).starts_with("DISCOVERY"), "Completed survey was not journalled");
        check(ledger.state().journal[0].descriptor == descriptor,
              "Journal did not retain real object identity");
        c.action = EconomicAction::SellSurvey;
        c.port = net->sites[0].id;
        check(!issue(c).starts_with("FIRST-SURVEY"),
              "Remote observer sold data without physical port access");
        auto docked = eye;
        docked.position = net->sites[0].metres;
        c.observer = docked;
        check(issue(c).starts_with("FIRST-SURVEY"), "Connected port did not buy first survey data");
        const auto credits = player.credits;
        issue(c);
        check(player.credits == credits, "Same survey paid twice");
        check(e.moneyTotal() + player.credits == initialMoney, "Survey data sale created or lost money");
        c = {};
        c.port = net->sites[0].id;
        c.observer = docked;
        c.action = EconomicAction::UpgradeScanner;
        c.limitCents = 100000000;
        check(issue(c).starts_with("SCANNER UPGRADED"), "Scanner upgrade failed");
        c.action = EconomicAction::FitMiningHead;
        c.weaponMount = 1;
        check(issue(c).starts_with("MINING HEAD FITTED"),
              "Empty auxiliary station could not fit mining equipment");
        check(ledger.state().miningMount == 1 && player.weaponAt(1) == 0,
              "Mining fit overwrote a laser or failed to occupy its station");
        c.weaponMount = 0;
        check(!issue(c).starts_with("MINING HEAD FITTED"), "Mining fit overwrote the starter laser");
        const auto direction = glm::normalize(glm::dvec3(.37, .74, 1));
        SurveyObserver ground;
        ground.reference = planet;
        ground.position =
            direction * (std::pow(10., node.frame.logRadius) + u.terrainHeight(node, direction) + 10);
        ground.seconds = 101;
        ground.landed = true;
        const auto patch = planet.child(Kind::Surface, encodeTile(tileAt(direction, 7)));
        check(surveyAccess(u, ground, patch), "Local surface survey requires facing a distant patch centre");
        auto access = miningAccess(u, ground, patch, 1);
        check(access.allowed, "Physical near-ground mining access failed");
        c = {};
        c.action = EconomicAction::ExtractProspect;
        c.surveyTarget = patch;
        c.observer = ground;
        c.weaponMount = 1;
        c.workSeconds = 6;
        c.combatPresent = true;
        check(issue(c).starts_with("UNSURVEYED"), "Unsurveyed prospect yielded cargo");
        auto survey = c;
        survey.action = EconomicAction::ScanSurvey;
        survey.workSeconds = 8;
        survey.serviceUnits = 120;
        check(issue(survey).starts_with("DISCOVERY"), "Surface prospect did not enter journal");
        check(ledger.state().journal.back().grade == 3, "Expert navigation crew did not improve precision");
        const auto inventory = e.inventoryTotal(), delta = e.externalGoodsDelta();
        check(issue(c).starts_with("EXTRACTED"), "Scanned physical prospect yielded no material");
        check(player.massKg() == 10 && ledger.state().minedKg == 10,
              "Extraction ignored canonical cargo mass");
        check(e.inventoryTotal() == inventory && e.externalGoodsDelta() == delta,
              "Exported extraction broke network material accounting");
        const auto key = prospectAddress(u, patch);
        check(key && *key == patch, "Canonical prospect changed address");
        auto mountain = u.children(u.describe(patch), 1).at(0);
        auto rock = u.children(u.describe(mountain), 1).at(0);
        auto grain = u.children(u.describe(rock), 1).at(0);
        auto lattice = u.children(u.describe(grain), 1).at(0);
        GeometryGenerator generator(u);
        const auto rockGeometry = geometryFingerprint(*generator.generate(u.describe(rock), 1));
        const auto atomicGeometry = geometryFingerprint(*generator.generate(u.describe(lattice), 1));
        check(prospectAddress(u, rock) == key && prospectAddress(u, lattice) == key,
              "Zoom scale duplicated a resource deposit");
        for (unsigned i = 0; i < 600; ++i) {
            player.cargo = {};
            issue(c);
        }
        const auto reserves = prospectResources(u, patch);
        check(ledger.state().prospects.size() == 1 && ledger.state().prospects[0].extracted == reserves,
              "Repeated mining did not deplete the one shared prospect");
        const auto mined = ledger.state().minedKg;
        player.cargo = {};
        c.surveyTarget = lattice;
        issue(c);
        check(ledger.state().minedKg == mined, "Finer material scale replenished depleted resource stock");
        check(geometryFingerprint(*generator.generate(u.describe(rock), 1)) == rockGeometry &&
                  geometryFingerprint(*generator.generate(u.describe(lattice), 1)) == atomicGeometry,
              "Mining altered preserved rock or atomic geometry");
        auto nano = ground;
        nano.reference = lattice;
        nano.size = 1e-10;
        nano.position = {0, 0, std::pow(10., u.describe(lattice).frame.logRadius) * 3};
        nano.attitude = {1, 0, 0, 0};
        check(surveyAccess(u, nano, lattice) &&
                  surveyReading(u, lattice, 3, 102).formation ==
                      std::string(processName(crystalCell(u.describe(lattice).environment.mineral,
                                                          u.describe(lattice).environment)
                                                  .phase)),
              "Nanoscale survey did not report the real crystal phase");
        auto obstruction = ActivityBody{};
        obstruction.id = {planet, 1};
        obstruction.radius = 8;
        obstruction.position = glm::mix(access.ray.origin, access.endpoint, .5);
        check(!miningAccess(u, ground, patch, 1, std::span(&obstruction, 1)).allowed,
              "Mining beam ignored an intervening ship");
        MiningController tool;
        for (unsigned i = 0; i < 400; ++i)
            tool.update(access, 1, 1, .1, true);
        check(tool.heat() <= 100, "Mining ignored heat cap");
        MiningController busyTool;
        for (unsigned i = 0; i < 15; ++i)
            busyTool.update(access, 1, 1, .1, true);
        const auto partialWork = busyTool.progress();
        unsigned interruptedWork = 0;
        for (unsigned i = 0; i < 100; ++i)
            interruptedWork += busyTool.update(access, 1, 1, .1, true, false);
        check(!interruptedWork && busyTool.progress() == partialWork &&
                  busyTool.status().starts_with("MINING WAITING"),
              "Background ledger traffic discarded or completed pending mining work");
        unsigned resumedWork = 0;
        for (unsigned i = 0; i < 60; ++i)
            resumedWork += busyTool.update(access, 1, 1, .1, true, true);
        check(resumedWork == 1, "Pending mining work did not resume once after ledger acknowledgement");
        busyTool.update(access, 1, 1, .1, false, false);
        check(busyTool.progress() == 0, "Releasing the mining trigger while busy retained work");
        auto unsafe = ground;
        unsafe.size = .1;
        check(!miningAccess(u, unsafe, patch, 1).allowed, "Shrunken ship extracted full-size cargo");
        unsafe = ground;
        unsafe.velocity = {4, 0, 0};
        check(!miningAccess(u, unsafe, patch, 1).allowed, "Moving ship bypassed mining safety");
        const auto job = ledger.state().contracts.front();
        c = {};
        c.port = job.issuer;
        c.contract = job.id;
        c.action = EconomicAction::AcceptSurvey;
        const auto beforeJob = e.moneyTotal() + player.credits;
        check(issue(c).starts_with("SURVEY ACCEPTED"), "Funded survey contract could not be accepted");
        check(e.moneyTotal() + player.credits + ledger.state().contracts.front().escrow == beforeJob,
              "Accepted survey reward not cash backed");
        auto remote = orbit(u, job.target);
        c.action = EconomicAction::ScanSurvey;
        c.surveyTarget = job.target;
        c.observer = remote;
        c.workSeconds = 8;
        const auto reportMessage = issue(c);
        check(reportMessage.starts_with("DISCOVERY") || reportMessage.starts_with("SURVEY UPDATED"),
              "Mission survey failed");
        c.action = EconomicAction::ClaimSurvey;
        c.observer = docked;
        c.surveyTarget.reset();
        check(issue(c).starts_with("SURVEY CONTRACT COMPLETE"),
              "Completed survey could not be claimed at issuer");
        const auto paid = player.credits;
        issue(c);
        check(player.credits == paid, "Survey contract paid twice");
        check(e.moneyTotal() + player.credits == beforeJob, "Survey escrow did not conserve money");
        const auto cancelled = ledger.state().contracts[1];
        c = {};
        c.port = cancelled.issuer;
        c.contract = cancelled.id;
        c.action = EconomicAction::AcceptSurvey;
        check(issue(c).starts_with("SURVEY ACCEPTED"), "Second survey could not reserve funds");
        c.action = EconomicAction::CancelSurvey;
        check(issue(c).starts_with("SURVEY CANCELLED") && e.moneyTotal() + player.credits == beforeJob,
              "Cancelled survey did not return issuer escrow");
        const auto advanced = ledger.state().contracts[2];
        auto instrument = orbit(u, advanced.target);
        c = {};
        c.action = EconomicAction::ScanSurvey;
        c.surveyTarget = advanced.target;
        c.observer = instrument;
        c.workSeconds = 8;
        c.serviceUnits = 120;
        issue(c);
        c.action = EconomicAction::AcceptSurvey;
        c.port = advanced.issuer;
        c.contract = advanced.id;
        check(issue(c).starts_with("SURVEY ACCEPTED"), "Advanced survey could not be accepted");
        c.action = EconomicAction::ScanSurvey;
        c.serviceUnits = 0;
        check(issue(c).starts_with("LOWER SCAN PRECISION"),
              "Low-precision rescan refreshed a higher-grade report");
        c.action = EconomicAction::ClaimSurvey;
        c.observer = docked;
        check(issue(c).starts_with("NEW SURVEY REQUIRED"),
              "Pre-acceptance precision completed a new assignment");
        c.action = EconomicAction::ScanSurvey;
        c.observer = instrument;
        c.serviceUnits = 120;
        issue(c);
        c.action = EconomicAction::ClaimSurvey;
        c.observer = docked;
        check(issue(c).starts_with("SURVEY CONTRACT COMPLETE") &&
                  e.moneyTotal() + player.credits == beforeJob,
              "Fresh advanced observation did not complete funded contract");
        ExplorationLedger restored;
        const auto saved = ledger.serialize();
        check(restored.restore(saved, u) && restored.serialize() == saved,
              "Exploration journal/depletion/equipment failed save round trip");
        check(!restored.restore(saved + "garbage", u) && restored.serialize() == saved,
              "Malformed exploration save changed valid state");
        check(descriptorFingerprint(u.describe(planet)) == descriptor,
              "Survey/mining changed generated world descriptors");
        Economy reloaded(net);
        check(reloaded.restore(e.serialize()), "New gameplay invalidated the unchanged economy save format");
        check(player.restore(player.serialize()), "New gameplay invalidated the unchanged wallet format");
        EconomySnapshot snapshot;
        snapshot.network = net;
        snapshot.sites = e.sites();
        snapshot.player = player;
        snapshot.exploration = ledger.state();
        auto view = economicMarketView(&snapshot, net->sites[0].id, 0, 10, true, MarketPage::Journal);
        check(view.ready && !view.detail.empty(), "Survey journal cannot be viewed through market adapter");
        std::cout << "PASS: " << checks
                  << " survey, precision, crew, equipment, finite extraction, shared scale identity, funded "
                     "contracts and restart checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
