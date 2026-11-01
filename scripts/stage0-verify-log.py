#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import sys


BOOTSTRAP_RE = re.compile(
    r"UIRepo_F4 Stage0: native bootstrap loaded \(build ([^,]+), runtime ([^,]+), "
    r"F4SE ([^,]+), commonlib_NonVR ([0-9a-f]{40})\)"
)
SWAP_CHAIN_RE = re.compile(
    r"UIRepo_F4 Stage0: swap chain .* owners Present '([^']+)' "
    r"ResizeBuffers '([^']+)' GetBuffer '([^']+)' \(([^)]+)\)"
)
TARGET_RE = re.compile(
    r"(?:Fallout-native|fallback) render target generation \d+ acquired "
    r"\((\d+)x(\d+), format (\d+), samples (\d+)"
)
FIRST_FRAME_RE = re.compile(
    r"first native probe frame rendered \(rects (\d+), text (\d+), "
    r"glyph runs (\d+), clips (\d+)/(\d+)\)"
)
RECOVERY_FRAME_RE = re.compile(
    r"UIRepo_F4 Stage0: native probe recovery frame rendered \(generation (\d+), "
    r"rects (\d+), text (\d+), glyph runs (\d+), clips (\d+)/(\d+)\)"
)
FOCUS_RE = re.compile(
    r"UIRepo_F4 Stage0: app focus (active|inactive) \(transition (\d+)\)"
)
RESIZE_COMPLETION_RE = re.compile(
    r"UIRepo_F4 Stage0: ResizeBuffers completed \(buffers (\d+), requested (\d+)x(\d+), "
    r"format (\d+), flags 0x([0-9A-Fa-f]+), HRESULT 0x([0-9A-Fa-f]{8})\)"
)
PROVENANCE_SUBMODULE_RE = re.compile(r"^ ([0-9a-f]{40}) (\S.*)$")


def contains_any(text: str, needles: tuple[str, ...]) -> bool:
    return any(needle in text for needle in needles)


def first_after(text: str, start: int, needles: tuple[str, ...]) -> int:
    positions = [text.find(needle, start) for needle in needles]
    positions = [position for position in positions if position >= 0]
    return min(positions) if positions else -1


def read_manifest(path: Path) -> tuple[dict[str, str], dict[str, str]]:
    fields: dict[str, str] = {}
    hashes: dict[str, str] = {}
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise ValueError(f"could not read manifest {path}: {exc}") from exc

    for line in lines:
        key, separator, value = line.partition("=")
        if separator:
            fields[key.strip()] = value.strip()
            continue

        parts = line.split()
        if len(parts) < 2:
            continue
        digest = parts[0].strip().lower()
        artifact = parts[-1].lstrip("*")
        if len(digest) == 64 and all(character in "0123456789abcdef" for character in digest):
            hashes[Path(artifact).name] = digest

    if not fields.get("workflow_sha"):
        raise ValueError(f"manifest {path} does not contain workflow_sha")
    if not fields.get("commonlib_sha"):
        raise ValueError(f"manifest {path} does not contain commonlib_sha")
    return fields, hashes


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise ValueError(f"could not hash {path}: {exc}") from exc
    return digest.hexdigest()


def verify_artifact_hash(path: Path, name: str, hashes: dict[str, str]) -> bool:
    expected = hashes.get(name)
    if not expected:
        print(f"FAIL: manifest does not contain SHA-256 for {name}")
        return False
    try:
        actual = sha256_file(path)
    except ValueError as exc:
        print(f"FAIL: {exc}")
        return False
    if actual != expected:
        print(f"FAIL: {name} SHA-256 mismatch (expected {expected}, got {actual})")
        return False
    print(f"PASS: {name} SHA-256 matches manifest")
    return True


