#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

build_sha="0123456789abcdef0123456789abcdef01234567"
commonlib_sha="7351f6d4e8a9b7c3faf47dbdb8f7713a035999e7"
log="$tmp/UIRepo_F4.log"
manifest="$tmp/STAGE0_BUILD.txt"
dll="$tmp/UIRepo_F4.dll"
pdb="$tmp/UIRepo_F4.pdb"
provenance="$tmp/COMMONLIB_PROVENANCE.txt"
printf 'stage0-dll-fixture\n' > "$dll"
printf 'stage0-pdb-fixture\n' > "$pdb"
cat > "$provenance" <<EOF
commonlib_sha=$commonlib_sha
 1111111111111111111111111111111111111111 lib/commonlib-shared
 2222222222222222222222222222222222222222 lib/commonlib-shared/lib/rapidjson
EOF
dll_hash="$(sha256sum "$dll" | awk '{print $1}')"
pdb_hash="$(sha256sum "$pdb" | awk '{print $1}')"
provenance_hash="$(sha256sum "$provenance" | awk '{print $1}')"
cat > "$manifest" <<EOF
workflow_sha=$build_sha
source_head_sha=89abcdef0123456789abcdef0123456789abcdef
commonlib_sha=$commonlib_sha
$dll_hash  build/windows/x64/release/UIRepo_F4.dll
$pdb_hash  build/windows/x64/release/UIRepo_F4.pdb
$provenance_hash  build/windows/x64/release/COMMONLIB_PROVENANCE.txt
EOF

