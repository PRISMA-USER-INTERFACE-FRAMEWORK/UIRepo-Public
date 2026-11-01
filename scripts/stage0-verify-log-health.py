#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import sys


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


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject Stage 0 logs containing UIRepo renderer failure-path diagnostics."
    )
    parser.add_argument("log", type=Path, help="Path to UIRepo_F4.log")
    args = parser.parse_args()

    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"FAIL: could not read {args.log}: {exc}", file=sys.stderr)
        return 2

    failures: list[tuple[str, int]] = []
    for marker in RENDER_FAILURE_MARKERS:
        count = text.count(marker)
        if count:
            failures.append((marker, count))
            print(f"FAIL: renderer failure marker observed {count} time(s): {marker}")

    if failures:
        print(f"Stage 0 renderer log health failed: {len(failures)} failure class(es) observed.")
        return 1

    print("PASS: Stage 0 log contains no known UIRepo renderer failure-path diagnostics.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
