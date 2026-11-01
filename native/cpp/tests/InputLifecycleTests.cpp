#include "DocumentRegistry.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using uirepo::Align;
using uirepo::Axis;
using uirepo::Length;
using uirepo::Size;
using uirepo::Style;
using uirepo::fo4::DocumentRegistry;
using uirepo::input::KeyEnter;
using uirepo::input::KeyEscape;
using uirepo::input::KeyEvent;
using uirepo::input::KeyEventType;
using uirepo::input::NavigationCommand;
using uirepo::input::PointerButton;
using uirepo::input::PointerEvent;
using uirepo::input::PointerEventType;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

Style fixedStyle(float width, float height) {
    Style style{};
    style.width = Length::Pixels(width);
    style.height = Length::Pixels(height);
    style.alignItems = Align::Start;
    return style;
}

void testPointerSequenceOwnership() {
    using uirepo::input::PointerSequenceState;
    using uirepo::input::SequenceOwner;

    PointerSequenceState sequences;
    sequences.begin(PointerButton::Left, true);
    sequences.begin(PointerButton::Right, false);
    require(sequences.owner(PointerButton::Left) == SequenceOwner::UIRepo, "handled pointer press must own its sequence");
    require(sequences.owner(PointerButton::Right) == SequenceOwner::Fallout, "unhandled pointer press must remain Fallout-owned");
    require(sequences.anyOwned(), "UI-owned pointer sequence must report active ownership");
    require(sequences.end(PointerButton::Left), "UI-owned pointer release must remain consumed");
    require(!sequences.anyOwned(), "ending the only UI-owned sequence must clear active ownership");
    require(!sequences.end(PointerButton::Right), "Fallout-owned pointer release must fall through");

    sequences.begin(PointerButton::Middle, true);
    sequences.clear();
    require(sequences.owner(PointerButton::Middle) == SequenceOwner::None, "explicit reset must clear pointer ownership");
}

struct PointerLog {
    std::vector<std::uint32_t> events;
    DocumentRegistry* registry{};
    DocumentRegistry::NodeHandle destroyOnEvent{};
    std::uint32_t destroyEvent{};
};

void pointerCallback(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle node,
    const UIRepoNodePointerEventV1* event,
    void* userData) {
    auto& log = *static_cast<PointerLog*>(userData);
    log.events.push_back(event->type);
    if (log.registry != nullptr && node == log.destroyOnEvent && event->type == log.destroyEvent) {
        (void)log.registry->destroyNode(document, node);
    }
}

struct FocusLog {
    DocumentRegistry* registry{};
    DocumentRegistry::DocumentHandle document{};
    DocumentRegistry::NodeHandle redirect{};
    bool clearOnGain{};
    int activates{};
};

void focusCallback(
    UIRepoDocumentHandle document,
    UIRepoNodeHandle,
    std::uint32_t eventType,
    void* userData) {
    auto& log = *static_cast<FocusLog*>(userData);
    if (eventType == UIREPO_EVENT_ACTIVATE) {
        ++log.activates;
        return;
    }
    if (eventType == UIREPO_EVENT_FOCUS_LOST && log.redirect != 0) {
        const auto redirect = log.redirect;
        log.redirect = 0;
        (void)log.registry->focusNode(log.document, redirect);
        return;
    }
    if (eventType == UIREPO_EVENT_FOCUS_GAINED && log.clearOnGain) {
        log.clearOnGain = false;
        (void)log.registry->clearDocumentFocus(document);
    }
}

struct KeyLog {
    DocumentRegistry* registry{};
    std::vector<UIRepoKeyEventV1> events;
    bool consume{};
    bool destroyOnDown{};
};

std::uint8_t keyCallback(
    UIRepoDocumentHandle document,
    const UIRepoKeyEventV1* event,
    void* userData) {
    auto& log = *static_cast<KeyLog*>(userData);
    log.events.push_back(*event);
    if (log.destroyOnDown && event->type == UIREPO_KEY_DOWN && log.registry != nullptr) {
        (void)log.registry->destroyDocument(document);
    }
    return log.consume ? 1u : 0u;
}

struct ValueLog {
    int changes{};
};

