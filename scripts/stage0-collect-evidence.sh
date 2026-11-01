#!/usr/bin/env bash
set -euo pipefail

# Stage 0 evidence collection on Linux.
#
# A port of stage0-collect-evidence.ps1 for the platform the framework is
# actually being tested on. The verification it performs is identical where it
# can be - the same manifest checks, the same SHA-256 comparisons, the same
# Python verifiers, which are stdlib-only and portable.
#
# One thing is deliberately NOT identical. The Windows collector requires
# `fallout_exit_code=0` from stage0-run-fallout.ps1. On Linux the game is
# started by Mod Organizer through Proton, so the observer is not its parent
# and cannot read an exit status. This collector accepts the observer's
# `evidence_mode=linux-observer` in its place, and stamps the bundle
# accordingly so nobody can mistake it for a Windows-grade one.
#
# Usage:
#   scripts/stage0-collect-evidence.sh \
#       --log <UIRepo_F4.log> \
#       --bundle <dir containing build/windows/x64/release> \
#       [--installed-dll <path>] [--process-evidence <STAGE0_PROCESS.txt>] \
#       [--evidence-dir <dir>] [--baseline-only]

root="$(cd "$(dirname "$0")/.." && pwd)"

log_path=""
bundle_root=""
installed_dll=""
process_evidence=""
evidence_dir="$PWD/Stage0Evidence"
baseline_only=false

fail() {
    echo "Stage 0 evidence collection failed: $*" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --log) log_path="${2:-}"; shift 2 ;;
        --bundle) bundle_root="${2:-}"; shift 2 ;;
        --installed-dll) installed_dll="${2:-}"; shift 2 ;;
        --process-evidence) process_evidence="${2:-}"; shift 2 ;;
        --evidence-dir) evidence_dir="${2:-}"; shift 2 ;;
        --baseline-only) baseline_only=true; shift ;;
        *) fail "unknown argument: $1" ;;
    esac
done

[ -n "$log_path" ] || fail "--log is required"
[ -n "$bundle_root" ] || fail "--bundle is required"
[ -f "$log_path" ] || fail "log does not exist: $log_path"

release="$bundle_root/build/windows/x64/release"
manifest="$release/STAGE0_BUILD.txt"
dll="$release/UIRepo_F4.dll"
pdb="$release/UIRepo_F4.pdb"
provenance="$release/COMMONLIB_PROVENANCE.txt"

for required in "$manifest" "$dll" "$pdb" "$provenance"; do
    [ -f "$required" ] || fail "required Stage 0 file is missing: $required"
done

if [ "$baseline_only" = false ]; then
    [ -n "$installed_dll" ] ||
        fail "final collection requires --installed-dll so the installed plugin is proven identical to the CI artifact"
    [ -n "$process_evidence" ] ||
        fail "final collection requires --process-evidence from stage0-watch-fallout.sh"
fi

python="$(command -v python3 || command -v python || true)"
[ -n "$python" ] || fail "Python 3 is required to run the Stage 0 verifiers"

manifest_value() { grep -E "^$1=" "$manifest" | head -1 | cut -d= -f2-; }
manifest_hash() { grep -E "  build/windows/x64/release/$1\$" "$manifest" | head -1 | cut -d' ' -f1; }

for required in workflow_sha source_head_sha commonlib_sha; do
    [ -n "$(manifest_value "$required")" ] || fail "STAGE0_BUILD.txt is missing $required"
done

sha_of() { sha256sum "$1" | cut -d' ' -f1; }

bundle_dll_hash="$(sha_of "$dll")"
bundle_pdb_hash="$(sha_of "$pdb")"
provenance_hash="$(sha_of "$provenance")"

[ "$bundle_dll_hash" = "$(manifest_hash UIRepo_F4.dll)" ] || fail "CI bundle DLL hash does not match STAGE0_BUILD.txt"
[ "$bundle_pdb_hash" = "$(manifest_hash UIRepo_F4.pdb)" ] || fail "CI bundle PDB hash does not match STAGE0_BUILD.txt"
[ "$provenance_hash" = "$(manifest_hash COMMONLIB_PROVENANCE.txt)" ] ||
    fail "CommonLib provenance hash does not match STAGE0_BUILD.txt"

