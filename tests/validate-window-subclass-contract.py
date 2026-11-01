#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dispatcher = (root / "native/fo4/src/GameThreadDispatcher.cpp").read_text(encoding="utf-8")
diagnostics = (root / "native/fo4/src/Stage0Diagnostics.cpp").read_text(encoding="utf-8")
plugin = (root / "native/fo4/src/Plugin.cpp").read_text(encoding="utf-8")


def between(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


# The two procs deliberately have separate IDs and separate responsibilities.
assert "kSubclassId = 0x55495245504F4744ull" in dispatcher
assert "kWindowLifecycleSubclassId = 0x53544730u" in diagnostics

# Stage0's proc is observational: it records activation/style transitions but may
# not own input state, focus leases, cursor capture, or message consumption.
stage0_proc = between(diagnostics, "LRESULT CALLBACK windowLifecycleProc(", "class MainMenuLifecycleSink")
assert "WM_ACTIVATEAPP" in stage0_proc
assert "g_pendingFocusRecoveryTransition" in stage0_proc
for forbidden in (
    "FocusLeaseManager",
    "resetInputOwnership",
    "ReleaseCapture",
    "return 0;",
):
    assert forbidden not in stage0_proc, f"Stage0 lifecycle proc must stay observational: found {forbidden}"
assert "return DefSubclassProc(hwnd, message, wParam, lParam);" in stage0_proc

# The dispatcher is the sole WM_ACTIVATEAPP input/focus owner. Its activation arm
# flushes retained input, releases the lease, leaves handled=false, then forwards
# the message through the subclass chain.
dispatch_proc = between(dispatcher, "LRESULT CALLBACK subclassProc(", "} // namespace")
activate = between(dispatch_proc, "case WM_ACTIVATEAPP:", "default:")
assert "wParam == FALSE" in activate
assert "resetInputOwnership(hwnd)" in activate
assert "FocusLeaseManager::releaseAll()" in activate
assert "handled =" not in activate
assert "return" not in activate
assert "return DefSubclassProc(hwnd, message, wParam, lParam);" in dispatch_proc

# Bring-up is deterministic: install the input dispatcher first and the Stage0
# observer second. Correctness does not depend on callback order because both
# procs forward WM_ACTIVATEAPP and only the dispatcher mutates input/focus state.
bootstrap = between(
    plugin,
    "GameThreadDispatcher::queueGameThreadBootstrap([hwnd]()",
    "});\n\n            if (rendererData->renderWindow[0].swapChain",
)
assert bootstrap.index("GameThreadDispatcher::attachWindow(hwnd)") < bootstrap.index(
    "Stage0Diagnostics::installWindowLifecycleSink"
)

print("window subclass activation contract passed")
