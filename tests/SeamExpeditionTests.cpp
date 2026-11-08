#include "infinity/Economy.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/Streaming.hpp"
#include <iostream>
#include <numeric>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
SurveyObserver closeObserver(const Universe& u, const Address& a) {
    SurveyObserver o;
    o.reference = a;
    const double radius = std::pow(10., u.describe(a).frame.logRadius);
    o.position = {0, 0, radius * 1.1};
    o.size = std::clamp(radius * .001, 1e-12, .001);
    o.seconds = 200;
    return o;
}
} // namespace
int main() {
    try {
        Universe u;
        const auto planet = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto p = u.describe(planet);
        const auto sites = generateProspects(u, p, tileAt(glm::normalize(glm::dvec3(.37, .74, 1)), 7));
        check(!sites.empty(), "No real prospect fixture");
        SurveyObserver ground;
        ground.reference = planet;
        ground.seconds = 100;
        const ProspectSite* site = nullptr;
        for (const auto& s : sites) {
            ground.position = s.metres + s.direction * 16.;
            const auto to = u.relative(s.rocks.front().address, planet, 0).position - ground.position;
            ground.attitude = glm::quatLookAtRH(glm::normalize(to), s.direction);
            if (seamEntryPoint(u, ground, s.rocks.front().address)) {
                site = &s;
                break;
            }
        }
        check(site != nullptr, "Generated clearings cannot start an expedition");
        const auto source = site->rocks.front().address;
        const auto descriptor = descriptorFingerprint(u.describe(source));
        GeometryGenerator generator(u);
        const auto geometry = geometryFingerprint(*generator.generate(u.describe(source), 1));
        auto net = std::make_shared<EconomicNetwork>();
        net->system = planet.parent();
        net->universeSeed = u.seed();
        net->fingerprint = 570057;
        EconomicSite port;
        port.id = {planet, 0};
        port.orbital = true;
        port.capacityKg = 2000000;
        port.initial.fill(10000);
        net->sites.push_back(port);
        Economy economy(net);
        EconomyPlayer player;
        ExplorationLedger ledger;
        uint64_t sequence = 0;
        auto issue = [&](EconomicAction action, const Address& target, const SurveyObserver& observer) {
            EconomicCommand c;
            c.action = action;
            c.sequence = ++sequence;
            c.surveyTarget = target;
            c.observer = observer;
            c.workSeconds = action == EconomicAction::ExtractProspect ? 6 : 8;
            c.combatPresent = true;
            c.weaponMount = 0;
            return ledger.execute(u, economy, player, c);
        };
        check(issue(EconomicAction::BeginSeam, source, ground).starts_with("SURVEY THE SURFACE"),
              "Unsurveyed ground accepted an expedition");
        check(issue(EconomicAction::ScanSurvey, site->surface, ground).starts_with("DISCOVERY"),
              "Surface survey failed");
        check(issue(EconomicAction::BeginSeam, source, ground).starts_with("SEAM EXPEDITION"),
              "Full-size expedition could not begin at real clearing");
        check(ledger.state().seams.size() == 1 && ledger.state().seams.back().source == source &&
                  ledger.state().seams.back().pool == site->pool,
              "Expedition lost canonical source or reserve identity");
        const auto entry = ledger.state().seams.back().entryMetres;
        auto small = closeObserver(u, source);
        check(issue(EconomicAction::MarkSeam, source, small).starts_with("MARK REQUIRES"),
              "Unscanned material could be marked");
        auto magnified = small;
        magnified.size = 1;
        check(!seamSurveyAccess(u, magnified, ledger.state().seams.back(), source),
              "Camera magnification substituted for shrinking");
        auto fast = small;
        fast.velocity = {10 * small.size, 0, 0};
        check(!seamSurveyAccess(u, fast, ledger.state().seams.back(), source),
              "Unsafe high-speed inspection accepted");
        auto remote = small;
        remote.position *= 10;
        check(!seamSurveyAccess(u, remote, ledger.state().seams.back(), source),
              "Remote microscopic inspection accepted");
        check(issue(EconomicAction::ScanSurvey, source, small).starts_with("CLOSE SEAM SURVEY"),
              "Physical tiny inspection did not register");
        check(issue(EconomicAction::MarkSeam, source, small).starts_with("MATERIAL MARKED"),
              "Rock-scale close inspection could not be marked");
        auto grain = u.children(u.describe(source), 1).front();
        auto grainEye = closeObserver(u, grain);
        check(issue(EconomicAction::ScanSurvey, grain, grainEye).starts_with("CLOSE SEAM SURVEY"),
              "Actual mineral refinement lost source lineage");
        check(!ledger.state().seams.back().marked && ledger.state().seams.back().material == grain,
              "A finer inspection silently retained the previous material mark");
        check(issue(EconomicAction::MarkSeam, source, small).starts_with("MARK REQUIRES"),
              "Mark targeted a different material from the close survey");
        check(issue(EconomicAction::MarkSeam, grain, grainEye).starts_with("MATERIAL MARKED"),
              "Close mineral observation could not be marked");
        check(ledger.state().seams.back().marked && ledger.state().seams.back().material == grain,
              "Mark replaced actual mineral identity");
        ExplorationLedger loaded;
        auto saved = ledger.serialize();
        check(loaded.restore(saved, u) && loaded.serialize() == saved,
              "Saved entry/material/inspection did not round-trip");
        check(!loaded.restore(saved + "garbage", u) && loaded.serialize() == saved,
              "Malformed seam save modified the live ledger");
        check(issue(EconomicAction::BeginSeam, source, small).starts_with("BEGIN AT FULL SIZE"),
              "Tiny ship moved its full-size return beacon");
        check(ledger.state().seams.back().entryMetres == entry, "Failed start overwrote entry beacon");
        const auto* mark = markedSeam(ledger.state(), source);
        check(mark && markedSeam(ledger.state(), grain) == mark,
              "Rock and mineral did not resolve the same mark");
        check(!miningAccess(u, small, source, 0, {}, mark).allowed,
              "Tiny ship loaded full-size extracted cargo");
        const auto access = miningAccess(u, ground, source, 0, {}, mark);
        check(access.allowed && glm::length(access.endpoint - u.relative(grain, planet, 0).position) < 1e-6,
              "Marked beam failed to target the actual surveyed material");
        auto farGround = ground;
        farGround.position += tangentFrame(site->direction) * glm::dvec3(500, 0, 0);
        check(!miningAccess(u, farGround, source, 0, {}, mark).allowed,
              "Mark could be mined from elsewhere in the district");
        // Install a grade-I head using the real compatible equipment save layout.
        saved = ledger.serialize();
        const auto start = saved.find(" 1 0 3 ");
        check(start != std::string::npos, "Unexpected default equipment layout");
        saved.replace(start, 7, " 1 1 0 ");
        check(ledger.restore(saved, u), "Could not restore fitted mining-head fixture");
        const auto slot = ledger.state().seams.back().resource;
        constexpr std::array goods{Good::Ice, Good::Ore, Good::Silicate, Good::Carbon};
        const auto before = player.cargo;
        check(issue(EconomicAction::ExtractProspect, source, ground).starts_with("SEAM EXTRACTED"),
              "Return to full size could not extract the saved mark");
        check(player.cargo[size_t(goods[slot])] - before[size_t(goods[slot])] == 20 &&
                  ledger.state().seams.back().minedKg == 20,
              "Close inspection did not produce selective improved extraction");
        for (unsigned k = 0; k < 4; ++k)
            if (k != slot)
                check(player.cargo[size_t(goods[k])] == before[size_t(goods[k])],
                      "Selective mining added unwanted constituents");
        const auto& reserve = ledger.state().prospects.front();
        check(reserve.extracted[slot] == 20 && reserve.capacity == prospectResources(u, site->pool),
              "Mark minted a second reserve or changed physical capacity");
        const auto mined = ledger.state().minedKg;
        player.capacityKg = player.massKg();
        check(issue(EconomicAction::ExtractProspect, grain, ground).starts_with("HOLD FULL") &&
                  ledger.state().minedKg == mined,
              "Full cargo hold destroyed marked stock");
        saved = ledger.serialize();
        check(loaded.restore(saved, u) && loaded.serialize() == saved,
              "Completed extraction outcomes lost on restart");
        EconomicCommand replay;
        replay.sequence = sequence;
        replay.action = EconomicAction::ExtractProspect;
        replay.surveyTarget = source;
        replay.observer = ground;
        replay.workSeconds = 6;
        check(ledger.execute(u, economy, player, replay).starts_with("ORDER ALREADY") &&
                  ledger.state().minedKg == mined, "Replayed extraction minted duplicate cargo");
        player.capacityKg = 10000;
        player.capacityLitres = 32000;
        std::string outcome;
        for (unsigned k = 0; k < 300; ++k) {
            outcome = issue(EconomicAction::ExtractProspect, source, ground);
            if (!outcome.starts_with("SEAM EXTRACTED"))
                break;
        }
        check(outcome.starts_with("MARKED MATERIAL EXHAUSTED"), "Selective seam stock never exhausted");
        check(ledger.state().prospects.front().extracted[slot] ==
                  ledger.state().prospects.front().capacity[slot], "Marked stock was not conserved");
        saved = ledger.serialize();
        check(loaded.restore(saved, u) && loaded.serialize() == saved,
              "Depleted marked material refilled on restart");
        ExplorationLedger empty, oldVersion;
        const auto v2 = "INFINITE_EXPLORATION 2 1 0 " + std::to_string(noWeaponMount) +
                        " 0 0 0 0\n0\n0\n0\n";
        check(oldVersion.restore(v2, u) && oldVersion.state().seams.empty(), "Version-two save migration failed");
        check(descriptorFingerprint(u.describe(source)) == descriptor &&
                  geometryFingerprint(*generator.generate(u.describe(source), 1)) == geometry,
              "Prospecting expedition changed generated objects or geometry");
        check(shipDamageMultiplier(1) == 1 && shipDamageMultiplier(1e-12) == 25 &&
                  shipDamageMultiplier(.001) > 5,
              "Tiny hull did not become progressively fragile");
        check(shipContactDamage(10, 1, .3) == (10 - 2) * .3 &&
                  shipContactDamage(10e-9, 1e-9, .3) > shipContactDamage(10, 1, .3) * 25 &&
                  shipContactDamage(.1e-9, 1e-9, .3) == 0,
              "Tiny impacts are not fragile or safe slow flight was damaged");
        Navigation nav(u);
        nav.changeFrame(source);
        selectSeamSource(nav, source);
        check(!nav.selected, "Entry beacon attempted to select the current coordinate frame");
        nav.changeFrame(source.parent());
        selectSeamSource(nav, source);
        check(nav.selected == source, "Saved source could not be selected after leaving its frame");
        nav.changeFrame(planet);
        nav.setRenderScale(2);
        nav.offset = ground.position / 100. - glm::dvec3(0, 0, 1);
        RegionCache cache(u);
        FlightController flight(u, nav, cache);
        Pilot pilot(u, nav, flight);
        pilot.applyCombatDamage(4);
        check(pilot.ship().hull == 96, "Normal ship damage changed");
        pilot.toggleShrink();
        for (unsigned k = 0; k < 30; ++k)
            pilot.update({}, .1);
        const double hull = pilot.ship().hull, multiplier = shipDamageMultiplier(pilot.sizeRatio());
        pilot.applyCombatDamage(4);
        check(multiplier > 5 && pilot.ship().hull <= hull - 4 * multiplier + 1e-8,
              "Actual tiny pilot ignored amplified weapon damage");
        std::cout << "PASS: " << checks << " shrinking seam expedition, saved marks, selective finite mining and fragile hull checks\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
