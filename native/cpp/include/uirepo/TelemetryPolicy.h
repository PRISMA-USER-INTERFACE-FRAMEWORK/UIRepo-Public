#pragma once

#include <chrono>

namespace uirepo::telemetry {

class PeriodicGate final {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
    using TimePoint = Clock::time_point;

    explicit PeriodicGate(Duration interval) noexcept
        : interval_(interval > Duration::zero() ? interval : Duration{1}) {}

    void reset(TimePoint now) noexcept {
        next_ = now + interval_;
        armed_ = true;
    }

    void clear() noexcept {
        next_ = TimePoint{};
        armed_ = false;
    }

    [[nodiscard]] bool due(TimePoint now) noexcept {
        if (!armed_) {
            reset(now);
            return false;
        }
        if (now < next_) {
            return false;
        }
        next_ = now + interval_;
        return true;
    }

private:
    Duration interval_;
    TimePoint next_{};
    bool armed_{};
};

} // namespace uirepo::telemetry
