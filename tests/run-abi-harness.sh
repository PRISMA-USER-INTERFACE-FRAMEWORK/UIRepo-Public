#!/usr/bin/env bash
set -euo pipefail

# Builds and runs the ABI negotiation / multi-consumer harness.
#
# The consumers and providers are separate shared libraries on purpose: this is
# the only place the ABI is exercised across a real module boundary instead of
# within one translation unit.

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

targets=(
    uirepo_abi_provider_v1
    uirepo_abi_provider_v2
    uirepo_abi_consumer_documents
    uirepo_abi_consumer_surfaces
    uirepo_abi_consumer_state
    uirepo_abi_harness
)

for target in "${targets[@]}"; do
    xmake build -y "$target"
done

bin="$(xmake show -t uirepo_abi_harness 2>/dev/null | awk '/targetfile/ {print $2}')"
if [ -z "${bin:-}" ] || [ ! -x "$bin" ]; then
    bin="$(find build -type f -name 'uirepo_abi_harness' -perm -u+x | head -1)"
fi
[ -n "$bin" ] || { echo "could not locate the built ABI harness" >&2; exit 1; }
dir="$(dirname "$bin")"

lib() {
    for candidate in "$dir/lib$1.so" "$dir/$1.dll" "$dir/lib$1.dylib"; do
        if [ -f "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    echo "could not locate built module $1 in $dir" >&2
    return 1
}

"$bin" \
    "$(lib uirepo_abi_provider_v1)" \
    "$(lib uirepo_abi_provider_v2)" \
    "$(lib uirepo_abi_consumer_documents)" \
    "$(lib uirepo_abi_consumer_surfaces)" \
    "$(lib uirepo_abi_consumer_state)"
