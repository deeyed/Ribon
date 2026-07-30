#!/usr/bin/env python3
"""Reject production Ribos APIs that dispatch caller-provided artifact bytes."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
PUBLIC = ROOT / "language/ribos/vm/include/ribos/vm"
SOURCES = ROOT / "language/ribos/vm/src"

FORBIDDEN_NAMES = (
    "ribos_vm_execute_bytes",
    "ribos_vm_execute_artifact",
    "ribos_vm_dispatch_bytes",
    "ribos_vm_dispatch_artifact",
)


def main() -> int:
    failures: list[str] = []
    header_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted(PUBLIC.glob("*.h"))
    )
    source_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in sorted(SOURCES.glob("*.[ch]"))
    )

    for name in FORBIDDEN_NAMES:
        if name in header_text or name in source_text:
            failures.append(f"forbidden production symbol: {name}")

    raw_execute = re.compile(
        r"ribos_[a-z0-9_]*execute[a-z0-9_]*\s*\("
        r"(?:(?!\);).)*(?:uint8_t|void\s*\*)\s*\*",
        re.IGNORECASE | re.DOTALL,
    )
    match = raw_execute.search(header_text)
    if match is not None:
        failures.append(
            "execute declaration accepts raw byte or void pointer input"
        )

    prepared = PUBLIC / "prepared.h"
    text = prepared.read_text(encoding="utf-8")
    required = (
        "typedef struct RibosAuthorizedArtifact RibosAuthorizedArtifact;",
        "const RibosAuthorizedArtifact **authorized_artifact",
        "const RibosPreparedProgram **prepared_program",
    )
    for spelling in required:
        if spelling not in text:
            failures.append(f"missing opaque preparation shape: {spelling}")

    if failures:
        for failure in failures:
            print(f"RIBOS-PREPARED-API-FAIL {failure}")
        return 1
    print(
        "RIBOS-PREPARED-API-OK "
        "raw-execute=no authorized=opaque prepared=opaque"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
