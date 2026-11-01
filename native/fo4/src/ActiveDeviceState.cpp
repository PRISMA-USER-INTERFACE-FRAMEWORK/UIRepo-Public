#include "ActiveDeviceState.h"

#include <mutex>

namespace uirepo::fo4::ActiveDeviceState {
namespace {

std::mutex g_mutex;
input::ActiveDeviceTracker g_tracker;
bool g_havePointerPosition{};
float g_pointerX{};
float g_pointerY{};
PointerLocation g_routedLocation{PointerLocation::Unknown};
float g_routedX{};
float g_routedY{};

} // namespace

input::ActiveInputDevice active() noexcept {
    std::scoped_lock lock(g_mutex);
    return g_tracker.active();
}

void noteKeyboardKey(bool down) noexcept {
    std::scoped_lock lock(g_mutex);
    (void)g_tracker.noteKeyboardKey(down);
}

void notePointerButton(bool down) noexcept {
    std::scoped_lock lock(g_mutex);
    (void)g_tracker.notePointerButton(down);
}

void notePointerPosition(float x, float y) noexcept {
    std::scoped_lock lock(g_mutex);
    if (!g_havePointerPosition) {
        g_havePointerPosition = true;
        g_pointerX = x;
        g_pointerY = y;
        return;
    }

    const float deltaX = x - g_pointerX;
    const float deltaY = y - g_pointerY;
    g_pointerX = x;
    g_pointerY = y;
    (void)g_tracker.notePointerMove(deltaX, deltaY);
}

void noteRoutedPoint(float x, float y) noexcept {
    std::scoped_lock lock(g_mutex);
    g_routedLocation = PointerLocation::InsideClient;
    g_routedX = x;
    g_routedY = y;
}

void notePointerLeft() noexcept {
    std::scoped_lock lock(g_mutex);
    g_routedLocation = PointerLocation::OutsideClient;
}

PointerLocation routedPointerLocation(float& x, float& y) noexcept {
    std::scoped_lock lock(g_mutex);
    if (g_routedLocation == PointerLocation::InsideClient) {
        x = g_routedX;
        y = g_routedY;
    }
    return g_routedLocation;
}

void noteWheel(float delta) noexcept {
    std::scoped_lock lock(g_mutex);
    (void)g_tracker.noteWheel(delta);
}

void noteControllerButton(bool down) noexcept {
    std::scoped_lock lock(g_mutex);
    (void)g_tracker.noteControllerButton(down);
}

void noteControllerAnalog(float magnitude) noexcept {
    std::scoped_lock lock(g_mutex);
    (void)g_tracker.noteControllerAnalog(magnitude);
}

void resetPointerBaseline() noexcept {
    std::scoped_lock lock(g_mutex);
    g_havePointerPosition = false;
    g_pointerX = 0.0f;
    g_pointerY = 0.0f;
    g_routedLocation = PointerLocation::Unknown;
    g_routedX = 0.0f;
    g_routedY = 0.0f;
}

} // namespace uirepo::fo4::ActiveDeviceState