void valueCallback(
    UIRepoDocumentHandle,
    UIRepoNodeHandle,
    std::uint32_t eventType,
    void* userData) {
    if (eventType == UIREPO_EVENT_VALUE_CHANGED) {
        ++static_cast<ValueLog*>(userData)->changes;
    }
}

void testSliderPointerTracking() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("slider-root");
    require(document != 0, "could not create slider document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable slider input");

    const auto root = registry.rootNode(document);
    const auto slider = registry.createSlider(document, root, "slider", 0.0f, 0.0f, 100.0f, 1.0f);
    require(slider != 0, "could not create slider");
    require(registry.setNodeStyle(document, slider, fixedStyle(200.0f, 40.0f)), "could not style slider");

    ValueLog log{};
    require(registry.setNodeEventHandler(document, slider, &valueCallback, &log), "could not register slider value handler");
    registry.layoutVisible(Size{240.0f, 160.0f});

    float value = -1.0f;
    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonDown,
            .button = PointerButton::Left,
            .x = 100.0f,
            .y = 20.0f,
        }),
        "left press on the slider track must be consumed");
    require(registry.hasPointerCapture(), "slider press must acquire pointer capture");
    require(
        registry.getSliderValue(document, slider, value) && value == 50.0f,
        "press at the track midpoint must jump the slider to the midpoint value");
    require(log.changes == 1, "press that moves the slider must fire value-changed once");

    require(
        registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 150.0f, .y = 20.0f}),
        "captured slider drag movement must be consumed");
    require(
        registry.getSliderValue(document, slider, value) && value == 75.0f,
        "drag must track the pointer position along the track");
    require(log.changes == 2, "drag that moves the slider must fire value-changed again");

    require(
        registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = -40.0f, .y = 20.0f}),
        "drag past the track edge must stay captured");
    require(
        registry.getSliderValue(document, slider, value) && value == 0.0f,
        "dragging past the left edge must clamp to the minimum");

    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonUp,
            .button = PointerButton::Left,
            .x = -40.0f,
            .y = 20.0f,
        }),
        "slider release must be consumed");
    require(!registry.hasPointerCapture(), "slider release must clear pointer capture");

    require(registry.routeNavigation(NavigationCommand::Right), "arrow-key adjustment must still work after pointer use");
    require(
        registry.getSliderValue(document, slider, value) && value == 1.0f,
        "right navigation must step the slider up from the minimum");
}

void testWheelRouting() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("wheel-root");
    require(document != 0, "could not create wheel document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable wheel input");

    const auto root = registry.rootNode(document);
    const auto stack = registry.createStack(document, root, "scroll", Axis::Column);
    require(stack != 0, "could not create scroll stack");
    require(registry.setNodeStyle(document, stack, fixedStyle(120.0f, 60.0f)), "could not style scroll stack");
    require(registry.setStackScrollable(document, stack, true), "could not enable retained scrolling");

    for (int index = 0; index < 3; ++index) {
        const auto button = registry.createButton(
            document,
            stack,
            "wheel-button-" + std::to_string(index),
            "Item");
        require(button != 0, "could not create scroll child");
        require(registry.setNodeStyle(document, button, fixedStyle(120.0f, 40.0f)), "could not style scroll child");
    }

    registry.layoutVisible(Size{240.0f, 160.0f});
    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::Wheel,
            .x = 20.0f,
            .y = 20.0f,
            .wheelDelta = -uirepo::input::WheelDeltaPerNotch,
        }),
        "wheel down should consume when retained scroll state changes");
    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::Wheel,
            .x = 20.0f,
            .y = 20.0f,
            .wheelDelta = uirepo::input::WheelDeltaPerNotch,
        }),
        "wheel up should scroll back toward the start");
    require(
        !registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::Wheel,
            .x = 20.0f,
            .y = 20.0f,
            .wheelDelta = uirepo::input::WheelDeltaPerNotch,
        }),
        "wheel at the scroll limit must fall through");
}

