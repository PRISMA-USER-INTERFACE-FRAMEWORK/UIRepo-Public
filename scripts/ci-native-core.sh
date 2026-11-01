#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

export XMAKE_ROOT=y

section() { printf '\n========== %s ==========\n' "$1"; }

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

private_only() {
    if [ "${UIREPO_PUBLIC_CI:-}" = 1 ]; then echo "skipped in public CI (private-only): $*"; else "$@"; fi
}

section "validate native stack contract"
bash tests/validate-native-stack.sh
python3 tests/validate-focus-loss-input-flush.py
python3 tests/validate-window-subclass-contract.py
python3 tests/validate-extended-pointer-input.py
python3 tests/validate-ime-positioning.py
python3 tests/validate-target-color-space.py
bash tests/validate-renderer-production.sh
bash tests/validate-long-session-telemetry.sh
private_only bash tests/validate-stage0.sh
private_only bash tests/validate-commonlib-provenance-label.sh
private_only bash tests/validate-stage0-windows.sh
bash tests/validate-stage0-recovery.sh
bash tests/validate-stage0-focus.sh
bash tests/validate-stage0-window-style.sh
bash tests/validate-stage0-session.sh
private_only bash tests/validate-stage0-repeated-lifecycle.sh
private_only bash tests/validate-stage0-process-exit.sh
bash tests/validate-stage0-log-health.sh
private_only bash tests/validate-stage0-linux.sh
bash tests/validate-stage4-surfaces.sh
bash tests/validate-abi-compatibility.sh
bash tests/validate-sdk-package.sh

section "ABI negotiation and multi-consumer harness"
bash tests/run-abi-harness.sh
bash tests/stage0-log-verifier.sh
bash tests/stage0-repeated-lifecycle.sh
bash tests/stage0-log-health.sh
bash tests/renderer-telemetry-verifier.sh

section "configure portable native graph"
rm -rf build .xmake
xmake f -p linux -a x86_64 -m release -c -y

tests=(
    uirepo_core_tests
    uirepo_policy_tests
    uirepo_dispatch_lifecycle_tests
    uirepo_focus_lease_tests
    uirepo_active_device_tests
    uirepo_extended_pointer_tests
    uirepo_bounded_cache_tests
    uirepo_image_policy_tests
    uirepo_spatial_navigation_tests
    uirepo_native_tree_foundation_tests
    uirepo_native_tree_mutation_tests
    uirepo_animation_tests
    uirepo_retained_animation_tests
    uirepo_api_result_tests
    uirepo_native_api_contract_tests
    uirepo_render_hook_broker_tests
    uirepo_active_device_state_tests
    uirepo_input_lifecycle_tests
    uirepo_control_registry_tests
    uirepo_registry_lifecycle_tests
    uirepo_package_tests
    uirepo_declarative_tests
    uirepo_native_api_v2_tests
    uirepo_package_host_tests
    uirepo_package_host_runtime_tests
)

section "build portable regressions"
for target in "${tests[@]}"; do
    echo "building $target"
    xmake build -y "$target"
done

section "run portable regressions"
for target in "${tests[@]}"; do
    binary="build/linux/x86_64/release/$target"
    test -x "$binary" || { echo "missing test binary: $binary" >&2; exit 1; }
    echo "running $target"
    "$binary"
done

echo "portable native regressions passed under xmake"
