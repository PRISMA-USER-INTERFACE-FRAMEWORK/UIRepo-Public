#!/usr/bin/env bash
# Provision clang-cl, lld-link, llvm-ar, llvm-rc and the xwin CRT/SDK on a GitHub-hosted Ubuntu runner.
set -euo pipefail

: "${XWIN_ROOT:?XWIN_ROOT must name the directory that receives the xwin splat}"

LLVM_VERSION=19
XWIN_VERSION=0.10.0
XWIN_SHA256=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
XWIN_CRT_VERSION=14.44.17.14
XWIN_SDK_VERSION=10.0.26100

section() { printf '\n========== %s ==========\n' "$1"; }

section "install the LLVM $LLVM_VERSION tools"
sudo apt-get update -qq
sudo apt-get install -y -qq "clang-$LLVM_VERSION" "lld-$LLVM_VERSION" "llvm-$LLVM_VERSION" file
llvm_bin="/usr/lib/llvm-$LLVM_VERSION/bin"
mkdir -p "$HOME/.local/bin"
link_tool() {
    local name="$1" binary="$2" package="$3"
    [ -x "$llvm_bin/$binary" ] || { echo "$name: $llvm_bin/$binary is missing; the apt package $package should have provided it" >&2; exit 1; }
    ln -sf "$llvm_bin/$binary" "$HOME/.local/bin/$name"
}
link_tool clang-cl clang "clang-$LLVM_VERSION"
link_tool lld-link lld-link "lld-$LLVM_VERSION"
link_tool llvm-ar llvm-ar "llvm-$LLVM_VERSION"
link_tool llvm-rc llvm-rc "llvm-$LLVM_VERSION"
link_tool llvm-readobj llvm-readobj "llvm-$LLVM_VERSION"
link_tool llvm-objdump llvm-objdump "llvm-$LLVM_VERSION"
probe_tool() {
    local status=0
    "$1" --version >/dev/null 2>&1 || status=$?
    [ "$status" -lt 126 ]
}
export PATH="$HOME/.local/bin:$PATH"
[ -z "${GITHUB_PATH:-}" ] || echo "$HOME/.local/bin" >> "$GITHUB_PATH"
for tool in clang-cl lld-link llvm-ar llvm-rc llvm-readobj llvm-objdump; do
    [ "$(command -v "$tool")" = "$HOME/.local/bin/$tool" ] || { echo "$tool does not resolve to $HOME/.local/bin/$tool: $(command -v "$tool" || echo 'not on PATH')" >&2; exit 1; }
    probe_tool "$tool" || { echo "$tool is linked at $HOME/.local/bin/$tool but does not run" >&2; exit 1; }
    echo "$tool: $(command -v "$tool") -> $(readlink -f "$HOME/.local/bin/$tool")"
done
clang_major="$(clang-cl --version | sed -n 's/.*clang version \([0-9][0-9]*\)\..*/\1/p' | head -1)"
[ "${clang_major:-0}" -ge 19 ] || { echo "clang-cl major version ${clang_major:-unknown} is below 19; the pinned xwin CRT needs Clang 19 or newer" >&2; exit 1; }
echo "clang-cl major version: $clang_major"

section "xwin $XWIN_VERSION (CRT $XWIN_CRT_VERSION, SDK $XWIN_SDK_VERSION)"
if [ -d "$XWIN_ROOT/crt/lib/x86_64" ] && [ -d "$XWIN_ROOT/sdk/lib/um/x86_64" ]; then
    echo "xwin splat already present at $XWIN_ROOT (cache hit)"
else
    work="$(mktemp -d)"
    archive="xwin-$XWIN_VERSION-x86_64-unknown-linux-musl.tar.gz"
    curl --fail --location --silent --show-error --retry 3 \
        --output "$work/$archive" "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/$archive"
    echo "$XWIN_SHA256  $work/$archive" | sha256sum -c -
    tar -xzf "$work/$archive" -C "$work"
    xwin="$(find "$work" -type f -name xwin | head -1)"
    [ -n "$xwin" ] || { echo "the xwin archive holds no xwin binary" >&2; exit 1; }
    chmod +x "$xwin"
    mkdir -p "$XWIN_ROOT"
    "$xwin" --accept-license --cache-dir "$work/dl" --crt-version "$XWIN_CRT_VERSION" --sdk-version "$XWIN_SDK_VERSION" \
        splat --output "$XWIN_ROOT"
    rm -rf "$work"
fi
test -d "$XWIN_ROOT/crt/lib/x86_64" || { echo "missing xwin CRT libs: $XWIN_ROOT/crt/lib/x86_64" >&2; exit 1; }
test -d "$XWIN_ROOT/sdk/lib/um/x86_64" || { echo "missing xwin SDK libs: $XWIN_ROOT/sdk/lib/um/x86_64" >&2; exit 1; }
echo "xwin ready at $XWIN_ROOT"

section "smoke-compile a Windows translation unit with the xmake xwin flags"
smoke="$(mktemp -d)"
printf '#include <windows.h>\n#include <vector>\nint main() { std::vector<int> v(1); return static_cast<int>(v.size()) - 1 + static_cast<int>(GetCurrentProcessId() & 0); }\n' > "$smoke/smoke.cpp"
clang-cl --target=x86_64-pc-windows-msvc -fms-compatibility -fms-extensions /std:c++20 \
    "/imsvc$XWIN_ROOT/crt/include" "/imsvc$XWIN_ROOT/sdk/include/ucrt" "/imsvc$XWIN_ROOT/sdk/include/um" \
    "/imsvc$XWIN_ROOT/sdk/include/shared" "/imsvc$XWIN_ROOT/sdk/include/winrt" /c "$smoke/smoke.cpp" "/Fo$smoke/smoke.obj" \
    || { echo "clang-cl cannot compile a trivial Windows translation unit against the xwin CRT/SDK at $XWIN_ROOT" >&2; exit 1; }
object_headers="$(llvm-readobj --file-headers "$smoke/smoke.obj")"
grep -Fq 'IMAGE_FILE_MACHINE_AMD64' <<<"$object_headers" || { echo "the smoke object is not an x86_64 COFF object" >&2; exit 1; }
rm -rf "$smoke"
echo "clang-cl compiled a Windows x86_64 object against the xwin CRT/SDK"
