#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

fail() {
    echo "SDK package contract failed: $*" >&2
    exit 1
}

stage=build/sdk-validate
bash scripts/package-sdk.sh "$stage" >/dev/null || fail "packaging the SDK failed"

for expected in \
    "$stage/include/uirepo/NativeApi.h" \
    "$stage/include/uirepo/NativeApiLoader.h" \
    "$stage/include/uirepo/NativeApiCpp.h" \
    "$stage/include/uirepo/NativeApiCppTypes.h" \
    "$stage/README.md" \
    "$stage/SDK_VERSION.txt" \
    "$stage/samples/minimal/src/main.cpp" \
    "$stage/samples/minimal/xmake.lua"; do
    test -f "$expected" || fail "staged package is missing $expected"
done

grep -Fq 'uirepo_abi_version=' "$stage/SDK_VERSION.txt" || fail "package must record the ABI version it was cut from"
grep -Fq 'uirepo_source_sha=' "$stage/SDK_VERSION.txt" || fail "package must record its source revision"

# The package has to compile with nothing but its own include directory. This
# catches a convenience header quietly depending on the source tree.
compiler="${CXX:-g++}"
if ! command -v "$compiler" >/dev/null 2>&1; then
    fail "no C++ compiler available to verify package self-containment"
fi

probe="$stage/self-containment-probe.cpp"
cat > "$probe" <<'PROBE'
#include "uirepo/NativeApi.h"
#include "uirepo/NativeApiLoader.h"
#include "uirepo/NativeApiCpp.h"

/* Included twice on purpose: the package headers must be idempotent. */
#include "uirepo/NativeApi.h"
#include "uirepo/NativeApiLoader.h"
#include "uirepo/NativeApiCpp.h"

#include <type_traits>

static_assert(sizeof(UIRepoNativeApiV1) > 0);
static_assert(UIREPO_NATIVE_API_VERSION >= 1u);
static_assert(!std::is_same_v<
    uirepo::sdk::native::DocumentHandle,
    uirepo::sdk::native::SurfaceHandle>);

int main() { return 0; }
PROBE

"$compiler" -std=c++17 -Wall -Wextra -Werror -I "$stage/include" -fsyntax-only "$probe" ||
    fail "staged package is not self-contained under C++17"

# Execute the C++ wrapper against a synthetic public API table. The test source
# includes only the staged SDK, so RAII/result/type-safety behaviour cannot be
# accidentally supplied by framework internals.
cpp_test_source=sdk/tests/NativeApiCppTests.cpp
test -f "$cpp_test_source" || fail "C++ wrapper regression source is missing"
cpp_test_binary="$stage/native-api-cpp-tests"
"$compiler" -std=c++17 -Wall -Wextra -Werror -I "$stage/include" "$cpp_test_source" -o "$cpp_test_binary" ||
    fail "C++ wrapper regression did not compile against the staged package"
"$cpp_test_binary" || fail "C++ wrapper regression failed"

# An #include inside a namespace puts every declaration in that header into
# the namespace. For windows.h that means ::GetModuleHandleW stops existing,
# and it compiles fine anywhere the block is #ifdef'd out, so it is invisible
# off Windows.
for package_header in "$stage/include/uirepo/"*.h; do
    if awk '/^namespace /{ns=1} /^[[:space:]]*#[[:space:]]*include/{if(ns) exit 1}' "$package_header"; then
        :
    else
        fail "$(basename "$package_header") has an #include inside a namespace"
    fi
done

# A consumer's translation unit must not acquire framework internals.
if grep -rnE '#include *[<"](uirepo/(Core|RegistryTypes|D3D11Renderer|D3D11SurfaceManager|Controls)\.h)' "$stage/include"; then
    fail "package header pulls in a framework-internal header"
fi

# The C++ wrapper is convenience only. It must stay entirely above NativeApi.h
# and must not smuggle platform/framework types into the SDK.
grep -Fq '#include "uirepo/NativeApi.h"' "$stage/include/uirepo/NativeApiCpp.h" ||
    fail "C++ wrapper must be implemented over the public C ABI"
if grep -nE 'windows\.h|RE::|F4SE::|ID3D11|DocumentRegistry|Runtime\.h|Core\.h' "$stage/include/uirepo/NativeApiCpp.h" "$stage/include/uirepo/NativeApiCppTypes.h"; then
    fail "C++ wrapper reaches below the public C ABI"
fi
grep -Fq 'class Result' "$stage/include/uirepo/NativeApiCppTypes.h" ||
    fail "C++ wrapper must expose Result-style failures rather than exceptions"
grep -Fq '~Document()' "$stage/include/uirepo/NativeApiCpp.h" ||
    fail "C++ wrapper must release documents by RAII"
grep -Fq '~Surface()' "$stage/include/uirepo/NativeApiCpp.h" ||
    fail "C++ wrapper must release surfaces by RAII"

# The sample is the proof that a consumer needs no link-time dependency.
if grep -nE 'add_deps|add_links|add_syslinks *\(.*uirepo' "$stage/samples/minimal/xmake.lua"; then
    fail "the sample must not acquire a build-time dependency on the framework"
fi
grep -Fq 'add_includedirs' "$stage/samples/minimal/xmake.lua" ||
    fail "the sample must show that an include directory is the whole integration"

# The in-game sample must consume public SDK headers and nothing else: no
# framework sources, no internal includes, no link against the framework.
sample=sdk/samples/f4se-plugin/src/main.cpp
test -f "$sample" || fail "the in-game SDK sample plugin is missing"
if grep -nE '#include "(Runtime|DocumentRegistry|RenderHook|uirepo/(Core|RegistryTypes|D3D11))' "$sample"; then
    fail "the sample plugin reaches into framework internals"
fi
grep -Fq 'uirepo/NativeApiLoader.h' "$sample" || fail "the sample plugin must consume the public SDK header"

# The loader includes windows.h and CommonLibF4 refuses to follow it, so
# the sample must demonstrate the working order rather than a broken one.
sample_commonlib_line="$(grep -n 'F4SE/F4SE.h' "$sample" | head -1 | cut -d: -f1)"
sample_loader_line="$(grep -n 'uirepo/NativeApiLoader.h' "$sample" | head -1 | cut -d: -f1)"
if [ -n "$sample_commonlib_line" ] && [ -n "$sample_loader_line" ]; then
    [ "$sample_commonlib_line" -lt "$sample_loader_line" ] ||
        fail "the sample must include CommonLibF4 before the UIRepo loader, or CommonLib rejects the build"
fi
grep -Fq 'Windows API detected' sdk/README.md ||
    fail "the SDK README must warn about the CommonLibF4 include-order requirement"
grep -Fq 'UIRepoSample_F4' xmake.lua || fail "the sample plugin has no build target"
grep -Fq 'UIRepoSample_F4' scripts/ci-fo4-dll-build.sh || fail "the sample plugin is not built in CI"

# A plugin that F4SE will refuse to load must not pass CI. On the OG runtime
# the Query export is what F4SE actually checks; without it the plugin is
# rejected as "not an F4SE plugin" with no symbol named.
grep -Fq 'F4SE_PLUGIN_QUERY' "$sample" ||
    fail "the sample plugin must export F4SEPlugin_Query or the OG runtime will refuse it"
grep -Fq 'SDK sample plugin is missing required F4SE export' scripts/ci-fo4-dll-build.sh ||
    fail "CI must verify the sample's F4SE exports, not merely that the file exists"

rm -rf "$stage"
echo "SDK package contract passed"