cat > "$log" <<LOG
UIRepo_F4 Stage0: native bootstrap loaded (build $build_sha, runtime 1.10.163, F4SE 0.6.23, commonlib_NonVR $commonlib_sha)
UIRepo_F4 Stage0: session event game-data-not-ready
UIRepo_F4 Stage0: session event game-data-ready
UIRepo_F4 Stage0: Fallout render window 0x1 attached to the game-thread dispatcher
UIRepo_F4 Stage0: window lifecycle sink installed
UIRepo_F4 Stage0: swap chain 0x2 owners Present 'dxgi.dll' ResizeBuffers 'dxgi.dll' GetBuffer 'dxgi.dll' (normal COM ownership)
UIRepo_F4 Stage0: MainMenu lifecycle sink installed
UIRepo_F4 Stage0: main-menu opened
UIRepo_F4: engine UIEndFrame compositor installed at 0x3 (Fallout4.exe) with ResizeBuffers lifecycle hook 0x4 (dxgi.dll)
UIRepo_F4: initialized Fallout-native D3D11 renderer generation 1 initialized (feature level 0xB000)
UIRepo_F4: Fallout-native render target generation 2 acquired (1920x1080, format 28, samples 1)
UIRepo_F4: first native probe frame rendered (rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: app focus inactive (transition 1)
UIRepo_F4 Stage0: app focus active (transition 2)
UIRepo_F4 Stage0: app focus inactive (transition 3)
UIRepo_F4 Stage0: app focus active (transition 4)
UIRepo_F4 Stage0: app focus inactive (transition 5)
UIRepo_F4 Stage0: app focus active (transition 6)
UIRepo_F4 Stage0: app focus inactive (transition 7)
UIRepo_F4 Stage0: app focus active (transition 8)
UIRepo_F4 Stage0: app focus inactive (transition 9)
UIRepo_F4 Stage0: app focus active (transition 10)
UIRepo_F4 Stage0: main-menu closed
UIRepo_F4 Stage0: session event new-game
UIRepo_F4 Stage0: main-menu opened
UIRepo_F4 Stage0: session event pre-load-game
UIRepo_F4 Stage0: session event post-load-game success true
UIRepo_F4 Stage0: ResizeBuffers pre-release of native render target
UIRepo_F4 Stage0: ResizeBuffers completed (buffers 2, requested 1600x900, format 28, flags 0x0, HRESULT 0x00000000)
UIRepo_F4: Fallout-native render target generation 3 acquired (1600x900, format 28, samples 1)
UIRepo_F4 Stage0: native probe recovery frame rendered (generation 3, rects 1, text 2, glyph runs 2, clips 0/0)
UIRepo_F4 Stage0: main-menu closed
UIRepo_F4 Stage0: session event new-game
UIRepo_F4 Stage0: main-menu opened
UIRepo_F4 Stage0: session event pre-load-game
UIRepo_F4 Stage0: session event post-load-game success true
LOG

python3 scripts/stage0-verify-log.py "$log"
python3 scripts/stage0-verify-log.py --expect-build-sha "$build_sha" "$log"
python3 scripts/stage0-verify-log.py \
    --manifest "$manifest" \
    --dll "$dll" \
    --pdb "$pdb" \
    --provenance "$provenance" \
    --full-cycle \
    "$log"

if python3 scripts/stage0-verify-log.py \
    --expect-build-sha deadbeefdeadbeefdeadbeefdeadbeefdeadbeef \
    "$log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted the wrong build identity" >&2
    exit 1
fi

printf 'tampered\n' >> "$dll"
if python3 scripts/stage0-verify-log.py \
    --manifest "$manifest" --dll "$dll" "$log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a tampered DLL" >&2
    exit 1
fi
printf 'stage0-dll-fixture\n' > "$dll"

cp "$provenance" "$tmp/tampered-provenance.txt"
printf 'tampered\n' >> "$tmp/tampered-provenance.txt"
if python3 scripts/stage0-verify-log.py \
    --manifest "$manifest" --provenance "$tmp/tampered-provenance.txt" "$log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted tampered CommonLib provenance" >&2
    exit 1
fi

cp "$provenance" "$tmp/bad-submodule-provenance.txt"
sed -i 's/^ 111111/+/; s/^+111111/+111111/' "$tmp/bad-submodule-provenance.txt"
bad_provenance_hash="$(sha256sum "$tmp/bad-submodule-provenance.txt" | awk '{print $1}')"
sed "s#^$provenance_hash  build/windows/x64/release/COMMONLIB_PROVENANCE.txt#$bad_provenance_hash  build/windows/x64/release/COMMONLIB_PROVENANCE.txt#" \
    "$manifest" > "$tmp/bad-submodule-manifest.txt"
if python3 scripts/stage0-verify-log.py \
    --manifest "$tmp/bad-submodule-manifest.txt" \
    --provenance "$tmp/bad-submodule-provenance.txt" \
    "$log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted mismatched CommonLib submodule status" >&2
    exit 1
fi

cp "$log" "$tmp/not-ready-only.log"
sed -i '/session event game-data-ready$/d' "$tmp/not-ready-only.log"
if python3 scripts/stage0-verify-log.py "$tmp/not-ready-only.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted game-data-not-ready as ready" >&2
    exit 1
fi

cp "$log" "$tmp/missing-window-sink.log"
sed -i '/window lifecycle sink installed/d' "$tmp/missing-window-sink.log"
if python3 scripts/stage0-verify-log.py "$tmp/missing-window-sink.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a log without the window lifecycle observer" >&2
    exit 1
fi

cp "$log" "$tmp/incomplete-alt-tab.log"
sed -i '/app focus inactive (transition 9)/d; /app focus active (transition 10)/d' "$tmp/incomplete-alt-tab.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/incomplete-alt-tab.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted fewer than five alt-tab focus cycles" >&2
    exit 1
fi

cp "$log" "$tmp/broken-focus-sequence.log"
sed -i 's/app focus active (transition 10)/app focus active (transition 11)/' "$tmp/broken-focus-sequence.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/broken-focus-sequence.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a non-contiguous app-focus transition sequence" >&2
    exit 1
fi

cp "$log" "$tmp/failed-load.log"
sed -i '0,/post-load-game success true/{s/post-load-game success true/post-load-game success false/;}' "$tmp/failed-load.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/failed-load.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a failed save-load attempt" >&2
    exit 1
fi

cp "$log" "$tmp/missing-resize.log"
sed -i '/ResizeBuffers pre-release of native render target/d' "$tmp/missing-resize.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/missing-resize.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a full-cycle log without ResizeBuffers pre-release evidence" >&2
    exit 1
fi

cp "$log" "$tmp/failed-resize.log"
sed -i 's/HRESULT 0x00000000/HRESULT 0x887A0005/' "$tmp/failed-resize.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/failed-resize.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a failed ResizeBuffers HRESULT" >&2
    exit 1
fi

cp "$log" "$tmp/mismatched-resize.log"
sed -i 's/generation 3 acquired (1600x900/generation 3 acquired (1280x720/' "$tmp/mismatched-resize.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/mismatched-resize.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a target that did not match explicit resize dimensions" >&2
    exit 1
fi

cp "$log" "$tmp/missing-recovery-frame.log"
sed -i '/native probe recovery frame rendered/d' "$tmp/missing-recovery-frame.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/missing-recovery-frame.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a reacquired target without a successful probe recovery frame" >&2
    exit 1
fi

cp "$log" "$tmp/mismatched-recovery-generation.log"
sed -i 's/recovery frame rendered (generation 3/recovery frame rendered (generation 4/' "$tmp/mismatched-recovery-generation.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/mismatched-recovery-generation.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a recovery frame from the wrong target generation" >&2
    exit 1
fi

cp "$log" "$tmp/incomplete-cycle.log"
sed -i '0,/session event new-game/{/session event new-game/d;}' "$tmp/incomplete-cycle.log"
if python3 scripts/stage0-verify-log.py --full-cycle "$tmp/incomplete-cycle.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted an incomplete repeated new-game cycle" >&2
    exit 1
fi

cp "$log" "$tmp/unresolved-owner.log"
sed -i "s/Present 'dxgi.dll'/Present '<unknown>'/" "$tmp/unresolved-owner.log"
if python3 scripts/stage0-verify-log.py "$tmp/unresolved-owner.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted an unresolved Present owner" >&2
    exit 1
fi

cp "$log" "$tmp/zero-glyphs.log"
sed -i 's/glyph runs 2/glyph runs 0/' "$tmp/zero-glyphs.log"
if python3 scripts/stage0-verify-log.py "$tmp/zero-glyphs.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a first frame with zero glyph execution" >&2
    exit 1
fi

cp "$log" "$tmp/duplicate-first-frame.log"
grep 'first native probe frame rendered' "$log" >> "$tmp/duplicate-first-frame.log"
if python3 scripts/stage0-verify-log.py "$tmp/duplicate-first-frame.log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted duplicate first-frame diagnostics" >&2
    exit 1
fi

sed -i '/first native probe frame rendered/d' "$log"
if python3 scripts/stage0-verify-log.py "$log" >/dev/null 2>&1; then
    echo "Stage 0 verifier accepted a log missing first-frame evidence" >&2
    exit 1
fi

echo "Stage 0 acceptance-log/artifact/provenance verifier regressions passed"
