#pragma once
#include "infinite/Economy.hpp"
#include "infinite/FireControl.hpp"
#include "infinite/Recovery.hpp"
#include "infinite/Renderer.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
namespace infinite {
class CombatOperationsValidation {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    ShipCrew& crew_;
    WeaponSystem& weapons_;
    FireControl& fire_;
    ActivityState& activity_;
    ActivityBody target_;
    RecoveryController recovery_;
    std::shared_ptr<const EconomicNetwork> network_;
    std::unique_ptr<Economy> ledger_;
    EconomyPlayer player_;
    std::shared_ptr<EconomySnapshot> snapshot_;
    MarketView market_;
    CrewView crewView_;
    unsigned stage_ = 0, age_ = 0;
    bool finished_ = false;
    size_t port_ = 0;
    uint64_t survivor_ = 0;
    void expect(bool x, const char* why) {
        if (!x)
            throw std::runtime_error(why);
    }
    void next(unsigned n) {
        stage_ = n;
        age_ = 0;
    }
    void place(const Address& frame, glm::dvec3 eye) {
        std::ostringstream text;
        text.precision(17);
        const auto offset = eye / 1000. - glm::dvec3(0, 0, 1);
        text << frame.str() << " 3 1 0 0 0 " << offset.x << ' ' << offset.y << ' ' << offset.z;
        expect(nav_.restore(text.str()), "Invalid combat operations view");
        pilot_.resetMotion();
    }
    void refresh() {
        snapshot_ = std::make_shared<EconomySnapshot>();
        snapshot_->network = network_;
        snapshot_->sites = ledger_->sites();
        snapshot_->player = player_;
        snapshot_->tick = ledger_->tick();
        snapshot_->crewMarkets = ledger_->crewMarkets();
        snapshot_->wrecks = ledger_->wrecks();
        snapshot_->combatContracts = ledger_->combatContracts();
        market_.credits = player_.credits;
        market_.cargoKg = player_.massKg();
        market_.cargoLitres = player_.volumeLitres();
        market_.activeJob = player_.rescued.empty()
                                ? ""
                                : "RESCUED " + std::to_string(player_.rescued.size()) + " / F2 SERVICES";
    }
    void shoot(ShipSystem system, unsigned shots) {
        WeaponRay ray;
        ray.reference = target_.id.home;
        ray.origin = nav_.cameraPosition() * 1000.;
        ray.seconds = activity_.seconds;
        ray.targetSystem = system;
        ray.direct = true;
        ray.direction = glm::normalize(
            target_.position + target_.orientation * (systemOffset(system) * target_.radius) - ray.origin);
        for (unsigned i = 0; i < shots; ++i) {
            weapons_.advance(1);
            expect(weapons_.fire(u_, ray, std::span(&target_, 1)), "Native component shot rejected");
        }
    }

