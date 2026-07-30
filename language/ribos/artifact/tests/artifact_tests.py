#!/usr/bin/env python3
"""Check deterministic Ribos bytecode artifacts against the frozen wire ABI."""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


RIBOS = Path(__file__).resolve().parents[2]
CORPUS = RIBOS / "frontend" / "tests" / "semantic" / "positive"
SCHEMA_DIGEST = bytes.fromhex(
    "237898e5b4b7fd5f8cccf9edcf5da50f"
    "b6699f24b869e878638f27885091a4a8"
)
ROW_SIZES = [128, 32, 32, 1, 104, 32, 32, 32, 48, 4, 16, 16, 40]


def u16(data: bytes, offset: int) -> int:
    """Read one little-endian u16 from a checked Python slice."""

    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    """Read one little-endian u32 from a checked Python slice."""

    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    """Read one little-endian u64 from a checked Python slice."""

    return struct.unpack_from("<Q", data, offset)[0]


def emit(compiler: Path, source: Path, output: Path) -> subprocess.CompletedProcess[str]:
    """Compile one source fixture into an unsigned artifact."""

    return subprocess.run(
        [str(compiler), "--emit-artifact", str(output), str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def check_artifact(path: Path, fixture: Path, failures: list[str]) -> bytes:
    """Validate envelope, hash, payload header, and canonical section layout."""

    data = path.read_bytes()
    if len(data) < 288:
        failures.append(f"{fixture.name}: artifact is too small")
        return data
    if data[:8] != b"RIBOSA1\0":
        failures.append(f"{fixture.name}: envelope magic mismatch")
        return data
    if (
        (u16(data, 8), u16(data, 10)) != (1, 0)
        or u32(data, 12) != 128
        or u32(data, 16) != 0
        or u16(data, 20) != 1
        or u16(data, 22) != 0
    ):
        failures.append(f"{fixture.name}: envelope ABI mismatch")
    payload_offset = u64(data, 24)
    payload_length = u64(data, 32)
    key_offset = u64(data, 40)
    key_length = u32(data, 48)
    signature_length = u32(data, 52)
    signature_offset = u64(data, 56)
    total_length = u64(data, 64)
    if (
        payload_offset != 128
        or key_offset != payload_offset + payload_length
        or signature_offset != key_offset + key_length
        or total_length != signature_offset + signature_length
        or total_length != len(data)
        or key_length != 0
        or signature_length != 0
    ):
        failures.append(f"{fixture.name}: non-canonical envelope ranges")
        return data
    payload = data[payload_offset : payload_offset + payload_length]
    if hashlib.sha256(payload).digest() != data[72:104]:
        failures.append(f"{fixture.name}: SHA-256 mismatch")
    if payload[:8] != b"RIBBC01\0":
        failures.append(f"{fixture.name}: payload magic mismatch")
        return data
    if (
        (u16(payload, 8), u16(payload, 10)) != (1, 0)
        or (u16(payload, 12), u16(payload, 14)) != (1, 0)
        or u32(payload, 16) != 160
        or u32(payload, 20) != 1
        or u32(payload, 24) != 13
        or u32(payload, 32) != u32(payload, 36)
        or u32(payload, 36) > 16384
        or u32(payload, 44) & ~u32(payload, 40)
        or u64(payload, 56) > u64(payload, 48)
        or u64(payload, 72) > u64(payload, 64)
        or payload[96:128] != SCHEMA_DIGEST
        or u64(payload, 128) != 160
        or u64(payload, 136) != 13 * 32
        or u64(payload, 144) != payload_length
    ):
        failures.append(f"{fixture.name}: payload ABI or budget mismatch")
    cursor = 160 + 13 * 32
    counts: dict[int, int] = {}
    for index, expected_row_size in enumerate(ROW_SIZES):
        descriptor = 160 + index * 32
        kind = u16(payload, descriptor)
        flags = u16(payload, descriptor + 2)
        row_size = u32(payload, descriptor + 4)
        offset = u64(payload, descriptor + 8)
        length = u64(payload, descriptor + 16)
        count = u32(payload, descriptor + 24)
        reserved = u32(payload, descriptor + 28)
        aligned = (cursor + 7) & ~7
        if (
            kind != index + 1
            or flags != 0
            or reserved != 0
            or row_size != expected_row_size
            or offset != aligned
            or length != count * row_size
            or offset + length > payload_length
            or any(payload[cursor:offset])
        ):
            failures.append(
                f"{fixture.name}: invalid section descriptor {index + 1}"
            )
        counts[kind] = count
        cursor = offset + length
    if cursor != payload_length:
        failures.append(f"{fixture.name}: trailing or missing payload bytes")
    if (
        counts.get(1, 0) == 0
        or counts.get(5, 0) == 0
        or counts.get(6, 0) == 0
        or counts.get(9, 0) == 0
        or counts.get(13, 0) == 0
    ):
        failures.append(f"{fixture.name}: required artifact table is empty")
    if fixture.name in {"policy_pipeline.rbs", "nested_resources.rbs"} and (
        counts.get(11, 0) == 0 or counts.get(12, 0) == 0
    ):
        failures.append(f"{fixture.name}: helper import/bound tables are empty")
    return data


def main(argv: list[str]) -> int:
    """Emit every semantic-positive fixture twice and require byte identity."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args(argv)
    failures: list[str] = []
    fixture_count = 0

    with tempfile.TemporaryDirectory(prefix="ribos-artifact-") as directory:
        root = Path(directory)
        for fixture in sorted(CORPUS.glob("*.rbs")):
            first_path = root / f"{fixture.stem}-a.rba"
            second_path = root / f"{fixture.stem}-b.rba"
            first = emit(args.compiler, fixture, first_path)
            second = emit(args.compiler, fixture, second_path)
            fixture_count += 1
            if first.returncode != 0 or second.returncode != 0:
                failures.append(
                    f"{fixture.name}: emission failed\n"
                    f"first={first.stdout}{first.stderr}"
                    f"second={second.stdout}{second.stderr}"
                )
                continue
            if "RIBOS-ARTIFACT-EMIT-OK" not in first.stdout:
                failures.append(f"{fixture.name}: missing emission marker")
            first_bytes = check_artifact(first_path, fixture, failures)
            second_bytes = check_artifact(second_path, fixture, failures)
            if first_bytes != second_bytes:
                failures.append(f"{fixture.name}: artifact is not deterministic")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-ARTIFACT-CORPUS-OK "
        f"fixtures={fixture_count} deterministic=1 endian=little "
        "hash=sha256 vm=1.0 isa=1.0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
