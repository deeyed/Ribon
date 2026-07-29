#!/usr/bin/env python3
"""Enforce the architecture/environment/port/protocol hard boundary."""

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
        ROOT / "src" / "image-formats",
        re.compile(
            r"(?:RibonArchDescriptor|RIBON_CAP_ARCHITECTURE|"
            r"RIBON_ARCHITECTURE_|canonical_name|\"(?:x86_64|aarch64|riscv64)\")"
        ),
        "image parser contains ISA selection authority",
    )
    scan(
        ROOT / "src" / "common",
        re.compile(
            r"(?:canonical_name.{0,80}machine|machine.{0,80}canonical_name)",
            re.DOTALL,
        ),
        "generic common layer selects machine by canonical name",
    )
    scan(
        ROOT / "src" / "protocols" / "os" / "parus",
        re.compile(r"\b(?:uefi|bios|mmio|pl011|e820|fdt_parse)\b", re.IGNORECASE),
        "Parus protocol contains environment or device dependencies",
    )
    scan(
        ROOT / "targets" / "qemu-aarch64-virt-raw-fdt",
        re.compile(r"\b(?:rpi5|raspberry|kernel8|config\.txt|bcm2712)\b", re.IGNORECASE),
        "QEMU target contains board artifacts",
    )
    scan(
        ROOT / "targets" / "qemu-riscv64-virt-opensbi",
        re.compile(r"\b(?:aarch64|uefi|bios|rpi5|raspberry|pl011)\b", re.IGNORECASE),
        "RISC-V OpenSBI target contains another frontend or board",
    )

    manifests = sorted((ROOT / "products").rglob("*.json"))
    if not manifests:
        fail("no product manifests found")
    for path in manifests:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        product_kind = manifest.get("product_kind")
        plugins = manifest.get("plugins")
        if not isinstance(plugins, list):
            fail(f"{path.relative_to(ROOT)} has no plugin list")
        ids = [item.get("id", "") for item in plugins if isinstance(item, dict)]
        prefixes = ["arch."]
        prefixes.append(
            "personality." if product_kind == "firmware" else "environment."
        )
        for prefix in prefixes:
            if sum(plugin_id.startswith(prefix) for plugin_id in ids) != 1:
                fail(f"{path.relative_to(ROOT)} does not select exactly one {prefix[:-1]}")
        forbidden = "environment." if product_kind == "firmware" else "personality."
        if any(plugin_id.startswith(forbidden) for plugin_id in ids):
            fail(f"{path.relative_to(ROOT)} selects forbidden {forbidden[:-1]}")
    print("RIBON-R4-FRONTEND-BOUNDARY-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
