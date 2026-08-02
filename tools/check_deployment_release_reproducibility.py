#!/usr/bin/env python3
"""Rebuild D08 release artifacts in two clean roots and compare exact bytes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


SELECTED_TREES = (
    "sdk/install",
    "sdk/examples/deployment-consumer",
    "targets/rpi5-aarch64-modules-fixture/package",
    "release/rpi5-prehardware",
)
SELECTED_FILES = ("release/deployment-release.json",)


def run_build(make: str, source: Path, build: Path, log: Path) -> None:
    """Build one release closure and retain its bounded diagnostic log."""

    result = subprocess.run(
        [
            make,
            "--no-print-directory",
            f"BUILD_ROOT={build}",
            "deployment-release-artifacts",
        ],
        cwd=source,
        text=True,
        capture_output=True,
        check=False,
    )
    log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise ValueError(f"clean-root build failed ({result.returncode}); see {log.name}")


def file_map(root: Path) -> dict[str, str]:
    """Return exact selected release bytes keyed without clean-root paths."""

    result: dict[str, str] = {}
    for relative in SELECTED_TREES:
        tree = root / relative
        if not tree.is_dir():
            raise ValueError(f"selected release tree is missing: {relative}")
        for path in sorted(tree.rglob("*")):
            if path.is_file():
                key = f"{relative}/{path.relative_to(tree).as_posix()}"
                result[key] = hashlib.sha256(path.read_bytes()).hexdigest()
    for relative in SELECTED_FILES:
        path = root / relative
        if not path.is_file():
            raise ValueError(f"selected release file is missing: {relative}")
        result[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()

    source = args.root.resolve()
    work = args.work_root.resolve()
    if not (source / "Makefile").is_file() or source == work or source.is_relative_to(work):
        raise ValueError("source or clean-root work directory is invalid")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    first = work / "root-a"
    second = work / "root-b"
    run_build(args.make, source, first, work / "root-a.log")
    run_build(args.make, source, second, work / "root-b.log")
    first_map = file_map(first)
    second_map = file_map(second)
    if first_map != second_map:
        names = sorted(set(first_map) | set(second_map))
        differences = [name for name in names if first_map.get(name) != second_map.get(name)]
        raise ValueError(f"clean-root release bytes differ: {differences[:8]}")
    identity = hashlib.sha256(
        json.dumps(first_map, separators=(",", ":"), sort_keys=True).encode("utf-8")
    ).hexdigest()
    report = {
        "schema": "ribon-deployment-reproducibility-v1",
        "clean_roots": 2,
        "file_count": len(first_map),
        "release_identity": identity,
        "result": "byte-identical",
    }
    (work / "result.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        "RIBON-D08-DEPLOYMENT-REPRODUCIBLE-OK "
        f"roots=2 files={len(first_map)} identity={identity}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"RIBON-D08-DEPLOYMENT-REPRODUCIBLE-FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
