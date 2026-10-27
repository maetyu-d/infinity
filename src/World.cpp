#include "infinite/World.hpp"
#include <algorithm>
#include <sqlite3.h>
#include <stdexcept>
#include <zstd.h>

namespace infinite {
RegionCache::RegionCache(const Universe& u, size_t budget, size_t jobs)
    : universe_(u), budget_(budget), maxJobs_(std::max(size_t(1), jobs)) {}
RegionCache::~RegionCache() {
    {
        std::lock_guard lock(mutex_);
        for (auto& [key, e] : entries_)
            if (e.cancel)
                e.cancel->store(true);
        for (auto& [key, e] : catalogs_)
            if (e.cancel)
                e.cancel->store(true);
    }
    tasks_.wait();
}
void RegionCache::beginFrame() {
    std::lock_guard lock(mutex_);
    ++frame_;
}
std::shared_ptr<const Geometry> RegionCache::request(const Node& n, unsigned lod, unsigned priority,
                                                     unsigned view) {
    lod = geometryDetail(n.address.kind(), lod);
    auto make = [&, this](unsigned level, unsigned p) {
        return schedule(
            n, n.address.str() + "@", static_cast<int>(view * 8 + level),
            [this, n, level, view](const std::atomic<bool>* cancel) {
                return GeometryGenerator(universe_).generate(n, level, cancel, view);
            },
            [lod, view](const Geometry& g) {
                return std::abs(double(g.detail) - lod) + (g.viewKey == view ? 0 : 8);
            },
            p);
    };
    auto result = make(lod, priority + 1);
    if (!result && lod > 0)
        result = make(0, priority);
    return result;
}
std::shared_ptr<const Geometry> RegionCache::requestSection(const Node& n, const Address& anchor,
                                                            int bucket) {
    if (!n.address.ancestorOf(anchor) || n.address == anchor)
        throw std::invalid_argument("Section must refine its parent");
    return schedule(
        n, n.address.str() + "|" + anchor.str() + "@", bucket,
        [this, n, anchor, bucket](const std::atomic<bool>* cancel) {
            return GeometryGenerator(universe_).section(n, anchor, bucket, cancel);
        },
        [bucket](const Geometry& g) { return std::abs(g.logUnits - bucket * .5); }, 2);
}
std::shared_ptr<const Geometry>
RegionCache::schedule(const Node& n, const std::string& prefix, int variant,
                      std::function<std::shared_ptr<Geometry>(const std::atomic<bool>*)> generate,
                      std::function<double(const Geometry&)> score, unsigned priority) {
    if (n.seed != seedOf(n.address, universe_.seed()))
        throw std::invalid_argument("Foreign universe node in cache");
    std::lock_guard lock(mutex_);
    std::string key = prefix + std::to_string(variant);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second.used = frame_;
        it->second.priority = priority;
        if (it->second.geometry) {
            ++stats_.hits;
            return it->second.geometry;
        }
    } else {
        // Bounded demand queue; focus requests displace lower-priority queued work.
        if (entries_.size() >= 96) {
            auto victim = entries_.end();
            for (auto i = entries_.begin(); i != entries_.end(); ++i)
                if (!i->second.running &&
                    (victim == entries_.end() || i->second.used < victim->second.used ||
                     (i->second.used == victim->second.used && i->second.priority > victim->second.priority)))
                    victim = i;
            if (victim != entries_.end()) {
                if (victim->second.geometry)
                    stats_.bytes -= victim->second.geometry->residentBytes();
                if (victim->second.cancel)
                    victim->second.cancel->store(true);
                entries_.erase(victim);
                ++stats_.evicted;
            }
        }
        if (entries_.size() < 96) {
            Entry e;
            e.cancel = std::make_shared<std::atomic<bool>>(false);
            e.used = frame_;
            e.priority = priority;
            e.prefix = prefix;
            e.variant = variant;
            e.generate = std::move(generate);
            entries_.emplace(key, std::move(e));
            ++stats_.misses;
        }
    }
    std::shared_ptr<const Geometry> fallback;
    double best = 1e9;
    for (auto& [k, e] : entries_)
        if (e.prefix == prefix && e.geometry) {
            double delta = score(*e.geometry);
            if (delta < best) {
                best = delta;
                fallback = e.geometry;
                e.used = frame_;
            }
        }
    return fallback;
}
void RegionCache::dispatch() {
    // Called under the mutex. Submission never waits for worker completion.
    while (stats_.pending < maxJobs_) {
        auto best = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            auto& e = it->second;
            if (e.running || e.geometry || !e.generate || e.cancel->load())
                continue;
            if (best == entries_.end() || e.priority < best->second.priority ||
                (e.priority == best->second.priority && e.used > best->second.used))
                best = it;
        }
        if (best == entries_.end())
            break;
        auto key = best->first;
        auto token = best->second.cancel;
        auto generate = best->second.generate;
        best->second.generate = {};
        best->second.running = true;
        ++stats_.pending;
        stats_.peakPending = std::max(stats_.peakPending, stats_.pending);
        tasks_.run([this, key, token, generate = std::move(generate)] {
            std::shared_ptr<Geometry> geometry;
            std::string error;
            try {
                if (!token->load())
                    geometry = generate(token.get());
            } catch (const std::exception& e) {
                error = e.what();
            }
            std::lock_guard guard(mutex_);
            --stats_.pending;
            auto found = entries_.find(key);
            if (found == entries_.end() || found->second.cancel != token)
                return;
            found->second.running = false;
            if (!error.empty() && !token->load()) {
                stats_.error = error;
                entries_.erase(found);
                return;
            }
            if (!geometry || token->load() || geometry->residentBytes() > budget_) {
                ++stats_.cancelled;
                entries_.erase(found);
                return;
            }
            stats_.bytes += geometry->residentBytes();
            stats_.lastMs = geometry->generationMs;
            ++stats_.completed;
            found->second.geometry = std::move(geometry);
            found->second.cancel.reset();
            trim();
        });
    }
    stats_.queued = 0;
    for (auto& [key, e] : entries_)
        stats_.queued += !e.running && !e.geometry;
}
std::shared_ptr<const std::vector<Node>> RegionCache::catalog(const Node& parent) {
    std::lock_guard lock(mutex_);
    auto it = catalogs_.find(parent.address);
    if (it != catalogs_.end()) {
        it->second.used = frame_;
        return it->second.nodes;
    }
    if (stats_.pending >= maxJobs_)
        return {};
    auto token = std::make_shared<std::atomic<bool>>(false);
    catalogs_[parent.address] = {{}, token, frame_, 0};
    ++stats_.pending;
    stats_.peakPending = std::max(stats_.peakPending, stats_.pending);
    tasks_.run([this, parent, token] {
        auto nodes = std::make_shared<std::vector<Node>>();
        std::string error;
        try {
            for (auto a : universe_.children(parent)) {
                if (token->load())
                    break;
                nodes->push_back(universe_.describe(a));
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
        std::lock_guard guard(mutex_);
        --stats_.pending;
        auto it = catalogs_.find(parent.address);
        if (it == catalogs_.end() || it->second.cancel != token)
            return;
        if (token->load()) {
            ++stats_.cancelled;
            catalogs_.erase(it);
            return;
        }
        if (!error.empty()) {
            stats_.error = error;
            catalogs_.erase(it);
            return;
        }
        size_t bytes = sizeof(*nodes) + nodes->capacity() * sizeof(Node);
        if (bytes > budget_) {
            catalogs_.erase(it);
            return;
        }
        it->second.nodes = nodes;
        it->second.cancel.reset();
        it->second.bytes = bytes;
        stats_.bytes += bytes;
        trim();
    });
    return {};
}
void RegionCache::trim() {
    while (stats_.bytes > budget_ || catalogs_.size() > 12) {
        auto g = entries_.end();
        auto c = catalogs_.end();
        for (auto i = entries_.begin(); i != entries_.end(); ++i)
            if (i->second.geometry && (g == entries_.end() || i->second.used < g->second.used))
                g = i;
        for (auto i = catalogs_.begin(); i != catalogs_.end(); ++i)
            if (i->second.nodes && (c == catalogs_.end() || i->second.used < c->second.used))
                c = i;
        if (c != catalogs_.end() &&
            (catalogs_.size() > 12 || g == entries_.end() || c->second.used < g->second.used)) {
            stats_.bytes -= c->second.bytes;
            catalogs_.erase(c);
            ++stats_.evicted;
        } else if (g != entries_.end()) {
            stats_.bytes -= g->second.geometry->residentBytes();
            entries_.erase(g);
            ++stats_.evicted;
        } else
            break;
    }
}
void RegionCache::finishFrame() {
    std::lock_guard lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& e = it->second;
        if (!e.geometry && frame_ > e.used + 1) {
            if (!e.cancel->exchange(true))
                ++stats_.superseded;
            if (!e.running) {
                it = entries_.erase(it);
                continue;
            }
        }
        ++it;
    }
    for (auto& [key, e] : catalogs_)
        if (e.cancel && frame_ > e.used + 1)
            e.cancel->store(true);
    trim();
    dispatch();
}
GenerationStats RegionCache::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}
void RegionCache::wait() {
    for (;;) {
        {
            std::lock_guard lock(mutex_);
            dispatch();
            if (!stats_.pending && !stats_.queued)
                break;
        }
        tasks_.wait();
    }
}
void RegionCache::clear() {
    {
        std::lock_guard lock(mutex_);
        for (auto& [key, e] : entries_)
            if (e.cancel)
                e.cancel->store(true);
        for (auto& [key, e] : catalogs_)
            if (e.cancel)
                e.cancel->store(true);
    }
    tasks_.wait();
    std::lock_guard lock(mutex_);
    entries_.clear();
    catalogs_.clear();
    stats_.bytes = stats_.queued = 0;
}