  public:
    CombatOperationsValidation(const Universe& u, Navigation& nav, Pilot& pilot, ShipCrew& crew,
                               WeaponSystem& weapons, FireControl& fire, ActivityState& activity)
        : u_(u), nav_(nav), pilot_(pilot), crew_(crew), weapons_(weapons), fire_(fire), activity_(activity) {
        const auto system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
        network_ = generateEconomicNetwork(u_, system);
        expect(bool(network_), "Native economic network missing");
        ledger_ = std::make_unique<Economy>(network_);
        ledger_->advanceTo(12);
        activity_.seconds = 60;
        for (unsigned p = 0; p < 4; ++p) {
            auto home = system.child(Kind::Planet, p);
            const auto seed =
                mix64(seedOf(home, u_.seed()) ^ mix64(raiderActivitySlot) ^ 0x5245534355453532ULL);
            if (seed % 4) {
                target_ = sampleRaider(u_, home, 60);
                break;
            }
        }
        activity_.selected = target_.id;
        place(target_.id.home, target_.position + glm::dvec3(0, 0, 600));
        crew_.toggleAutomatic();
        crew_.setWeaponLevels({1, 1, 1});
        weapons_.setLoadout({1, 1, 1});
        crew_.manWeapon(2, 1);
        crew_.manWeapon(5, 2);
        for (unsigned i = 0; i < 100; ++i)
            crew_.advance(.25);
        StationControl{}.prepareWeapons(weapons_, crew_);
        fire_.order(FireOrder::Hold);
        refresh();
        auto port = std::find_if(network_->sites.begin(), network_->sites.end(),
                                 [&](const auto& s) { return s.orbital && s.id.home == target_.id.home; });
        expect(port != network_->sites.end(), "No existing rescue port");
        port_ = size_t(port - network_->sites.begin());
    }
    void advance(Renderer& renderer) {
        ++age_;
        weapons_.advance(1. / 60);
        crew_.advance(.25);
        StationControl{}.prepareWeapons(weapons_, crew_);
        if (stage_ == 1 && weapons_.shotsFired() < 2) {
            WeaponRay ray;
            ray.reference = nav_.anchor;
            ray.origin = nav_.cameraPosition() * 1000.;
            ray.seconds = activity_.seconds;
            fire_.update(u_, ray, nav_.orientation, target_.id, 0, crew_, weapons_, std::span(&target_, 1),
                         1. / 60, false, renderer.submittedGeometry(nav_.anchor).get());
        }
        if (stage_ == 2 && age_ == 1) {
            fire_.order(FireOrder::Hold);
            shoot(ShipSystem::Engines, 4);
            auto b = target_;
            weapons_.apply(b);
            expect(b.hull > 50 && b.systems[size_t(ShipSystem::Engines)] == 0,
                   "Component targeting changed hull incorrectly");
        }
        if (stage_ == 3 && age_ == 1) {
            crew_.observeHull(pilot_.ship().hull);
            pilot_.applyCombatDamage(25);
            crew_.observeCombatHit(pilot_.ship().hull, ShipSystem::StarboardGun, 25);
            crew_.order(0, CrewOrder::Firefight, CrewStation::Weapons);
            crew_.order(1, CrewOrder::Firefight, CrewStation::Weapons);
            crew_.order(3, CrewOrder::Repair, CrewStation::Weapons);
        }
        if (stage_ == 5 && age_ == 1) {
            for (unsigned i = 0; i < 10; ++i) {
                auto b = target_;
                weapons_.apply(b);
                if (b.disabled)
                    break;
                shoot(ShipSystem::Hull, 1);
            }
            auto b = target_;
            weapons_.apply(b);
            expect(b.disabled, "Native wreck did not disable");
            auto damage = std::find_if(weapons_.damage().begin(), weapons_.damage().end(),
                                       [&](const auto& d) { return d.id == target_.id; });
            EconomicCommand c;
            c.sequence = player_.lastCommand + 1;
            c.action = EconomicAction::RegisterWreck;
            c.combatTarget = target_.id;
            c.wreck = *damage;
            expect(ledger_->execute(c, player_).starts_with("WRECK REGISTERED"),
                   "Native wreck registration failed");
            expect(ledger_->wrecks().back().survivor.has_value(), "Expected canonical survivor absent");
            survivor_ = ledger_->wrecks().back().survivor->id;
            place(target_.id.home, target_.position + glm::dvec3(0, 0, 80));
            refresh();
        }
        if (stage_ == 5 || stage_ == 6) {
            auto b = target_;
            weapons_.apply(b);
            NpcPlayer ship;
            ship.reference = nav_.anchor;
            ship.position = nav_.cameraPosition() * 1000.;
            ship.hull = pilot_.ship().hull;
            if (recovery_.update(u_, ship, b, .1, stage_ == 6,
                                 renderer.submittedGeometry(nav_.anchor).get())) {
                EconomicCommand c;
                c.sequence = player_.lastCommand + 1;
                c.action = EconomicAction::RecoverWreck;
                c.combatTarget = b.id;
                c.combatPresent = true;
                c.targetDistance = std::max(0., recovery_.range() - b.radius);
                c.relativeSpeed = recovery_.relativeSpeed();
                c.crewAboard = unsigned(crew_.living());
                expect(ledger_->execute(c, player_).starts_with("RECOVERED"),
                       "Native physical transfer rejected");
                expect(player_.massKg() == 22 && player_.rescued.size() == 1,
                       "Native salvage or survivor transfer failed");
                refresh();
                next(7);
            }
        }
        if (stage_ == 8 && age_ == 1) {
            const auto& port = network_->sites[port_];
            place(port.id.home, port.metres + glm::dvec3(0, 0, 150));
            expect(economicTradeAccess(u_, nav_, pilot_, port), "Rescue handover was not physically berthed");
            market_ = economicMarketView(snapshot_.get(), port.id, 2, 1, true, MarketPage::Services,
                                         {pilot_.ship().hull});
        }
        if (stage_ == 9 && age_ == 1) {
            EconomicCommand c;
            c.sequence = player_.lastCommand + 1;
            c.port = network_->sites[port_].id;
            c.action = EconomicAction::DisembarkRescued;
            expect(ledger_->execute(c, player_).starts_with("SURVIVORS DISEMBARKED"),
                   "Native port handover failed");
            refresh();
            const auto rows = economicCrewRows(*snapshot_, port_, crew_.members());
            auto person = std::find_if(rows.begin(), rows.end(),
                                       [&](const auto& r) { return r.person.id == survivor_; });
            expect(person != rows.end(), "Rescued identity missing from crew office");
            MarketContext context;
            context.crew = crew_.members();
            market_ = economicMarketView(snapshot_.get(), network_->sites[port_].id,
                                         size_t(person - rows.begin()), 1, true, MarketPage::Crew, context);
        }
        crewView_ = crew_.view(5, CrewStation::Weapons);
        crewView_.flightOpen = stage_ <= 4 || stage_ == 40;
        for (unsigned i = 0; i < maxWeaponMounts; ++i)
            if (!fire_.status()[i].empty())
                crewView_.gunStatus[i] = fire_.status()[i] + " / " + crewView_.gunStatus[i];
        renderer.setCrew(&crewView_);
        renderer.setEconomy(snapshot_, &market_);
    }
    void annotate(FlightInstruments& t) const {
        t.crewFire =
            "GUNNERS " + std::string(fireOrderName(fire_.order())) +
            ((stage_ <= 4 || stage_ == 40) ? " / CTRL6 HOLD 7 DEF 8 FOCUS" : " / 6 HOLD 7 DEF 8 FOCUS");
        if (stage_ == 2)
            t.systemTarget = "9 TARGET ENGINES / 0% / HULL REMAINS INTACT";
        if (stage_ == 3)
            t.systemTarget = "STBD GUN OFFLINE / CREW DAMAGE CONTROL";
        if (stage_ == 4 || stage_ == 40)
            t.systemTarget = "STBD GUN RESTORED / SPARES CONSUMED";
        if (stage_ == 5 || stage_ == 6)
            t.recoveryStatus = recovery_.status();
        if (stage_ == 7)
            t.recoveryStatus = "22KG RECOVERED / SURVIVOR ABOARD / F2 SERVICES";
    }
    void observe(Renderer& renderer, const std::string& directory) {
        auto capture = [&](const char* name) {
            renderer.screenshot((std::filesystem::path(directory) / (std::string(name) + ".ppm")).string());
            std::cout << "COMBAT_OPS_CAPTURE " << name << '\n';
        };
        if (stage_ == 0 && age_ == 40)
            capture("ops-hold-fire");
        else if (stage_ == 0 && age_ > 44) {
            fire_.order(FireOrder::Defend);
            next(1);
        } else if (stage_ == 1 && age_ == 100) {
            expect(weapons_.mount(1).shotsFired + weapons_.mount(2).shotsFired >= 2 &&
                       weapons_.mount(0).shotsFired == 0,
                   "Crew fire failed or used captain's gun");
            capture("ops-autonomous-gunners");
        } else if (stage_ == 1 && age_ > 104)
            next(2);
        else if (stage_ == 2 && age_ == 20)
            capture("ops-targeted-engines");
        else if (stage_ == 2 && age_ > 24)
            next(3);
        else if (stage_ == 3 && age_ == 10)
            capture("ops-component-damage");
        else if (stage_ == 3 && age_ > 14)
            next(4);
        else if (stage_ == 4 && crew_.systemHealth(ShipSystem::StarboardGun) >= 99.99 && age_ > 30) {
            capture("ops-crew-repaired");
            next(40);
        } else if (stage_ == 40 && age_ > 5)
            next(5);
        else if (stage_ == 5 && age_ == 20)
            capture("ops-wreck-approach");
        else if (stage_ == 5 && age_ > 24)
            next(6);
        else if (stage_ == 6 && age_ == 25)
            capture("ops-recovery-progress");
        else if (stage_ == 7 && age_ == 20)
            capture("ops-salvage-rescued");
        else if (stage_ == 7 && age_ > 24)
            next(8);
        else if (stage_ == 8 && age_ == 20)
            capture("ops-rescue-port");
        else if (stage_ == 8 && age_ > 24)
            next(9);
        else if (stage_ == 9 && age_ == 20)
            capture("ops-rescue-crew-office");
        else if (stage_ == 9 && age_ > 28) {
            std::cout << "PASS: native autonomous guns, targeted engines, crew repairs, physical salvage, "
                         "rescued identity and port handover\n";
            finished_ = true;
        }
        expect(age_ < 1200, "Native combat operations stalled");
    }
    bool finished() const {
        return finished_;
    }
};
} // namespace infinite
