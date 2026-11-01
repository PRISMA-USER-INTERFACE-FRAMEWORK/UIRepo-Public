#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
controls = (root / "native/cpp/include/uirepo/Controls.h").read_text(encoding="utf-8")
registry_header = (root / "native/fo4/src/DocumentRegistry.h").read_text(encoding="utf-8")
registry_controls = (root / "native/fo4/src/DocumentRegistryControls.cpp").read_text(encoding="utf-8")
dispatcher = (root / "native/fo4/src/GameThreadDispatcher.cpp").read_text(encoding="utf-8")
xmake = (root / "xmake.lua").read_text(encoding="utf-8")
control_tests = (root / "native/cpp/tests/ControlRegistryTests.cpp").read_text(encoding="utf-8")
ci_native = (root / "scripts/ci-native-core.sh").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"IME positioning validation failed: {message}")


for token in (
    "Rect endCaretRect(const TextMeasurer& measurer) const noexcept",
    "content.width - caretWidth",
    "content.height, std::max(1.0f, measuredHeight)",
):
    require(token in controls, f"retained caret geometry is missing {token}")

require(
    "focusedTextInputCaret(Rect& caret) const noexcept" in registry_header,
    "registry focused-text-input caret query is missing",
)
for token in (
    "inputEligible(*entry)",
    "case DocumentHealth::Creating:",
    "case DocumentHealth::Faulted:",
    "case DocumentHealth::Destroying:",
    "case DocumentHealth::Destroyed:",
    "dynamic_cast<const TextInputNode*>",
    "nodeInteractive(input)",
    "entry->document->focused() != input",
    "input->endCaretRect(entry->document->textMeasurer())",
):
    require(token in registry_controls, f"registry caret query is missing {token}")

for token in (
    "#include <imm.h>",
    "WM_IME_STARTCOMPOSITION",
    "ImmGetContext",
    "ImmSetCompositionWindow",
    "ImmSetCandidateWindow",
    "ImmReleaseContext",
    "CFS_FORCE_POSITION",
    "CFS_CANDIDATEPOS",
    "GetClientRect",
    "inputViewportSize()",
):
    require(token in dispatcher, f"Win32 IME adapter is missing {token}")

ime_helper = dispatcher[
    dispatcher.index("[[nodiscard]] bool retainedCaretClientPoints"):
    dispatcher.index("void failClosed")
]
require("ScreenToClient" not in ime_helper, "IME positioning must stay in client coordinates")
require("ClientToScreen" not in ime_helper, "IME positioning must not convert to screen coordinates")
require("MapWindowPoints" not in ime_helper, "IME positioning must not convert to screen coordinates")

ime_case = dispatcher[
    dispatcher.index("case WM_IME_STARTCOMPOSITION:"):
    dispatcher.index("case WM_CAPTURECHANGED:")
]
require(
    "positionImeForFocusedTextInput(hwnd);" in ime_case,
    "WM_IME_STARTCOMPOSITION does not position the focused retained caret",
)
require("handled =" not in ime_case, "WM_IME_STARTCOMPOSITION must continue normal Win32 processing")
require("return DefSubclassProc(hwnd, message, wParam, lParam);" in dispatcher, "window subclass no longer forwards unhandled messages")

raw_escape = dispatcher[
    dispatcher.index("[[nodiscard]] bool routeOwnedRawEscape"):
    dispatcher.index("[[nodiscard]] bool routeUtf16Text")
]
require(
    raw_escape.count(".modifiers = inputModifiers(),") >= 2,
    "IME work must not change raw Escape down/up modifier routing",
)

uirepo_f4 = xmake[xmake.index('target("UIRepo_F4")'):xmake.index('target("UIRepoSample_F4")')]
require('"imm32"' in uirepo_f4, "UIRepo_F4 must link imm32 through the authoritative xmake target")

for token in (
    "testTextInputCaretGeometry();",
    "testFocusedTextInputCaretRegistryQuery();",
    "focusedTextInputCaret(caret)",
    "overflow.right() <= content.right()",
):
    require(token in control_tests, f"portable caret regression is missing {token}")

require(
    "python3 tests/validate-ime-positioning.py" in ci_native,
    "native-core CI does not run the IME source contract",
)

print("IME positioning contract validated")
