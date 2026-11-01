#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "Stage 0 recovery validation failed: $*" >&2
    exit 1
}

grep -Fq 'lastReportedProbeGeneration_' native/fo4/src/Runtime.h || fail "runtime must retain the last successfully reported probe generation"
grep -Fq 'lastReportedProbeGeneration_ != rendererGeneration_' native/fo4/src/Runtime.cpp || fail "probe recovery logging must be generation-gated"
grep -Fq 'native probe recovery frame rendered (generation {}' native/fo4/src/Runtime.cpp || fail "runtime must log successful probe rendering after renderer/target generation changes"
grep -Fq 'RECOVERY_FRAME_RE' scripts/stage0-verify-log.py || fail "Stage 0 verifier must parse recovery-frame diagnostics"
grep -Fq 'post-resize probe recovery frame' scripts/stage0-verify-log.py || fail "full-cycle verification must require a post-resize recovery frame"
grep -Fq 'probe recovery frame generation does not match' scripts/stage0-verify-log.py || fail "recovery-frame generation must match the reacquired render target"
grep -Fq 'reacquired target without a successful probe recovery frame' tests/stage0-log-verifier.sh || fail "missing recovery-frame regression is absent"
grep -Fq 'recovery frame from the wrong target generation' tests/stage0-log-verifier.sh || fail "wrong-generation recovery regression is absent"

echo "Stage 0 probe recovery contract passed"
