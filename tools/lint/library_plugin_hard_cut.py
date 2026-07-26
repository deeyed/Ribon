#!/usr/bin/env python3
"""Reject retired Profile/FirmwareAdapter ABI and legacy source ownership."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCAN_ROOTS = (ROOT / "include" / "Ribon", ROOT / "src", ROOT / "tests", ROOT / "qstar")
FORBIDDEN_CONTENT = (
    re.compile(r"\bRibonProfile(?:Ops)?\b"),
    re.compile(r"\bRibonFirmwareAdapter\b"),
    re.compile(r"\bribon_profile_"),
    re.compile(r"\bRIBON_PROFILE_"),
    re.compile(r"<Ribon/(?:profile|firmware|platform|ribon)\.h>"),
)
FORBIDDEN_PATHS = (
    Path("include/Ribon/profile.h"),
    Path("src/profiles"),
)


def main() -> int:
    """Scan active code, headers, tests and build metadata."""

    findings: list[str] = []
    for path in FORBIDDEN_PATHS:
        if (ROOT / path).exists():
            findings.append(f"path:{path}")
    for scan_root in SCAN_ROOTS:
        if not scan_root.exists():
            continue
        for directory, names, files in os.walk(scan_root):
            names[:] = sorted(name for name in names if name != "__pycache__")
            for name in sorted(files):
                path = Path(directory) / name
                if path.suffix not in {".c", ".h", ".qst", ".lua"}:
                    continue
                text = path.read_text(encoding="utf-8", errors="strict")
                for pattern in FORBIDDEN_CONTENT:
                    if pattern.search(text):
                        findings.append(
                            f"content:{path.relative_to(ROOT)}:{pattern.pattern}"
                        )
    if findings:
        for finding in findings:
            print(f"RIBON-R3-HARD-CUT-FINDING: {finding}")
        return 1
    print("RIBON-R3-LIBRARY-PLUGIN-HARD-CUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
