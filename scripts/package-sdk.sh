#!/usr/bin/env bash
set -euo pipefail

# Assembles the shippable UIRepo consumer SDK.
#
# The point of assembling rather than pointing consumers at the source tree is
# that the package has to be self-sufficient: exactly four headers, no internal
# includes, no build-time dependency on the framework. The minimal sample is
# built against this staged output, so anything missing from the package fails
# the sample build rather than being silently supplied by the repository.

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

stage="${1:-build/sdk}"
public_header=native/cpp/include/uirepo/NativeApi.h
loader_header=sdk/include/uirepo/NativeApiLoader.h
cpp_header=sdk/include/uirepo/NativeApiCpp.h
cpp_types_header=sdk/include/uirepo/NativeApiCppTypes.h

fail() {
    echo "SDK packaging failed: $*" >&2
    exit 1
}

test -f "$public_header" || fail "public ABI header is missing"
test -f "$loader_header" || fail "SDK loader header is missing"
test -f "$cpp_header" || fail "SDK C++ wrapper header is missing"
test -f "$cpp_types_header" || fail "SDK C++ wrapper types header is missing"

# Nothing internal may reach a consumer's translation unit.
if grep -nE 'std::|Microsoft::WRL|RE::|F4SE::|ID3D11' "$public_header"; then
    fail "public ABI header leaks an implementation type"
fi
for convenience_header in "$loader_header" "$cpp_header" "$cpp_types_header"; do
    quoted_includes="$(grep -nE '^[[:space:]]*#[[:space:]]*include[[:space:]]+"' "$convenience_header" || true)"
    if [ -n "$quoted_includes" ]; then
        unexpected="$(printf '%s\n' "$quoted_includes" | grep -Fv -e '"uirepo/NativeApi.h"' -e '"uirepo/NativeApiCppTypes.h"' || true)"
        if [ -n "$unexpected" ]; then
            printf '%s\n' "$unexpected" >&2
            fail "$(basename "$convenience_header") includes a quoted header outside the package contract"
        fi
    fi

    for internal in Core.h RegistryTypes.h D3D11Renderer.h D3D11SurfaceManager.h Controls.h; do
        if grep -Fq "$internal" "$convenience_header"; then
            fail "package header references internal header $internal"
        fi
    done
done

abi_version="$(grep -oE '#define UIREPO_NATIVE_API_VERSION [0-9]+' "$public_header" | grep -oE '[0-9]+')"
[ -n "$abi_version" ] || fail "could not read the ABI version from the public header"
build_sha="$(git rev-parse HEAD 2>/dev/null || echo unknown)"

rm -rf "$stage"
mkdir -p "$stage/include/uirepo" "$stage/samples/minimal/src"

cp "$public_header" "$stage/include/uirepo/NativeApi.h"
cp "$loader_header" "$stage/include/uirepo/NativeApiLoader.h"
cp "$cpp_header" "$stage/include/uirepo/NativeApiCpp.h"
cp "$cpp_types_header" "$stage/include/uirepo/NativeApiCppTypes.h"
cp sdk/README.md "$stage/README.md"
cp sdk/samples/minimal/src/main.cpp "$stage/samples/minimal/src/main.cpp"
cp sdk/samples/minimal/README.md "$stage/samples/minimal/README.md"
cp sdk/samples/minimal/xmake.lua "$stage/samples/minimal/xmake.lua"

cat > "$stage/SDK_VERSION.txt" <<VERSION
uirepo_abi_version=$abi_version
uirepo_source_sha=$build_sha
VERSION

# The package contains exactly the ABI, loader, and optional C++ wrapper with its value types.
header_count="$(find "$stage/include" -name '*.h' | wc -l)"
[ "$header_count" -eq 4 ] || fail "expected exactly 4 package headers, found $header_count"

echo "SDK staged at $stage (ABI v$abi_version, source $build_sha)"
