#!/usr/bin/env python3
"""Reject the retired monolithic service table ABI from active Ribon sources."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCAN_ROOTS = (ROOT / "include" / "Ribon", ROOT / "src", ROOT / "products", ROOT / "targets", ROOT / "tests", ROOT / "examples", ROOT / "qstar", ROOT / "sdk")
FORBIDDEN_PATHS = (
    Path("include/Ribon/firmware/services.h"),
    Path("src/common/services.c"),
)
FORBIDDEN_CONTENT = (
    re.compile(r"\bRibonServiceTable\b"),
    re.compile(r"\bribon_service_table_"),
    re.compile(r"\bRIBON_SERVICE_TABLE_"),
    re.compile(r"<Ribon/firmware/services\.h>"),
)


def main() -> int:
    """Scan source-owned active inputs without treating historical records as ABI."""

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
                if path.suffix not in {".c", ".h", ".qst", ".json", ".py", ".md"}:
                    continue
                text = path.read_text(encoding="utf-8", errors="strict")
                for pattern in FORBIDDEN_CONTENT:
                    if pattern.search(text):
                        findings.append(
                            f"content:{path.relative_to(ROOT)}:{pattern.pattern}"
                        )
    if findings:
        for finding in findings:
            print(f"RIBON-R6-HARD-CUT-FINDING: {finding}")
        return 1
    print("RIBON-R6-MONOLITHIC-SERVICE-HARD-CUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
