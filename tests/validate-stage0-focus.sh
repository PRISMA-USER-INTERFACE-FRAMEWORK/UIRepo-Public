#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "Stage 0 focus validation failed: $*" >&2
    exit 1
}

grep -Fq 'installWindowLifecycleSink' native/fo4/src/Stage0Diagnostics.h || fail "window lifecycle observer API is missing"
grep -Fq 'consumeFocusRecoveryTransition' native/fo4/src/Stage0Diagnostics.h || fail "focus recovery checkpoint API is missing"
grep -Fq 'SetWindowSubclass' native/fo4/src/Stage0Diagnostics.cpp || fail "window lifecycle observer must use a non-invasive window subclass"
grep -Fq 'WM_ACTIVATEAPP' native/fo4/src/Stage0Diagnostics.cpp || fail "alt-tab app activation diagnostics are missing"
grep -Fq 'app focus {} (transition {})' native/fo4/src/Stage0Diagnostics.cpp || fail "ordered app-focus transition diagnostics are missing"
grep -Fq 'g_pendingFocusRecoveryTransition' native/fo4/src/Stage0Diagnostics.cpp || fail "focus returns must create a pending render-recovery checkpoint"
grep -Fq 'window lifecycle sink installed' native/fo4/src/Plugin.cpp || fail "plugin bootstrap must prove the window lifecycle observer installed"
grep -Fq 'focus recovery frame rendered (transition {}' native/fo4/src/Runtime.cpp || fail "successful probe rendering after focus return is not logged"
grep -Fq 'FOCUS_RE' scripts/stage0-verify-log.py || fail "Stage 0 verifier must parse app-focus transitions"
grep -Fq 'alt-tab focus cycles' scripts/stage0-verify-log.py || fail "full-cycle verifier must require alt-tab focus cycles"
grep -Fq 'contiguous app-focus transition sequence' scripts/stage0-verify-log.py || fail "full-cycle verifier must reject broken focus-transition evidence"
grep -Fq 'FOCUS_RECOVERY_RE' scripts/stage0-verify-repeated-lifecycle.py || fail "final repeated-lifecycle verifier must parse focus recovery frames"
grep -Fq 'returned to a successful probe frame' scripts/stage0-verify-repeated-lifecycle.py || fail "final evidence must require successful rendering after each focus return"
grep -Fq 'accepted fewer than five alt-tab focus cycles' tests/stage0-log-verifier.sh || fail "missing four-cycle rejection regression"
grep -Fq 'accepted a non-contiguous app-focus transition sequence' tests/stage0-log-verifier.sh || fail "missing broken focus-sequence rejection regression"
grep -Fq 'accepted an alt-tab return without a successful probe frame' tests/stage0-repeated-lifecycle.sh || fail "missing focus-return render recovery rejection regression"
grep -Fq 'accepted an alt-tab recovery frame with zero glyph execution' tests/stage0-repeated-lifecycle.sh || fail "missing focus recovery execution regression"

echo "Stage 0 focus/alt-tab contract passed"
