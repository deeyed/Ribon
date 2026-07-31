#!/usr/bin/env python3
"""Wrap one unsigned Ribos artifact in a deterministic test signature envelope."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct


ENVELOPE_BYTES = 128
SIGNED_FLAG = 1
ED25519_ALGORITHM = 1
SIGNATURE_BYTES = 64
KEY_ID = b"ribon-r18-fixture-key"
SIGNATURE = bytes([0xA5]) * SIGNATURE_BYTES


def u32(data: bytes, offset: int) -> int:
    """Read one little-endian u32."""

    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    """Read one little-endian u64."""

    return struct.unpack_from("<Q", data, offset)[0]


def signed_fixture(unsigned: bytes) -> bytes:
    """Return a deterministic structurally signed copy of one unsigned artifact."""

    if (
        len(unsigned) < ENVELOPE_BYTES
        or unsigned[:8] != b"RIBOSA1\0"
        or u32(unsigned, 12) != ENVELOPE_BYTES
        or u32(unsigned, 16) != 0
        or u64(unsigned, 24) != ENVELOPE_BYTES
        or u64(unsigned, 64) != len(unsigned)
    ):
        raise ValueError("input is not a canonical unsigned Ribos artifact")
    payload_length = u64(unsigned, 32)
    if ENVELOPE_BYTES + payload_length != len(unsigned):
        raise ValueError("unsigned artifact payload bounds are inconsistent")

    output = bytearray(unsigned)
    key_offset = len(unsigned)
    signature_offset = key_offset + len(KEY_ID)
    total_length = signature_offset + len(SIGNATURE)
    struct.pack_into("<I", output, 16, SIGNED_FLAG)
    struct.pack_into("<H", output, 22, ED25519_ALGORITHM)
    struct.pack_into("<Q", output, 40, key_offset)
    struct.pack_into("<I", output, 48, len(KEY_ID))
    struct.pack_into("<I", output, 52, len(SIGNATURE))
    struct.pack_into("<Q", output, 56, signature_offset)
    struct.pack_into("<Q", output, 64, total_length)
    output.extend(KEY_ID)
    output.extend(SIGNATURE)
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-sha256", type=Path)
    args = parser.parse_args()

    output = signed_fixture(args.input.read_bytes())
    digest = hashlib.sha256(output).hexdigest()
    if args.expected_sha256 is not None:
        expected = args.expected_sha256.read_text(encoding="utf-8").strip()
        if digest != expected:
            raise SystemExit(
                f"RIBOS-R18-GOLDEN-FAIL expected={expected} observed={digest}"
            )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(
        f"RIBOS-R18-SIGNED-FIXTURE-OK sha256={digest} "
        f"bytes={len(output)} signature=fixture-ed25519-shape"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
