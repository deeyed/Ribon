#!/usr/bin/env python3
"""Enforce the architecture-neutral Ribos VM runtime public-header boundary."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
HEADER = ROOT / "language/ribos/vm/include/ribos/vm/runtime.h"

ALLOWED_INCLUDES = {
    "<stddef.h>",
    "<stdint.h>",
    '"ribos/schema/schema.h"',
}

REQUIRED_PUBLIC_SHAPES = (
    "typedef struct RibosVmLimits",
    "typedef struct RibosPreparedProgram RibosPreparedProgram;",
    "typedef struct RibosVmContext",
    "typedef struct RibosVmEmbedder",
    "typedef struct RibosVmOutcome",
)

FORBIDDEN_PATTERNS = (
    re.compile(r"#\s*pragma\s+pack"),
    re.compile(r"__attribute__\s*\(\(\s*packed"),
    re.compile(r"\bFILE\b"),
    re.compile(r"\bRibonService"),
    re.compile(r"\bsize_t\s+[A-Za-z_][A-Za-z0-9_]*\s*;"),
    re.compile(r"#\s*include\s*[<\"](?:sys/|windows|efi|Ribon/)"),
)


def main() -> int:
    text = HEADER.read_text(encoding="utf-8")
    failures: list[str] = []

    includes = re.findall(r"#\s*include\s+([<\"][^>\"]+[>\"])", text)
    for include in includes:
        if include not in ALLOWED_INCLUDES:
            failures.append(f"forbidden include: {include}")

    for shape in REQUIRED_PUBLIC_SHAPES:
        if shape not in text:
            failures.append(f"missing public shape: {shape}")

    for pattern in FORBIDDEN_PATTERNS:
        match = pattern.search(text)
        if match is not None:
            line = text.count("\n", 0, match.start()) + 1
            failures.append(
                f"{HEADER.relative_to(ROOT)}:{line}: "
                f"forbidden={match.group(0)!r}"
            )

    if failures:
        for failure in failures:
            print(f"RIBOS-RUNTIME-HEADER-FAIL {failure}")
        return 1
    print(
        "RIBOS-RUNTIME-HEADER-OK "
        "fixed-width-fields=yes packed=no product-service-types=no"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
