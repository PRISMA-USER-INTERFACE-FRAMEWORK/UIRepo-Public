#pragma once

// Portable, dependency-free animation primitives for the UIRepo retained core.
//
// This is deliberately a *value* system: it computes eased, interpolated values
// from an explicit delta-time clock. It does not own a wall clock, does not
// mutate nodes, and pulls in nothing beyond the C++ standard library and the
// core geometry types. That keeps it deterministic and unit-testable on the
// portable CI lane, and leaves node/style application (and data-binding) to the
// consumer or a later increment.

#include "uirepo/Core.h"

#include <cstdint>

namespace uirepo::anim {

enum class Easing : std::uint8_t {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
};

// Applies `easing` to normalized progress `t`. Finite and infinite values are
// clamped to [0, 1], while NaN is treated as the safe start value 0. The result
// is always finite and in [0, 1], with ease(_, 0) == 0 and ease(_, 1) == 1.
[[nodiscard]] float ease(Easing easing, float t) noexcept;

// Linear interpolation for the animatable core value types. `t` uses the same
// normalization as ease(): NaN -> 0, -infinity -> 0, +infinity -> 1, and finite
// values clamp to [0, 1]. Color channels are rounded to the nearest byte.
[[nodiscard]] float lerp(float from, float to, float t) noexcept;
[[nodiscard]] Point lerp(Point from, Point to, float t) noexcept;
[[nodiscard]] Size lerp(Size from, Size to, float t) noexcept;
[[nodiscard]] Color lerp(Color from, Color to, float t) noexcept;

enum class Repeat : std::uint8_t {
    Once,
    Loop,
    PingPong,
};

// Deterministic playback clock advanced by explicit delta-time (seconds). It is
// independent of any real wall clock, so animations are fully reproducible in
// tests. A non-positive or non-finite `duration` is normalized to zero; a Once
// playback then stays at progress 0 and unfinished until the first positive
// advance after its delay. A non-positive or non-finite `delay` is treated as zero.
class Playback {
public:
    explicit Playback(
        float duration,
        Easing easing = Easing::Linear,
        Repeat repeat = Repeat::Once,
        float delay = 0.0f) noexcept;

    // Advance the clock by `dt` seconds. Non-positive or non-finite `dt` is
    // ignored. Finite elapsed time saturates instead of overflowing to infinity.
    void advance(float dt) noexcept;
    void reset() noexcept { elapsed_ = 0.0f; }

    [[nodiscard]] float elapsed() const noexcept { return elapsed_; }
    [[nodiscard]] float duration() const noexcept { return duration_; }
    [[nodiscard]] float delay() const noexcept { return delay_; }
    [[nodiscard]] Easing easing() const noexcept { return easing_; }
    [[nodiscard]] Repeat repeat() const noexcept { return repeat_; }

    // Normalized, un-eased progress. Always finite and in [0, 1].
    [[nodiscard]] float progress() const noexcept;
    // Eased progress in [0, 1] (progress() passed through easing()).
    [[nodiscard]] float value() const noexcept { return ease(easing_, progress()); }
    // Only `Once` playbacks finish; `Loop`/`PingPong` run indefinitely.
    [[nodiscard]] bool finished() const noexcept;

private:
    float duration_;
    float delay_;
    Easing easing_;
    Repeat repeat_;
    float elapsed_{0.0f};
};

// A typed tween interpolates `from`..`to` under a Playback clock. `T` must be
// one of the types with a lerp() overload above (float, Point, Size, Color).
template <class T>
class Tween {
public:
    Tween(T from, T to, Playback playback) noexcept
        : from_(from), to_(to), playback_(playback) {}

    void advance(float dt) noexcept { playback_.advance(dt); }
    void reset() noexcept { playback_.reset(); }

    [[nodiscard]] bool finished() const noexcept { return playback_.finished(); }
    // Eased progress in [0, 1].
    [[nodiscard]] float progress() const noexcept { return playback_.value(); }
    // The interpolated value at the current eased progress.
    [[nodiscard]] T value() const noexcept { return lerp(from_, to_, playback_.value()); }

    [[nodiscard]] const T& from() const noexcept { return from_; }
    [[nodiscard]] const T& to() const noexcept { return to_; }
    [[nodiscard]] const Playback& playback() const noexcept { return playback_; }

private:
    T from_;
    T to_;
    Playback playback_;
};

} // namespace uirepo::anim
