#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

log="$tmp/repeated-lifecycle.log"
cat > "$log" <<'LOG'
UIRepo_F4 Stage0: main-menu opened
UIRepo_F4 Stage0: main-menu closed
UIRepo_F4 Stage0: main-menu opened
UIRepo_F4 Stage0: app focus inactive (transition 1)
UIRepo_F4 Stage0: app focus active (transition 2)
UIRepo_F4 Stage0: focus recovery frame rendered (transition 2, generation 2, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: app focus inactive (transition 3)
UIRepo_F4 Stage0: app focus active (transition 4)
UIRepo_F4 Stage0: focus recovery frame rendered (transition 4, generation 2, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: app focus inactive (transition 5)
UIRepo_F4 Stage0: app focus active (transition 6)
UIRepo_F4 Stage0: focus recovery frame rendered (transition 6, generation 2, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: app focus inactive (transition 7)
UIRepo_F4 Stage0: app focus active (transition 8)
UIRepo_F4 Stage0: focus recovery frame rendered (transition 8, generation 2, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: app focus inactive (transition 9)
UIRepo_F4 Stage0: app focus active (transition 10)
UIRepo_F4 Stage0: focus recovery frame rendered (transition 10, generation 2, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: window style changed (transition 1, index -16, old 0x14CF0000, new 0x94000000)
UIRepo_F4 Stage0: window style changed (transition 2, index -20, old 0x00000100, new 0x00000000)
UIRepo_F4 Stage0: window style recovery frame rendered (transition 2, generation 2, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: ResizeBuffers pre-release of native render target
UIRepo_F4 Stage0: ResizeBuffers completed (buffers 2, requested 1600x900, format 28, flags 0x0, HRESULT 0x00000000)
UIRepo_F4: Fallout-native render target generation 3 acquired (1600x900, format 28, samples 1)
UIRepo_F4 Stage0: native probe recovery frame rendered (generation 3, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: ResizeBuffers pre-release of native render target
UIRepo_F4 Stage0: ResizeBuffers completed (buffers 2, requested 1280x720, format 28, flags 0x0, HRESULT 0x00000000)
UIRepo_F4: Fallout-native render target generation 4 acquired (1280x720, format 28, samples 1)
UIRepo_F4 Stage0: native probe recovery frame rendered (generation 4, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: main-menu closed
UIRepo_F4 Stage0: main-menu opened
LOG

python3 scripts/stage0-verify-repeated-lifecycle.py "$log"

cp "$log" "$tmp/missing-focus-recovery.log"
sed -i '/focus recovery frame rendered (transition 10/d' "$tmp/missing-focus-recovery.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/missing-focus-recovery.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted an alt-tab return without a successful probe frame" >&2
    exit 1
fi

cp "$log" "$tmp/zero-focus-glyphs.log"
sed -i 's/focus recovery frame rendered (transition 8, generation 2, rects 1, text 2, glyph runs 2/focus recovery frame rendered (transition 8, generation 2, rects 1, text 2, glyph runs 0/' "$tmp/zero-focus-glyphs.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/zero-focus-glyphs.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted an alt-tab recovery frame with zero glyph execution" >&2
    exit 1
fi

cp "$log" "$tmp/missing-style-recovery.log"
sed -i '/window style recovery frame rendered/d' "$tmp/missing-style-recovery.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/missing-style-recovery.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted a live window-style change without rendered recovery" >&2
    exit 1
fi

cp "$log" "$tmp/stale-style-recovery.log"
sed -i 's/window style recovery frame rendered (transition 2/window style recovery frame rendered (transition 1/' "$tmp/stale-style-recovery.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/stale-style-recovery.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted recovery for a stale window-style transition" >&2
    exit 1
fi

cp "$log" "$tmp/incomplete-second-resize.log"
sed -i '/requested 1280x720/,+2d' "$tmp/incomplete-second-resize.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/incomplete-second-resize.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted an incomplete second resize cycle" >&2
    exit 1
fi

cp "$log" "$tmp/one-return.log"
tac "$tmp/one-return.log" | sed '0,/UIRepo_F4 Stage0: main-menu opened/d' | tac > "$tmp/one-return.tmp"
mv "$tmp/one-return.tmp" "$tmp/one-return.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/one-return.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier counted startup as one of two required MainMenu returns" >&2
    exit 1
fi

cp "$log" "$tmp/failed-second-resize.log"
sed -i 's/requested 1280x720, format 28, flags 0x0, HRESULT 0x00000000/requested 1280x720, format 28, flags 0x0, HRESULT 0x887A0005/' "$tmp/failed-second-resize.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/failed-second-resize.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted a failed second ResizeBuffers call" >&2
    exit 1
fi

cp "$log" "$tmp/mismatched-second-target.log"
sed -i 's/generation 4 acquired (1280x720/generation 4 acquired (1024x768/' "$tmp/mismatched-second-target.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/mismatched-second-target.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted mismatched second resize dimensions" >&2
    exit 1
fi

cp "$log" "$tmp/wrong-second-generation.log"
sed -i 's/recovery frame rendered (generation 4/recovery frame rendered (generation 5/' "$tmp/wrong-second-generation.log"
if python3 scripts/stage0-verify-repeated-lifecycle.py "$tmp/wrong-second-generation.log" >/dev/null 2>&1; then
    echo "Repeated lifecycle verifier accepted a recovery frame from the wrong second target generation" >&2
    exit 1
fi

echo "Stage 0 repeated lifecycle regressions passed"