def verify_commonlib_provenance(
    path: Path,
    expected_commonlib_sha: str,
    hashes: dict[str, str],
) -> bool:
    name = "COMMONLIB_PROVENANCE.txt"
    if not verify_artifact_hash(path, name, hashes):
        return False

    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        print(f"FAIL: could not read {path}: {exc}")
        return False

    expected_header = f"commonlib_sha={expected_commonlib_sha}"
    if not lines or lines[0].strip().lower() != expected_header.lower():
        actual = lines[0].strip() if lines else "<missing>"
        print(
            "FAIL: CommonLib provenance top-level SHA does not match manifest "
            f"({actual!r} != {expected_header!r})"
        )
        return False

    submodule_lines = [line for line in lines[1:] if line.strip()]
    if not submodule_lines:
        print("FAIL: CommonLib provenance contains no recursive submodule status")
        return False

    for line in submodule_lines:
        if line[0] in "-+U":
            print(f"FAIL: CommonLib provenance contains bad submodule status: {line}")
            return False
        if PROVENANCE_SUBMODULE_RE.fullmatch(line) is None:
            print(f"FAIL: malformed CommonLib recursive submodule status: {line}")
            return False

    print(
        "PASS: recursive CommonLibF4 provenance "
        f"({len(submodule_lines)} verified submodule entries)"
    )
    return True


