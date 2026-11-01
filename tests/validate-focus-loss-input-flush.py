#!/usr/bin/env python3
from pathlib import Path

source = Path("native/fo4/src/GameThreadDispatcher.cpp").read_text(encoding="utf-8")


def section(start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


pointer_flush = section("flushOwnedPointerSequences()", "void flushOwnedKeySequences()")
assert "SequenceOwner::UIRepo" in pointer_flush
assert "PointerEventType::ButtonUp" in pointer_flush
assert "kSyntheticReleaseCoordinate" in pointer_flush
assert "handlePointerLeave()" in pointer_flush
assert pointer_flush.index("PointerEventType::ButtonUp") < pointer_flush.index("g_pointerSequences.clear()")
assert pointer_flush.index("g_pointerSequences.clear()") < pointer_flush.index("handlePointerLeave()")

key_flush = section("void flushOwnedKeySequences()", "void resetInputOwnership(")
assert "KeySequenceState::kVirtualKeyCount" in key_flush
assert "SequenceOwner::UIRepo" in key_flush
assert "KeyEventType::Up" in key_flush
assert "KeyEscape" in key_flush
assert "g_rawEscapeDocument" in key_flush
assert key_flush.index("KeyEventType::Up") < key_flush.index("g_keySequences.clear()")
assert key_flush.index("KeyEscape") < key_flush.index("g_rawEscapeOwned = false")

reset = section("void resetInputOwnership(", "void armMouseLeaveTracking(")
for required in (
    "flushOwnedPointerSequences()",
    "flushOwnedKeySequences()",
    "g_textDecoder.clear()",
    "g_mouseLeaveArmed = false",
    "ActiveDeviceState::resetPointerBaseline()",
    "cancelPointerCapture()",
):
    assert required in reset, f"resetInputOwnership lost {required}"
assert reset.index("flushOwnedPointerSequences()") < reset.index("ActiveDeviceState::resetPointerBaseline()")
assert reset.index("flushOwnedKeySequences()") < reset.index("ActiveDeviceState::resetPointerBaseline()")

capture = section("case WM_CAPTURECHANGED:", "case WM_CANCELMODE:")
assert "flushOwnedPointerSequences()" in capture, "capture loss must flush an owned pointer press before focus-loss messages arrive"

focus_loss = section("case WM_CANCELMODE:", "default:")
for marker in ("case WM_KILLFOCUS:", "case WM_ACTIVATEAPP:"):
    assert marker in focus_loss
# Raw Escape ownership is stored in FocusLeaseManager, so synthetic releases must
# be routed before releaseAll() erases the owning document from the lease state.
assert focus_loss.index("resetInputOwnership(hwnd)") < focus_loss.index("FocusLeaseManager::releaseAll()")
activate = focus_loss[focus_loss.index("case WM_ACTIVATEAPP:"):]
assert activate.index("resetInputOwnership(hwnd)") < activate.index("FocusLeaseManager::releaseAll()")

assert "resetInputOwnership(existing, true)" not in source
assert "resetInputOwnership(hwnd, true)" not in source

print("focus-loss input flush contract passed")
