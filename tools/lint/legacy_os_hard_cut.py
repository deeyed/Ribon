#!/usr/bin/env python3
"""Fail when the retired OS identifier remains in an active path or file."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RETIRED_IDENTIFIER = bytes((107, 97, 105, 114, 111, 110)).decode("ascii")
IGNORED_DIRECTORIES = {".git", "build", "__pycache__"}


def path_findings() -> list[str]:
    """Return active paths containing the retired identifier."""

    findings: list[str] = []
    for directory, names, files in os.walk(ROOT):
        names[:] = sorted(name for name in names if name not in IGNORED_DIRECTORIES)
        relative_directory = Path(directory).relative_to(ROOT)
        for name in names + sorted(files):
            relative = relative_directory / name
            if RETIRED_IDENTIFIER in relative.as_posix().lower():
                findings.append(relative.as_posix())
    return findings


def content_findings() -> list[str]:
    """Use ripgrep to return active content containing the retired identifier."""

    result = subprocess.run(
        (
            "rg",
            "-n",
            "-i",
            "--hidden",
            "--glob",
            "!.git/**",
            "--glob",
            "!build/**",
            RETIRED_IDENTIFIER,
            ".",
        ),
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode == 1:
        return []
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"ripgrep failed: {result.returncode}")
    return [line for line in result.stdout.splitlines() if line]


def main() -> int:
    """Run the active-tree hard-cut gate."""

    findings = [f"path:{item}" for item in path_findings()]
    try:
        findings.extend(f"content:{item}" for item in content_findings())
    except RuntimeError as error:
        print(f"RIBON-LEGACY-HARD-CUT-ERROR: {error}", file=sys.stderr)
        return 2
    if findings:
        for finding in findings:
            print(f"RIBON-LEGACY-HARD-CUT-FINDING: {finding}", file=sys.stderr)
        print(f"RIBON-LEGACY-HARD-CUT-FAIL: {len(findings)}", file=sys.stderr)
        return 1
    print("RIBON-LEGACY-HARD-CUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
