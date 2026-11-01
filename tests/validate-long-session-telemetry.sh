#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "long-session telemetry validation failed: $*" >&2
    exit 1
}

policy=native/cpp/include/uirepo/TelemetryPolicy.h
renderer_h=native/cpp/include/uirepo/D3D11Renderer.h
surface_h=native/cpp/include/uirepo/D3D11SurfaceManager.h
runtime_h=native/fo4/src/Runtime.h
runtime_cpp=native/fo4/src/Runtime.cpp
runtime_surface=native/fo4/src/RuntimeSurface.cpp
telemetry_cpp=native/fo4/src/RuntimeTelemetry.cpp
verifier=scripts/verify-renderer-telemetry.py

for path in "$policy" "$renderer_h" "$surface_h" "$runtime_h" "$runtime_cpp" "$runtime_surface" "$telemetry_cpp" "$verifier"; do
    test -f "$path" || fail "missing telemetry contract file: $path"
done

grep -Fq 'class PeriodicGate final' "$policy" || fail "periodic telemetry gate is missing"
grep -Fq 'next_ = now + interval_' "$policy" || fail "telemetry cadence must schedule from the current report time"
grep -Fq 'telemetry::PeriodicGate telemetryGate_{std::chrono::minutes(5)}' "$runtime_h" || fail "runtime telemetry interval must remain explicitly bounded to five minutes"
grep -Fq 'telemetryRendererIdentity_' "$runtime_h" || fail "telemetry must distinguish renderer lifetime from target generation"
grep -Fq 'maybeLogResourceSnapshot();' "$runtime_cpp" || fail "completed runtime frames do not evaluate telemetry cadence"
grep -Fq 'logResourceSnapshot("reset")' "$runtime_cpp" || fail "renderer reset does not preserve a final resource snapshot"
grep -Fq 'logResourceSnapshot("shutdown")' "$runtime_surface" || fail "offscreen shutdown does not capture live surface counters before teardown"
grep -Fq 'telemetryShutdownSnapshotLogged_ = true' "$runtime_surface" || fail "offscreen shutdown snapshot is not de-duplicated"
grep -Fq 'telemetryRendererIdentity_ != renderer_.get()' "$telemetry_cpp" || fail "renderer epoch does not follow renderer-object replacement"
grep -Fq 'renderer-epoch {} target-generation {}' "$telemetry_cpp" || fail "telemetry log must separate renderer epoch from target generation"
grep -Fq 'logResourceSnapshot("baseline")' "$telemetry_cpp" || fail "renderer lifetime changes do not emit a baseline snapshot"
grep -Fq 'logResourceSnapshot("periodic")' "$telemetry_cpp" || fail "periodic resource snapshots are not wired"
grep -Fq 'visible-docs {}' "$telemetry_cpp" || fail "telemetry log omits visible document count"
grep -Fq 'glyph(entries {} bytes {} hits {} misses {} uploads {} evictions {} rejections {})' "$telemetry_cpp" || fail "telemetry log omits glyph cache accounting"
grep -Fq 'image(entries {} bytes {} hits {} misses {} uploads {} evictions {} rejections {} decode-failures {} negative-hits {})' "$telemetry_cpp" || fail "telemetry log omits image cache accounting"
grep -Fq 'text-layout(entries {} bytes {} hits {} misses {} evictions {} rejections {})' "$telemetry_cpp" || fail "telemetry log omits text-layout cache accounting"
grep -Fq 'surfaces(live {} live-bytes {} pooled {} pooled-bytes {} allocations {} reuses {} resizes {} rebuilds {} rejections {} drains {})' "$telemetry_cpp" || fail "telemetry log omits offscreen surface accounting"

# Keep the evidence verifier's default caps synchronized with the runtime defaults.
grep -Fq 'DefaultGlyphCacheBytes = 16u * 1024u * 1024u' "$renderer_h" || fail "runtime glyph byte cap changed"
grep -Fq 'DefaultGlyphCacheEntries = 2048u' "$renderer_h" || fail "runtime glyph entry cap changed"
grep -Fq 'DefaultImageCacheBytes = 64u * 1024u * 1024u' "$renderer_h" || fail "runtime image byte cap changed"
grep -Fq 'DefaultImageCacheEntries = 256u' "$renderer_h" || fail "runtime image entry cap changed"
grep -Fq 'DefaultTextLayoutCacheBytes = 4u * 1024u * 1024u' "$renderer_h" || fail "runtime text-layout byte cap changed"
grep -Fq 'DefaultTextLayoutCacheEntries = 512u' "$renderer_h" || fail "runtime text-layout entry cap changed"
grep -Fq 'DefaultMaxLiveBytes = 128u * 1024u * 1024u' "$surface_h" || fail "runtime surface live-byte cap changed"
grep -Fq 'DefaultMaxPoolBytes = 64u * 1024u * 1024u' "$surface_h" || fail "runtime surface pool-byte cap changed"
grep -Fq 'DefaultMaxPoolEntries = 16' "$surface_h" || fail "runtime surface pool-entry cap changed"
grep -Fq 'GLYPH_MAX_ENTRIES = 2048' "$verifier" || fail "verifier glyph entry cap drifted"
grep -Fq 'GLYPH_MAX_BYTES = 16 * 1024 * 1024' "$verifier" || fail "verifier glyph byte cap drifted"
grep -Fq 'IMAGE_MAX_ENTRIES = 256' "$verifier" || fail "verifier image entry cap drifted"
grep -Fq 'IMAGE_MAX_BYTES = 64 * 1024 * 1024' "$verifier" || fail "verifier image byte cap drifted"
grep -Fq 'TEXT_LAYOUT_MAX_ENTRIES = 512' "$verifier" || fail "verifier text-layout entry cap drifted"
grep -Fq 'TEXT_LAYOUT_MAX_BYTES = 4 * 1024 * 1024' "$verifier" || fail "verifier text-layout byte cap drifted"
grep -Fq 'SURFACE_MAX_LIVE_BYTES = 128 * 1024 * 1024' "$verifier" || fail "verifier surface live-byte cap drifted"
grep -Fq 'SURFACE_MAX_POOL_ENTRIES = 16' "$verifier" || fail "verifier surface pool-entry cap drifted"
grep -Fq 'SURFACE_MAX_POOL_BYTES = 64 * 1024 * 1024' "$verifier" || fail "verifier surface pool-byte cap drifted"

if grep -R -n -F 'UIRepo_F4 telemetry:' native/fo4/src --include='*.cpp' | grep -v 'RuntimeTelemetry.cpp'; then
    fail "resource telemetry must have one centralized log site"
fi

echo "long-session telemetry contract passed"
