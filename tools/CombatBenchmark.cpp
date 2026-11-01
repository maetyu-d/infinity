#include "infinite/FireControl.hpp"
#include "infinite/Recovery.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>

// Count allocations on the measured thread, excluding setup and CSV output.
thread_local bool measuring = false;
thread_local uint64_t allocations = 0;
void* operator new(std::size_t n) {
    if (measuring)
        ++allocations;
    if (auto p = std::malloc(std::max<size_t>(n, 1)))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}

using namespace infinite;
namespace {
ShipCrew staffed() {
    ShipCrew crew(52);
    crew.toggleAutomatic();
    crew.setWeaponLevels({1, 1, 1});
    crew.reconcile({2, 0, 0});
    crew.manWeapon(6, 1);
    crew.manWeapon(7, 2);
    for (unsigned i = 0; i < 100; ++i)
        crew.advance(.25);
    return crew;
}
template <class Setup, class Work> void measure(std::string_view name, Setup setup, Work work) {
    constexpr unsigned batches = 11, iterations = 60;
    std::vector<double> times;
    uint64_t count = 0, checksum = 0;
    for (unsigned batch = 0; batch < batches; ++batch) {
        setup();
        allocations = 0;
        const auto start = std::chrono::steady_clock::now();
        measuring = true;
        for (unsigned i = 0; i < iterations; ++i)
            checksum += work(i);
        measuring = false;
        const auto end = std::chrono::steady_clock::now();
        count += allocations;
        times.push_back(std::chrono::duration<double, std::micro>(end - start).count() / iterations);
    }
    std::sort(times.begin(), times.end());
    std::cout << name << ',' << std::fixed << std::setprecision(3) << times[batches / 2] << ','
              << times.back() << ',' << double(count) / (batches * iterations) << ',' << checksum << '\n';
}
} // namespace
int main() {
    Universe universe;
    const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    auto sample = sampleRaider(universe, home, 100);
    WeaponRay ray;
    ray.reference = home;
    ray.origin = sample.position + glm::dvec3(1800, 0, 0);
    ray.direction = {-1, 0, 0};
    ray.seconds = 100;
    std::vector<ActivityBody> bodies(128, sample);
    for (unsigned i = 0; i < bodies.size(); ++i) {
        bodies[i].id.slot += i;
        bodies[i].position = sample.position + glm::dvec3(-8000 - i * 10, 0, 0);
        bodies[i].economicBrief = std::string(80, 'B');
        bodies[i].economicReport.fill(std::string(80, 'R'));
    }
    bodies.front() = sample;
    auto crew = staffed();
    WeaponSystem guns;
    FireControl control;
    auto setup = [&](FireOrder order, unsigned history) {
        guns = WeaponSystem{};
        guns.setLoadout({1, 1, 1});
        StationControl{}.prepareWeapons(guns, crew);
        for (unsigned i = 0; i < history; ++i) {
            auto old = sample;
            old.kind = ActivityKind::Ferry;
            old.id.slot = 10000 + i;
            guns.receiveNpcDamage(old, 1, 100);
        }
        control = FireControl{};
        control.order(order);
    };
    std::cout << "scenario,median_us_per_update,worst_batch_us_per_update,allocations_per_update,checksum\n";
    measure(
        "hold_128_contacts_2048_receipts", [&] { setup(FireOrder::Hold, 2048); },
        [&](unsigned) {
            return control.update(universe, ray, {1, 0, 0, 0}, sample.id, 0, crew, guns, bodies, 1. / 60,
                                  false);
        });
    measure(
        "defend_128_contacts_128_receipts", [&] { setup(FireOrder::Defend, 128); },
        [&](unsigned) {
            guns.advance(1. / 60);
            return control.update(universe, ray, {1, 0, 0, 0}, sample.id, 0, crew, guns, bodies, 1. / 60,
                                  false);
        });
    measure(
        "focus_128_contacts_128_receipts", [&] { setup(FireOrder::Focus, 128); },
        [&](unsigned) {
            guns.advance(1. / 60);
            return control.update(universe, ray, {1, 0, 0, 0}, sample.id, 0, crew, guns, bodies, 1. / 60,
                                  false);
        });
    measure(
        "weapon_trace_128_contacts", [] {},
        [&](unsigned) {
            const auto trace = traceWeapon(universe, ray, 3000, bodies);
            return trace.body ? trace.body->id.slot : 0;
        });
    auto wreck = sample;
    wreck.disabled = true;
    wreck.velocity = {};
    NpcPlayer player;
    player.reference = home;
    player.position = wreck.position + glm::dvec3(90, 0, 0);
    RecoveryController recovery;
    measure(
        "recovery_idle_near_wreck", [&] { recovery = RecoveryController{}; },
        [&](unsigned) { return recovery.update(universe, player, wreck, 1. / 60, false); });
}
