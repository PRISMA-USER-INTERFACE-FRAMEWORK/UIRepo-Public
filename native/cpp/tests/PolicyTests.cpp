#include "uirepo/DispatchQueue.h"
#include "uirepo/InputPolicy.h"
#include "uirepo/InputRouter.h"
#include "uirepo/PausePolicy.h"
#include "uirepo/TelemetryPolicy.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void testInputOwnership() {
    using namespace uirepo::input;

    require(
        afterFocusedDocumentRelease(false) == CaptureOwner::None,
        "focus release without passive overlay should release capture");
    require(
        afterFocusedDocumentRelease(true) == CaptureOwner::PassiveOverlay,
        "visible passive overlay should retain passive capture");
    require(
        shouldRoutePointer(CaptureOwner::FocusedDocument, true),
        "focused pointer-enabled document should receive pointer input");
    require(
        !shouldRoutePointer(CaptureOwner::FocusedDocument, false),
        "focused pointer-disabled document should not receive pointer input");
    require(
        nextCursorVisibilityAction(CaptureOwner::FocusedDocument, false) ==
            CursorVisibilityAction::HideVanilla,
        "focused document should hide vanilla cursor once");
    require(
        nextCursorVisibilityAction(CaptureOwner::None, true) ==
            CursorVisibilityAction::RestoreVanilla,
        "capture release should restore vanilla cursor");
}

void testInputRegions() {
    using namespace uirepo::input;

    RegionState state;
    state.setFocusedDocument(11);
    require(state.cursorOwned(), "focused document should own the UI cursor");
    require(state.shouldRoute(1000, 1000), "full capture should route any pointer position");

    state.setMode(CaptureMode::Regions);
    const std::array regions{
        InputRegion{10, 20, 100, 50},
        InputRegion{300, 400, 20, 20},
    };
    state.setRegions(11, regions);
    require(state.shouldRoute(10, 20), "region should include its top-left edge");
    require(state.shouldRoute(109, 69), "region should include its last interior pixel");
    require(!state.shouldRoute(110, 70), "region right/bottom edges should be exclusive");
    require(!state.shouldRoute(0, 0), "region capture should click through outside regions");

    state.setFocusedDocument(12);
    require(state.mode() == CaptureMode::Full, "focus handoff should reset capture mode");
    require(state.regions().empty(), "focus handoff should clear stale regions");

    std::array<InputRegion, kMaxInputRegions + 20> many{};
    state.setMode(CaptureMode::Regions);
    state.setRegions(12, many);
    require(state.regions().size() == kMaxInputRegions, "input regions must remain bounded");

    state.onDocumentDestroyed(12);
    require(state.focusedDocument() == 0, "destroyed focused document should clear region state");
    require(!state.cursorOwned(), "destroyed focused document should release cursor ownership");
}

void testControllerRouting() {
    using namespace uirepo::input;

    struct Expected {
        std::uint32_t code;
        NavigationCommand command;
    };
    constexpr std::array expected{
        Expected{0x0001u, NavigationCommand::Up},
        Expected{0x0002u, NavigationCommand::Down},
        Expected{0x0004u, NavigationCommand::Left},
        Expected{0x0008u, NavigationCommand::Right},
        Expected{0x1000u, NavigationCommand::Accept},
        Expected{0x2000u, NavigationCommand::Cancel},
    };

    for (const Expected& item : expected) {
        const auto raw = routeController(true, true, item.code);
        require(raw.command == item.command, "raw controller button mapped incorrectly");
        require(raw.routeToDocument, "mapped controller button should route to focused document");
        require(!raw.consumeGameEvent, "mapping alone must not consume Fallout controller input");

        const auto tagged = routeController(true, true, item.code | kGamepadTag);
        require(tagged.command == item.command, "tagged controller button mapped incorrectly");
    }

    require(
        routeController(true, false, 0x0001u).command == NavigationCommand::None,
        "unfocused document must not steal controller input");
    require(
        routeController(false, true, 0x0001u).command == NavigationCommand::None,
        "non-gamepad input must bypass gamepad routing");
    require(
        routeController(true, true, 0x4000u).command == NavigationCommand::None,
        "unmapped controller buttons must remain available to Fallout");
}

