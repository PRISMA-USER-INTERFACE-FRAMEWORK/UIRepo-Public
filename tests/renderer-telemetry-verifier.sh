#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

log="$tmp/UIRepo_F4.log"
cat > "$log" <<'LOG'
[info] UIRepo_F4 telemetry: reason baseline renderer-epoch 1 target-generation 2 target 1920x1080 visible-docs 1 glyph(entries 10 bytes 4096 hits 0 misses 10 uploads 10 evictions 0 rejections 0) image(entries 2 bytes 8192 hits 0 misses 2 uploads 2 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 8 bytes 20480 hits 0 misses 8 evictions 0 rejections 0) surfaces(live 1 live-bytes 1048576 pooled 0 pooled-bytes 0 allocations 1 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason periodic renderer-epoch 1 target-generation 2 target 1920x1080 visible-docs 2 glyph(entries 14 bytes 6144 hits 100 misses 14 uploads 14 evictions 0 rejections 0) image(entries 3 bytes 12288 hits 20 misses 3 uploads 3 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 12 bytes 30720 hits 40 misses 12 evictions 0 rejections 0) surfaces(live 1 live-bytes 1048576 pooled 1 pooled-bytes 262144 allocations 2 reuses 1 resizes 1 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason periodic renderer-epoch 1 target-generation 3 target 1600x900 visible-docs 2 glyph(entries 12 bytes 5120 hits 220 misses 20 uploads 20 evictions 8 rejections 0) image(entries 3 bytes 12288 hits 60 misses 4 uploads 4 evictions 1 rejections 0 decode-failures 1 negative-hits 3) text-layout(entries 12 bytes 30720 hits 90 misses 16 evictions 4 rejections 0) surfaces(live 1 live-bytes 1048576 pooled 1 pooled-bytes 262144 allocations 2 reuses 3 resizes 1 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason shutdown renderer-epoch 1 target-generation 3 target 1600x900 visible-docs 0 glyph(entries 12 bytes 5120 hits 230 misses 20 uploads 20 evictions 8 rejections 0) image(entries 3 bytes 12288 hits 65 misses 4 uploads 4 evictions 1 rejections 0 decode-failures 1 negative-hits 3) text-layout(entries 12 bytes 30720 hits 95 misses 16 evictions 4 rejections 0) surfaces(live 1 live-bytes 1048576 pooled 1 pooled-bytes 262144 allocations 2 reuses 3 resizes 1 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason reset renderer-epoch 1 target-generation 3 target 1600x900 visible-docs 0 glyph(entries 12 bytes 5120 hits 230 misses 20 uploads 20 evictions 8 rejections 0) image(entries 3 bytes 12288 hits 65 misses 4 uploads 4 evictions 1 rejections 0 decode-failures 1 negative-hits 3) text-layout(entries 12 bytes 30720 hits 95 misses 16 evictions 4 rejections 0) surfaces(live 0 live-bytes 0 pooled 0 pooled-bytes 0 allocations 0 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
LOG

python3 scripts/verify-renderer-telemetry.py "$log"
python3 scripts/verify-renderer-telemetry.py --long-session "$log"

cp "$log" "$tmp/over-budget.log"
sed -i '0,/glyph(entries 10 bytes 4096/{s/glyph(entries 10 bytes 4096/glyph(entries 2049 bytes 4096/;}' "$tmp/over-budget.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/over-budget.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted glyph entries over the runtime cap" >&2
    exit 1
fi

cp "$log" "$tmp/text-layout-residency.log"
sed -i '0,/text-layout(entries 8 bytes 20480/{s/text-layout(entries 8 bytes 20480/text-layout(entries 513 bytes 20480/;}' "$tmp/text-layout-residency.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/text-layout-residency.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted text-layout entries over the runtime cap" >&2
    exit 1
fi

cp "$log" "$tmp/text-layout-bytes.log"
sed -i '0,/text-layout(entries 8 bytes 20480/{s/text-layout(entries 8 bytes 20480/text-layout(entries 8 bytes 4194305/;}' "$tmp/text-layout-bytes.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/text-layout-bytes.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted text-layout bytes over the runtime cap" >&2
    exit 1
fi

cp "$log" "$tmp/counter-regression.log"
sed -i '0,/hits 220 misses 20 uploads 20/{s/hits 220 misses 20 uploads 20/hits 90 misses 20 uploads 20/;}' "$tmp/counter-regression.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/counter-regression.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted a cumulative counter regression" >&2
    exit 1
fi

cp "$log" "$tmp/text-layout-counter-regression.log"
sed -i '0,/text-layout(entries 12 bytes 30720 hits 90/{s/text-layout(entries 12 bytes 30720 hits 90/text-layout(entries 12 bytes 30720 hits 30/;}' "$tmp/text-layout-counter-regression.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/text-layout-counter-regression.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted a text-layout cumulative counter regression" >&2
    exit 1
fi

cp "$log" "$tmp/target-regression.log"
sed -i '0,/target-generation 3/{s/target-generation 3/target-generation 1/;}' "$tmp/target-regression.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/target-regression.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted a target-generation regression" >&2
    exit 1
fi

cp "$log" "$tmp/no-baseline.log"
sed -i '/reason baseline/d' "$tmp/no-baseline.log"
if python3 scripts/verify-renderer-telemetry.py "$tmp/no-baseline.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted a log without baseline evidence" >&2
    exit 1
fi

cp "$log" "$tmp/short-session.log"
sed -i '0,/reason periodic/{/reason periodic/d;}' "$tmp/short-session.log"
if python3 scripts/verify-renderer-telemetry.py --long-session "$tmp/short-session.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted fewer than two periodic long-session snapshots" >&2
    exit 1
fi

cat > "$tmp/split-epochs.log" <<'LOG'
[info] UIRepo_F4 telemetry: reason baseline renderer-epoch 2 target-generation 4 target 1920x1080 visible-docs 1 glyph(entries 1 bytes 100 hits 0 misses 1 uploads 1 evictions 0 rejections 0) image(entries 0 bytes 0 hits 0 misses 0 uploads 0 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 1 bytes 2048 hits 0 misses 1 evictions 0 rejections 0) surfaces(live 0 live-bytes 0 pooled 0 pooled-bytes 0 allocations 0 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason periodic renderer-epoch 2 target-generation 4 target 1920x1080 visible-docs 1 glyph(entries 1 bytes 100 hits 10 misses 1 uploads 1 evictions 0 rejections 0) image(entries 0 bytes 0 hits 0 misses 0 uploads 0 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 1 bytes 2048 hits 10 misses 1 evictions 0 rejections 0) surfaces(live 0 live-bytes 0 pooled 0 pooled-bytes 0 allocations 0 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason baseline renderer-epoch 3 target-generation 6 target 1600x900 visible-docs 1 glyph(entries 1 bytes 100 hits 0 misses 1 uploads 1 evictions 0 rejections 0) image(entries 0 bytes 0 hits 0 misses 0 uploads 0 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 1 bytes 2048 hits 0 misses 1 evictions 0 rejections 0) surfaces(live 0 live-bytes 0 pooled 0 pooled-bytes 0 allocations 0 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason periodic renderer-epoch 3 target-generation 6 target 1600x900 visible-docs 1 glyph(entries 1 bytes 100 hits 10 misses 1 uploads 1 evictions 0 rejections 0) image(entries 0 bytes 0 hits 0 misses 0 uploads 0 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 1 bytes 2048 hits 10 misses 1 evictions 0 rejections 0) surfaces(live 0 live-bytes 0 pooled 0 pooled-bytes 0 allocations 0 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
[info] UIRepo_F4 telemetry: reason reset renderer-epoch 3 target-generation 6 target 1600x900 visible-docs 0 glyph(entries 1 bytes 100 hits 10 misses 1 uploads 1 evictions 0 rejections 0) image(entries 0 bytes 0 hits 0 misses 0 uploads 0 evictions 0 rejections 0 decode-failures 0 negative-hits 0) text-layout(entries 1 bytes 2048 hits 10 misses 1 evictions 0 rejections 0) surfaces(live 0 live-bytes 0 pooled 0 pooled-bytes 0 allocations 0 reuses 0 resizes 0 rebuilds 0 rejections 0 drains 0)
LOG
if python3 scripts/verify-renderer-telemetry.py --long-session "$tmp/split-epochs.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted periodic checkpoints split across renderer epochs" >&2
    exit 1
fi

cp "$log" "$tmp/missing-final.log"
sed -i '/reason shutdown/d; /reason reset/d' "$tmp/missing-final.log"
if python3 scripts/verify-renderer-telemetry.py --long-session "$tmp/missing-final.log" >/dev/null 2>&1; then
    echo "renderer telemetry verifier accepted long-session evidence without shutdown/reset" >&2
    exit 1
fi

echo "renderer telemetry verifier regressions passed"
