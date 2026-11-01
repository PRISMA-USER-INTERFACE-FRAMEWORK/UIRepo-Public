#!/usr/bin/env bash
# Cross-compile the portable UIRepo core, D3D11 renderer, and Windows regression
# executables with the same xmake graph used by the FO4 DLL build. Nothing
# Windows-native runs in this job: clang-cl/xwin compiles and links Windows x64
# binaries from Linux.
#
# D3D11/WARP test binaries are intentionally compile/link validation only in this
# xwin job. An actual Windows self-hosted runner now executes the same Stage 0
# regressions in `windows-stage0` with native MSVC, including Microsoft WARP and
# DirectWrite. This lane remains independent Windows MSVC-ABI compile/link coverage.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

XWIN_ROOT="${XWIN_ROOT:-/opt/xwin}"
export XWIN_ROOT
export XMAKE_ROOT=y

section() { printf '\n========== %s ==========\n' "$1"; }

test -d "$XWIN_ROOT/crt" || { echo "missing xwin CRT: $XWIN_ROOT/crt" >&2; exit 1; }
test -d "$XWIN_ROOT/sdk" || { echo "missing xwin SDK: $XWIN_ROOT/sdk" >&2; exit 1; }

section "provision xmake"
XMAKE_PREFIX="${XMAKE_PREFIX:-$HOME/.uirepo-xmake}"
if ! command -v xmake >/dev/null 2>&1; then
    if [ ! -x "$XMAKE_PREFIX/bin/xmake" ]; then
        echo "building xmake from source into $XMAKE_PREFIX"
        tmp="$(mktemp -d)"
        git clone --depth 1 --recursive https://github.com/xmake-io/xmake.git "$tmp/xmake" >/dev/null 2>&1
        (
            cd "$tmp/xmake"
            ./configure --prefix="$XMAKE_PREFIX" >/dev/null 2>&1
            make -j"$(nproc)" >/dev/null 2>&1
            make install >/dev/null 2>&1
        )
        rm -rf "$tmp"
    fi
    export PATH="$XMAKE_PREFIX/bin:$PATH"
fi
echo "xmake: $(command -v xmake) -> $(xmake --version 2>&1 | head -1)"

section "install xwin toolchain globally"
GLOBALDIR="${XMAKE_GLOBALDIR:-$HOME}/.xmake"
mkdir -p "$GLOBALDIR/toolchains/xwin"
cp "$root/xmake/xwin-toolchain.lua" "$GLOBALDIR/toolchains/xwin/xmake.lua"

section "configure windows/x64"
rm -rf build .xmake
xmake f -p windows -a x64 -m release --toolchain=xwin -c -y

targets=(
    uirepo_core_tests
    uirepo_policy_tests
    uirepo_retained_animation_tests
    uirepo_spatial_navigation_tests
    uirepo_api_result_tests
    uirepo_native_api_contract_tests
    uirepo_declarative_tests
    uirepo_package_host_tests
    uirepo_d3d11_tests
    uirepo_d3d11_target_color_space_tests
    uirepo_d3d11_debug_tests
    uirepo_d3d11_image_tests
    uirepo_d3d11_text_tests
    uirepo_d3d11_surface_tests
    uirepo_d3d11_surface_device_tests
    uirepo_com_identity_tests
)

abi_modules=(
    uirepo_abi_provider_v1
    uirepo_abi_provider_v2
    uirepo_abi_consumer_documents
    uirepo_abi_consumer_surfaces
    uirepo_abi_consumer_state
)

section "cross-build Windows regression targets"
for target in "${targets[@]}"; do
    echo "building $target"
    xmake build -y "$target"
done

# The SDK sample is the proof that a separately built consumer needs nothing
# but the staged package: no framework sources, no import library, no link.
# It is built from the package rather than the source tree, so a packaging
# mistake fails here instead of reaching a plugin author.
# The harness consumers and providers are separate modules compiled against
# the same public table. Cross-building them proves the layout and calling
# convention agree under the MSVC ABI; the native Windows job executes them.
section "cross-build the ABI harness modules"
for target in "${abi_modules[@]}" uirepo_abi_harness; do
    echo "building $target"
    xmake build -y "$target"
done

section "stage the consumer SDK and cross-build the sample against it"
bash scripts/package-sdk.sh
xmake build -y uirepo_sdk_sample

section "verify Windows artifacts"
bin="build/windows/x64/release"
for target in "${targets[@]}"; do
    exe="$bin/$target.exe"
    test -s "$exe" || { echo "missing cross-built artifact: $exe" >&2; exit 1; }
done

test -s "$bin/uirepo_abi_harness.exe" || { echo "missing cross-built ABI harness" >&2; exit 1; }
for module in "${abi_modules[@]}"; do
    test -s "$bin/$module.dll" || { echo "missing cross-built ABI module: $module.dll" >&2; exit 1; }
done

sample="$bin/uirepo_sdk_sample.dll"
test -s "$sample" || { echo "missing cross-built SDK sample: $sample" >&2; exit 1; }

ls -lh "$bin"/*.exe "$sample"
command -v file >/dev/null && file "$bin"/*.exe "$sample" || true
echo "xmake xwin cross-build OK (compile + link only; native execution is covered by windows-stage0)"
