#include "infinite/SurfaceLife.hpp"
#include <algorithm>
#include <chrono>

namespace infinite {
SurfaceLifeSummary summarizeSurfaceLife(const SurfaceWorld& world) {
    SurfaceLifeSummary result;
    for (const auto& site : world.settlements) {
        result.population += site.population;
        result.ports += site.population > 0;
        result.openPorts += site.population > 0 && !site.sealed;
    }
    return result;
}
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
    return request(planet, false);
}
void SurfaceLifeCache::survey(const Node& planet) {
    request(planet, true);
}
std::optional<SurfaceLifeSummary> SurfaceLifeCache::summary(const Address& planet) const {
    std::lock_guard lock(mutex_);
    auto found = summaries_.find(planet);
    return found == summaries_.end() ? std::nullopt : std::optional(found->second.result);
}
std::shared_ptr<const SurfaceWorld> SurfaceLifeCache::request(const Node& planet, bool surveyOnly) {
    std::lock_guard lock(mutex_);
    auto found = entries_.find(planet.address);
    if (found != entries_.end()) {
        found->second.used = frame_;
        if (!surveyOnly)
            found->second.surveyOnly = false;
        return found->second.world;
    }
    if (surveyOnly && summaries_.contains(planet.address))
        return {};
    if (stats_.pending >= 2 || planet.address.kind() != Kind::Planet || !hasSolidSurface(planet))
        return {};
    auto token = std::make_shared<std::atomic<bool>>(false);
    entries_.emplace(planet.address, Entry{{}, token, frame_, surveyOnly});
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
        if (!token->load() && world) {
            summaries_.insert_or_assign(planet.address, SurveyEntry{summarizeSurfaceLife(*world), frame_});
            while (summaries_.size() > 256) {
                auto oldest =
                    std::min_element(summaries_.begin(), summaries_.end(), [](const auto& a, const auto& b) {
                        return a.second.used < b.second.used;
                    });
                summaries_.erase(oldest);
            }
        }
        if (token->load() || !world || (!it->second.surveyOnly && world->residentBytes() > budget_)) {
            ++stats_.cancelled;
            entries_.erase(it);
        } else if (it->second.surveyOnly) {
            entries_.erase(it);
            ++stats_.completed;
            stats_.lastMs = ms;
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
