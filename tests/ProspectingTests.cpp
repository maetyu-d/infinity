#include "infinity/Economy.hpp"
#include "infinity/Piloting.hpp"
#include "infinity/Streaming.hpp"
#include "infinity/World.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        Universe u;
        const auto planet = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto p = u.describe(planet);
        const auto d = glm::normalize(glm::dvec3(.37, .74, 1));
        const auto tile = tileAt(d, 7);
        auto before = std::chrono::steady_clock::now();
        const auto sites = generateProspects(u, p, tile);
        auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        check(!sites.empty() && sites.size() <= maxRegionalProspects, "No bounded local outcrops");
        const auto replay = generateProspects(u, p, tile);
        check(sites.size() == replay.size(), "Unstable prospect count");
        std::set<Address> addresses;
        unsigned easy = 0;
        for (size_t i = 0; i < sites.size(); ++i) {
            const auto& s = sites[i];
            check(s.surface == replay[i].surface && s.metres == replay[i].metres &&
                      s.rocks[0].seed == replay[i].rocks[0].seed,
                  "Prospects depend on generation order");
            check(addresses.insert(s.surface).second &&
                      s.pool == planet.child(Kind::Surface, encodeTile(tile)),
                  "Duplicate or cross-region prospect");
            check(std::abs(glm::length(s.metres) - std::pow(10., p.frame.logRadius) -
                           u.terrainHeight(p, s.direction)) < 1e-5,
                  "Outcrop does not sit on actual terrain");
            check(s.parentFrames.size() == u.frames(s.rocks[0].address.parent()).size(),
                  "Outcrop lost its coordinate lineage");
            for (const auto& rock : s.rocks)
                check(descriptorFingerprint(rock) == descriptorFingerprint(u.describe(rock.address)),
                      "Presentation substituted a different rock");
            check(prospectAddress(u, s.rocks[0].address) == s.pool,
                  "Finer scale invented another resource pool");
            if (s.easy) {
                ++easy;
                check(s.slopeDegrees < 12 && s.clearanceMetres < 2, "Easy site has unsafe footing");
            }
        }
        check(easy > 0, "Local generation did not offer a gentler sampling site");
        const auto tileMesh = terrainGeometry(u, p, tile, 0);
        check(tileMesh->exposureColors.size() == 81 && tileMesh->exposureColors != tileMesh->terrainAlbedo,
              "Geological surface presentation is missing");
        const auto physical = geometryFingerprint(*tileMesh);
        tileMesh->exposureColors.clear();
        check(geometryFingerprint(*tileMesh) == physical,
              "Exposure presentation changed physical terrain identity");
        const auto oldRock = Address::parse("0,0,0/1:0/2:9/3:0/4:979510287489940/5:0/6:0");
        check(descriptorFingerprint(u.describe(oldRock)) == 17940071816253843805ULL,
              "Resource additions changed an established saved rock descriptor");
        const auto mountain = u.describe(sites.front().rocks.front().address.parent());
        const auto children = u.children(mountain);
        check(children.size() == 27, "Resource formations were not added to the existing rock catalogue");
        for (unsigned i = 0; i < 24; ++i) {
            check(children[i] == mountain.address.child(Kind::Rock, i),
                  "Resource generation replaced an existing rock identity");
            const auto node = u.describe(children[i]);
            const Random random{node.seed};
            check(std::abs(node.frame.logRadius -
                           std::min(-.4 + .7 * random.unit(7), mountain.frame.logRadius - 1)) < 1e-14,
                  "Resource formations resized an established rock");
        }
        for (const auto& rock : sites.front().rocks)
            if (rock.address.id() & exposureRockIdFlag)
                check(std::pow(10., rock.frame.logRadius) >= 1,
                      "New mineral formation is too small to resolve from a ship");
        auto dry = p;
        dry.environment.chemistry.ice = 0;
        dry.environment.chemistry.carbon = 0;
        dry.environment.chemistry.normalize();
        const auto constrained = resourceExposure(dry, d, geologySample(*p.geology, d));
        check(!constrained.fractions[0] && !constrained.fractions[3],
              "Concentration invented absent constituents");
        std::atomic<bool> stop = true;
        check(generateProspects(u, p, tile, &stop).empty(), "Prospecting generation ignored cancellation");
        std::set<ResourceStock> yields;
        for (unsigned i = 0; i < 32; ++i) {
            auto t =
                tileAt(glm::normalize(glm::dvec3(std::sin(i * 2.4), .05 + i * .025, std::cos(i * 2.4))), 7);
            auto key = planet.child(Kind::Surface, encodeTile(t));
            auto reserve = prospectResources(u, key);
            yields.insert(reserve);
            const auto mass = std::accumulate(reserve.begin(), reserve.end(), int64_t(0));
            check(mass >= 100 && mass <= 5000, "Invalid finite deposit mass");
        }
        check(yields.size() > 8, "Every region still has the same generic mineral bucket");
        const auto pool = planet.child(Kind::Surface, encodeTile(tile));
        const auto legacy = legacyProspectResources(u, pool);
        std::ostringstream old;
        old << "INFINITE_EXPLORATION 1 1 1 1 " << std::accumulate(legacy.begin(), legacy.end(), int64_t(0))
            << " 0 0 0\n0\n1\n"
            << std::quoted(pool.str());
        for (auto kg : legacy)
            old << ' ' << kg;
        old << "\n0\n";
        ExplorationLedger ledger;
        check(ledger.restore(old.str(), u), "Version-one save did not migrate");
        check(ledger.state().prospects[0].capacity == legacy &&
                  ledger.state().prospects[0].extracted == legacy,
              "Migration refilled a depleted legacy deposit");
        ExplorationLedger again;
        check(again.restore(ledger.serialize(), u) && again.state().prospects[0].capacity == legacy,
              "Frozen capacity did not survive restart");
        auto network = std::make_shared<EconomicNetwork>();
        network->system = planet.parent();
        network->universeSeed = u.seed();
        network->fingerprint = 560056;
        EconomicSite port;
        port.id = {planet, 0};
        port.capacityKg = 100000;
        port.initial.fill(100);
        network->sites.push_back(port);
        Economy economy(network);
        EconomyPlayer player;
        SurveyObserver observer;
        observer.reference = planet;
        observer.position = d * (std::pow(10., p.frame.logRadius) + u.terrainHeight(p, d) + 10);
        observer.landed = true;
        EconomicCommand c;
        c.sequence = 1;
        c.action = EconomicAction::ScanSurvey;
        c.surveyTarget = pool;
        c.observer = observer;
        c.workSeconds = 8;
        check(ledger.execute(u, economy, player, c).starts_with("DISCOVERY"),
              "Migrated region could not be resurveyed");
        check(ledger.state().journal[0].resources == legacy, "Resurvey silently expanded a depleted pool");
        c.sequence = 2;
        c.action = EconomicAction::ExtractProspect;
        c.weaponMount = 1;
        c.workSeconds = 6;
        c.combatPresent = true;
        ledger.execute(u, economy, player, c);
        check(!player.massKg(), "New geology refilled already exhausted stock");
        RegionCache cache(u, 512 * 1024, 1);
        std::shared_ptr<const std::vector<ProspectSite>> cached;
        for (unsigned i = 0; i < 20 && !cached; ++i) {
            cache.beginFrame();
            cached = cache.prospects(p, tile);
            cache.finishFrame();
            cache.wait();
        }
        check(cached && cached->size() == sites.size(), "Async prospect generation failed");
        for (unsigned i = 0; i < 8; ++i) {
            cache.beginFrame();
            cache.prospects(p, tileAt(glm::normalize(glm::dvec3(1, .2 + i * .15, .1)), 7));
            cache.finishFrame();
            cache.wait();
        }
        check(cache.stats().bytes <= 512 * 1024 && cache.stats().peakPending <= 1 &&
                  cache.stats().evicted > 0,
              "Prospects bypassed worker or cache limits");
        cache.clear();
        check(!cache.stats().bytes && !cache.stats().pending, "Cancelled catalog retained accounted memory");
        {
            const auto& target = sites.front();
            Navigation nav(u);
            RegionCache flightCache(u, 4 * 1024 * 1024, 1);
            FlightController spatial(u, nav, flightCache);
            Pilot pilot(u, nav, spatial);
            const auto eye = target.metres + target.direction * 60. +
                             tangentFrame(target.direction) * glm::dvec3(0, 0, 180);
            const auto q = glm::quatLookAtRH(glm::normalize(target.metres - eye), target.direction);
            const double log = 2;
            const auto offset = eye / 100. - q * glm::dvec3(0, 0, 1);
            std::ostringstream view;
            view.precision(17);
            view << planet.str() << ' ' << log << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
                 << offset.x << ' ' << offset.y << ' ' << offset.z;
            check(nav.restore(view.str()), "Prospect approach fixture invalid");
            pilot.resetMotion();
            FlightWaypoint point;
            point.reference = planet;
            point.metres = target.metres + target.direction * 16.;
            point.standOff = 12;
            point.surfacePort = true;
            point.label = target.name;
            const auto originalView = nav.serialize();
            pilot.navigateWaypoint(point);
            check(nav.serialize() == originalView, "Prospect approach teleported the ship");
            bool accessible = false;
            for (unsigned i = 0; i < 12000 && !accessible; ++i) {
                flightCache.beginFrame();
                pilot.update({}, 1. / 60);
                nav.update(1. / 60);
                flightCache.finishFrame();
                if (i % 20 == 0)
                    flightCache.wait();
                SurveyObserver o;
                o.reference = nav.anchor;
                o.position = nav.cameraPosition() * std::pow(10., nav.logDistance);
                o.velocity =
                    u.relative(pilot.ship().frame, nav.anchor, 0).orientation * pilot.ship().velocity;
                o.attitude = nav.orientation;
                o.size = pilot.sizeRatio();
                o.hull = pilot.ship().hull;
                o.field = pilot.instruments().fieldActive;
                accessible = miningAccess(u, o, target.surface, 0).allowed;
            }
            if (!accessible || pilot.ship().hull < 100)
                std::cerr << "APPROACH altitude=" << pilot.instruments().altitude
                          << " speed=" << pilot.instruments().speed << " hull=" << pilot.ship().hull
                          << " phase=" << pilot.instruments().phase
                          << " field=" << pilot.instruments().fieldActive << " frame=" << nav.anchor.str()
                          << "\n";
            check(accessible && pilot.ship().hull == 100,
                  "New prospect approach did not reach usable mining altitude and speed");
        }
        unsigned gases = 0;
        for (unsigned i = 0; i < u.planetCount(u.describe(planet.parent())); ++i) {
            auto body = u.describe(planet.parent().child(Kind::Planet, i));
            if (!hasSolidSurface(body)) {
                ++gases;
                check(generateProspects(u, body, tile).empty(), "Gas giant generated landable mining sites");
            }
        }
        check(gases > 0, "Gas exclusion test missed gas giants");
        std::cout << "PASS: " << checks
                  << " geological prospects, real geometry, resource diversity, legacy depletion and "
                     "async-cache checks; region generation "
                  << elapsed << " ms\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
