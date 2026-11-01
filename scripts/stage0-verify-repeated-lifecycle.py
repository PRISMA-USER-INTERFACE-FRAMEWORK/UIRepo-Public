#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


RESIZE_MARKER = "UIRepo_F4 Stage0: ResizeBuffers pre-release of native render target"
RESIZE_COMPLETION_RE = re.compile(
    r"UIRepo_F4 Stage0: ResizeBuffers completed \(buffers (\d+), requested (\d+)x(\d+), "
    r"format (\d+), flags 0x([0-9A-Fa-f]+), HRESULT 0x([0-9A-Fa-f]{8})\)"
)
TARGET_RE = re.compile(
    r"(?:Fallout-native|fallback) render target generation (\d+) acquired "
    r"\((\d+)x(\d+), format (\d+), samples (\d+)"
)
RECOVERY_RE = re.compile(
    r"UIRepo_F4 Stage0: native probe recovery frame rendered \(generation (\d+), "
    r"rects (\d+), text (\d+), glyph runs (\d+), clips (\d+)/(\d+)\)"
)
FOCUS_RE = re.compile(
    r"UIRepo_F4 Stage0: app focus (active|inactive) \(transition (\d+)\)"
)
FOCUS_RECOVERY_RE = re.compile(
    r"UIRepo_F4 Stage0: focus recovery frame rendered \(transition (\d+), generation (\d+), "
    r"rects (\d+), text (\d+), glyph runs (\d+), clips (\d+)/(\d+)\)"
)
WINDOW_STYLE_RE = re.compile(
    r"UIRepo_F4 Stage0: window style changed \(transition (\d+), index (-?\d+), "
    r"old 0x([0-9A-Fa-f]{8}), new 0x([0-9A-Fa-f]{8})\)"
)
WINDOW_STYLE_RECOVERY_RE = re.compile(
    r"UIRepo_F4 Stage0: window style recovery frame rendered \(transition (\d+), generation (\d+), "
    r"rects (\d+), text (\d+), glyph runs (\d+), clips (\d+)/(\d+)\)"
)
MAIN_MENU_MARKER = "UIRepo_F4 Stage0: main-menu opened"
RENDER_FAILURE_MARKERS = (
    "UIRepo_F4: Fallout render target is not owned by the renderer D3D11 device",
    "UIRepo_F4: Fallout renderer device returned no immediate D3D11 context",
    "UIRepo_F4: Fallout-native renderer initialization failed",
    "UIRepo_F4: Fallout render target resource is not a D3D11 texture",
    "UIRepo_F4: fallback IDXGISwapChain::GetDevice failed",
    "UIRepo_F4: fallback D3D11 immediate context is unavailable",
    "UIRepo_F4: fallback renderer initialization failed",
    "UIRepo_F4: fallback failed to acquire swap-chain backbuffer",
    "UIRepo_F4: fallback failed to create backbuffer RTV",
    "UIRepo_F4: renderer rejected the built-in probe display list",
    "UIRepo_F4: built-in probe frame failed",
)


def verify_recovery_counters(
    label: str,
    generation: int,
    rects: int,
    text_commands: int,
    glyph_runs: int,
    clip_pushes: int,
    clip_pops: int,
) -> list[str]:
    failures: list[str] = []
    if generation <= 0 or rects <= 0 or text_commands <= 0 or glyph_runs <= 0:
        print(
            f"FAIL: {label} counters must be non-zero "
            f"(generation {generation}, rects {rects}, text {text_commands}, glyph runs {glyph_runs})"
        )
        failures.append(f"{label} execution")
    if clip_pushes != clip_pops:
        print(f"FAIL: {label} clips are unbalanced ({clip_pushes}/{clip_pops})")
        failures.append(f"{label} clip balance")
    return failures