provenance_commonlib="$(grep -E '^commonlib_sha=' "$provenance" | head -1 | cut -d= -f2-)"
[ -n "$provenance_commonlib" ] || fail "COMMONLIB_PROVENANCE.txt is missing its CommonLibF4 SHA"
[ "$provenance_commonlib" = "$(manifest_value commonlib_sha)" ] ||
    fail "CommonLib provenance SHA does not match STAGE0_BUILD.txt"

if grep -nE '^[-+U]' "$provenance" | grep -vE '^[0-9]+:commonlib_sha=' | grep -qE '^[0-9]+:[-+U]'; then
    fail "COMMONLIB_PROVENANCE.txt contains missing, mismatched, or conflicted submodules"
fi
echo "PASS: recursive CommonLibF4 provenance matches the CI manifest"

installed_hash=""
if [ -n "$installed_dll" ]; then
    [ -f "$installed_dll" ] || fail "installed DLL does not exist: $installed_dll"
    installed_hash="$(sha_of "$installed_dll")"
    [ "$installed_hash" = "$bundle_dll_hash" ] ||
        fail "installed UIRepo_F4.dll does not match the CI artifact ($installed_hash != $bundle_dll_hash)"
    echo "PASS: installed UIRepo_F4.dll matches the CI artifact"
fi

evidence_mode="none"
if [ -n "$process_evidence" ]; then
    [ -f "$process_evidence" ] || fail "process evidence does not exist: $process_evidence"
    process_value() { grep -E "^$1=" "$process_evidence" | head -1 | cut -d= -f2-; }

    evidence_mode="$(process_value evidence_mode)"
    [ "$(process_value fallout_started)" = "true" ] || fail "process evidence does not prove Fallout4.exe started"

    case "$evidence_mode" in
        linux-observer)
            # The exit code is genuinely unavailable here. Everything that IS
            # observable must still hold, and the observer must have said so
            # rather than reporting a number it could not know.
            [ "$(process_value fallout_exit_code)" = "unavailable" ] ||
                fail "linux-observer evidence must record fallout_exit_code=unavailable, not a fabricated value"
            [ -n "$(process_value fallout_exit_code_reason)" ] ||
                fail "linux-observer evidence must state why the exit code is unavailable"
            [ "$(process_value crash_dump_new)" = "false" ] ||
                fail "a new crash dump appeared during the run"
            [ "$(process_value fallout_termination)" = "observed-disappeared" ] ||
                fail "linux-observer evidence does not record how the process ended"
            echo "PASS: Fallout ran from the expected directory and left no new crash dump (exit code unavailable on Linux)"
            ;;
        ""|*)
            # A Windows bundle collected on this machine is still valid.
            [ "$(process_value fallout_exit_code)" = "0" ] ||
                fail "Fallout4.exe did not exit normally (exit code $(process_value fallout_exit_code))"
            echo "PASS: Fallout4.exe process evidence reports normal exit code 0"
            ;;
    esac

    start_utc="$(process_value fallout_start_utc)"
    exit_utc="$(process_value fallout_exit_utc)"
    if [ -n "$start_utc" ] && [ -n "$exit_utc" ]; then
        [ "$(date -u -d "$exit_utc" +%s)" -ge "$(date -u -d "$start_utc" +%s)" ] ||
            fail "process evidence has an exit timestamp before the start timestamp"
    fi
fi

echo "========== Stage 0 log/artifact verification =========="
verify_output="$("$python" "$root/scripts/stage0-verify-log.py" \
    --manifest "$manifest" --dll "$dll" --pdb "$pdb" --provenance "$provenance" \
    ${baseline_only:+} $( [ "$baseline_only" = false ] && echo --full-cycle ) "$log_path" 2>&1)" || {
    echo "$verify_output"
    fail "Stage 0 verifier failed"
}
echo "$verify_output"

echo "========== Stage 0 log health =========="
health_output="$("$python" "$root/scripts/stage0-verify-log-health.py" "$log_path" 2>&1)" || {
    echo "$health_output"
    fail "Stage 0 log health verifier failed"
}
echo "$health_output"

repeated_output=""
if [ "$baseline_only" = false ]; then
    echo "========== Stage 0 repeated lifecycle verification =========="
    repeated_output="$("$python" "$root/scripts/stage0-verify-repeated-lifecycle.py" "$log_path" 2>&1)" || {
        echo "$repeated_output"
        fail "Stage 0 repeated lifecycle verifier failed"
    }
    echo "$repeated_output"
