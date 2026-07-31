#!/usr/bin/env python3
"""Create a structurally valid Ribos artifact rejected by the independent verifier."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct


ENVELOPE_BYTES = 128
PAYLOAD_HEADER_BYTES = 160
SECTION_DESCRIPTOR_BYTES = 32
INSTRUCTION_SECTION_INDEX = 8
INSTRUCTION_KIND = 9
RETURN_OPCODE = 0x17


def main() -> int:
    """Replace the first opcode with RETURN and reseal only the payload hash."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    artifact = bytearray(args.input.read_bytes())
    if len(artifact) < ENVELOPE_BYTES + PAYLOAD_HEADER_BYTES:
        raise ValueError("artifact is shorter than the canonical headers")
    payload = memoryview(artifact)[ENVELOPE_BYTES:]
    descriptor = PAYLOAD_HEADER_BYTES + (
        INSTRUCTION_SECTION_INDEX * SECTION_DESCRIPTOR_BYTES
    )
    kind = struct.unpack_from("<H", payload, descriptor)[0]
    data_offset = struct.unpack_from("<Q", payload, descriptor + 8)[0]
    data_length = struct.unpack_from("<Q", payload, descriptor + 16)[0]
    if kind != INSTRUCTION_KIND or data_length == 0 or data_offset >= len(payload):
        raise ValueError("artifact has no canonical instruction section")
    payload[data_offset] = RETURN_OPCODE
    artifact[72:104] = hashlib.sha256(payload).digest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(artifact)
    print("RIBOS-VERIFIER-INVALID-OK structural=yes verifier=reject")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
