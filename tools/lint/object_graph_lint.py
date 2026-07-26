#!/usr/bin/env python3
"""Verify that each Ribon host archive contains exactly one selected mode."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


MODES = ("normal", "recovery", "provisioning", "diagnostic")
FORBIDDEN_HOST_OBJECTS = ("uefi.o", "bios.o", "rpi.o")


def archive_members(path: Path) -> list[str]:
    """Return stable archive member names from the platform ar tool."""

    result = subprocess.run(
        ["ar", "t", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def parse_archive(value: str) -> tuple[str, Path]:
    """Parse MODE=PATH command-line syntax."""

    mode, separator, raw_path = value.partition("=")
    if separator == "" or mode not in MODES or raw_path == "":
        raise argparse.ArgumentTypeError(f"expected one of {MODES} as MODE=PATH")
    return mode, Path(raw_path)


def main() -> int:
    """Check mode isolation and host platform object isolation."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--archive",
        action="append",
        type=parse_archive,
        required=True,
        metavar="MODE=PATH",
    )
    args = parser.parse_args()
    archives = dict(args.archive)
    if set(archives) != set(MODES):
        print("object_graph_lint: all four mode archives are required", file=sys.stderr)
        return 1

    failures: list[str] = []
    for mode in MODES:
        path = archives[mode]
        if not path.is_file():
            failures.append(f"{mode}: missing archive {path}")
            continue
        members = archive_members(path)
        present_modes = [candidate for candidate in MODES if f"{candidate}.o" in members]
        if present_modes != [mode]:
            failures.append(
                f"{mode}: selected mode members are {present_modes}, expected [{mode}]"
            )
        unexpected_platform = sorted(set(members) & set(FORBIDDEN_HOST_OBJECTS))
        if unexpected_platform:
            failures.append(
                f"{mode}: host archive contains platform objects {unexpected_platform}"
            )

    if failures:
        for failure in failures:
            print(f"object_graph_lint: {failure}", file=sys.stderr)
        return 1
    print("RIBON-R2-MODE-OBJECT-GRAPH-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