static void check(int result, sqlite3* db) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW)
        throw std::runtime_error(sqlite3_errmsg(db));
}
struct Statement {
    sqlite3_stmt* value = nullptr;
    sqlite3* db;
    Statement(sqlite3* d, const char* sql) : db(d) {
        check(sqlite3_prepare_v2(db, sql, -1, &value, nullptr), db);
    }
    ~Statement() {
        sqlite3_finalize(value);
    }
    void text(int i, std::string_view s) {
        check(sqlite3_bind_text(value, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT), db);
    }
    int step() {
        int r = sqlite3_step(value);
        check(r, db);
        return r;
    }
};
Persistence::Persistence(const std::string& file, uint64_t seed) : universe_(std::to_string(seed)) {
    int result = sqlite3_open_v2(file.c_str(), &db_,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        std::string error = db_ ? sqlite3_errmsg(db_) : "SQLite allocation failed";
        if (db_)
            sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db_, 2000);
    try {
        exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; CREATE TABLE IF NOT EXISTS "
             "modifications(universe TEXT NOT NULL,address TEXT NOT NULL,bytes INTEGER NOT NULL,payload BLOB "
             "NOT NULL,PRIMARY KEY(universe,address)); CREATE TABLE IF NOT EXISTS state(universe TEXT "
             "PRIMARY KEY,payload TEXT NOT NULL); PRAGMA user_version=1;");
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
Persistence::~Persistence() {
    if (db_)
        sqlite3_close(db_);
}
void Persistence::exec(const char* sql) {
    char* error = nullptr;
    int result = sqlite3_exec(db_, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
void Persistence::mark(const Address& a, std::string_view note) {
    if (note.size() > 1024 * 1024)
        throw std::invalid_argument("Marker exceeds 1 MiB");
    std::vector<char> compressed(ZSTD_compressBound(note.size()));
    size_t size = ZSTD_compress(compressed.data(), compressed.size(), note.data(), note.size(), 3);
    if (ZSTD_isError(size))
        throw std::runtime_error(ZSTD_getErrorName(size));
    Statement s(db_, "INSERT INTO modifications VALUES(?1,?2,?3,?4) ON CONFLICT(universe,address) DO UPDATE "
                     "SET bytes=excluded.bytes,payload=excluded.payload");
    s.text(1, universe_);
    s.text(2, a.str());
    check(sqlite3_bind_int64(s.value, 3, static_cast<int64_t>(note.size())), db_);
    check(sqlite3_bind_blob(s.value, 4, compressed.data(), static_cast<int>(size), SQLITE_TRANSIENT), db_);
    s.step();
}
std::optional<std::string> Persistence::marker(const Address& a) const {
    Statement s(db_, "SELECT bytes,payload FROM modifications WHERE universe=?1 AND address=?2");
    s.text(1, universe_);
    s.text(2, a.str());
    if (s.step() != SQLITE_ROW)
        return {};
    int64_t bytes = sqlite3_column_int64(s.value, 0);
    if (bytes < 0 || bytes > 1024 * 1024)
        throw std::runtime_error("Corrupt marker length");
    std::string out(static_cast<size_t>(bytes), '\0');
    size_t result = ZSTD_decompress(out.data(), out.size(), sqlite3_column_blob(s.value, 1),
                                    static_cast<size_t>(sqlite3_column_bytes(s.value, 1)));
    if (ZSTD_isError(result) || result != out.size())
        throw std::runtime_error("Corrupt marker payload");
    return out;
}
size_t Persistence::markerCount() const {
    Statement s(db_, "SELECT COUNT(*) FROM modifications WHERE universe=?1");
    s.text(1, universe_);
    s.step();
    return static_cast<size_t>(sqlite3_column_int64(s.value, 0));
}
void Persistence::saveState(std::string_view state) {
    Statement s(
        db_, "INSERT INTO state VALUES(?1,?2) ON CONFLICT(universe) DO UPDATE SET payload=excluded.payload");
    s.text(1, universe_);
    s.text(2, state);
    s.step();
}
std::optional<std::string> Persistence::loadState() const {
    Statement s(db_, "SELECT payload FROM state WHERE universe=?1");
    s.text(1, universe_);
    if (s.step() != SQLITE_ROW)
        return {};
    return std::string(reinterpret_cast<const char*>(sqlite3_column_text(s.value, 0)),
                       static_cast<size_t>(sqlite3_column_bytes(s.value, 0)));
}
} // namespace infinite
