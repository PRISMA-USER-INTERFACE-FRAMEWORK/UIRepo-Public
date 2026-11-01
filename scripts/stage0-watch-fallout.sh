#!/usr/bin/env bash
set -euo pipefail

# Stage 0 process evidence on Linux, where Fallout runs under Proton/Wine.
#
# The Windows launcher (stage0-run-fallout.ps1) starts f4se_loader.exe itself,
# so it is the parent and can read a real exit code. On Linux the game is
# started by Mod Organizer through Proton and this script is not in that
# process tree, which means the kernel will not give us its exit status - only
# a parent can reap that.
#
# Rather than invent a number, this observer records exactly what it can prove
# and states plainly what it cannot. The evidence it writes is marked
# `evidence_mode=linux-observer`, and stage0-collect-evidence.sh refuses to
# treat it as equivalent to a Windows exit-code bundle.
#
# What it still proves:
#   - exactly one Fallout4.exe existed, so the evidence is unambiguous
#   - that process came from the expected game directory
#   - when it started and when it disappeared
#   - whether a new crash dump appeared while it ran
#
# Usage:
#   scripts/stage0-watch-fallout.sh --game-dir <dir> --evidence <file> [--crash-dumps <dir>] [--timeout <s>]
#
# Start it before launching the game from MO2. It waits for the process to
# appear, then follows it until it exits.

game_dir=""
evidence_path=""
crash_dir=""
appear_timeout=600
poll_seconds=2

fail() {
    echo "Stage 0 Linux process evidence failed: $*" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --game-dir) game_dir="${2:-}"; shift 2 ;;
        --evidence) evidence_path="${2:-}"; shift 2 ;;
        --crash-dumps) crash_dir="${2:-}"; shift 2 ;;
        --timeout) appear_timeout="${2:-}"; shift 2 ;;
        --poll) poll_seconds="${2:-}"; shift 2 ;;
        *) fail "unknown argument: $1" ;;
    esac
done

[ -n "$game_dir" ] || fail "--game-dir is required"
[ -n "$evidence_path" ] || fail "--evidence is required"
[ -d "$game_dir" ] || fail "game directory does not exist: $game_dir"
[ -x "$game_dir/f4se_loader.exe" ] || [ -f "$game_dir/f4se_loader.exe" ] ||
    fail "no f4se_loader.exe in $game_dir; Stage 0 requires the game be started through F4SE"

utc_now() { date -u +%Y-%m-%dT%H:%M:%SZ; }

# Wine reports Windows paths (Z:\run\media\...). Fold one to a Linux path so it
# can be compared with the directory we were told to expect.
wine_path_to_linux() {
    printf '%s' "$1" | sed -e 's|\\|/|g' -e 's|^[A-Za-z]:||'
}

count_crash_dumps() {
    if [ -n "$crash_dir" ] && [ -d "$crash_dir" ]; then
        find "$crash_dir" -type f 2>/dev/null | wc -l
    else
        echo 0
    fi
}

# One process only. Two would make every later claim ambiguous, which is the
# same rule the Windows launcher enforces.
find_fallout_pids() {
    pgrep -f 'Fallout4\.exe' 2>/dev/null | tr '\n' ' ' || true
}

existing="$(find_fallout_pids)"
if [ -n "${existing// /}" ]; then
    fail "Fallout4.exe is already running (pids: $existing); close it before starting the observer"
fi

crash_before="$(count_crash_dumps)"
echo "waiting up to ${appear_timeout}s for Fallout4.exe; launch it from Mod Organizer now"

waited=0
pids=""
while [ "$waited" -lt "$appear_timeout" ]; do
    pids="$(find_fallout_pids)"
    [ -n "${pids// /}" ] && break
    sleep "$poll_seconds"
    waited=$((waited + poll_seconds))
done

if [ -z "${pids// /}" ]; then
    {
        echo "evidence_mode=linux-observer"
        echo "observed_utc=$(utc_now)"
        echo "fallout_started=false"
        echo "fallout_start_failure=Fallout4.exe did not appear within ${appear_timeout}s"
    } > "$evidence_path"
    fail "Fallout4.exe did not appear within ${appear_timeout}s"
fi

set -- $pids
if [ "$#" -gt 1 ]; then
    fail "multiple Fallout4.exe processes appeared (pids: $pids); process evidence would be ambiguous"
fi
pid="$1"

raw_cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
wine_exe="$(printf '%s' "$raw_cmdline" | grep -oE '[A-Za-z]:\\[^ ]*Fallout4\.exe' | head -1 || true)"
[ -n "$wine_exe" ] || fail "could not read the Fallout4.exe path from pid $pid"

observed_dir="$(dirname "$(wine_path_to_linux "$wine_exe")")"
expected_dir="$(cd "$game_dir" && pwd -P)"
# Wine paths can carry doubled separators; normalise before comparing.
observed_dir="$(printf '%s' "$observed_dir" | sed -e 's|//*|/|g' -e 's|/$||')"

if [ "$observed_dir" != "$expected_dir" ]; then
    fail "Fallout4.exe is running from $observed_dir, not the expected $expected_dir"
fi

start_utc="$(utc_now)"
echo "observing Fallout4.exe pid $pid from $observed_dir"

while kill -0 "$pid" 2>/dev/null; do
    sleep "$poll_seconds"
done

exit_utc="$(utc_now)"
crash_after="$(count_crash_dumps)"
crash_new=false
[ "$crash_after" -gt "$crash_before" ] && crash_new=true

{
    echo "evidence_mode=linux-observer"
    echo "fallout_started=true"
    echo "fallout_path=$wine_exe"
    echo "fallout_linux_path=$observed_dir/Fallout4.exe"
    echo "fallout_pid=$pid"
    echo "fallout_start_utc=$start_utc"
    echo "fallout_exit_utc=$exit_utc"
    echo "fallout_termination=observed-disappeared"
    # Stated, not omitted: a reader must never mistake this for a real 0.
    echo "fallout_exit_code=unavailable"
    echo "fallout_exit_code_reason=the observer is not the process parent, so Linux cannot report its exit status"
    echo "crash_dumps_before=$crash_before"
    echo "crash_dumps_after=$crash_after"
    echo "crash_dump_new=$crash_new"
} > "$evidence_path"

if [ "$crash_new" = true ]; then
    fail "a new crash dump appeared while Fallout was running; see $crash_dir"
fi

echo "PASS: Fallout4.exe (pid $pid) ran from the expected directory and exited with no new crash dump"
echo "evidence: $evidence_path"
echo "NOTE: exit code is unavailable on Linux; this bundle is not equivalent to a Windows Stage 0 bundle"