void testHoverTransitionsAndCleanup() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("hover-root");
    require(document != 0, "could not create hover document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable hover input");

    const auto root = registry.rootNode(document);
    const auto row = registry.createStack(document, root, "row", Axis::Row);
    require(registry.setNodeStyle(document, row, fixedStyle(180.0f, 40.0f)), "could not style hover row");

    const auto first = registry.createButton(document, row, "hover-a", "A");
    const auto second = registry.createButton(document, row, "hover-b", "B");
    require(first != 0 && second != 0, "could not create hover buttons");
    require(registry.setNodeStyle(document, first, fixedStyle(80.0f, 40.0f)), "could not style first hover button");
    require(registry.setNodeStyle(document, second, fixedStyle(80.0f, 40.0f)), "could not style second hover button");

    PointerLog firstLog{};
    PointerLog secondLog{};
    require(registry.setNodePointerEventHandler(document, first, &pointerCallback, &firstLog), "could not register first hover handler");
    require(registry.setNodePointerEventHandler(document, second, &pointerCallback, &secondLog), "could not register second hover handler");

    registry.layoutVisible(Size{240.0f, 160.0f});
    require(
        !registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 20.0f, .y = 20.0f}),
        "passive hover must not consume pointer movement");
    require(firstLog.events == std::vector<std::uint32_t>{UIREPO_NODE_POINTER_ENTER}, "first hover enter was not emitted exactly once");

    (void)registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 25.0f, .y = 20.0f});
    require(
        std::count(firstLog.events.begin(), firstLog.events.end(), UIREPO_NODE_POINTER_ENTER) == 1,
        "duplicate hover move emitted another enter");

    (void)registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 100.0f, .y = 20.0f});
    require(firstLog.events.back() == UIREPO_NODE_POINTER_LEAVE, "A to B transition must leave A");
    require(secondLog.events == std::vector<std::uint32_t>{UIREPO_NODE_POINTER_ENTER}, "A to B transition must enter B");

    require(registry.setNodeVisible(document, second, false), "could not hide hovered node");
    require(secondLog.events.back() == UIREPO_NODE_POINTER_LEAVE, "hiding hovered node must emit leave");
}

void testPointerCaptureDragAndCancel() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("pointer-root");
    require(document != 0, "could not create pointer document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable pointer input");

    const auto root = registry.rootNode(document);
    const auto button = registry.createButton(document, root, "drag", "Drag");
    require(button != 0, "could not create drag button");
    require(registry.setNodeStyle(document, button, fixedStyle(100.0f, 50.0f)), "could not style drag button");

    PointerLog log{};
    require(registry.setNodePointerEventHandler(document, button, &pointerCallback, &log), "could not register drag handler");
    registry.layoutVisible(Size{240.0f, 160.0f});

    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonDown,
            .button = PointerButton::Right,
            .x = 10.0f,
            .y = 10.0f,
        }),
        "registered right-button behavior should own the press");
    require(registry.hasPointerCapture(), "owned right-button press must acquire capture");

    (void)registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 12.0f, .y = 12.0f});
    require(
        std::find(log.events.begin(), log.events.end(), UIREPO_NODE_POINTER_DRAG_BEGIN) == log.events.end(),
        "movement below drag threshold must not begin drag");

    require(
        registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 20.0f, .y = 20.0f}),
        "captured drag movement must be consumed");
    require(
        std::find(log.events.begin(), log.events.end(), UIREPO_NODE_POINTER_DRAG_BEGIN) != log.events.end(),
        "movement beyond threshold must begin drag");

    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonUp,
            .button = PointerButton::Right,
            .x = 220.0f,
            .y = 150.0f,
        }),
        "captured release must stay consumed outside source bounds");
    require(log.events.back() == UIREPO_NODE_POINTER_BUTTON_UP, "pointer up must terminate drag sequence");
    require(!registry.hasPointerCapture(), "pointer release must clear capture");

    PointerLog cancelLog{};
    require(registry.setNodePointerEventHandler(document, button, &pointerCallback, &cancelLog), "could not register cancel handler");
    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonDown,
            .button = PointerButton::Left,
            .x = 10.0f,
            .y = 10.0f,
        }),
        "left-button pointer behavior should own press");
    require(registry.destroyNode(document, button), "destroy during capture failed");
    require(cancelLog.events.back() == UIREPO_NODE_POINTER_CANCEL, "destroying captured node must emit cancel");
    require(!registry.hasPointerCapture(), "destroying captured node must clear capture");
}

