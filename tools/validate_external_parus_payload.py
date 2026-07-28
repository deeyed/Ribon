#!/usr/bin/env python3
"""Validate one immutable external Parus ELF against a Ribon product contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


PT_LOAD = 1
PF_X = 1
PF_W = 2
ARCHITECTURE_CONTRACTS = {
    "aarch64": {
        "machine": 183,
        "entry_abi": "arm64-rph1-v1",
        "load_base": 0x41000000,
        "load_size": 16 * 1024 * 1024,
    },
    "riscv64": {
        "machine": 243,
        "entry_abi": "riscv-rph1-v1",
        "load_base": 0x80400000,
        "load_size": 32 * 1024 * 1024,
    },
}


def sha256_file(path: Path) -> str:
    """Return the immutable identity of one source artifact."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_elf(path: Path) -> tuple[dict[str, int], list[dict[str, int]]]:
    """Read the ELF64 header and bounded PT_LOAD records."""

    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ValueError("payload is not ELF")
    if data[4] != 2 or data[5] != 1:
        raise ValueError("payload is not little-endian ELF64")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    machine = header[2]
    entry = header[4]
    phoff = header[5]
    phentsize = header[9]
    phnum = header[10]
    if phentsize < 56:
        raise ValueError("ELF program header size is invalid")
    segments: list[dict[str, int]] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        if offset + 56 > len(data):
            raise ValueError("ELF program header extends past the payload")
        values = struct.unpack_from("<IIQQQQQQ", data, offset)
        if values[0] != PT_LOAD:
            continue
        segments.append(
            {
                "index": index,
                "flags": values[1],
                "offset": values[2],
                "vaddr": values[3],
                "paddr": values[4],
                "filesz": values[5],
                "memsz": values[6],
                "align": values[7],
            }
        )
    return {"machine": machine, "entry": entry}, segments


def validate(
    manifest_path: Path,
    payload_path: Path,
) -> dict[str, object]:
    """Return deterministic validation facts or raise one bounded error."""

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    contract = manifest.get("payload")
    if not isinstance(contract, dict):
        raise ValueError("product manifest has no payload contract")
    architecture = contract.get("architecture")
    architecture_contract = ARCHITECTURE_CONTRACTS.get(architecture)
    if architecture_contract is None:
        raise ValueError("product payload architecture is unsupported")
    expected = {
        "architecture": architecture,
        "class": "external-kernel",
        "entry_abi": architecture_contract["entry_abi"],
        "format": "elf64",
        "load_base": architecture_contract["load_base"],
        "load_size": architecture_contract["load_size"],
    }
    if contract != expected:
        raise ValueError("product payload contract is not the selected RPH1 tuple")

    before = sha256_file(payload_path)
    header, segments = inspect_elf(payload_path)
    if header["machine"] != architecture_contract["machine"]:
        raise ValueError("payload machine does not match the product architecture")
    if not segments:
        raise ValueError("payload has no PT_LOAD segments")
    load_base = int(contract["load_base"])
    load_limit = load_base + int(contract["load_size"])
    ranges: list[tuple[int, int]] = []
    entry_is_executable = False
    for segment in segments:
        start = segment["paddr"]
        end = start + segment["memsz"]
        if (
            end < start
            or start < load_base
            or end > load_limit
            or segment["filesz"] > segment["memsz"]
            or segment["offset"] > payload_path.stat().st_size
            or segment["filesz"] > payload_path.stat().st_size - segment["offset"]
            or (segment["flags"] & (PF_X | PF_W)) == (PF_X | PF_W)
        ):
            raise ValueError("payload PT_LOAD violates the product window")
        if (
            segment["flags"] & PF_X
            and start <= header["entry"] < end
        ):
            entry_is_executable = True
        ranges.append((start, end))
    ranges.sort()
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ValueError("payload PT_LOAD ranges overlap")
    if not entry_is_executable:
        raise ValueError("payload entry is not in executable PT_LOAD")
    after = sha256_file(payload_path)
    if before != after:
        raise ValueError("payload changed during validation")
    return {
        "schema": "ribon-external-parus-payload-v0",
        "product_id": manifest.get("product_id"),
        "architecture": contract["architecture"],
        "entry_abi": contract["entry_abi"],
        "payload": {
            "path": str(payload_path),
            "sha256": before,
            "immutable": True,
        },
        "entry": f"0x{header['entry']:016x}",
        "load_window": {
            "base": f"0x{load_base:016x}",
            "limit": f"0x{load_limit:016x}",
        },
        "segments": [
            {
                **segment,
                "start": f"0x{segment['paddr']:016x}",
                "end": f"0x{segment['paddr'] + segment['memsz']:016x}",
            }
            for segment in segments
        ],
        "success": True,
    }


def main() -> int:
    """Validate and publish one machine-readable product input result."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--result", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = validate(args.manifest, args.payload)
    except (OSError, ValueError, json.JSONDecodeError, struct.error) as error:
        report = {
            "schema": "ribon-external-parus-payload-v0",
            "success": False,
            "failure": str(error),
        }
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "validate_external_parus_payload: "
        + ("success" if report["success"] else f"failure: {report['failure']}")
    )
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
