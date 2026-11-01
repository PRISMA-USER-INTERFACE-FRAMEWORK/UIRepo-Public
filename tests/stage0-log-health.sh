#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

clean="$tmp/clean.log"
cat > "$clean" <<'LOG'
UIRepo_F4 Stage0: native bootstrap loaded (build test, runtime 1.10.163, F4SE 0.6.23, commonlib_NonVR eb101e090f0f3010c66f8d0dd4d2331cb91711ac)
UIRepo_F4: initialized Fallout-native D3D11 renderer generation 1 initialized (feature level 0xB000)
UIRepo_F4: Fallout-native render target generation 2 acquired (1920x1080, format 28, samples 1)
UIRepo_F4: first native probe frame rendered (rects 1, text 2, glyph runs 2, clips 0/0)
LOG

python3 scripts/stage0-verify-log-health.py "$clean"

markers=(
  'UIRepo_F4: Fallout render target is not owned by the renderer D3D11 device'
  'UIRepo_F4: Fallout renderer device returned no immediate D3D11 context'
  'UIRepo_F4: Fallout-native renderer initialization failed'
  'UIRepo_F4: Fallout render target resource is not a D3D11 texture'
  'UIRepo_F4: fallback IDXGISwapChain::GetDevice failed'
  'UIRepo_F4: fallback D3D11 immediate context is unavailable'
  'UIRepo_F4: fallback renderer initialization failed'
  'UIRepo_F4: fallback failed to acquire swap-chain backbuffer'
  'UIRepo_F4: fallback failed to create backbuffer RTV'
  'UIRepo_F4: renderer rejected the built-in probe display list'
  'UIRepo_F4: built-in probe frame failed'
)

for index in "${!markers[@]}"; do
  polluted="$tmp/polluted-$index.log"
  cp "$clean" "$polluted"
  printf '%s\n' "${markers[$index]}" >> "$polluted"
  if python3 scripts/stage0-verify-log-health.py "$polluted" >/dev/null 2>&1; then
    echo "Stage 0 log-health verifier accepted renderer failure marker: ${markers[$index]}" >&2
    exit 1
  fi
done

echo "Stage 0 renderer log-health verifier regressions passed"