def verify_resize_cycle(segment: str, index: int) -> list[str]:
    failures: list[str] = []
    completion = RESIZE_COMPLETION_RE.search(segment)
    if completion is None:
        print(f"MISSING: resize cycle {index} completion")
        return [f"resize cycle {index} completion"]

    _, requested_width, requested_height, _, _, hresult = completion.groups()
    result_code = int(hresult, 16)
    if result_code & 0x80000000:
        print(f"FAIL: resize cycle {index} returned HRESULT 0x{hresult}")
        failures.append(f"resize cycle {index} successful completion")

    target = TARGET_RE.search(segment, completion.end())
    if target is None:
        print(f"MISSING: resize cycle {index} target reacquisition")
        failures.append(f"resize cycle {index} target reacquisition")
        return failures

    generation, width, height, fmt, samples = map(int, target.groups())
    request_width = int(requested_width)
    request_height = int(requested_height)
    if request_width > 0 and request_height > 0 and (
        width != request_width or height != request_height
    ):
        print(
            f"FAIL: resize cycle {index} target size {width}x{height} does not match "
            f"requested {request_width}x{request_height}"
        )
        failures.append(f"resize cycle {index} target dimensions")

    recovery = RECOVERY_RE.search(segment, target.end())
    if recovery is None:
        print(f"MISSING: resize cycle {index} probe recovery frame")
        failures.append(f"resize cycle {index} probe recovery frame")
        return failures

    (
        recovery_generation,
        rects,
        text_commands,
        glyph_runs,
        clip_pushes,
        clip_pops,
    ) = map(int, recovery.groups())
    if recovery_generation != generation:
        print(
            f"FAIL: resize cycle {index} recovery generation {recovery_generation} "
            f"does not match target generation {generation}"
        )
        failures.append(f"resize cycle {index} recovery generation")
    failures.extend(
        verify_recovery_counters(
            f"resize cycle {index} recovery",
            recovery_generation,
            rects,
            text_commands,
            glyph_runs,
            clip_pushes,
            clip_pops,
        )
    )

    if not failures:
        print(
            f"PASS: resize cycle {index} recovered and rendered "
            f"(generation {generation}, {width}x{height}, format {fmt}, samples {samples})"
        )
    return failures


def verify_focus_recovery(text: str) -> list[str]:
    failures: list[str] = []
    focus_matches = list(FOCUS_RE.finditer(text))
    expected_transition = 1
    inactive_pending = False
    required_returns: list[tuple[int, int, int]] = []

    for index, match in enumerate(focus_matches):
        state = match.group(1)
        transition = int(match.group(2))
        if transition != expected_transition:
            print(
                "FAIL: app-focus transition sequence is not contiguous "
                f"(expected {expected_transition}, got {transition})"
            )
            return ["contiguous app-focus transition sequence"]
        expected_transition += 1

        if state == "inactive":
            inactive_pending = True
            continue
        if inactive_pending:
            next_focus_start = (
                focus_matches[index + 1].start() if index + 1 < len(focus_matches) else len(text)
            )
            required_returns.append((transition, match.end(), next_focus_start))
            inactive_pending = False

    if len(required_returns) < 5:
        print(
            "MISSING: focus recovery cycles "
            f"(found {len(required_returns)}, need at least 5 inactive/active returns)"
        )
        failures.append("at least five focus recovery cycles")
        return failures

    recoveries: dict[int, list[re.Match[str]]] = {}
    for recovery in FOCUS_RECOVERY_RE.finditer(text):
        transition = int(recovery.group(1))
        recoveries.setdefault(transition, []).append(recovery)

    for transition, active_end, next_focus_start in required_returns:
        matches = recoveries.get(transition, [])
        if len(matches) != 1:
            print(
                f"FAIL: focus transition {transition} has {len(matches)} recovery frames; expected exactly 1"
            )
            failures.append(f"focus transition {transition} recovery frame count")
            continue

        recovery = matches[0]
        if recovery.start() < active_end or recovery.start() >= next_focus_start:
            print(
                f"FAIL: focus transition {transition} recovery frame was not rendered before the next focus transition"
            )
            failures.append(f"focus transition {transition} recovery ordering")
            continue

        (
            _,
            generation,
            rects,
            text_commands,
            glyph_runs,
            clip_pushes,
            clip_pops,
        ) = map(int, recovery.groups())
        current_failures = verify_recovery_counters(
            f"focus transition {transition} recovery",
            generation,
            rects,
            text_commands,
            glyph_runs,
            clip_pushes,
            clip_pops,
        )
        failures.extend(current_failures)
        if not current_failures:
            print(
                f"PASS: focus transition {transition} returned to a successful probe frame "
                f"(generation {generation}, rects {rects}, text {text_commands}, glyph runs {glyph_runs})"
            )

    return failures