def validate_structured_diagnostics(text: str) -> list[str]:
    failures: list[str] = []

    bootstrap = BOOTSTRAP_RE.search(text)
    if bootstrap is None:
        print("MISSING: structured bootstrap versions")
        failures.append("structured bootstrap versions")
    else:
        build, runtime, f4se, commonlib = bootstrap.groups()
        if not build.strip() or not runtime.strip() or not f4se.strip():
            print("MISSING: non-empty build/runtime/F4SE versions")
            failures.append("bootstrap version values")
        else:
            print(
                "PASS: structured bootstrap versions "
                f"(build {build}, runtime {runtime}, F4SE {f4se}, CommonLib {commonlib})"
            )

    swap = SWAP_CHAIN_RE.search(text)
    if swap is None:
        print("MISSING: complete Present/ResizeBuffers/GetBuffer ownership diagnostic")
        failures.append("complete swap-chain ownership diagnostic")
    else:
        present_owner, resize_owner, get_buffer_owner, classification = swap.groups()
        owners = (present_owner, resize_owner, get_buffer_owner)
        if any(owner in {"<null>", "<unknown>"} for owner in owners):
            print(f"FAIL: unresolved swap-chain owner module(s): {owners}")
            failures.append("resolved swap-chain owner modules")
        elif classification not in {
            "normal COM ownership",
            "known frame-generation proxy / borrowed",
        }:
            print(f"FAIL: unknown backbuffer ownership classification: {classification}")
            failures.append("backbuffer ownership classification")
        else:
            print(
                "PASS: swap-chain owners/classification "
                f"(Present {present_owner}, ResizeBuffers {resize_owner}, "
                f"GetBuffer {get_buffer_owner}, {classification})"
            )

    targets = TARGET_RE.findall(text)
    if not targets:
        print("MISSING: structured render-target dimensions/format/sample count")
        failures.append("structured render-target diagnostic")
    else:
        invalid_target = next(
            (
                values
                for values in targets
                if int(values[0]) <= 0 or int(values[1]) <= 0 or int(values[3]) <= 0
            ),
            None,
        )
        if invalid_target is not None:
            print(f"FAIL: invalid render-target diagnostic values: {invalid_target}")
            failures.append("valid render-target diagnostic values")
        else:
            width, height, fmt, samples = targets[0]
            print(
                "PASS: render-target diagnostics "
                f"({width}x{height}, format {fmt}, samples {samples})"
            )

    frames = FIRST_FRAME_RE.findall(text)
    if len(frames) != 1:
        print(f"FAIL: first-frame diagnostic count is {len(frames)}, expected exactly 1")
        failures.append("single first-frame diagnostic")
    else:
        rects, text_commands, glyph_runs, clip_pushes, clip_pops = map(int, frames[0])
        if rects <= 0 or text_commands <= 0 or glyph_runs <= 0:
            print(
                "FAIL: first-frame execution counters must be non-zero "
                f"(rects {rects}, text {text_commands}, glyph runs {glyph_runs})"
            )
            failures.append("first-frame primitive execution")
        elif clip_pushes != clip_pops:
            print(
                "FAIL: first-frame clip counters are unbalanced "
                f"({clip_pushes}/{clip_pops})"
            )
            failures.append("first-frame clip balance")
        else:
            print(
                "PASS: first-frame execution counters "
                f"(rects {rects}, text {text_commands}, glyph runs {glyph_runs}, "
                f"clips {clip_pushes}/{clip_pops})"
            )

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify that UIRepo_F4.log contains the Stage 0 Fallout acceptance evidence."
    )
    parser.add_argument("log", type=Path, help="Path to UIRepo_F4.log")
    parser.add_argument(
        "--full-cycle",
        action="store_true",
        help=(
            "Require five ordered alt-tab focus cycles, repeated new/load/MainMenu cycles with "
            "successful save loads, a successful ResizeBuffers call, ordered render-target "
            "reacquisition, and successful probe rendering on the reacquired target."
        ),
    )
    identity = parser.add_mutually_exclusive_group()
    identity.add_argument(
        "--expect-build-sha",
        help="Require the Stage 0 bootstrap log to contain this exact embedded CI build SHA.",
    )
    identity.add_argument(
        "--manifest",
        type=Path,
        help="Read expected build/CommonLib identity and artifact hashes from STAGE0_BUILD.txt.",
    )
    parser.add_argument(
        "--dll",
        type=Path,
        help="When --manifest is supplied, verify this UIRepo_F4.dll SHA-256 against the manifest.",
    )
    parser.add_argument(
        "--pdb",
        type=Path,
        help="When --manifest is supplied, verify this UIRepo_F4.pdb SHA-256 against the manifest.",
    )
    parser.add_argument(
        "--provenance",
        type=Path,
        help=(
            "When --manifest is supplied, verify COMMONLIB_PROVENANCE.txt hash, top-level SHA, "
            "and recursive submodule status."
        ),
    )
    args = parser.parse_args()

    artifact_args = (args.dll, args.pdb, args.provenance)
    if any(path is not None for path in artifact_args) and args.manifest is None:
        print("FAIL: --dll/--pdb/--provenance require --manifest", file=sys.stderr)
        return 2

    expected_build_sha = args.expect_build_sha
    expected_commonlib_sha: str | None = None
    manifest_hashes: dict[str, str] = {}
    if args.manifest is not None:
        try:
            manifest_fields, manifest_hashes = read_manifest(args.manifest)
        except ValueError as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 2
        expected_build_sha = manifest_fields["workflow_sha"]
        expected_commonlib_sha = manifest_fields["commonlib_sha"]

    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"FAIL: could not read {args.log}: {exc}", file=sys.stderr)
        return 2

    checks: list[tuple[str, tuple[str, ...]]] = [
        ("F4SE/native bootstrap", ("UIRepo_F4 Stage0: native bootstrap loaded",)),
        ("game-thread dispatcher", ("attached to the game-thread dispatcher",)),
        ("window lifecycle observer", ("UIRepo_F4 Stage0: window lifecycle sink installed",)),
        ("MainMenu lifecycle observer", ("UIRepo_F4 Stage0: MainMenu lifecycle sink installed",)),
        (
            "renderer hooks",
            (
                "engine UIEndFrame compositor installed",
                "DXGI Present/ResizeBuffers fallback compositor installed",
            ),
        ),
        (
            "D3D11 renderer initialization",
            (
                "Fallout-native D3D11 renderer generation",
                "fallback D3D11 renderer generation",
            ),
        ),
        ("game data ready", ("UIRepo_F4 Stage0: session event game-data-ready",)),
    ]

    if expected_build_sha:
        checks.append(
            (
                "exact CI build identity",
                (f"UIRepo_F4 Stage0: native bootstrap loaded (build {expected_build_sha},",),
            )
        )
    if expected_commonlib_sha:
        checks.append(
            (
                "exact CommonLibF4 identity",
                (f"commonlib_NonVR {expected_commonlib_sha})",),
            )
        )

    missing: list[str] = validate_structured_diagnostics(text)
    for label, needles in checks:
        if contains_any(text, needles):
            print(f"PASS: {label}")
        else:
            print(f"MISSING: {label}")
            missing.append(label)

    if args.full_cycle:
        focus_events = [(state, int(transition)) for state, transition in FOCUS_RE.findall(text)]
        focus_sequence_valid = True
        expected_transition = 1
        focus_cycles = 0
        inactive_pending = False
        for state, transition in focus_events:
            if transition != expected_transition:
                print(
                    "FAIL: app-focus transition sequence is not contiguous "
                    f"(expected {expected_transition}, got {transition})"
                )
                missing.append("contiguous app-focus transition sequence")
                focus_sequence_valid = False
                break
            expected_transition += 1

            if state == "inactive":
                inactive_pending = True
            elif inactive_pending:
                focus_cycles += 1
                inactive_pending = False

        if focus_sequence_valid:
            if focus_cycles >= 5:
                print(f"PASS: alt-tab focus cycles ({focus_cycles} inactive/active returns)")
            else:
                print(
                    "MISSING: alt-tab focus cycles "
                    f"(found {focus_cycles}, need at least 5 inactive/active returns)"
                )
                missing.append("alt-tab focus cycles")

        repeated_events = (
            ("new-game cycles", "UIRepo_F4 Stage0: session event new-game", 2),
            ("load-game begin cycles", "UIRepo_F4 Stage0: session event pre-load-game", 2),
            ("MainMenu returns", "UIRepo_F4 Stage0: main-menu opened", 2),
        )
        for label, marker, minimum in repeated_events:
            count = text.count(marker)
            if count >= minimum:
                print(f"PASS: {label} ({count} observations)")
            else:
                print(f"MISSING: {label} (found {count}, need at least {minimum})")
                missing.append(label)

        successful_load_marker = "UIRepo_F4 Stage0: session event post-load-game success true"
        failed_load_marker = "UIRepo_F4 Stage0: session event post-load-game success false"
        successful_load_count = text.count(successful_load_marker)
        failed_load_count = text.count(failed_load_marker)
        if failed_load_count:
            print(f"FAIL: load-game failures were observed ({failed_load_count})")
            missing.append("no failed load-game attempts")
        if successful_load_count >= 2:
            print(f"PASS: successful load-game cycles ({successful_load_count} observations)")
        else:
            print(
                "MISSING: successful load-game cycles "
                f"(found {successful_load_count}, need at least 2)"
            )
            missing.append("successful load-game cycles")

        target_markers = (
            "Fallout-native render target generation",
            "fallback render target generation",
        )
        target_acquisitions = sum(text.count(marker) for marker in target_markers)
        if target_acquisitions >= 2:
            print(f"PASS: render target reacquired ({target_acquisitions} acquisitions)")
        else:
            print(
                "MISSING: render target reacquisition "
                f"(found {target_acquisitions}, need at least 2)"
            )
            missing.append("render target reacquisition")

        resize_marker = "UIRepo_F4 Stage0: ResizeBuffers pre-release of native render target"
        resize_position = text.find(resize_marker)
        if resize_position < 0:
            print("MISSING: ResizeBuffers pre-release evidence")
            missing.append("ResizeBuffers pre-release evidence")
        else:
            completion = RESIZE_COMPLETION_RE.search(text, resize_position + len(resize_marker))
            if completion is None:
                print("MISSING: ResizeBuffers completion after target pre-release")
                missing.append("ResizeBuffers completion")
            else:
                buffer_count, requested_width, requested_height, fmt, flags, hresult = completion.groups()
                result_code = int(hresult, 16)
                if result_code & 0x80000000:
                    print(f"FAIL: ResizeBuffers returned failing HRESULT 0x{hresult}")
                    missing.append("successful ResizeBuffers completion")
                else:
                    print(
                        "PASS: ResizeBuffers completed successfully "
                        f"(buffers {buffer_count}, requested {requested_width}x{requested_height}, "
                        f"format {fmt}, flags 0x{flags}, HRESULT 0x{hresult})"
                    )

                reacquired = TARGET_RE.search(text, completion.end())
                if reacquired is None:
                    print("MISSING: render-target reacquisition after ResizeBuffers completion")
                    missing.append("ordered ResizeBuffers reacquisition")
                else:
                    target_width, target_height, target_format, target_samples = reacquired.groups()
                    request_width = int(requested_width)
                    request_height = int(requested_height)
                    if (
                        request_width > 0
                        and request_height > 0
                        and (int(target_width) != request_width or int(target_height) != request_height)
                    ):
                        print(
                            "FAIL: reacquired target size does not match explicit ResizeBuffers request "
                            f"({target_width}x{target_height} != {requested_width}x{requested_height})"
                        )
                        missing.append("ResizeBuffers target dimensions")
                    else:
                        print(
                            "PASS: ResizeBuffers completion precedes matching render-target acquisition "
                            f"({target_width}x{target_height}, format {target_format}, samples {target_samples})"
                        )

                    target_generation_match = re.search(
                        r"render target generation (\d+) acquired",
                        reacquired.group(0),
                    )
                    recovery = RECOVERY_FRAME_RE.search(text, reacquired.end())
                    if recovery is None:
                        print("MISSING: successful probe recovery frame after ResizeBuffers reacquisition")
                        missing.append("post-resize probe recovery frame")
                    else:
                        (
                            recovery_generation,
                            recovery_rects,
                            recovery_text,
                            recovery_glyphs,
                            recovery_clip_pushes,
                            recovery_clip_pops,
                        ) = map(int, recovery.groups())
                        target_generation = (
                            int(target_generation_match.group(1))
                            if target_generation_match is not None
                            else None
                        )
                        if target_generation is None or recovery_generation != target_generation:
                            print(
                                "FAIL: probe recovery frame generation does not match the "
                                f"reacquired target ({recovery_generation} != {target_generation})"
                            )
                            missing.append("post-resize probe recovery generation")
                        elif recovery_rects <= 0 or recovery_text <= 0 or recovery_glyphs <= 0:
                            print(
                                "FAIL: probe recovery execution counters must be non-zero "
                                f"(rects {recovery_rects}, text {recovery_text}, "
                                f"glyph runs {recovery_glyphs})"
                            )
                            missing.append("post-resize probe recovery execution")
                        elif recovery_clip_pushes != recovery_clip_pops:
                            print(
                                "FAIL: probe recovery clip counters are unbalanced "
                                f"({recovery_clip_pushes}/{recovery_clip_pops})"
                            )
                            missing.append("post-resize probe recovery clip balance")
                        else:
                            print(
                                "PASS: reacquired target rendered a successful probe recovery frame "
                                f"(generation {recovery_generation}, rects {recovery_rects}, "
                                f"text {recovery_text}, glyph runs {recovery_glyphs}, "
                                f"clips {recovery_clip_pushes}/{recovery_clip_pops})"
                            )

        swap_marker = "UIRepo_F4 Stage0: engine swap chain changed"
        swap_position = text.find(swap_marker)
        if swap_position >= 0:
            renderer_position = first_after(
                text,
                swap_position + len(swap_marker),
                (
                    "Fallout-native D3D11 renderer generation",
                    "fallback D3D11 renderer generation",
                ),
            )
            target_position = first_after(text, swap_position + len(swap_marker), target_markers)
            if renderer_position >= 0 and target_position >= 0:
                print("PASS: swap-chain replacement is followed by renderer/target recovery")
            else:
                print("MISSING: renderer/target recovery after swap-chain replacement")
                missing.append("swap-chain replacement recovery")

    artifact_failures = 0
    if args.dll is not None and not verify_artifact_hash(args.dll, "UIRepo_F4.dll", manifest_hashes):
        artifact_failures += 1
    if args.pdb is not None and not verify_artifact_hash(args.pdb, "UIRepo_F4.pdb", manifest_hashes):
        artifact_failures += 1
    if args.provenance is not None:
        assert expected_commonlib_sha is not None
        if not verify_commonlib_provenance(
            args.provenance,
            expected_commonlib_sha,
            manifest_hashes,
        ):
            artifact_failures += 1

    error_markers = (
        "UIRepo_F4 Stage0: native renderer hooks failed to install",
        "renderer rejected the built-in probe display list",
        "Fallout render target is not owned by the renderer D3D11 device",
        "UIRepo_F4 Stage0: window lifecycle sink could not be installed",
        "UIRepo_F4 Stage0: MainMenu lifecycle sink could not be installed",
    )
    found_errors = [marker for marker in error_markers if marker in text]
    for marker in found_errors:
        print(f"FAIL MARKER: {marker}")

    if missing or found_errors or artifact_failures:
        print(
            f"Stage 0 verification failed: {len(missing)} missing checks, "
            f"{len(found_errors)} failure markers, {artifact_failures} artifact/provenance failures."
        )
        return 1

    print("Stage 0 log/artifact verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
