#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import re
import sys


GLYPH_MAX_ENTRIES = 2048
GLYPH_MAX_BYTES = 16 * 1024 * 1024
IMAGE_MAX_ENTRIES = 256
IMAGE_MAX_BYTES = 64 * 1024 * 1024
TEXT_LAYOUT_MAX_ENTRIES = 512
TEXT_LAYOUT_MAX_BYTES = 4 * 1024 * 1024
SURFACE_MAX_LIVE_BYTES = 128 * 1024 * 1024
SURFACE_MAX_POOL_ENTRIES = 16
SURFACE_MAX_POOL_BYTES = 64 * 1024 * 1024

TELEMETRY_RE = re.compile(
    r"UIRepo_F4 telemetry: reason (?P<reason>\S+) renderer-epoch (?P<renderer_epoch>\d+) "
    r"target-generation (?P<target_generation>\d+) target (?P<width>\d+)x(?P<height>\d+) "
    r"visible-docs (?P<visible>\d+) "
    r"glyph\(entries (?P<glyph_entries>\d+) bytes (?P<glyph_bytes>\d+) "
    r"hits (?P<glyph_hits>\d+) misses (?P<glyph_misses>\d+) uploads (?P<glyph_uploads>\d+) "
    r"evictions (?P<glyph_evictions>\d+) rejections (?P<glyph_rejections>\d+)\) "
    r"image\(entries (?P<image_entries>\d+) bytes (?P<image_bytes>\d+) "
    r"hits (?P<image_hits>\d+) misses (?P<image_misses>\d+) uploads (?P<image_uploads>\d+) "
    r"evictions (?P<image_evictions>\d+) rejections (?P<image_rejections>\d+) "
    r"decode-failures (?P<image_decode_failures>\d+) negative-hits (?P<image_negative_hits>\d+)\) "
    r"text-layout\(entries (?P<text_layout_entries>\d+) bytes (?P<text_layout_bytes>\d+) "
    r"hits (?P<text_layout_hits>\d+) misses (?P<text_layout_misses>\d+) "
    r"evictions (?P<text_layout_evictions>\d+) rejections (?P<text_layout_rejections>\d+)\) "
    r"surfaces\(live (?P<surface_live>\d+) live-bytes (?P<surface_live_bytes>\d+) "
    r"pooled (?P<surface_pooled>\d+) pooled-bytes (?P<surface_pooled_bytes>\d+) "
    r"allocations (?P<surface_allocations>\d+) reuses (?P<surface_reuses>\d+) "
    r"resizes (?P<surface_resizes>\d+) rebuilds (?P<surface_rebuilds>\d+) "
    r"rejections (?P<surface_rejections>\d+) drains (?P<surface_drains>\d+)\)"
)

CUMULATIVE_FIELDS = (
    "glyph_hits",
    "glyph_misses",
    "glyph_uploads",
    "glyph_evictions",
    "glyph_rejections",
    "image_hits",
    "image_misses",
    "image_uploads",
    "image_evictions",
    "image_rejections",
    "image_decode_failures",
    "image_negative_hits",
    "text_layout_hits",
    "text_layout_misses",
    "text_layout_evictions",
    "text_layout_rejections",
    "surface_allocations",
    "surface_reuses",
    "surface_resizes",
    "surface_rebuilds",
    "surface_rejections",
    "surface_drains",
)


@dataclass(frozen=True)
class Snapshot:
    line: int
    values: dict[str, int | str]

    @property
    def reason(self) -> str:
        return str(self.values["reason"])

    @property
    def renderer_epoch(self) -> int:
        return int(self.values["renderer_epoch"])

    @property
    def target_generation(self) -> int:
        return int(self.values["target_generation"])


def parse(path: Path) -> list[Snapshot]:
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise ValueError(f"could not read {path}: {exc}") from exc

    snapshots: list[Snapshot] = []
    for number, line in enumerate(lines, 1):
        match = TELEMETRY_RE.search(line)
        if match is None:
            continue
        values: dict[str, int | str] = {"reason": match.group("reason")}
        for key, value in match.groupdict().items():
            if key != "reason":
                values[key] = int(value)
        snapshots.append(Snapshot(number, values))
    return snapshots


def validate_snapshot(snapshot: Snapshot) -> list[str]:
    values = snapshot.values
    failures: list[str] = []

    if snapshot.reason not in {"baseline", "periodic", "shutdown", "reset"}:
        failures.append(f"line {snapshot.line}: unknown telemetry reason {snapshot.reason!r}")
    if snapshot.renderer_epoch <= 0:
        failures.append(f"line {snapshot.line}: renderer epoch must be positive")
    if snapshot.target_generation <= 0:
        failures.append(f"line {snapshot.line}: target generation must be positive")
    if snapshot.reason in {"baseline", "periodic"} and (
        int(values["width"]) <= 0 or int(values["height"]) <= 0
    ):
        failures.append(f"line {snapshot.line}: active telemetry target must have non-zero dimensions")

    limits = (
        ("glyph_entries", GLYPH_MAX_ENTRIES),
        ("glyph_bytes", GLYPH_MAX_BYTES),
        ("image_entries", IMAGE_MAX_ENTRIES),
        ("image_bytes", IMAGE_MAX_BYTES),
        ("text_layout_entries", TEXT_LAYOUT_MAX_ENTRIES),
        ("text_layout_bytes", TEXT_LAYOUT_MAX_BYTES),
        ("surface_live_bytes", SURFACE_MAX_LIVE_BYTES),
        ("surface_pooled", SURFACE_MAX_POOL_ENTRIES),
        ("surface_pooled_bytes", SURFACE_MAX_POOL_BYTES),
    )
    for field, maximum in limits:
        actual = int(values[field])
        if actual > maximum:
            failures.append(
                f"line {snapshot.line}: {field} exceeded default runtime cap ({actual} > {maximum})"
            )
    return failures


