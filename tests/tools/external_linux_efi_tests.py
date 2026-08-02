#!/usr/bin/env python3
"""Hostile unit tests for the pinned Linux EFI input validator."""

from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_external_linux_efi",
    ROOT / "tools" / "prepare_external_linux_efi.py",
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def valid_image() -> bytearray:
    image = bytearray(0x200)
    image[0:2] = b"MZ"
    image[0x3C:0x40] = (0x80).to_bytes(4, "little")
    image[0x80:0x84] = b"PE\0\0"
    image[0x84:0x86] = (0x8664).to_bytes(2, "little")
    optional = 0x80 + 24
    image[optional:optional + 2] = (0x20B).to_bytes(2, "little")
    image[optional + 16:optional + 20] = (0x1000).to_bytes(4, "little")
    image[optional + 68:optional + 70] = (10).to_bytes(2, "little")
    return image


def must_reject(image: bytes) -> None:
    try:
        MODULE.validate_pe(image)
    except ValueError:
        return
    raise AssertionError("hostile PE input was accepted")


def main() -> int:
    image = valid_image()
    facts = MODULE.validate_pe(bytes(image))
    assert facts == {"entry_rva": 0x1000, "machine": 0x8664, "subsystem": 10}

    must_reject(b"")
    must_reject(bytes(image[:0x90]))
    for offset, value in (
        (0x84, 0xAA64),
        (0x80 + 24, 0x10B),
        (0x80 + 24 + 16, 0),
        (0x80 + 24 + 68, 11),
    ):
        hostile = valid_image()
        width = 4 if offset == 0x80 + 24 + 16 else 2
        hostile[offset:offset + width] = value.to_bytes(width, "little")
        must_reject(bytes(hostile))

    wrapping = valid_image()
    wrapping[0x3C:0x40] = (0xFFFFFFFF).to_bytes(4, "little")
    must_reject(bytes(wrapping))
    print("RIBON-EXTERNAL-LINUX-EFI-TESTS-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
