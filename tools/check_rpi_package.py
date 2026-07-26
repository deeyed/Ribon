#!/usr/bin/env python3
"""Validate the Ribon RPi5 raw-FDT package and its recorded object facts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


REQUIRED_FILES = (
    "kernel8.img",
    "boot/payload.elf",
    "config.txt",
    "cmdline.txt",
)


def fail(message: str) -> int:
    print(f"RIBON-RPI5-PACKAGE-FAIL: {message}", file=sys.stderr)
    return 1


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    if not args.package.is_dir():
        return fail("package directory is missing")
    manifest_path = args.package / "manifest.json"
    if not manifest_path.is_file():
        return fail("manifest.json is missing")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("schema") != "ribon-rpi5-package-v1"
        or manifest.get("platform") != "raspberrypi-rpi5"
        or manifest.get("environment") != "raw-fdt"
        or manifest.get("claim") != "package-only; no live RPi5 execution"
    ):
        return fail("manifest identity or evidence boundary is invalid")
    recorded = manifest.get("files")
    if not isinstance(recorded, dict) or set(recorded) != set(REQUIRED_FILES):
        return fail("manifest file set is not exact")
    for relative in REQUIRED_FILES:
        path = args.package / relative
        entry = recorded[relative]
        if (
            not path.is_file()
            or path.stat().st_size == 0
            or not isinstance(entry, dict)
            or entry.get("size") != path.stat().st_size
            or entry.get("sha256") != sha256(path)
        ):
            return fail(f"invalid or unbound file: {relative}")
    config = (args.package / "config.txt").read_text(encoding="utf-8")
    for required in ("arm_64bit=1", "kernel=kernel8.img", "enable_uart=1"):
        if required not in config:
            return fail(f"config.txt missing {required}")
    cmdline = (args.package / "cmdline.txt").read_text(encoding="utf-8").strip()
    if not cmdline or "\n" in cmdline or "\r" in cmdline:
        return fail("cmdline.txt must contain one non-empty line")
    print("RIBON-R4-RPI5-PACKAGE-OK package-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
