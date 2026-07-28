#!/usr/bin/env python3
"""Validate the Ribon RPi5 raw-FDT package and its recorded object facts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
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


def payload_load_ranges(path: Path) -> list[tuple[int, int]]:
    """Return the physical PT_LOAD ranges from one little-endian ELF64."""

    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ValueError("payload is not ELF")
    if data[4] != 2 or data[5] != 1:
        raise ValueError("payload is not little-endian ELF64")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    phoff = header[5]
    phentsize = header[9]
    phnum = header[10]
    if phentsize < 56:
        raise ValueError("payload program header size is invalid")
    ranges: list[tuple[int, int]] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        if offset + 56 > len(data):
            raise ValueError("payload program header extends past EOF")
        fields = struct.unpack_from("<IIQQQQQQ", data, offset)
        if fields[0] != 1:
            continue
        start = fields[4]
        size = fields[6]
        if size == 0 or start > (1 << 64) - 1 - size:
            raise ValueError("payload PT_LOAD range is invalid")
        ranges.append((start, start + size))
    if not ranges:
        raise ValueError("payload has no PT_LOAD range")
    return ranges


def loader_memory_range(path: Path) -> tuple[int, int]:
    """Read the RPi AArch64 image header's physical in-memory extent."""

    data = path.read_bytes()
    if len(data) < 64 or data[56:60] != b"ARM\x64":
        raise ValueError("kernel8.img has no AArch64 image header")
    start = struct.unpack_from("<Q", data, 8)[0]
    size = struct.unpack_from("<Q", data, 16)[0]
    if start == 0 or size == 0 or start > (1 << 64) - 1 - size:
        raise ValueError("kernel8.img memory range is invalid")
    return start, start + size


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
        or manifest.get("port") != "raspberrypi-rpi5"
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
    for required in (
        "device_tree=bcm2712-rpi-5-b.dtb",
        "arm_64bit=1",
        "kernel=kernel8.img",
        "enable_uart=1",
        "enable_rp1_uart=1",
        "uart_2ndstage=1",
        "os_check=0",
        "pciex4_reset=0",
    ):
        if required not in config:
            return fail(f"config.txt missing {required}")
    cmdline = (args.package / "cmdline.txt").read_text(encoding="utf-8").strip()
    if not cmdline or "\n" in cmdline or "\r" in cmdline:
        return fail("cmdline.txt must contain one non-empty line")
    try:
        loader_range = loader_memory_range(args.package / "kernel8.img")
        payload_ranges = payload_load_ranges(
            args.package / "boot" / "payload.elf"
        )
    except (OSError, ValueError, struct.error) as error:
        return fail(str(error))
    if any(
        loader_range[0] < payload_end
        and payload_start < loader_range[1]
        for payload_start, payload_end in payload_ranges
    ):
        return fail("Ribon in-memory image overlaps a payload PT_LOAD range")
    print("RIBON-R4-RPI5-PACKAGE-OK package-only disjoint-load-ranges")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
