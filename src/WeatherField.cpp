#include "infinite/WeatherField.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinite {
glm::ivec2 cloudBufferSize(int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    const double scale = std::min({1. / 3., 960. / w, 600. / h});
    return {std::max(1, int(w * scale)), std::max(1, int(h * scale))};
}
WeatherField bakeWeatherField(const Node& planet, double seconds, const std::atomic<bool>* cancel) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::invalid_argument("Invalid optical weather time");
    WeatherField field;
    field.planet = planet.address;
    field.seconds = seconds;
    const auto climate = planetClimate(planet);
    if (!climate.solid || climate.pressureBar == 0)
        return field;
    field.pixels.reserve(WeatherField::byteBudget);
    for (unsigned y = 0; y < WeatherField::height; ++y) {
        if (cancel && cancel->load()) {
            field.pixels.clear();
            return field;
        }
        const double latitude = std::numbers::pi * (.5 - double(y) / (WeatherField::height - 1));
        for (unsigned x = 0; x < WeatherField::width; ++x) {
            const double longitude = 2 * std::numbers::pi * ((x + .5) / WeatherField::width - .5);
            const glm::dvec3 d =
                y == 0 ? glm::dvec3(0, 1, 0)
                : y + 1 == WeatherField::height
                    ? glm::dvec3(0, -1, 0)
                    : glm::dvec3(std::cos(latitude) * std::cos(longitude), std::sin(latitude),
                                 std::cos(latitude) * std::sin(longitude));
            const auto sample = sampleWeather(climate, d, 0, seconds);
            for (double value : {sample.cloud, sample.storm, sample.precipitation})
                field.pixels.push_back(uint8_t(std::lround(std::clamp(value, 0., 1.) * 255)));
            field.pixels.push_back(255);
        }
    }
    size_t previous = 0;
    for (unsigned w = WeatherField::width, h = WeatherField::height; w > 1 || h > 1;) {
        const unsigned nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
        const size_t begin = field.pixels.size();
        for (unsigned y = 0; y < nh; ++y)
            for (unsigned x = 0; x < nw; ++x)
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (unsigned dy = 0; dy < 2; ++dy)
                        for (unsigned dx = 0; dx < 2; ++dx)
                            sum +=
                                field.pixels[previous +
                                             (std::min(h - 1, y * 2 + dy) * w + std::min(w - 1, x * 2 + dx)) *
                                                 4 +
                                             c];
                    field.pixels.push_back(uint8_t((sum + 2) / 4));
                }
        previous = begin;
        w = nw;
        h = nh;
    }
    return field;
}
WeatherFieldCache::~WeatherFieldCache() {
    {
        std::lock_guard lock(mutex_);
        for (auto& [key, e] : entries_)
            if (e.cancel)
                e.cancel->store(true);
    }
    tasks_.wait();
}
void WeatherFieldCache::beginFrame() {
    std::lock_guard lock(mutex_);
    ++frame_;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.cancel && frame_ - it->second.used > 8)
            it->second.cancel->store(true);
        if (it->second.field && frame_ - it->second.used > 300) {
            stats_.bytes -= it->second.field->pixels.size();
            it = entries_.erase(it);
            ++stats_.evicted;
        } else
            ++it;
    }
}
std::shared_ptr<const WeatherField> WeatherFieldCache::request(const Node& planet, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::invalid_argument("Invalid optical weather time");
    if (planetClimate(planet).pressureBar == 0)
        return {};
    const auto key = std::make_tuple(planet.address, planet.seed, seconds);
    std::lock_guard lock(mutex_);
    if (auto it = entries_.find(key); it != entries_.end()) {
        it->second.used = frame_;
        return it->second.field;
    }
    if (stats_.pending >= maxPending)
        return {};
    if (entries_.size() >= capacity) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.used < frame_ &&
                (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            return {};
        if (oldest->second.cancel)
            oldest->second.cancel->store(true);
        if (oldest->second.field)
            stats_.bytes -= oldest->second.field->pixels.size();
        entries_.erase(oldest);
        ++stats_.evicted;
    }
    auto token = std::make_shared<std::atomic<bool>>(false);
    entries_.emplace(key, Entry{{}, token, frame_});
    ++stats_.pending;
    tasks_.run([this, key, planet, seconds, token] {
        std::shared_ptr<const WeatherField> field;
        try {
            auto value = bakeWeatherField(planet, seconds, token.get());
            if (!value.pixels.empty())
                field = std::make_shared<WeatherField>(std::move(value));
        } catch (...) {
            token->store(true);
        }
        std::lock_guard lock(mutex_);
        --stats_.pending;
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.cancel != token)
            return;
        if (token->load() || !field) {
            entries_.erase(it);
            ++stats_.cancelled;
            return;
        }
        it->second.field = std::move(field);
        it->second.cancel.reset();
        stats_.bytes += it->second.field->pixels.size();
    });
    return {};
}
WeatherFieldStats WeatherFieldCache::stats() const {
    std::lock_guard lock(mutex_);
    auto s = stats_;
    s.entries = entries_.size();
    return s;
}
void WeatherFieldCache::wait() {
    tasks_.wait();
}
} // namespace infinite
