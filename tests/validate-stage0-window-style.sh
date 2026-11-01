#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "Stage 0 window-style validation failed: $*" >&2
    exit 1
}

header=native/fo4/src/Stage0Diagnostics.h
diagnostics=native/fo4/src/Stage0Diagnostics.cpp
runtime=native/fo4/src/Runtime.cpp
verifier=scripts/stage0-verify-repeated-lifecycle.py
regression=tests/stage0-repeated-lifecycle.sh

grep -Fq 'consumeWindowStyleRecoveryTransition' "$header" || fail "window-style recovery checkpoint API is missing"
grep -Fq 'WM_STYLECHANGED' "$diagnostics" || fail "window-style lifecycle diagnostics are missing"
grep -Fq 'GWL_STYLE' "$diagnostics" || fail "normal window-style changes are not observed"
grep -Fq 'GWL_EXSTYLE' "$diagnostics" || fail "extended window-style changes are not observed"
grep -Fq 'window style changed (transition {}' "$diagnostics" || fail "ordered window-style diagnostics are missing"
grep -Fq 'g_pendingWindowStyleRecoveryTransition' "$diagnostics" || fail "window-style changes must create a pending recovery checkpoint"
grep -Fq 'window style recovery frame rendered (transition {}' "$runtime" || fail "successful rendering after window-style changes is not logged"
grep -Fq 'WINDOW_STYLE_RE' "$verifier" || fail "final verifier must parse window-style transitions"
grep -Fq 'WINDOW_STYLE_RECOVERY_RE' "$verifier" || fail "final verifier must parse window-style recovery frames"
grep -Fq 'conditional recovery gate not applicable' "$verifier" || fail "window-style verification must remain conditional when no live style change occurs"
grep -Fq 'accepted a live window-style change without rendered recovery' "$regression" || fail "missing window-style recovery regression is absent"
grep -Fq 'accepted recovery for a stale window-style transition' "$regression" || fail "stale window-style recovery regression is absent"

echo "Stage 0 conditional window-style recovery contract passed"
