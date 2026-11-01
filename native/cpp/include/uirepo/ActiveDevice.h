#pragma once

#include <cmath>
#include <cstdint>

namespace uirepo::input {

enum class ActiveInputDevice : std::uint8_t {
    KeyboardMouse,
    Controller,
};

class ActiveDeviceTracker final {
public:
    static constexpr float kPointerMoveThreshold = 1.5f;
    static constexpr float kControllerAnalogThreshold = 0.35f;

    [[nodiscard]] ActiveInputDevice active() const noexcept { return active_; }

    [[nodiscard]] bool noteKeyboardKey(bool down) noexcept {
        return down && set(ActiveInputDevice::KeyboardMouse);
    }

    [[nodiscard]] bool notePointerButton(bool down) noexcept {
        return down && set(ActiveInputDevice::KeyboardMouse);
    }

    [[nodiscard]] bool noteWheel(float delta) noexcept {
        return std::isfinite(delta) && delta != 0.0f && set(ActiveInputDevice::KeyboardMouse);
    }

    [[nodiscard]] bool notePointerMove(float deltaX, float deltaY) noexcept {
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) {
            return false;
        }
        const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
        return distanceSquared >= kPointerMoveThreshold * kPointerMoveThreshold &&
            set(ActiveInputDevice::KeyboardMouse);
    }

    [[nodiscard]] bool noteControllerButton(bool down) noexcept {
        return down && set(ActiveInputDevice::Controller);
    }

    [[nodiscard]] bool noteControllerAnalog(float magnitude) noexcept {
        return std::isfinite(magnitude) && std::fabs(magnitude) >= kControllerAnalogThreshold &&
            set(ActiveInputDevice::Controller);
    }

private:
    [[nodiscard]] bool set(ActiveInputDevice device) noexcept {
        if (active_ == device) {
            return false;
        }
        active_ = device;
        return true;
    }

    ActiveInputDevice active_{ActiveInputDevice::KeyboardMouse};
};

} // namespace uirepo::input