def verify_window_style_recovery(text: str) -> list[str]:
    failures: list[str] = []
    events: list[tuple[int, str, re.Match[str]]] = []
    for match in WINDOW_STYLE_RE.finditer(text):
        events.append((match.start(), "style", match))
    for match in WINDOW_STYLE_RECOVERY_RE.finditer(text):
        events.append((match.start(), "recovery", match))
    events.sort(key=lambda item: item[0])

    if not any(kind == "style" for _, kind, _ in events):
        print("PASS: no live window-style transition was observed; conditional recovery gate not applicable")
        return failures

    expected_transition = 1
    pending_transition: int | None = None
    observed_styles = 0

    for _, kind, match in events:
        if kind == "style":
            transition = int(match.group(1))
            index = int(match.group(2))
            old_style = int(match.group(3), 16)
            new_style = int(match.group(4), 16)
            if transition != expected_transition:
                print(
                    "FAIL: window-style transition sequence is not contiguous "
                    f"(expected {expected_transition}, got {transition})"
                )
                failures.append("contiguous window-style transition sequence")
                expected_transition = transition + 1
            else:
                expected_transition += 1
            if index not in {-16, -20}:
                print(f"FAIL: window-style transition {transition} used unexpected index {index}")
                failures.append(f"window-style transition {transition} index")
            if old_style == new_style:
                print(f"FAIL: window-style transition {transition} did not change the style value")
                failures.append(f"window-style transition {transition} value")
            pending_transition = transition
            observed_styles += 1
            continue

        transition, generation, rects, text_commands, glyph_runs, clip_pushes, clip_pops = map(
            int, match.groups()
        )
        if pending_transition is None:
            print(f"FAIL: unexpected window-style recovery frame for transition {transition}")
            failures.append(f"unexpected window-style recovery {transition}")
            continue
        if transition != pending_transition:
            print(
                f"FAIL: window-style recovery transition {transition} does not match latest "
                f"pending style transition {pending_transition}"
            )
            failures.append("window-style recovery transition match")
            continue

        current_failures = verify_recovery_counters(
            f"window-style transition {transition} recovery",
            generation,
            rects,
            text_commands,
            glyph_runs,
            clip_pushes,
            clip_pops,
        )
        failures.extend(current_failures)
        if not current_failures:
            print(
                f"PASS: window-style transition {transition} recovered to a successful probe frame "
                f"(generation {generation})"
            )
        pending_transition = None

    if pending_transition is not None:
        print(
            f"MISSING: successful probe recovery after final window-style transition {pending_transition}"
        )
        failures.append("final window-style recovery frame")

    if not failures:
        print(f"PASS: observed window-style transitions recovered ({observed_styles} style changes)")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Verify repeated Stage 0 Fallout presentation/session lifecycle evidence that must be "
            "observed more than once in the final acceptance run."
        )
    )
    parser.add_argument("log", type=Path, help="Path to UIRepo_F4.log")
    args = parser.parse_args()

    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"FAIL: could not read {args.log}: {exc}", file=sys.stderr)
        return 2

    failures: list[str] = []

    for marker in RENDER_FAILURE_MARKERS:
        count = text.count(marker)
        if count:
            print(f"FAIL: renderer failure marker observed {count} time(s): {marker}")
            failures.append(f"renderer failure marker: {marker}")
    if not any(marker in text for marker in RENDER_FAILURE_MARKERS):
        print("PASS: no known UIRepo renderer failure-path diagnostics were observed")

    failures.extend(verify_focus_recovery(text))
    failures.extend(verify_window_style_recovery(text))

    main_menu_opens = text.count(MAIN_MENU_MARKER)
    if main_menu_opens >= 3:
        print(
            "PASS: MainMenu lifecycle includes startup plus two returns "
            f"({main_menu_opens} opens)"
        )
    else:
        print(
            "MISSING: startup MainMenu plus two returns "
            f"(found {main_menu_opens} opens, need at least 3)"
        )
        failures.append("two MainMenu returns after startup")

    resize_positions = [match.start() for match in re.finditer(re.escape(RESIZE_MARKER), text)]
    if len(resize_positions) < 2:
        print(
            "MISSING: repeated ResizeBuffers cycles "
            f"(found {len(resize_positions)}, need at least 2)"
        )
        failures.append("at least two ResizeBuffers cycles")
    else:
        print(f"PASS: repeated ResizeBuffers pre-release observations ({len(resize_positions)})")

    for offset, start in enumerate(resize_positions, start=1):
        end = resize_positions[offset] if offset < len(resize_positions) else len(text)
        segment_start = start + len(RESIZE_MARKER)
        failures.extend(verify_resize_cycle(text[segment_start:end], offset))

    if failures:
        print(f"Repeated lifecycle verification failed: {len(failures)} check(s).")
        return 1

    print("Repeated Stage 0 lifecycle verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