void testPortableNavigation() {
    using namespace uirepo;
    using namespace uirepo::input;

    auto root = std::make_unique<StackNode>("root", Axis::Column);
    auto& first = root->emplaceChild<ButtonNode>("first", "First");
    auto& skipped = root->emplaceChild<ButtonNode>("skipped", "Skipped");
    auto& last = root->emplaceChild<ButtonNode>("last", "Last");
    skipped.setEnabled(false);

    Document document(std::move(root));
    require(document.focus(first), "portable navigation setup could not focus first button");
    require(
        navigationTarget(document, NavigationCommand::Down) == &last,
        "navigation should skip disabled nodes");
    require(document.focus(last), "portable navigation setup could not focus last button");
    require(
        navigationTarget(document, NavigationCommand::Next) == &first,
        "forward navigation should wrap");
    require(
        navigationTarget(document, NavigationCommand::Previous) == &first,
        "backward navigation should skip disabled nodes and wrap");

    last.setVisible(false);
    require(document.focus(first), "portable navigation could not restore first focus");
    require(
        navigationTarget(document, NavigationCommand::Down) == nullptr,
        "navigation should not claim input when no alternate focusable node exists");

    require(navigationForKey(KeyTab, ModifierNone) == NavigationCommand::Next, "Tab mapping failed");
    require(
        navigationForKey(KeyTab, ModifierShift) == NavigationCommand::Previous,
        "Shift+Tab mapping failed");
    require(navigationForKey(KeyEnter, ModifierNone) == NavigationCommand::Accept, "Enter mapping failed");
    require(navigationForKey(KeySpace, ModifierNone) == NavigationCommand::Accept, "Space mapping failed");
    require(navigationForKey(KeyEscape, ModifierNone) == NavigationCommand::None, "Escape must fall through");
    require(navigationForKey(KeyF4, ModifierAlt) == NavigationCommand::None, "Alt+F4 must fall through");
}

void testSequenceOwnershipAndViewportMapping() {
    using namespace uirepo;
    using namespace uirepo::input;

    KeySequenceState keys;
    keys.begin(KeyDown, true);
    require(keys.owner(KeyDown) == SequenceOwner::UIRepo, "handled key-down must own the sequence");
    require(keys.end(KeyDown), "UI-owned key-up must remain consumed");
    require(keys.owner(KeyDown) == SequenceOwner::None, "key-up must clear sequence ownership");

    keys.begin(KeyTab, false);
    require(keys.owner(KeyTab) == SequenceOwner::Fallout, "unhandled key-down must remain Fallout-owned");
    require(!keys.end(KeyTab), "Fallout-owned key-up must fall through");

    keys.begin(KeyEnter, true);
    keys.clear();
    require(keys.owner(KeyEnter) == SequenceOwner::None, "focus loss must clear key ownership");

    require(
        consumePointerRelease(true, false),
        "mouse-up must stay consumed after a UI-owned mouse-down even if registry capture was cancelled");
    require(
        consumePointerRelease(false, true),
        "mouse-up handled directly by UIRepo must be consumed");
    require(
        !consumePointerRelease(false, false),
        "unowned mouse-up must fall through to Fallout");

    const Point mapped = mapClientToViewport(
        Point{640.0f, 360.0f},
        Size{1280.0f, 720.0f},
        Size{1920.0f, 1080.0f});
    require(std::fabs(mapped.x - 960.0f) < 0.001f, "client X did not scale to renderer viewport");
    require(std::fabs(mapped.y - 540.0f) < 0.001f, "client Y did not scale to renderer viewport");

    const Point fallback = mapClientToViewport(
        Point{25.0f, 40.0f},
        Size{},
        Size{1920.0f, 1080.0f});
    require(fallback.x == 25.0f && fallback.y == 40.0f, "invalid client size must preserve raw coordinates");

    const Size clientSize{1280.0f, 720.0f};
    require(pointInsideClient(Point{0.0f, 0.0f}, clientSize), "client origin must classify as inside");
    require(pointInsideClient(Point{1279.0f, 719.0f}, clientSize), "the last client pixel must classify as inside");
    require(!pointInsideClient(Point{-1.0f, 10.0f}, clientSize), "negative x must classify as outside");
    require(!pointInsideClient(Point{10.0f, -1.0f}, clientSize), "negative y must classify as outside");
    require(!pointInsideClient(Point{1280.0f, 10.0f}, clientSize), "x == width must classify as outside");
    require(!pointInsideClient(Point{10.0f, 720.0f}, clientSize), "y == height must classify as outside");
    require(!pointInsideClient(Point{-20.0f, 400.0f}, clientSize), "a captured move beyond the left edge must classify as outside");
}

