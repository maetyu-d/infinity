#pragma once
#include "SurfaceLife.hpp"
#include <tuple>

namespace infinite {
// Read-only sampling of the existing weather, for GPU optical presentation.
// R=cloud, G=storm, B=precipitation; no new procedural random stream.
struct WeatherField {
    static constexpr unsigned width = 128, height = 64;
    static constexpr size_t byteBudget = 43692;
    Address planet;
    double seconds = 0;
    std::vector<uint8_t> pixels; // Complete RGBA8 mip chain; U wraps, V clamps.
};
WeatherField bakeWeatherField(const Node&, double seconds, const std::atomic<bool>* cancel = nullptr);
glm::ivec2 cloudBufferSize(int framebufferWidth, int framebufferHeight);
struct WeatherFieldStats {
    size_t entries = 0, bytes = 0, pending = 0, cancelled = 0, evicted = 0;
};
class WeatherFieldCache {
  public:
    static constexpr size_t capacity = 4, maxPending = 2;
    ~WeatherFieldCache();
    void beginFrame();
    std::shared_ptr<const WeatherField> request(const Node&, double snapshotSeconds);
    WeatherFieldStats stats() const;
    void wait();

  private:
    struct Entry {
        std::shared_ptr<const WeatherField> field;
        std::shared_ptr<std::atomic<bool>> cancel;
        uint64_t used = 0;
    };
    mutable std::mutex mutex_;
    std::map<std::tuple<Address, uint64_t, double>, Entry> entries_;
    WeatherFieldStats stats_;
    uint64_t frame_ = 0;
    oneapi::tbb::task_group tasks_;
};
} // namespace infinite
