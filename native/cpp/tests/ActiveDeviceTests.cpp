#include "uirepo/ActiveDevice.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using uirepo::input::ActiveDeviceTracker;
using uirepo::input::ActiveInputDevice;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testControllerReleaseDoesNotStealOwnership() {
    ActiveDeviceTracker tracker;
    require(tracker.active() == ActiveInputDevice::KeyboardMouse, "default device must be keyboard/mouse");
    require(!tracker.noteControllerButton(false), "controller release changed active device");
    require(tracker.active() == ActiveInputDevice::KeyboardMouse, "controller release stole prompt ownership");
    require(tracker.noteControllerButton(true), "controller press did not switch active device");
    require(tracker.active() == ActiveInputDevice::Controller, "controller press selected wrong active device");
    require(!tracker.noteControllerButton(false), "controller release changed an already-controller owner");
}

void testStickDriftDoesNotFlipPrompts() {
    ActiveDeviceTracker tracker;
    require(tracker.noteControllerButton(true), "controller setup press failed");
    require(tracker.noteKeyboardKey(true), "keyboard setup press failed");
    require(tracker.active() == ActiveInputDevice::KeyboardMouse, "keyboard press did not become active");

    require(!tracker.noteControllerAnalog(0.05f), "sub-deadzone stick drift changed active device");
    require(!tracker.noteControllerAnalog(-0.2f), "negative sub-deadzone drift changed active device");
    require(tracker.active() == ActiveInputDevice::KeyboardMouse, "stick drift stole prompt ownership");

    require(tracker.noteControllerAnalog(0.8f), "deliberate controller analog motion did not switch device");
    require(tracker.active() == ActiveInputDevice::Controller, "controller analog motion selected wrong device");
}

void testMeaningfulKeyboardAndPointerActivitySwitchesBack() {
    ActiveDeviceTracker tracker;
    require(tracker.noteControllerButton(true), "controller setup press failed");

    require(!tracker.noteKeyboardKey(false), "keyboard key-up switched active device");
    require(tracker.active() == ActiveInputDevice::Controller, "keyboard key-up stole prompt ownership");
    require(tracker.noteKeyboardKey(true), "keyboard key-down did not switch active device");
    require(tracker.active() == ActiveInputDevice::KeyboardMouse, "keyboard key-down selected wrong device");

    require(tracker.noteControllerButton(true), "controller press did not switch back");
    require(!tracker.notePointerButton(false), "pointer release switched active device");
    require(tracker.notePointerButton(true), "pointer press did not switch active device");

    require(tracker.noteControllerButton(true), "controller press did not switch back for wheel test");
    require(!tracker.noteWheel(0.0f), "zero wheel delta switched active device");
    require(tracker.noteWheel(120.0f), "mouse wheel did not switch active device");
}

void testPointerJitterIsIgnored() {
    ActiveDeviceTracker tracker;
    require(tracker.noteControllerButton(true), "controller setup press failed");
    require(!tracker.notePointerMove(0.25f, 0.25f), "tiny pointer jitter switched active device");
    require(tracker.active() == ActiveInputDevice::Controller, "pointer jitter stole prompt ownership");
    require(tracker.notePointerMove(3.0f, 0.0f), "meaningful pointer movement did not switch active device");
    require(tracker.active() == ActiveInputDevice::KeyboardMouse, "pointer movement selected wrong device");
}

void testInvalidAnalogAndPointerValuesAreIgnored() {
    ActiveDeviceTracker tracker;
    require(tracker.noteControllerButton(true), "controller setup press failed");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(!tracker.notePointerMove(nan, 5.0f), "NaN pointer input switched active device");
    require(!tracker.noteControllerAnalog(nan), "NaN controller input changed tracker state");
    require(tracker.active() == ActiveInputDevice::Controller, "invalid input changed prompt ownership");
}

} // namespace

int main() {
    testControllerReleaseDoesNotStealOwnership();
    testStickDriftDoesNotFlipPrompts();
    testMeaningfulKeyboardAndPointerActivitySwitchesBack();
    testPointerJitterIsIgnored();
    testInvalidAnalogAndPointerValuesAreIgnored();
    std::cout << "UIRepo active device regressions passed\n";
    return 0;
}
