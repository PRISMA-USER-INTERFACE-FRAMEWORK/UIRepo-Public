#!/usr/bin/env bash
# Build UIRepo_F4.dll (CommonLibF4 F4SE plugin) by cross-compiling from Linux with
# xmake + the custom clang-cl/xwin toolchain. No Wine, no MSVC, no vcvars.
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
        ( cd "$tmp/xmake" && ./configure --prefix="$XMAKE_PREFIX" >/dev/null 2>&1 && make -j"$(nproc)" >/dev/null 2>&1 && make install >/dev/null 2>&1 )
        rm -rf "$tmp"
    fi
    export PATH="$XMAKE_PREFIX/bin:$PATH"
fi
echo "xmake: $(command -v xmake) -> $(xmake --version 2>&1 | head -1)"

section "install xwin toolchain globally"
# global.directory() is ${XMAKE_GLOBALDIR:-$HOME}/.xmake; it appends .xmake itself.
GLOBALDIR="${XMAKE_GLOBALDIR:-$HOME}/.xmake"
mkdir -p "$GLOBALDIR/toolchains/xwin"
cp "$root/xmake/xwin-toolchain.lua" "$GLOBALDIR/toolchains/xwin/xmake.lua"
echo "installed: $GLOBALDIR/toolchains/xwin/xmake.lua"

section "pin CommonLibF4 (commonlibf4-nomad)"
PIN="$(tr -d '\r\n' < deps/commonlibf4.ref)"
[[ "$PIN" =~ ^[0-9a-f]{40}$ ]] || { echo "bad CommonLibF4 pin" >&2; exit 1; }
rm -rf lib/commonlibf4
mkdir -p lib
# The clone is public; an optional read token reaches git through GIT_CONFIG_* so it is neither on a command line nor in any config file.
if [ -n "${COMMONLIB_READ_TOKEN:-}" ]; then
    export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0="url.https://x-access-token:${COMMONLIB_READ_TOKEN}@github.com/.insteadOf" GIT_CONFIG_VALUE_0="https://github.com/"
fi

redacted_git() {
    local out
    out="$(git "$@" 2>&1)" && return 0
    printf '%s\n' "${out//${COMMONLIB_READ_TOKEN:-@@no-token@@}/***}" >&2
    echo "git $* failed" >&2
    return 1
}

redacted_git clone --recurse-submodules https://github.com/G-A-R-D-E-N/commonlibf4-nomad.git lib/commonlibf4
redacted_git -C lib/commonlibf4 checkout --detach "$PIN"
redacted_git -C lib/commonlibf4 submodule sync --recursive
redacted_git -C lib/commonlibf4 submodule update --init --recursive
unset GIT_CONFIG_COUNT GIT_CONFIG_KEY_0 GIT_CONFIG_VALUE_0 COMMONLIB_READ_TOKEN
ACTUAL="$(git -C lib/commonlibf4 rev-parse HEAD)"
test "$ACTUAL" = "$PIN" || { echo "CommonLibF4 checkout mismatch: $ACTUAL != $PIN" >&2; exit 1; }

SUBMODULE_STATUS="$(git -C lib/commonlibf4 submodule status --recursive)"
printf '%s\n' "$SUBMODULE_STATUS"
if grep -Eq '^[-+U]' <<<"$SUBMODULE_STATUS"; then
    echo "CommonLibF4 has missing, mismatched, or conflicted recursive submodules" >&2
    exit 1
fi
if ! git -C lib/commonlibf4 diff --quiet --ignore-submodules=none HEAD --; then
    echo "CommonLibF4 checkout or recursive submodules differ from the pinned superproject" >&2
    git -C lib/commonlibf4 status --short --ignore-submodules=none >&2 || true
    exit 1
fi

export UIREPO_COMMONLIBF4_VERIFIED_SHA="$ACTUAL"
echo "CommonLibF4 pinned at $ACTUAL with recursive submodules verified"