fi

mkdir -p "$evidence_dir"
cp -f "$log_path" "$evidence_dir/UIRepo_F4.log"
cp -f "$manifest" "$evidence_dir/STAGE0_BUILD.txt"
cp -f "$dll" "$evidence_dir/UIRepo_F4.dll"
cp -f "$pdb" "$evidence_dir/UIRepo_F4.pdb"
cp -f "$provenance" "$evidence_dir/COMMONLIB_PROVENANCE.txt"
[ -n "$process_evidence" ] && cp -f "$process_evidence" "$evidence_dir/STAGE0_PROCESS.txt"

printf '%s\n' "$verify_output" > "$evidence_dir/verify-log.txt"
printf '%s\n' "$health_output" > "$evidence_dir/verify-log-health.txt"
[ -n "$repeated_output" ] && printf '%s\n' "$repeated_output" > "$evidence_dir/verify-repeated-lifecycle.txt"

system="$evidence_dir/system.txt"
{
    echo "collected_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "collector=stage0-collect-evidence.sh (linux)"
    echo "acceptance_mode=$evidence_mode"
    echo "workflow_sha=$(manifest_value workflow_sha)"
    echo "source_head_sha=$(manifest_value source_head_sha)"
    echo "commonlib_sha=$(manifest_value commonlib_sha)"
    echo "bundle_dll_sha256=$bundle_dll_hash"
    echo "bundle_pdb_sha256=$bundle_pdb_hash"
    echo "commonlib_provenance_sha256=$provenance_hash"
    [ -n "$installed_hash" ] && echo "installed_dll_sha256=$installed_hash"
    echo
    echo "[operating_system]"
    uname -a
    [ -r /etc/os-release ] && grep -E '^(NAME|VERSION)=' /etc/os-release
    echo
    echo "[graphics]"
    # Under Proton the D3D11 the framework talks to is DXVK on Vulkan, so the
    # Vulkan driver is the relevant one to record, not a D3D driver version.
    lspci -nn 2>/dev/null | grep -iE 'vga|3d controller' || echo "lspci unavailable"
    if command -v vulkaninfo >/dev/null 2>&1; then
        vulkaninfo --summary 2>/dev/null | sed -n '1,40p'
    else
        echo "vulkaninfo unavailable"
    fi
    echo
    echo "[proton]"
    echo "note: D3D11 is provided by DXVK; the Windows D3D11 debug layer does not exist here"
} > "$system"

cat > "$evidence_dir/MANUAL_CHECKLIST.txt" <<'CHECKLIST'
Stage 0 manual observations still requiring a human pass/fail decision:
[ ] Built-in UIRepo native runtime probe was visible.
[ ] DirectWrite probe text was readable and correctly positioned.
[ ] Verifier-confirmed alt-tab cycles completed without visible corruption or crash.
[ ] Verifier-confirmed repeated resize cycles completed without visible corruption or stale rendering.
[ ] Supported windowed/borderless mode transitions recovered correctly.
[ ] New game reached gameplay.
[ ] Verifier-confirmed MainMenu returns reached a stable visible MainMenu.
[ ] Existing save loaded and reached gameplay.
[ ] Repeated MainMenu/session cycle completed without visible corruption or crash.
[ ] Fallout closed without a visible crash dialog or hang.

Linux-specific, because this bundle was collected under Proton:
[ ] No UIRepo-attributable errors in the DXVK/Proton log, if one was captured.
    The Windows D3D11 debug layer does not exist here, so that check cannot run
    and must not be recorded as passed.
[ ] Process exit was observed rather than reaped: the exit code is unavailable.
    Confirm by hand that the game closed cleanly with no crash dialog.
CHECKLIST

echo "Stage 0 objective evidence collected at: $evidence_dir"
if [ "$evidence_mode" = "linux-observer" ]; then
    echo "acceptance_mode=linux-observer: exit code could not be read on this platform."
    echo "This bundle is not equivalent to a Windows Stage 0 bundle; see MANUAL_CHECKLIST.txt."
fi
echo "Complete MANUAL_CHECKLIST.txt after the in-game observations."
