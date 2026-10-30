#include "infinite/Economy.hpp"
#include "infinite/Piloting.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sqlite3.h>
#include <thread>
namespace infinite {
namespace {
void sql(sqlite3* db, const char* query) {
    char* err = nullptr;
    if (sqlite3_exec(db, query, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string message = err ? err : "Economy storage failure";
        sqlite3_free(err);
        throw std::runtime_error(message);
    }
}
struct Statement {
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db, const char* s) {
        if (sqlite3_prepare_v2(db, s, -1, &value, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() {
        sqlite3_finalize(value);
    }
};
} // namespace
ActivityBody sampleEconomicFreight(const Universe& u, const EconomicNetwork& n, const EconomicFlight& f,
                                   double seconds) {
    const auto home = u.describe(f.id.home);
    auto b = sampleActivity(home, f.id.slot, seconds);
    const double duration = std::max(1., f.endSeconds - f.startSeconds),
                 t = std::clamp((seconds - f.startSeconds) / duration, 0., 1.);

    auto ease = [](double x) {
        x = std::clamp(x, 0., 1.);
        return x * x * (3 - 2 * x);
    };
    auto arc = [](glm::dvec3 a, glm::dvec3 b, double x) {
        const double cosine = std::clamp(glm::dot(a, b), -1., 1.), angle = std::acos(cosine);
        auto tangent = b - a * cosine;
        if (glm::length(tangent) < 1e-10)
            tangent = glm::cross(a, std::abs(a.y) < .9 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0));
        return a * std::cos(angle * x) + glm::normalize(tangent) * std::sin(angle * x);
    };
    auto path = [&](double x) {
        auto source = f.from - f.sourceCentre, destination = f.to - f.destinationCentre;
        const double sr = std::max(glm::length(source), f.sourceRadius * 1.35),
                     dr = std::max(glm::length(destination), f.destinationRadius * 1.35);
        auto sd = glm::normalize(source), dd = glm::normalize(destination);
        if (glm::length(f.sourceCentre - f.destinationCentre) < 1.) {
            const double radius = std::max(sr, dr);
            if (x < .2)
                return f.sourceCentre + sd * glm::mix(glm::length(source), radius, ease(x / .2));
            if (x < .8)
                return f.sourceCentre + arc(sd, dd, ease((x - .2) / .6)) * radius;
            return f.destinationCentre + dd * glm::mix(radius, glm::length(destination), ease((x - .8) / .2));
        }
        const auto course = glm::normalize(f.destinationCentre - f.sourceCentre);
        if (x < .1)
            return f.sourceCentre + sd * glm::mix(glm::length(source), sr, ease(x / .1));
        if (x < .25)
            return f.sourceCentre + arc(sd, course, ease((x - .1) / .15)) * sr;
        if (x > .9)
            return f.destinationCentre + dd * glm::mix(dr, glm::length(destination), ease((x - .9) / .1));
        if (x > .75)
            return f.destinationCentre + arc(-course, dd, ease((x - .75) / .15)) * dr;
        const auto a = f.sourceCentre + course * sr, b = f.destinationCentre - course * dr;
        const auto segment = b - a;
        const auto nearest =
            a +
            segment * std::clamp(-glm::dot(a, segment) / std::max(1., glm::dot(segment, segment)), 0., 1.);
        const double progress = ease((x - .25) / .5);
        if (glm::length(nearest) < f.stellarRadius * 1.5) {
            auto side = glm::cross(glm::normalize(segment), glm::dvec3(0, 1, 0));
            if (glm::length(side) < 1e-9)
                side = {1, 0, 0};
            const auto middle = glm::normalize(side) * f.stellarRadius * 3.;
            return progress < .5 ? glm::mix(a, middle, ease(progress * 2))
                                 : glm::mix(middle, b, ease(progress * 2 - 1));
        }
        return glm::mix(a, b, progress);
    };
    const auto point = path(t);
    const double left = std::max(0., t - 1e-5), right = std::min(1., t + 1e-5);
    const auto velocity = (path(right) - path(left)) / (std::max(1e-9, right - left) * duration);
    auto pose = u.relative(n.system, f.id.home, 0);
    b.position = pose.position + pose.orientation * point;
    b.velocity = pose.orientation * velocity;
    if (f.phase == FreightPhase::Idle || f.phase == FreightPhase::Loading ||
        f.phase == FreightPhase::Holding) {
        b.position = pose.position + pose.orientation * f.from;
        b.velocity = {};
    }
    if (glm::length(b.velocity) > 1e-9) {
        auto direction = glm::normalize(b.velocity), up = glm::normalize(b.position);
        if (std::abs(glm::dot(up, direction)) > .98)
            up = {0, 1, 0};
        if (std::abs(glm::dot(up, direction)) > .98)
            up = {1, 0, 0};
        b.orientation = glm::quatLookAtRH(direction, up);
    }
    b.status = f.phase == FreightPhase::Idle         ? "AWAITING FUNDED CARGO"
               : f.phase == FreightPhase::Reposition ? "EMPTY / TO LOADING BERTH"
               : f.phase == FreightPhase::Loading    ? "LOADING / RESERVED STOCK"
               : f.phase == FreightPhase::Holding
                   ? "HOLDING / WAREHOUSE FULL"
                   : "FREIGHT / " + std::to_string(f.kg) + " KG " + std::string(goodInfo(f.good).name);
    return b;
}
bool economicTradeAccess(const Universe& u, const Navigation& nav, const Pilot& pilot,
                         const EconomicSite& site) {
    if (pilot.ship().shrinking || pilot.ship().logSize < -.00001 || pilot.ship().cruise ||
        glm::length(pilot.ship().velocity) > 3)
        return false;
    const auto home = u.describe(site.id.home);
    const auto point = u.pointRelative(site.id.home, site.metres / std::pow(10., home.frame.logRadius),
                                       nav.anchor, nav.logDistance);
    const double distance = glm::length(point - nav.cameraPosition()) * std::pow(10., nav.logDistance);
    return site.orbital ? distance < site.radius * 5 + 250 : pilot.ship().landed && distance < 250;
}
struct EconomyRuntime::Impl {
    const Universe& u;
    std::filesystem::path path;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::optional<Address> requested;
    uint64_t target = 0, revision = 0, useSerial = 0;
    bool stop = false;
    std::atomic<size_t> workerBytes{0};
    std::atomic<bool> cancel{false};
    std::deque<EconomicCommand> commands;
    std::shared_ptr<const EconomySnapshot> published;
    std::map<Address, std::unique_ptr<Economy>> active;
    std::map<Address, uint64_t> used;
    EconomyPlayer player;
    sqlite3* db = nullptr;
    std::string message;
    Impl(const Universe& world, std::filesystem::path directory)
        : u(world), path(std::move(directory) / "economy.sqlite") {
        worker = std::thread([this] { run(); });
    }
    std::optional<std::string> read(const std::string& key) {
        Statement s(db, "SELECT payload FROM economic_state WHERE universe=? AND key=?");
        auto seed = std::to_string(u.seed());
        sqlite3_bind_text(s.value, 1, seed.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s.value, 2, key.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(s.value) == SQLITE_ROW)
            return std::string(reinterpret_cast<const char*>(sqlite3_column_text(s.value, 0)));
        return {};
    }
    void write(const std::string& key, const std::string& value) {
        Statement s(db, "INSERT OR REPLACE INTO economic_state(universe,key,payload) VALUES(?,?,?)");
        auto seed = std::to_string(u.seed());
        sqlite3_bind_text(s.value, 1, seed.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s.value, 2, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s.value, 3, value.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(s.value) != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    void save() {
        sql(db, "BEGIN IMMEDIATE");
        try {
            write("wallet", player.serialize());
            for (const auto& [a, e] : active)
                write(a.str(), e->serialize());
            sql(db, "COMMIT");
        } catch (...) {
            sql(db, "ROLLBACK");
            throw;
        }
    }
    void publish(Economy* e, uint64_t goal) {
        auto s = std::make_shared<EconomySnapshot>();
        s->player = player;
        s->message = message;
        if (e) {
            // Alias ownership is avoided: the definition already has immutable shared lifetime.
            s->network = definitions.at(e->network().system);
            s->sites = e->sites();
            s->contracts = e->contracts();
            s->events = e->events();
            s->tick = e->tick();
            s->catchingUp = e->tick() < goal;
            for (const auto& f : e->freight()) {
                EconomicFlight flight;
                flight.id = f.id;
                flight.good = f.good;
                flight.kg = (f.phase == FreightPhase::Transit || f.phase == FreightPhase::Holding) ? f.kg : 0;
                flight.phase = f.phase;
                auto from = f.phase == FreightPhase::Reposition ? f.location : f.source;
                auto to = f.destination;
                if (f.phase == FreightPhase::Reposition)
                    to = f.source;
                if (f.phase == FreightPhase::Idle)
                    from = to = f.location;
                if (f.phase == FreightPhase::Loading)
                    to = from = f.source;
                if (f.phase == FreightPhase::Holding)
                    to = from = f.destination;
                const auto& source = e->network().sites[from];
                const auto& destination = e->network().sites[to];
                auto berth = [&](const EconomicSite& site) {
                    auto radial = glm::normalize(site.metres);
                    auto along = glm::cross(radial, glm::dvec3(0, 1, 0));
                    if (glm::length(along) < .01)
                        along = glm::cross(radial, glm::dvec3(1, 0, 0));
                    along = glm::normalize(along);
                    auto across = glm::cross(radial, along);
                    const auto ordinal = f.id.slot - 100;
                    auto offset = site.orbital ? radial * (800. + (ordinal % 3) * 120.) +
                                                     along * (double(ordinal % 8) - 3.5) * 190. +
                                                     across * (320. + ordinal / 8 * 450.)
                                               : radial * 160.;
                    auto rotation = u.relative(site.id.home, e->network().system, 0).orientation;
                    return site.systemMetres + rotation * offset;
                };
                flight.from = berth(source);
                flight.to = berth(destination);
                flight.sourceCentre = u.relative(source.id.home, e->network().system, 0).position;
                flight.destinationCentre = u.relative(destination.id.home, e->network().system, 0).position;
                flight.sourceRadius = std::pow(10., u.describe(source.id.home).frame.logRadius);
                flight.destinationRadius = std::pow(10., u.describe(destination.id.home).frame.logRadius);
                flight.stellarRadius = u.describe(e->network().system).stellarRadiusMetres;
                flight.startSeconds = f.startTick * economyTickSeconds;
                flight.endSeconds = f.endTick * economyTickSeconds;
                s->flights.push_back(flight);
            }
        }
        size_t bytes = 0;
        for (const auto& [_, economy] : active)
            bytes += economy->residentBytes();
        workerBytes = bytes;
        std::lock_guard lock(mutex);
        published = std::move(s);
    }
    std::map<Address, std::shared_ptr<const EconomicNetwork>> definitions;
    void run() {
        try {
            if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK)
                throw std::runtime_error("Cannot open economic ledger");
            sqlite3_busy_timeout(db, 3000);
            sql(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; CREATE TABLE IF NOT EXISTS "
                    "economic_state(universe TEXT NOT NULL,key TEXT NOT NULL,payload TEXT NOT NULL,PRIMARY "
                    "KEY(universe,key));");
            if (auto text = read("wallet"); text && !player.restore(*text))
                throw std::runtime_error("ECONOMY SAVE INVALID / original ledger retained");
            publish(nullptr, 0);
            uint64_t handled = 0;
            bool catching = false;
            while (true) {
                std::optional<Address> system;
                uint64_t goal, seen;
                std::deque<EconomicCommand> orders;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return stop || revision != handled || catching; });
                    if (stop)
                        break;
                    system = requested;
                    goal = target;
                    seen = revision;
                    orders.swap(commands);
                    cancel = false;
                }
                if (!system) {
                    handled = seen;
                    catching = false;
                    continue;
                }
                if (!active.contains(*system)) {
                    auto definition = generateEconomicNetwork(u, *system, &cancel);
                    if (!definition) {
                        // Cancellation rejects queued orders explicitly. A lost acknowledgement
                        // would leave the helm's provisional cargo lock engaged indefinitely.
                        for (const auto& c : orders)
                            player.lastCommand = std::max(player.lastCommand, c.sequence);
                        message = "ORDER CANCELLED / SYSTEM CHANGED";
                        save();
                        publish(nullptr, 0);
                        handled = seen;
                        catching = false;
                        continue;
                    }
                    auto e = std::make_unique<Economy>(definition);
                    if (auto text = read(system->str()); text && !e->restore(*text))
                        throw std::runtime_error("ECONOMY NETWORK SAVE INVALID / original ledger retained");
                    active.emplace(*system, std::move(e));
                    definitions[*system] = definition;
                    if (active.size() > 3) {
                        save();
                        auto oldest = active.end();
                        for (auto it = active.begin(); it != active.end(); it++)
                            if (it->first != *system &&
                                (oldest == active.end() || used[it->first] < used[oldest->first]))
                                oldest = it;
                        if (oldest != active.end()) {
                            definitions.erase(oldest->first);
                            used.erase(oldest->first);
                            active.erase(oldest);
                        }
                    }
                }
                auto& economy = *active.at(*system);
                used[*system] = ++useSerial;
                economy.advanceTo(goal, 16);
                catching = economy.tick() < goal;
                if (!catching)
                    for (const auto& c : orders) {
                        if (economy.siteIndex(c.port))
                            message = economy.execute(c, player);
                        else {
                            message = "ORDER CANCELLED / MARKET CHANGED";
                            player.lastCommand = std::max(player.lastCommand, c.sequence);
                        }
                    }
                else if (!orders.empty()) {
                    message = "ECONOMY CATCHING UP / ORDER NOT SUBMITTED";
                    for (const auto& c : orders)
                        player.lastCommand = std::max(player.lastCommand, c.sequence);
                }
                save();
                publish(&economy, goal);
                handled = seen;
            }
            save();
        } catch (const std::exception& error) {
            message = error.what();
            std::lock_guard lock(mutex);
            // A failed disk transaction must not publish an uncommitted wallet.
            auto s = published ? std::make_shared<EconomySnapshot>(*published)
                               : std::make_shared<EconomySnapshot>();
            s->message = message;
            s->catchingUp = true;
            published = s;
        }
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }
};
EconomyRuntime::EconomyRuntime(const Universe& u, const std::filesystem::path& data)
    : impl_(std::make_unique<Impl>(u, data)) {}
EconomyRuntime::~EconomyRuntime() {
    flush();
}
void EconomyRuntime::request(const Address& a, double seconds) {
    if (a.kind() != Kind::System || !std::isfinite(seconds) || seconds < 0)
        return;
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    const auto tick = uint64_t(std::min(1e12, seconds / economyTickSeconds));
    if (p.requested == a && p.target == tick)
        return;
    if (p.requested != a)
        p.cancel = true;
    p.requested = a;
    p.target = tick;
    p.revision++;
    p.wake.notify_one();
}
bool EconomyRuntime::command(const EconomicCommand& c) {
    auto& p = *impl_;
    std::lock_guard lock(p.mutex);
    if (p.stop || p.commands.size() >= 16)
        return false;
    p.commands.push_back(c);
    p.revision++;
    p.wake.notify_one();
    return true;
}
std::shared_ptr<const EconomySnapshot> EconomyRuntime::snapshot() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->published;
}
void EconomyRuntime::flush() {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        p.stop = true;
        p.cancel = true;
        p.wake.notify_one();
    }
    if (p.worker.joinable())
        p.worker.join();
}
size_t EconomyRuntime::residentBytes() const {
    auto s = snapshot();
    if (!s)
        return 0;
    return impl_->workerBytes.load() + sizeof(*s) + s->sites.capacity() * sizeof(EconomicSiteState) +
           s->flights.capacity() * sizeof(EconomicFlight);
}
} // namespace infinite
