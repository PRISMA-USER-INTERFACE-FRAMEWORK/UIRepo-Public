#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
router = (root / "native/cpp/include/uirepo/InputRouter.h").read_text(encoding="utf-8")
api = (root / "native/cpp/include/uirepo/NativeApi.h").read_text(encoding="utf-8")
native_api = "".join(
    path.read_text(encoding="utf-8") for path in sorted((root / "native/fo4/src").glob("NativeApi*.cpp"))
)
dispatcher = (root / "native/fo4/src/GameThreadDispatcher.cpp").read_text(encoding="utf-8")
registry_header = (root / "native/fo4/src/DocumentRegistry.h").read_text(encoding="utf-8")
registry_pointer = (root / "native/fo4/src/DocumentRegistryPointer.cpp").read_text(encoding="utf-8")
abi_tests = (root / "native/cpp/tests/NativeApiContractTests.cpp").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"extended pointer validation failed: {message}")


for token in (
    "HorizontalWheel",
    "PointerButton::X1",
    "PointerButton::X2",
    "std::array<SequenceOwner, 5>",
    "routeHorizontalWheel",
    "stack->axis() != Axis::Row",
):
    require(token in router, f"portable input contract is missing {token}")

for token in (
    "UIREPO_POINTER_HORIZONTAL_WHEEL = 5",
    "UIREPO_POINTER_BUTTON_X1 = 4",
    "UIREPO_POINTER_BUTTON_X2 = 5",
):
    require(token in api, f"public V1 enum contract is missing {token}")

require(
    "static_assert(sizeof(UIRepoPointerEventV1) == 28);" in abi_tests,
    "UIRepoPointerEventV1 layout must remain frozen at 28 bytes",
)
require(
    "static_assert(sizeof(UIRepoNodePointerEventV1) == 28);" in abi_tests,
    "UIRepoNodePointerEventV1 layout must remain frozen at 28 bytes",
)
require(
    "static_assert(sizeof(UIRepoNativeApiV1) == 432);" in abi_tests,
    "UIRepoNativeApiV1 layout must remain frozen at 432 bytes",
)

for token in (
    "WM_XBUTTONDOWN",
    "WM_XBUTTONUP",
    "WM_XBUTTONDBLCLK",
    "GET_XBUTTON_WPARAM",
    "WM_MOUSEHWHEEL",
    "PointerEventType::HorizontalWheel",
    "routeHorizontalWheelEvent",
):
    require(token in dispatcher, f"Win32 adapter is missing {token}")

require(
    dispatcher.count("input::PointerButton::X1") >= 2 and dispatcher.count("input::PointerButton::X2") >= 2,
    "X1/X2 must participate in both routing and focus-loss sequence flushing",
)
require(
    "return TRUE;" in dispatcher[dispatcher.index("case WM_XBUTTONDOWN"):dispatcher.index("case WM_MOUSEWHEEL")],
    "handled XBUTTON messages must return TRUE per the Win32 contract",
)
require(
    "routeHorizontalWheelEvent(Point point, float wheelDelta)" in registry_header,
    "registry horizontal-wheel entry point is missing",
)
require(
    "input::routeHorizontalWheel(*entry->document, point, wheelDelta)" in registry_pointer,
    "registry must route horizontal wheel through the retained row-scroll helper",
)

for token in (
    "case UIREPO_POINTER_HORIZONTAL_WHEEL:",
    "destination.type = input::PointerEventType::HorizontalWheel;",
    "case UIREPO_POINTER_BUTTON_X1:",
    "destination.button = input::PointerButton::X1;",
    "case UIREPO_POINTER_BUTTON_X2:",
    "destination.button = input::PointerButton::X2;",
    "registry.routeHorizontalWheelEvent(Point{event.x, event.y}, event.wheelDelta)",
):
    require(token in native_api, f"V1 pointer conversion is missing {token}")

print("extended pointer input contract validated")
