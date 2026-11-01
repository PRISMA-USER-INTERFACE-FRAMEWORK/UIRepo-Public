#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "Stage 0 log-health validation failed: $*" >&2
    exit 1
}

runtime=native/fo4/src/Runtime.cpp
verifier=scripts/stage0-verify-log-health.py
repeated=scripts/stage0-verify-repeated-lifecycle.py
regression=tests/stage0-log-health.sh

test -f "$verifier" || fail "renderer log-health verifier is missing"
test -f "$regression" || fail "renderer log-health verifier regression is missing"
grep -Fq 'struct RuntimeErrorOnce final' "$runtime" || fail "runtime renderer failures must be guarded against per-frame log spam"
for field in \
    targetDeviceMismatch \
    engineImmediateContextUnavailable \
    engineRendererInitialization \
    engineTargetNotTexture \
    fallbackGetDevice \
    fallbackImmediateContextUnavailable \
    fallbackRendererInitialization \
    fallbackGetBuffer \
    fallbackCreateRenderTargetView \
    probeFrameException \
    probeFrameUnknownException; do
    grep -Fq "g_runtimeErrorOnce.$field" "$runtime" || fail "runtime log-once guard is missing for $field"
done
grep -Fq 'RENDER_FAILURE_MARKERS' "$verifier" || fail "log-health verifier must enumerate renderer failure classes"
grep -Fq 'renderer failure marker observed' "$verifier" || fail "log-health verifier must reject observed renderer failures"
grep -Fq 'RENDER_FAILURE_MARKERS' "$repeated" || fail "final repeated-lifecycle verifier must enforce renderer log health"
grep -Fq 'renderer failure marker observed' "$repeated" || fail "final repeated-lifecycle evidence must reject renderer failure diagnostics"

echo "Stage 0 renderer log-health contract passed"
