#include "infinity/World.hpp"
namespace infinity {
std::shared_ptr<const std::vector<ProspectSite>> RegionCache::prospects(const Node& planet, Tile region) {
    if (!hasSolidSurface(planet))
        return {};
    const auto key = planet.address.child(Kind::Surface, encodeTile(region));
    std::lock_guard lock(mutex_);
    if (auto found = prospects_.find(key); found != prospects_.end()) {
        found->second.used = frame_;
        return found->second.sites;
    }
    if (stats_.pending >= maxJobs_)
        return {};
    auto token = std::make_shared<std::atomic<bool>>(false);
    prospects_[key] = {{}, token, frame_, 0};
    ++stats_.pending;
    stats_.peakPending = std::max(stats_.peakPending, stats_.pending);
    tasks_.run([this, planet, region, key, token] {
        std::shared_ptr<std::vector<ProspectSite>> result;
        std::string error;
        try {
            result = std::make_shared<std::vector<ProspectSite>>(
                generateProspects(universe_, planet, region, token.get()));
        } catch (const std::exception& e) {
            error = e.what();
        }
        std::lock_guard guard(mutex_);
        --stats_.pending;
        auto found = prospects_.find(key);
        if (found == prospects_.end() || found->second.cancel != token)
            return;
        if (token->load() || !error.empty()) {
            if (token->load())
                ++stats_.cancelled;
            else
                stats_.error = error;
            prospects_.erase(found);
            return;
        }
        size_t bytes = sizeof(*result) + result->capacity() * sizeof(ProspectSite);
        for (const auto& s : *result)
            bytes += s.rocks.capacity() * sizeof(Node) + s.parentFrames.capacity() * sizeof(LocalFrame) +
                     s.name.capacity() +
                     (s.surface.path.capacity() + s.pool.path.capacity()) * sizeof(Segment);
        if (bytes > budget_) {
            prospects_.erase(found);
            return;
        }
        found->second.sites = std::move(result);
        found->second.cancel.reset();
        found->second.bytes = bytes;
        stats_.bytes += bytes;
        trim();
    });
    return {};
}
} // namespace infinity
