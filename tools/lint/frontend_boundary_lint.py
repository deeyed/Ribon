#!/usr/bin/env python3
"""Enforce the R4 architecture/environment/platform/protocol hard boundary."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    print(f"RIBON-FRONTEND-BOUNDARY-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def scan(directory: Path, pattern: re.Pattern[str], label: str) -> None:
    for path in sorted(directory.rglob("*")):
        if path.suffix not in {".c", ".h", ".S"}:
            continue
        text = path.read_text(encoding="utf-8")
        match = pattern.search(text)
        if match is not None:
            line = text.count("\n", 0, match.start()) + 1
            fail(f"{label}: {path.relative_to(ROOT)}:{line}: {match.group(0)}")


def main() -> int:
    if (ROOT / "src" / "boot").exists():
        fail("src/boot must not exist")
    scan(
        ROOT / "src" / "environments",
        re.compile(r"\b(?:parus|rph1)\b", re.IGNORECASE),
        "environment contains OS protocol policy",
    )
    scan(
        ROOT / "src" / "arch",
        re.compile(r"\b(?:parus|rpi5|raspberry|qemu|bcm2712)\b", re.IGNORECASE),
        "architecture contains OS, board, or emulator policy",
    )
    scan(
        ROOT / "src" / "protocols" / "parus",
        re.compile(r"\b(?:uefi|bios|mmio|pl011|e820|fdt_parse)\b", re.IGNORECASE),
        "Parus protocol contains environment or device dependencies",
    )
    scan(
        ROOT / "targets" / "qemu-aarch64-virt-raw-fdt",
        re.compile(r"\b(?:rpi5|raspberry|kernel8|config\.txt|bcm2712)\b", re.IGNORECASE),
        "QEMU target contains board artifacts",
    )

    manifests = sorted((ROOT / "products").rglob("*.json"))
    if not manifests:
        fail("no product manifests found")
    for path in manifests:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        plugins = manifest.get("plugins")
        if not isinstance(plugins, list):
            fail(f"{path.relative_to(ROOT)} has no plugin list")
        ids = [item.get("id", "") for item in plugins if isinstance(item, dict)]
        for prefix in ("arch.", "environment.", "platform."):
            if sum(plugin_id.startswith(prefix) for plugin_id in ids) != 1:
                fail(f"{path.relative_to(ROOT)} does not select exactly one {prefix[:-1]}")
    print("RIBON-R4-FRONTEND-BOUNDARY-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
