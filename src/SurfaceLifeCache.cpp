#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <chrono>

namespace infinite {
SurfaceLifeCache::SurfaceLifeCache(const Universe& universe, size_t budget)
    : universe_(universe), budget_(std::max(size_t(1024), budget)) {}
SurfaceLifeCache::~SurfaceLifeCache() {
    {
        std::lock_guard lock(mutex_);
        for (auto& [key, entry] : entries_)
            if (entry.cancel)
                entry.cancel->store(true);
    }
    tasks_.wait();
}
void SurfaceLifeCache::beginFrame() {
    std::lock_guard lock(mutex_);
    ++frame_;
}
std::shared_ptr<const SurfaceWorld> SurfaceLifeCache::request(const Node& planet) {
    std::lock_guard lock(mutex_);
    auto found = entries_.find(planet.address);
    if (found != entries_.end()) {
        found->second.used = frame_;
        return found->second.world;
    }
    if (stats_.pending >= 2 || planet.address.kind() != Kind::Planet || !hasSolidSurface(planet))
        return {};
    auto token = std::make_shared<std::atomic<bool>>(false);
    entries_.emplace(planet.address, Entry{{}, token, frame_});
    ++stats_.pending;
    tasks_.run([this, planet, token] {
        std::shared_ptr<const SurfaceWorld> world;
        std::string error;
        const auto begin = std::chrono::steady_clock::now();
        try {
            world = generateSurfaceLife(universe_, planet, token.get());
        } catch (const std::exception& e) {
            error = e.what();
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        std::lock_guard lock(mutex_);
        --stats_.pending;
        auto it = entries_.find(planet.address);
        if (it == entries_.end() || it->second.cancel != token)
            return;
        if (token->load() || !world || world->residentBytes() > budget_) {
            ++stats_.cancelled;
            entries_.erase(it);
        } else {
            it->second.world = std::move(world);
            it->second.cancel.reset();
            ++stats_.completed;
            stats_.lastMs = ms;
            stats_.bytes += it->second.world->residentBytes();
        }
        if (!error.empty())
            stats_.error = std::move(error);
        // Publication also enforces the cap, even when no further frames run.
        while (stats_.bytes > budget_) {
            auto oldest = entries_.end();
            for (auto candidate = entries_.begin(); candidate != entries_.end(); ++candidate)
                if (candidate->second.world &&
                    (oldest == entries_.end() || candidate->second.used < oldest->second.used))
                    oldest = candidate;
            if (oldest == entries_.end())
                break;
            stats_.bytes -= oldest->second.world->residentBytes();
            entries_.erase(oldest);
            ++stats_.evicted;
        }
    });
    return {};
}
std::shared_ptr<const SurfaceWorld> SurfaceLifeCache::find(const Address& planet) const {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(planet);
    return it == entries_.end() ? nullptr : it->second.world;
}
void SurfaceLifeCache::finishFrame() {
    std::lock_guard lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (frame_ - it->second.used > 8 && it->second.cancel) {
            it->second.cancel->store(true);
            ++it;
        } else if (frame_ - it->second.used > 300 && it->second.world) {
            stats_.bytes -= it->second.world->residentBytes();
            it = entries_.erase(it);
            ++stats_.evicted;
        } else
            ++it;
    }
}
void SurfaceLifeCache::wait() {
    tasks_.wait();
}
SurfaceCacheStats SurfaceLifeCache::stats() const {
    std::lock_guard lock(mutex_);
    auto stats = stats_;
    stats.entries = entries_.size();
    return stats;
}
} // namespace infinite
