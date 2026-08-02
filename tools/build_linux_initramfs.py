#!/usr/bin/env python3
"""Build a deterministic newc initramfs around one static typed PID 1."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ELF_MACHINE_AARCH64 = 183
ELF_MACHINE_X86_64 = 62
ELF_MACHINE_RISCV64 = 243
MAX_INITRAMFS_SIZE = 1024 * 1024


def validate_init(data: bytes, architecture: str) -> None:
    """Reject dynamic, wrong-machine, or malformed init executables."""

    expected_machine = {
        "aarch64": ELF_MACHINE_AARCH64,
        "x86_64": ELF_MACHINE_X86_64,
        "riscv64": ELF_MACHINE_RISCV64,
    }[architecture]

    if (
        len(data) < 64
        or data[:4] != b"\x7fELF"
        or data[4] != 2
        or data[5] != 1
        or int.from_bytes(data[18:20], "little") != expected_machine
        or int.from_bytes(data[24:32], "little") == 0
    ):
        raise ValueError(
            f"PID 1 must be one static ELF64 little-endian {architecture} image"
        )
    phoff = int.from_bytes(data[32:40], "little")
    phentsize = int.from_bytes(data[54:56], "little")
    phnum = int.from_bytes(data[56:58], "little")
    if phnum == 0 or phentsize < 56 or phoff > len(data):
        raise ValueError("PID 1 has an invalid program-header table")
    for index in range(phnum):
        offset = phoff + index * phentsize
        if offset > len(data) or len(data) - offset < phentsize:
            raise ValueError("PID 1 program-header table is truncated")
        if int.from_bytes(data[offset : offset + 4], "little") == 3:
            raise ValueError("PID 1 must not contain PT_INTERP")


def _pad4(output: bytearray) -> None:
    output.extend(b"\0" * ((-len(output)) & 3))


def _newc_entry(
    output: bytearray,
    name: str,
    data: bytes,
    mode: int,
    ino: int,
    rdev_major: int = 0,
    rdev_minor: int = 0,
) -> None:
    name_bytes = name.encode("ascii") + b"\0"
    fields = (
        ino, mode, 0, 0, 1, 0, len(data), 0, 0,
        rdev_major, rdev_minor, len(name_bytes), 0,
    )
    output.extend(b"070701")
    output.extend("".join(f"{value:08x}" for value in fields).encode("ascii"))
    output.extend(name_bytes)
    _pad4(output)
    output.extend(data)
    _pad4(output)


def build_archive(init: bytes) -> bytes:
    """Create one canonical root/dev/console/init archive."""

    output = bytearray()
    _newc_entry(output, ".", b"", 0o040755, 1)
    _newc_entry(output, "dev", b"", 0o040755, 2)
    _newc_entry(output, "dev/console", b"", 0o020600, 3, 5, 1)
    _newc_entry(output, "init", init, 0o100755, 4)
    _newc_entry(output, "TRAILER!!!", b"", 0, 5)
    output.extend(b"\0" * ((-len(output)) & 511))
    if not 0 < len(output) <= MAX_INITRAMFS_SIZE:
        raise ValueError("initramfs exceeds its fixed maximum")
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--init", type=Path, required=True)
    parser.add_argument(
        "--architecture",
        choices=("aarch64", "riscv64", "x86_64"),
        default="aarch64",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--component-manifest", type=Path, required=True)
    args = parser.parse_args()

    init = args.init.read_bytes()
    validate_init(init, args.architecture)
    archive = build_archive(init)
    digest = hashlib.sha256(archive).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.component_manifest.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(archive)
    manifest = {
        "schema": "ribon-boot-module-components-v1",
        "components": [
            {
                "name": "initramfs",
                "role": "auxiliary",
                "source": args.output.name,
                "expected_sha256": digest,
                "expected_size": len(archive),
                "maximum_size": MAX_INITRAMFS_SIZE,
            }
        ],
    }
    args.component_manifest.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"RIBON-LINUX-INITRAMFS-OK sha256={digest} size={len(archive)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
