#pragma once
#include "infinity/Armament.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace infinity {
class CutterAudio {
  public:
    explicit CutterAudio(bool enabled) {
        if (!enabled || !SDL_InitSubSystem(SDL_INIT_AUDIO))
            return;
        initialized_ = true;
        const SDL_AudioSpec format{SDL_AUDIO_F32, 1, sampleRate};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, fill, this);
        if (stream_ && !SDL_ResumeAudioStreamDevice(stream_)) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
    }
    ~CutterAudio() {
        if (stream_)
            SDL_DestroyAudioStream(stream_);
        if (initialized_)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    void update(double drive, double strain, bool warning, bool fall) {
        if (!stream_)
            return;
        drive_.store(float(std::clamp(drive, 0., 1.)), std::memory_order_relaxed);
        strain_.store(float(std::clamp(strain, 0., 1.)), std::memory_order_relaxed);
        if (warning)
            warning_.store(true, std::memory_order_relaxed);
        if (fall)
            fall_.store(true, std::memory_order_relaxed);
    }

    void fire(unsigned level) {
        cue(level == missileLauncher ? 2 : level == ionDisruptor ? 1 : level == pointDefence ? 3 : 0);
    }
    void impact(bool heavy) { cue(heavy ? 5 : 4); }
    void arrival() { arrivalCue_.store(true, std::memory_order_relaxed); }

    void updateWarp(bool engaged, double speed, bool focused) {
        if (!stream_)
            return;
        if (focused && engaged != warpEngaged_)
            warpCue_.store(engaged ? 1 : -1, std::memory_order_relaxed);
        warpEngaged_ = engaged;
        const double intensity = std::clamp((std::log10(std::max(1., speed)) - 3.7) / 3.6, 0., 1.);
        warpDrive_.store(focused && engaged ? float(.16 + .84 * intensity) : 0.f,
                         std::memory_order_relaxed);
    }

  private:
    static constexpr int sampleRate = 48000;
    static constexpr std::array<int, 6> duration{4800, 8400, 12500, 2400, 4000, 14500};
    void cue(unsigned index) {
        if (stream_)
            weaponCues_.fetch_or(1u << index, std::memory_order_relaxed);
    }
    static void SDLCALL fill(void* context, SDL_AudioStream* stream, int bytes, int) {
        auto& audio = *static_cast<CutterAudio*>(context);
        int samples = (std::clamp(bytes, 0, 32768) + sizeof(float) - 1) / sizeof(float);
        const float drive = audio.drive_.load(std::memory_order_relaxed);
        const float strain = audio.strain_.load(std::memory_order_relaxed);
        if (audio.warning_.exchange(false, std::memory_order_relaxed))
            audio.alertSamples_ = sampleRate / 3;
        if (audio.fall_.exchange(false, std::memory_order_relaxed))
            audio.crack_ = 1.;
        const unsigned cues = audio.weaponCues_.exchange(0, std::memory_order_relaxed);
        if (audio.arrivalCue_.exchange(false, std::memory_order_relaxed))
            audio.arrivalAge_ = 0;
        for (unsigned i = 0; i < duration.size(); ++i)
            if (cues & (1u << i))
                audio.weaponAge_[i] = 0;
        if (const int event = audio.warpCue_.exchange(0, std::memory_order_relaxed)) {
            audio.warpDirection_ = event;
            audio.warpAge_ = 0;
        }
        const float warp = audio.warpDrive_.load(std::memory_order_relaxed);
        std::array<float, 256> buffer;
        while (samples > 0) {
            const int count = std::min(samples, int(buffer.size()));
            for (int i = 0; i < count; ++i)
                buffer[i] = audio.sample(drive, strain, warp);
            if (!SDL_PutAudioStreamData(stream, buffer.data(), count * sizeof(float)))
                return;
            samples -= count;
        }
    }
    float sample(float drive, float strain, float warp) {
        constexpr double tau = 2 * std::numbers::pi;
        level_ += (drive - level_) * .002;
        pitch_ += (125 + 55 * drive + 230 * strain - pitch_) * .001;
        phase_ += pitch_ / sampleRate;
        phase_ -= std::floor(phase_);
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        const double noise = double(noise_) / UINT32_MAX * 2 - 1;
        crack_ *= .99976;
        double value = level_ * (.064 * std::sin(tau * phase_) + .029 * std::sin(tau * phase_ * 3) +
                                 (.014 + .034 * strain) * noise) + .21 * crack_ * noise;
        if (alertSamples_ > 0) {
            const int age = sampleRate / 3 - alertSamples_--;
            const int pulse = age % (sampleRate / 6);
            const double envelope = std::clamp(std::min(pulse, sampleRate / 10 - pulse) / 240., 0., 1.);
            value += .11 * envelope * std::sin(tau * age * 630 / sampleRate);
        }
        constexpr std::array<double, 6> frequency{310, 580, 92, 760, 190, 57};
        constexpr std::array<double, 6> pitchDrop{155, 450, 48, 300, 105, 34};
        constexpr std::array<double, 6> volume{.17, .15, .22, .11, .12, .22};
        for (unsigned i = 0; i < duration.size(); ++i) {
            if (weaponAge_[i] >= duration[i])
                continue;
            const double age = weaponAge_[i]++;
            const double t = age / sampleRate, progress = age / duration[i];
            const double phase = tau * t * (frequency[i] - .5 * pitchDrop[i] * progress);
            const double remaining = 1 - progress;
            const double envelope = std::min(1., age / 48.) * remaining * remaining;
            const double texture = i == 1 ? .2 : i >= 2 ? .65 : .38;
            const double tone = .75 * std::sin(phase) + .25 * std::sin(phase * 2.);
            value += volume[i] * envelope * ((1 - texture) * tone + texture * noise);
        }
        warpLevel_ += (warp - warpLevel_) * .00018;
        if (warpLevel_ > .0001) {
            warpPhase_ += (42. + 35. * warpLevel_) / sampleRate;
            warpPhase_ -= std::floor(warpPhase_);
            value += warpLevel_ * (.058 * std::sin(tau * warpPhase_) +
                                  .024 * std::sin(tau * warpPhase_ * 3.) + .010 * noise);
        }
        if (warpAge_ < warpCueSamples) {
            const double t = double(warpAge_++) / sampleRate;
            const double age = t * sampleRate / warpCueSamples;
            const double frequency = warpDirection_ > 0 ? 57. + 90. * age : 225. - 90. * age;
            const double phase = tau * t * frequency;
            const double envelope = std::min(1., age * 18.) * (1. - age) * (1. - age);
            value += .14 * envelope * (.70 * std::sin(phase) +
                                      .22 * std::sin(phase * 2.7) + .08 * noise);
        }
        if (arrivalAge_ < sampleRate) {
            const double t = double(arrivalAge_++) / sampleRate;
            const double age = std::fmod(t, .5);
            const double envelope = std::min(1., age * 60) * std::exp(-age * 12);
            value += .07 * envelope * std::sin(tau * (t < .5 ? 660. : 990.) * age);
        }
        return float(std::clamp(value, -.85, .85));
    }
    SDL_AudioStream* stream_ = nullptr;
    bool initialized_ = false;
    std::atomic<float> drive_{0}, strain_{0};
    std::atomic<bool> warning_{false}, fall_{false};
    std::atomic<unsigned> weaponCues_{0};
    std::atomic<bool> arrivalCue_{false};
    int arrivalAge_ = sampleRate;
    std::array<int, 6> weaponAge_ = duration;
    double phase_ = 0, level_ = 0, pitch_ = 125, crack_ = 0;
    uint32_t noise_ = 174713;
    int alertSamples_ = 0;
    static constexpr int warpCueSamples = sampleRate * 4 / 5;
    std::atomic<float> warpDrive_{0};
    std::atomic<int> warpCue_{0};
    bool warpEngaged_ = false;
    double warpLevel_ = 0, warpPhase_ = 0;
    int warpDirection_ = 1, warpAge_ = warpCueSamples;
};
} // namespace infinity
