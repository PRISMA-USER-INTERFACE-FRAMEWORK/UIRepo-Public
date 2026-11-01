#include "uirepo/Animation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace uirepo::anim {
namespace {

[[nodiscard]] float normalizedUnit(double value) noexcept {
    if (std::isnan(value) || value <= 0.0) {
        return 0.0f;
    }
    if (value >= 1.0) {
        return 1.0f;
    }
    return static_cast<float>(value);
}

[[nodiscard]] float finiteNonNegativeOrZero(float value) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

} // namespace

float ease(Easing easing, float t) noexcept {
    t = normalizedUnit(t);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseInQuad:
        return t * t;
    case Easing::EaseOutQuad:
        // 1 - (1 - t)^2, expanded.
        return t * (2.0f - t);
    case Easing::EaseInOutQuad:
        return t < 0.5f ? 2.0f * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case Easing::EaseInCubic:
        return t * t * t;
    case Easing::EaseOutCubic: {
        const float u = 1.0f - t;
        return 1.0f - u * u * u;
    }
    case Easing::EaseInOutCubic:
        return t < 0.5f ? 4.0f * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }
    return t;
}

float lerp(float from, float to, float t) noexcept {
    t = normalizedUnit(t);
    return from + (to - from) * t;
}

Point lerp(Point from, Point to, float t) noexcept {
    return {lerp(from.x, to.x, t), lerp(from.y, to.y, t)};
}

Size lerp(Size from, Size to, float t) noexcept {
    return {lerp(from.width, to.width, t), lerp(from.height, to.height, t)};
}

Color lerp(Color from, Color to, float t) noexcept {
    const auto channel = [t](std::uint8_t a, std::uint8_t b) noexcept -> std::uint8_t {
        const float mixed = lerp(static_cast<float>(a), static_cast<float>(b), t);
        const float rounded = std::clamp(std::round(mixed), 0.0f, 255.0f);
        return static_cast<std::uint8_t>(rounded);
    };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b),
            channel(from.a, to.a)};
}

Playback::Playback(float duration, Easing easing, Repeat repeat, float delay) noexcept
    : duration_(finiteNonNegativeOrZero(duration)),
      delay_(finiteNonNegativeOrZero(delay)),
      easing_(easing),
      repeat_(repeat) {}

void Playback::advance(float dt) noexcept {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }

    const double next = static_cast<double>(elapsed_) + static_cast<double>(dt);
    elapsed_ = next >= static_cast<double>(std::numeric_limits<float>::max())
        ? std::numeric_limits<float>::max()
        : static_cast<float>(next);
}

float Playback::progress() const noexcept {
    if (elapsed_ <= delay_) {
        return 0.0f;
    }

    const double local = static_cast<double>(elapsed_) - static_cast<double>(delay_);
    const double duration = static_cast<double>(duration_);
    if (duration <= 0.0) {
        return 1.0f;
    }

    switch (repeat_) {
    case Repeat::Once:
        return normalizedUnit(local / duration);
    case Repeat::Loop: {
        const double phase = std::fmod(local, duration) / duration;
        return normalizedUnit(phase);
    }
    case Repeat::PingPong: {
        const double phase = std::fmod(local, duration * 2.0) / duration;
        return phase <= 1.0 ? normalizedUnit(phase) : normalizedUnit(2.0 - phase);
    }
    }
    return 0.0f;
}

bool Playback::finished() const noexcept {
    if (repeat_ != Repeat::Once) {
        return false;
    }
    if (duration_ <= 0.0f) {
        return elapsed_ > delay_;
    }
    return (elapsed_ - delay_) >= duration_;
}

} // namespace uirepo::anim