void testReentrantFocusAndStaleHandles() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("focus-root");
    require(document != 0, "could not create focus document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable focus input");

    const auto root = registry.rootNode(document);
    const auto first = registry.createButton(document, root, "focus-a", "A");
    const auto second = registry.createButton(document, root, "focus-b", "B");
    require(first != 0 && second != 0, "could not create focus buttons");

    FocusLog firstLog{.registry = &registry, .document = document};
    FocusLog secondLog{.registry = &registry, .document = document};
    require(registry.setNodeEventHandler(document, first, &focusCallback, &firstLog), "could not set first focus handler");
    require(registry.setNodeEventHandler(document, second, &focusCallback, &secondLog), "could not set second focus handler");

    require(registry.focusNode(document, first), "initial focus failed");
    firstLog.redirect = second;
    require(
        !registry.focusNode(document, second),
        "outer focus request must not report stale success after reentrant takeover");
    require(registry.routeNavigation(NavigationCommand::Accept), "redirected focus owner should receive Accept");
    require(secondLog.activates == 1, "redirected focus did not leave second node authoritative");

    firstLog.clearOnGain = true;
    require(
        !registry.focusNode(document, first),
        "focus-gained callback that clears focus must not report stale success");
    require(!registry.routeNavigation(NavigationCommand::Accept), "cleared reentrant focus must leave no activation owner");

    const auto stale = second;
    require(registry.destroyNode(document, second), "could not destroy stale-handle node");
    const auto replacement = registry.createButton(document, root, "focus-c", "C");
    require(replacement != 0 && replacement != stale, "destroyed node handle must never be reused");
    require(!registry.setNodeVisible(document, stale, false), "stale handle became valid after later allocation");
}

void testDocumentKeyCallbacks() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("key-root");
    require(document != 0, "could not create key document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable key document input");

    const auto root = registry.rootNode(document);
    const auto button = registry.createButton(document, root, "key-button", "Key target");
    require(button != 0, "could not create key target");

    FocusLog focusLog{.registry = &registry, .document = document};
    require(registry.setNodeEventHandler(document, button, &focusCallback, &focusLog), "could not register key target event handler");
    require(registry.focusNode(document, button), "could not focus key target");

    KeyLog keyLog{.registry = &registry, .consume = true};
    require(registry.setDocumentKeyEventHandler(document, &keyCallback, &keyLog), "could not register document key handler");
    require(registry.hasDocumentKeyEventHandler(document), "document key handler was not retained");

    require(
        registry.routeDocumentKeyEvent(KeyEvent{
            .type = KeyEventType::Down,
            .keyCode = KeyEscape,
        }),
        "consumed Escape down must report handled");
    require(
        registry.routeDocumentKeyEvent(KeyEvent{
            .type = KeyEventType::Up,
            .keyCode = KeyEscape,
        }),
        "consumed Escape up must report handled");
    require(keyLog.events.size() == 2, "document key handler did not receive both make and break");
    require(keyLog.events[0].type == UIREPO_KEY_DOWN && keyLog.events[0].keyCode == KeyEscape, "Escape down callback payload was wrong");
    require(keyLog.events[1].type == UIREPO_KEY_UP && keyLog.events[1].keyCode == KeyEscape, "Escape up callback payload was wrong");

    keyLog.consume = false;
    const KeyEvent enter{.type = KeyEventType::Down, .keyCode = KeyEnter};
    require(!registry.routeDocumentKeyEvent(enter), "non-consuming document key callback must fall through");
    require(registry.routeKeyEvent(enter), "built-in key routing must remain available after callback fall-through");
    require(focusLog.activates == 1, "fall-through Enter did not activate the focused control");

    keyLog.consume = true;
    keyLog.destroyOnDown = true;
    require(
        registry.routeDocumentKeyEvent(KeyEvent{
            .type = KeyEventType::Down,
            .keyCode = KeyEscape,
        }),
        "self-destroying key callback must complete without deadlock");
    require(!registry.hasDocumentKeyEventHandler(document), "destroyed document retained a key handler");
    require(
        !registry.setDocumentKeyEventHandler(document, &keyCallback, &keyLog),
        "destroyed document handle became valid for key registration");
}

