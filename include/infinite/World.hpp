#pragma once
#include "Geometry.hpp"
#include <map>
#include <mutex>
#include <oneapi/tbb/task_group.h>
#include <set>
struct sqlite3;

namespace infinite {
struct GenerationStats {
    uint64_t hits = 0, misses = 0, completed = 0, cancelled = 0, evicted = 0, superseded = 0;
    size_t bytes = 0, pending = 0, queued = 0, peakPending = 0;
    double lastMs = 0;
    std::string error;
};
class RegionCache {
  public:
    explicit RegionCache(const Universe&, size_t budget = 32 * 1024 * 1024, size_t jobs = 3);
    ~RegionCache();
    void beginFrame();
    std::shared_ptr<const Geometry> request(const Node&, unsigned lod, unsigned priority = 0,
                                            unsigned viewKey = 0);
    std::shared_ptr<const std::vector<Node>> catalog(const Node&);
    std::shared_ptr<const Geometry> requestSection(const Node&, const Address& anchor, int halfDecade);
    std::shared_ptr<const Geometry> requestTerrain(const Node& planet, Tile, unsigned stitch);
    std::shared_ptr<const Geometry> requestCosmicBoundary(const Node& root);
    void finishFrame();
    GenerationStats stats() const;
    void clear();
    void wait();

  private:
    struct Entry {
        std::shared_ptr<Geometry> geometry;
        std::shared_ptr<std::atomic<bool>> cancel;
        uint64_t used = 0;
        bool running = false;
        unsigned priority = 0;
        std::string prefix;
        int variant = 0;
        std::function<std::shared_ptr<Geometry>(const std::atomic<bool>*)> generate;
    };
    struct CatalogEntry {
        std::shared_ptr<std::vector<Node>> nodes;
        std::shared_ptr<std::atomic<bool>> cancel;
        uint64_t used = 0;
        size_t bytes = 0;
    };
    std::map<Address, CatalogEntry> catalogs_;
    const Universe& universe_;
    size_t budget_, maxJobs_;
    uint64_t frame_ = 0;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    oneapi::tbb::task_group tasks_;
    GenerationStats stats_;
    void trim();
    void dispatch();
    std::shared_ptr<const Geometry>
    schedule(const Node&, const std::string& prefix, int variant,
             std::function<std::shared_ptr<Geometry>(const std::atomic<bool>*)>,
             std::function<double(const Geometry&)> score, unsigned priority);
};
class Persistence {
  public:
    Persistence(const std::string& filename, uint64_t universeSeed);
    ~Persistence();
    Persistence(const Persistence&) = delete;
    Persistence& operator=(const Persistence&) = delete;
    void mark(const Address&, std::string_view note);
    std::optional<std::string> marker(const Address&) const;
    size_t markerCount() const;
    void saveState(std::string_view);
    std::optional<std::string> loadState() const;

  private:
    sqlite3* db_ = nullptr;
    std::string universe_;
    void exec(const char*);
};
} // namespace infinite