def validate_monotonic(snapshots: list[Snapshot]) -> list[str]:
    failures: list[str] = []
    previous_by_epoch: dict[int, Snapshot] = {}
    for snapshot in snapshots:
        previous = previous_by_epoch.get(snapshot.renderer_epoch)
        disposal_reset = (
            previous is not None
            and previous.reason == "shutdown"
            and snapshot.reason == "reset"
        )
        if previous is not None and not disposal_reset:
            if snapshot.target_generation < previous.target_generation:
                failures.append(
                    f"line {snapshot.line}: target generation decreased within renderer epoch "
                    f"{snapshot.renderer_epoch} ({snapshot.target_generation} < "
                    f"{previous.target_generation})"
                )
            for field in CUMULATIVE_FIELDS:
                before = int(previous.values[field])
                after = int(snapshot.values[field])
                if after < before:
                    failures.append(
                        f"line {snapshot.line}: {field} decreased within renderer epoch "
                        f"{snapshot.renderer_epoch} ({after} < {before})"
                    )
        previous_by_epoch[snapshot.renderer_epoch] = snapshot
    return failures


def validate_epoch_sequences(snapshots: list[Snapshot]) -> list[str]:
    failures: list[str] = []
    states: dict[int, dict[str, bool]] = {}

    for snapshot in snapshots:
        state = states.setdefault(
            snapshot.renderer_epoch,
            {"baseline": False, "shutdown": False, "reset": False},
        )

        if snapshot.reason == "baseline":
            if state["baseline"]:
                failures.append(
                    f"line {snapshot.line}: renderer epoch {snapshot.renderer_epoch} has duplicate baseline"
                )
            if state["shutdown"] or state["reset"]:
                failures.append(
                    f"line {snapshot.line}: renderer epoch {snapshot.renderer_epoch} resumed after teardown"
                )
            state["baseline"] = True
        elif snapshot.reason == "periodic":
            if not state["baseline"]:
                failures.append(
                    f"line {snapshot.line}: periodic telemetry for renderer epoch "
                    f"{snapshot.renderer_epoch} appeared before baseline"
                )
            if state["shutdown"] or state["reset"]:
                failures.append(
                    f"line {snapshot.line}: periodic telemetry for renderer epoch "
                    f"{snapshot.renderer_epoch} appeared after teardown"
                )
        elif snapshot.reason == "shutdown":
            if state["shutdown"] or state["reset"]:
                failures.append(
                    f"line {snapshot.line}: renderer epoch {snapshot.renderer_epoch} has duplicate/out-of-order shutdown"
                )
            state["shutdown"] = True
        elif snapshot.reason == "reset":
            if state["reset"]:
                failures.append(
                    f"line {snapshot.line}: renderer epoch {snapshot.renderer_epoch} has duplicate reset"
                )
            state["reset"] = True

    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify bounded UIRepo renderer telemetry from an extended Fallout session."
    )
    parser.add_argument("log", type=Path, help="Path to UIRepo_F4.log")
    parser.add_argument(
        "--long-session",
        action="store_true",
        help=(
            "Require a baseline, at least two periodic snapshots in one renderer epoch, "
            "and a final shutdown/reset snapshot."
        ),
    )
    args = parser.parse_args()

    try:
        snapshots = parse(args.log)
    except ValueError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2

    failures: list[str] = []
    if not snapshots:
        failures.append("no UIRepo_F4 telemetry snapshots found")
    else:
        failures.extend(
            failure
            for snapshot in snapshots
            for failure in validate_snapshot(snapshot)
        )
        failures.extend(validate_monotonic(snapshots))
        failures.extend(validate_epoch_sequences(snapshots))

        baseline_count = sum(snapshot.reason == "baseline" for snapshot in snapshots)
        periodic_by_epoch = Counter(
            snapshot.renderer_epoch for snapshot in snapshots if snapshot.reason == "periodic"
        )
        final_count = sum(snapshot.reason in {"shutdown", "reset"} for snapshot in snapshots)
        if baseline_count == 0:
            failures.append("no baseline telemetry snapshot found")
        if args.long_session:
            longest_epoch_periodics = max(periodic_by_epoch.values(), default=0)
            if longest_epoch_periodics < 2:
                failures.append(
                    "long-session evidence needs at least two periodic snapshots within one "
                    f"renderer epoch; best epoch has {longest_epoch_periodics}"
                )
            if final_count == 0:
                failures.append("long-session evidence has no final shutdown/reset snapshot")
            elif snapshots[-1].reason not in {"shutdown", "reset"}:
                failures.append("long-session telemetry does not end with shutdown/reset evidence")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print(f"Renderer telemetry verification failed with {len(failures)} issue(s).")
        return 1

    print(
        "Renderer telemetry verification passed "
        f"({len(snapshots)} snapshots across "
        f"{len({snapshot.renderer_epoch for snapshot in snapshots})} renderer epoch(s))."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