void testPointerCallbackMayDestroyItself() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("self-destroy-root");
    require(document != 0, "could not create self-destroy document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable self-destroy input");

    const auto root = registry.rootNode(document);
    const auto button = registry.createButton(document, root, "self-destroy", "Destroy");
    require(button != 0, "could not create self-destroy button");
    require(registry.setNodeStyle(document, button, fixedStyle(100.0f, 50.0f)), "could not style self-destroy button");

    PointerLog log{
        .registry = &registry,
        .destroyOnEvent = button,
        .destroyEvent = UIREPO_NODE_POINTER_ENTER,
    };
    require(registry.setNodePointerEventHandler(document, button, &pointerCallback, &log), "could not register self-destroy handler");
    registry.layoutVisible(Size{240.0f, 160.0f});

    (void)registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 10.0f, .y = 10.0f});
    require(!registry.setNodeVisible(document, button, false), "self-destroy callback did not invalidate its own handle");
}

void testPointerLeaveDoesNotClampCapturedSlider() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("leave-slider-root");
    require(document != 0, "could not create leave-slider document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable input");

    const auto root = registry.rootNode(document);
    const auto slider = registry.createSlider(document, root, "slider", 0.0f, 0.0f, 100.0f, 1.0f);
    require(slider != 0, "could not create slider");
    require(registry.setNodeStyle(document, slider, fixedStyle(200.0f, 40.0f)), "could not style slider");
    registry.layoutVisible(Size{240.0f, 160.0f});

    float value = -1.0f;
    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonDown,
            .button = PointerButton::Left,
            .x = 100.0f,
            .y = 20.0f,
        }),
        "press on the slider must be consumed");
    require(registry.hasPointerCapture(), "press must acquire capture");
    require(
        registry.getSliderValue(document, slider, value) && value == 50.0f,
        "press at midpoint jumps to 50");

    require(registry.handlePointerLeave(), "explicit pointer leave must be handled");
    require(
        registry.getSliderValue(document, slider, value) && value == 50.0f,
        "pointer leave must NOT mutate the captured slider value");
    require(registry.hasPointerCapture(), "pointer leave must NOT release drag capture");

    require(
        registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 150.0f, .y = 20.0f}),
        "a real captured move after leave must be consumed");
    require(
        registry.getSliderValue(document, slider, value) && value == 75.0f,
        "captured drag must still track after a leave");

    require(
        registry.routePointerEvent(PointerEvent{
            .type = PointerEventType::ButtonUp,
            .button = PointerButton::Left,
            .x = 150.0f,
            .y = 20.0f,
        }),
        "release must be consumed");
    require(!registry.hasPointerCapture(), "release clears capture");
}

void testPointerLeaveClearsHover() {
    DocumentRegistry registry;
    const auto document = registry.createDocument("leave-hover-root");
    require(document != 0, "could not create leave-hover document");
    require(registry.setDocumentInputEnabled(document, true), "could not enable input");

    const auto root = registry.rootNode(document);
    const auto button = registry.createButton(document, root, "hover", "H");
    require(button != 0, "could not create hover button");
    require(registry.setNodeStyle(document, button, fixedStyle(80.0f, 40.0f)), "could not style hover button");

    PointerLog log{};
    require(registry.setNodePointerEventHandler(document, button, &pointerCallback, &log), "could not register hover handler");
    registry.layoutVisible(Size{240.0f, 160.0f});

    (void)registry.routePointerEvent(PointerEvent{.type = PointerEventType::Move, .x = 20.0f, .y = 20.0f});
    require(
        !log.events.empty() && log.events.back() == UIREPO_NODE_POINTER_ENTER,
        "hover move must emit enter");

    require(registry.handlePointerLeave(), "explicit pointer leave must be handled");
    require(log.events.back() == UIREPO_NODE_POINTER_LEAVE, "pointer leave must clear hover with a single leave");
}

} // namespace

int main() {
    testPointerSequenceOwnership();
    testWheelRouting();
    testHoverTransitionsAndCleanup();
    testPointerCaptureDragAndCancel();
    testSliderPointerTracking();
    testPointerLeaveDoesNotClampCapturedSlider();
    testPointerLeaveClearsHover();
    testReentrantFocusAndStaleHandles();
    testDocumentKeyCallbacks();
    testPointerCallbackMayDestroyItself();
    std::cout << "UIRepo input lifecycle regressions passed\n";
    return 0;
}