void testDispatchQueue() {
    using namespace uirepo::dispatch;

    Queue queue;
    require(!queue.tryEnqueue([] {}).accepted, "detached queue must reject work");

    queue.attach();
    require(queue.ready(), "attached queue should be ready");

    int ran = 0;
    const auto first = queue.tryEnqueue([&] { ++ran; }, 10);
    const auto second = queue.tryEnqueue([&] { ran += 10; }, 20);
    require(first.accepted && first.needsWake, "first queued task should request a wake");
    require(second.accepted && !second.needsWake, "existing pending wake should coalesce later work");

    queue.beginDrain();
    auto task = queue.takeNext([](OwnerHandle) { return true; });
    require(task.has_value() && task->owner == 10, "queue should preserve FIFO owner ordering");
    require(!queue.takeNext().has_value(), "nested callback execution must be refused");
    task->function();
    queue.finishExecuting();
    queue.endDrain();
    require(ran == 1, "first queued function did not run");
    require(queue.takeWakeRearm(), "remaining work should request a new wake after drain budget ends");

    queue.beginDrain();
    task = queue.takeNext([](OwnerHandle owner) { return owner == 20; });
    require(task.has_value() && task->owner == 20, "second live owner should still dispatch");
    task->function();
    queue.finishExecuting();
    queue.endDrain();
    require(ran == 11, "second queued function did not run");

    queue.cancelOwner(30);
    require(queue.isCancelled(30), "owner cancellation tombstone was not recorded");
    require(!queue.tryEnqueue([] {}, 30).accepted, "cancelled owner must reject new callbacks");

    queue.detach();
    require(!queue.ready(), "detached queue should not remain ready");
    queue.attach();

    for (std::size_t index = 0; index < kMaxQueue; ++index) {
        require(queue.tryEnqueue([] {}).accepted, "queue rejected work before reaching its bound");
    }
    require(!queue.tryEnqueue([] {}).accepted, "full queue should reject ordinary work");
    require(queue.tryEnqueue([] {}, 0, true).accepted, "one safety task should survive queue saturation");
    require(!queue.tryEnqueue([] {}, 0, true).accepted, "saturated queue should not admit multiple safety bypasses");

    queue.failClosed();
    require(queue.failed(), "fail-closed state was not recorded");
    require(!queue.ready(), "failed queue must not report ready");
    require(!queue.tryEnqueue([] {}).accepted, "failed queue must reject new work");
}

void testPausePolicy() {
    using uirepo::pause::State;

    State state;
    bool target = true;
    require(state.shouldSendMessage(target), "first pause target should always emit a message");
    require(!target, "default pause target should be unpaused");

    state.setTarget(true);
    require(state.shouldSendMessage(target) && target, "pause target change should emit kShow");
    state.markMessageSent(true);
    require(!state.shouldSendMessage(target), "duplicate pause target should not emit another message");

    for (int frame = 0; frame < State::kVerifyAfterCompleteFrames; ++frame) {
        require(!state.advancePresentedFrame(), "pause verification fired before enough complete frames");
    }
    require(state.advancePresentedFrame(), "pause verification did not fire after the real-frame budget");
    require(!state.advancePresentedFrame(), "pause verification should fire only once per message");

    state.setTarget(false);
    require(state.shouldSendMessage(target) && !target, "unpause target change should emit kHide");
    state.markMessageSent(false);

    for (int frame = 0; frame < 3; ++frame) {
        require(!state.advancePresentedFrame(), "new pause verification fired too early");
    }
    state.setTarget(true);
    require(state.shouldSendMessage(target) && target, "newer pause target should be visible before verification");
    state.markMessageSent(true);
    for (int frame = 0; frame < State::kVerifyAfterCompleteFrames; ++frame) {
        require(!state.advancePresentedFrame(), "re-armed pause verification inherited stale frame count");
    }
    require(state.advancePresentedFrame(), "re-armed pause verification did not receive a full frame budget");
}

void testPeriodicTelemetryGate() {
    using namespace std::chrono_literals;
    using Gate = uirepo::telemetry::PeriodicGate;

    Gate gate{10s};
    const Gate::TimePoint start{};

    require(!gate.due(start), "unarmed telemetry gate should arm without reporting");
    require(!gate.due(start + 9s), "telemetry gate fired before its interval");
    require(gate.due(start + 10s), "telemetry gate did not fire at its interval");
    require(!gate.due(start + 10s), "telemetry gate fired twice at the same timestamp");

    require(gate.due(start + 35s), "telemetry gate did not recover after a long scheduling gap");
    require(!gate.due(start + 44s), "telemetry gate burst after a long scheduling gap");
    require(gate.due(start + 45s), "telemetry gate did not schedule from the last report time");

    gate.reset(start + 100s);
    require(!gate.due(start + 109s), "reset telemetry gate fired too early");
    require(gate.due(start + 110s), "reset telemetry gate did not honor a full interval");

    gate.clear();
    require(!gate.due(start + 200s), "cleared telemetry gate should re-arm without reporting");
}

} // namespace

int main() {
    testInputOwnership();
    testInputRegions();
    testControllerRouting();
    testPortableNavigation();
    testSequenceOwnershipAndViewportMapping();
    testDispatchQueue();
    testPausePolicy();
    testPeriodicTelemetryGate();
    std::cout << "UIRepo policy regressions passed\n";
    return 0;
}
