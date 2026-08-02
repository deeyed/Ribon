#!/usr/bin/env python3
"""Validate one pinned x86_64 Linux EFI-stub input and publish provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import urllib.request


EXPECTED_HASH = "2a0deaeab7dd3edf23c68597e1c79e0bd0f1ad92381cc90b3abd0187e96f28fe"
EXPECTED_SIZE = 5739520
EXPECTED_URL = (
    "https://downloads.openwrt.org/releases/24.10.0/targets/x86/64/"
    "openwrt-24.10.0-x86-64-generic-kernel.bin"
)


def validate_pe(data: bytes) -> dict[str, int]:
    """Return stable PE facts or reject a non-x86_64 EFI application."""

    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError("artifact is not a DOS-wrapped PE image")
    pe = int.from_bytes(data[0x3C:0x40], "little")
    if pe > len(data) - 0x78 or data[pe : pe + 4] != b"PE\0\0":
        raise ValueError("artifact has no bounded PE signature")
    optional = pe + 24
    machine = int.from_bytes(data[pe + 4 : pe + 6], "little")
    magic = int.from_bytes(data[optional : optional + 2], "little")
    entry_rva = int.from_bytes(data[optional + 16 : optional + 20], "little")
    subsystem = int.from_bytes(data[optional + 68 : optional + 70], "little")
    if machine != 0x8664 or magic != 0x20B or entry_rva == 0 or subsystem != 10:
        raise ValueError("artifact is not one PE32+ x86_64 EFI application")
    return {"entry_rva": entry_rva, "machine": machine, "subsystem": subsystem}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--descriptor", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--allow-download", action="store_true")
    args = parser.parse_args()

    descriptor = json.loads(args.descriptor.read_text(encoding="utf-8"))
    artifact = descriptor.get("artifact") if isinstance(descriptor, dict) else None
    expected_keys = {
        "architecture", "class", "license", "maximum_size", "sha256",
        "size", "source", "version",
    }
    if (
        set(descriptor) != {"artifact", "schema"}
        or descriptor.get("schema") != "ribon-external-linux-efi-v1"
        or not isinstance(artifact, dict)
        or set(artifact) != expected_keys
        or artifact.get("architecture") != "x86_64"
        or artifact.get("class") != "linux-x86_64-efi-stub"
        or artifact.get("source") != EXPECTED_URL
        or artifact.get("sha256") != EXPECTED_HASH
        or artifact.get("size") != EXPECTED_SIZE
        or artifact.get("maximum_size") != 16777216
    ):
        raise ValueError("external Linux EFI descriptor is not the pinned contract")
    downloaded = False
    if not args.cache.is_file():
        if not args.allow_download:
            raise ValueError("validated cache is absent and download is disabled")
        with urllib.request.urlopen(EXPECTED_URL, timeout=30) as response:
            data = response.read(artifact["maximum_size"] + 1)
        downloaded = True
    else:
        data = args.cache.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != EXPECTED_SIZE or digest != EXPECTED_HASH:
        raise ValueError("cached artifact identity does not match the descriptor")
    pe = validate_pe(data)
    if downloaded:
        args.cache.parent.mkdir(parents=True, exist_ok=True)
        args.cache.write_bytes(data)
    report = {
        "artifact": {
            "architecture": "x86_64",
            "class": "linux-x86_64-efi-stub",
            "sha256": digest,
            "size": len(data),
        },
        "descriptor_sha256": hashlib.sha256(args.descriptor.read_bytes()).hexdigest(),
        "pe": pe,
        "schema": "ribon-external-linux-efi-validation-v1",
    }
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"RIBON-LINUX-X86_64-EFI-INPUT-OK sha256={digest} size={len(data)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
