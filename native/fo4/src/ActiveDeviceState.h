#pragma once

#include "uirepo/ActiveDevice.h"

namespace uirepo::fo4::ActiveDeviceState {

enum class PointerLocation {
    Unknown,
    InsideClient,
    OutsideClient,
};

[[nodiscard]] input::ActiveInputDevice active() noexcept;
void noteKeyboardKey(bool down) noexcept;
void notePointerButton(bool down) noexcept;
void notePointerPosition(float x, float y) noexcept;
void noteRoutedPoint(float x, float y) noexcept;
void notePointerLeft() noexcept;
[[nodiscard]] PointerLocation routedPointerLocation(float& x, float& y) noexcept;
void noteWheel(float delta) noexcept;
void noteControllerButton(bool down) noexcept;
void noteControllerAnalog(float magnitude) noexcept;
void resetPointerBaseline() noexcept;

} // namespace uirepo::fo4::ActiveDeviceState