section "configure (windows/x64, xwin toolchain)"
rm -rf build .xmake
xmake f -p windows -a x64 -m release --toolchain=xwin -c -y

section "build UIRepo_F4.dll"
xmake build -y UIRepo_F4

# The SDK sample plugin is the only consumer of the public ABI that is a real
# F4SE plugin, so it needs CommonLibF4 and can only build in this lane. If a
# change breaks what a mod author can build, it breaks here.
section "build the SDK sample plugin against the public ABI"
xmake build -y UIRepoSample_F4
sample_dll="build/windows/x64/release/UIRepoSample_F4.dll"
test -s "$sample_dll" || { echo "missing SDK sample plugin: $sample_dll" >&2; exit 1; }

# Existing is not the same as loadable. A plugin missing F4SEPlugin_Query is
# rejected by F4SE on the OG runtime with "does not appear to be an F4SE
# plugin" - which names no symbol and looks nothing like a missing export. The
# sample is what a mod author copies, so it has to be verified the same way the
# framework is.
if command -v llvm-readobj >/dev/null 2>&1; then
    sample_exports="$(llvm-readobj --coff-exports "$sample_dll")"
elif command -v llvm-objdump >/dev/null 2>&1; then
    sample_exports="$(llvm-objdump -p "$sample_dll")"
else
    sample_exports="$(objdump -p "$sample_dll")"
fi
for symbol in F4SEPlugin_Query F4SEPlugin_Load F4SEPlugin_Version; do
    grep -Fq "$symbol" <<<"$sample_exports" || {
        echo "SDK sample plugin is missing required F4SE export: $symbol" >&2
        exit 1
    }
done
echo "SDK sample plugin cross-built and F4SE exports verified: $sample_dll"

section "verify artifact"
dll="build/windows/x64/release/UIRepo_F4.dll"
pdb="build/windows/x64/release/UIRepo_F4.pdb"
provenance="build/windows/x64/release/COMMONLIB_PROVENANCE.txt"
test -s "$dll" || { echo "missing DLL: $dll" >&2; find build -name 'UIRepo_F4*' 2>/dev/null; exit 1; }
test -s "$pdb" || { echo "missing PDB: $pdb" >&2; find build -name 'UIRepo_F4*' 2>/dev/null; exit 1; }
{
    printf 'commonlib_sha=%s\n' "$ACTUAL"
    printf '%s\n' "$SUBMODULE_STATUS"
} > "$provenance"
test -s "$provenance" || { echo "missing CommonLib provenance: $provenance" >&2; exit 1; }
ls -lh "$dll" "$pdb" "$provenance"
cat "$provenance"

command -v file >/dev/null || { echo "missing 'file' utility for PE verification" >&2; exit 1; }
file_info="$(file "$dll")"
echo "$file_info"
grep -Fq 'PE32+' <<<"$file_info" || { echo "UIRepo_F4.dll is not a PE32+ image" >&2; exit 1; }
grep -Eq 'x86-64|x86_64' <<<"$file_info" || { echo "UIRepo_F4.dll is not x64" >&2; exit 1; }

section "verify F4SE exports"
if command -v llvm-readobj >/dev/null 2>&1; then
    export_dump="$(llvm-readobj --coff-exports "$dll")"
elif command -v llvm-objdump >/dev/null 2>&1; then
    export_dump="$(llvm-objdump -p "$dll")"
elif command -v objdump >/dev/null 2>&1; then
    export_dump="$(objdump -p "$dll")"
else
    echo "no PE export inspector found (llvm-readobj/llvm-objdump/objdump)" >&2
    exit 1
fi
printf '%s\n' "$export_dump"
for symbol in F4SEPlugin_Preload F4SEPlugin_Query F4SEPlugin_Load F4SEPlugin_Version; do
    grep -Fq "$symbol" <<<"$export_dump" || {
        echo "missing required F4SE export: $symbol" >&2
        exit 1
    }
done

echo "UIRepo_F4.dll cross-built with clang-cl + xwin; PE/x64/PDB/F4SE exports/CommonLib provenance verified"
