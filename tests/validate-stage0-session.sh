#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "Stage 0 session validation failed: $*" >&2
    exit 1
}

grep -Fq 'logSessionEvent(std::uint32_t type, const void* data)' native/fo4/src/Stage0Diagnostics.h || fail "session diagnostics must receive F4SE message data"
grep -Fq 'post-load-game success {}' native/fo4/src/Stage0Diagnostics.cpp || fail "post-load-game diagnostics must record F4SE load success"
grep -Fq 'data != nullptr ? "true" : "false"' native/fo4/src/Stage0Diagnostics.cpp || fail "post-load-game diagnostics must decode the F4SE boolean payload"
grep -Fq 'game-data-not-ready' native/fo4/src/Stage0Diagnostics.cpp || fail "game-data diagnostics must distinguish the not-ready notification"
grep -Fq 'logSessionEvent(message->type, message->data)' native/fo4/src/Plugin.cpp || fail "F4SE message payload must reach Stage 0 session diagnostics"
grep -Fq 'if (message->data == nullptr)' native/fo4/src/Plugin.cpp || fail "Fallout bootstrap must ignore the false GameDataReady notification"
grep -Fq 'successful load-game cycles' scripts/stage0-verify-log.py || fail "full-cycle verifier must require successful save loads"
grep -Fq 'no failed load-game attempts' scripts/stage0-verify-log.py || fail "full-cycle verifier must reject failed save loads"
grep -Fq 'accepted game-data-not-ready as ready' tests/stage0-log-verifier.sh || fail "not-ready bootstrap regression is missing"
grep -Fq 'accepted a failed save-load attempt' tests/stage0-log-verifier.sh || fail "failed save-load regression is missing"

echo "Stage 0 session/load-result contract passed"
