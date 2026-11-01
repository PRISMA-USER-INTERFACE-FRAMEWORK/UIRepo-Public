#include "ActiveDeviceState.h"

#include <cstdlib>
#include <iostream>

using uirepo::fo4::ActiveDeviceState::PointerLocation;
namespace State = uirepo::fo4::ActiveDeviceState;

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testRoutedPointerLocationTristate() {
    float x = -7.0f;
    float y = -7.0f;

    State::resetPointerBaseline();
    require(
        State::routedPointerLocation(x, y) == PointerLocation::Unknown,
        "no routed pointer yet must report Unknown so the engine-position fallback stays allowed");

    State::noteRoutedPoint(12.0f, 34.0f);
    x = 0.0f;
    y = 0.0f;
    require(
        State::routedPointerLocation(x, y) == PointerLocation::InsideClient,
        "a routed pointer must report InsideClient");
    require(x == 12.0f && y == 34.0f, "InsideClient must yield the routed viewport coordinates");

    State::notePointerLeft();
    require(
        State::routedPointerLocation(x, y) == PointerLocation::OutsideClient,
        "after a client leave the location must be OutsideClient, not a stale InsideClient point");

    State::notePointerLeft();
    require(
        State::routedPointerLocation(x, y) == PointerLocation::OutsideClient,
        "a captured move outside the client must keep the location OutsideClient");

    State::noteRoutedPoint(5.0f, 6.0f);
    require(
        State::routedPointerLocation(x, y) == PointerLocation::InsideClient && x == 5.0f && y == 6.0f,
        "re-entering the client must return to InsideClient with fresh coordinates");

    State::resetPointerBaseline();
    require(
        State::routedPointerLocation(x, y) == PointerLocation::Unknown,
        "resetting the pointer baseline must return to Unknown");
}

} // namespace

int main() {
    testRoutedPointerLocationTristate();
    std::cout << "UIRepo ActiveDeviceState regressions passed\n";
    return 0;
}
