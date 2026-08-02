#!/usr/bin/env python3
"""Independently inspect D06 confirmed update and protected journals."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


def load_tool(name: str):
    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(f"ribon_d06_{name}", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def protected_selector(wire: bytes) -> dict[str, object] | None:
    if wire == bytes(160):
        return None
    if (
        len(wire) != 160
        or wire[:15] != b"RIBON-PSTATE-S1"
        or wire[15] != 0
        or wire[16] != 1
        or any(wire[17:20])
        or struct.unpack_from("<I", wire, 20)[0] != 160
        or struct.unpack_from("<I", wire, 24)[0] >= 2
        or struct.unpack_from("<I", wire, 28)[0] != 0
        or struct.unpack_from("<Q", wire, 32)[0] == 0
        or not any(wire[40:72])
        or not any(wire[72:104])
        or any(wire[104:144])
        or struct.unpack_from("<I", wire, 144)[0] != crc32c(wire[:144])
        or any(wire[148:])
    ):
        return None
    return {
        "record_slot": struct.unpack_from("<I", wire, 24)[0],
        "generation": struct.unpack_from("<Q", wire, 32)[0],
        "domain": wire[40:72],
        "record_digest": wire[72:104],
    }


def protected_record(wire: bytes) -> dict[str, object]:
    if (
        len(wire) != 160
        or wire[:15] != b"RIBON-PSTATE-R1"
        or wire[15] != 0
        or wire[16] != 1
        or any(wire[17:20])
        or struct.unpack_from("<I", wire, 20)[0] != 160
        or struct.unpack_from("<I", wire, 24)[0] != 1
        or struct.unpack_from("<I", wire, 28)[0] not in (1, 2)
        or struct.unpack_from("<Q", wire, 32)[0] == 0
        or not any(wire[64:96])
        or any(wire[136:144])
        or struct.unpack_from("<I", wire, 144)[0] != crc32c(wire[:144])
        or any(wire[148:])
    ):
        raise ValueError("protected record is malformed")
    return {
        "kind": struct.unpack_from("<I", wire, 28)[0],
        "generation": struct.unpack_from("<Q", wire, 32)[0],
        "confirmed_floor": struct.unpack_from("<Q", wire, 40)[0],
        "pending_sequence": struct.unpack_from("<Q", wire, 48)[0],
        "attempts_remaining": struct.unpack_from("<I", wire, 56)[0],
        "domain": wire[64:96],
        "binding_digest": wire[96:128],
        "attempt_sequence": struct.unpack_from("<Q", wire, 128)[0],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--disk", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--expected-active-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        disk = args.disk.read_bytes()
        manifest_bytes = args.manifest.read_bytes()
        transaction = load_tool("inspect_qemu_update_transaction.py")
        disk_tool = load_tool("inspect_qemu_update_disk.py")
        layout_tool = load_tool("update_layout.py")
        anchor = disk[disk_tool.ANCHOR_OFFSET:
                      disk_tool.ANCHOR_OFFSET + disk_tool.ANCHOR_BYTES]
        identity = anchor[128:640]
        regions = disk_tool.layout_regions(identity)
        by_kind = {int(region["kind"]): region for region in regions}
        slot_a = by_kind[5]
        journal = by_kind[10]
        trailing = by_kind[11]
        active_a = disk[int(slot_a["offset"]):
                        int(slot_a["offset"]) + int(slot_a["length"])]
        if hashlib.sha256(active_a).hexdigest() != args.expected_active_sha256:
            raise ValueError("confirmed predecessor bytes changed")
        journal_offset = int(journal["offset"])
        selectors = []
        for index in range(2):
            offset = journal_offset + 2 * transaction.RECORD_BYTES + \
                index * transaction.SELECTOR_BYTES
            value = transaction.selector_open(
                disk[offset:offset + transaction.SELECTOR_BYTES], layout_tool.crc32c
            )
            if value is not None:
                value["selector_slot"] = index
                selectors.append(value)
        selectors.sort(key=lambda value: int(value["generation"]), reverse=True)
        if not selectors or (len(selectors) > 1 and
                             selectors[0]["generation"] == selectors[1]["generation"]):
            raise ValueError("transaction selector authority is missing or conflicting")
        selected = selectors[0]
        record_offset = journal_offset + int(selected["record_slot"]) * \
            transaction.RECORD_BYTES
        record = transaction.record_open(
            disk[record_offset:record_offset + transaction.RECORD_BYTES],
            layout_tool.crc32c,
        )
        metadata = record["metadata"]
        manifest_sha = hashlib.sha256(manifest_bytes).hexdigest()
        if (
            record["generation"] != 5
            or record["target_slot"] != 1
            or record["target_state"] != 4
            or metadata["active_slot"] != 1
            or metadata["pending_slot"] != 0xFFFFFFFF
            or metadata["slots"][1]["state"] != 4
            or metadata["slots"][1]["manifest_sha256"] != manifest_sha
        ):
            raise ValueError("update journal is not exact B-confirmed generation 5")
        protected_base = int(trailing["offset"])
        protected_selectors = []
        for index in range(2):
            offset = protected_base + (2 + index) * 512
            value = protected_selector(disk[offset:offset + 160])
            if value is not None:
                protected_selectors.append(value)
        protected_selectors.sort(
            key=lambda value: int(value["generation"]), reverse=True
        )
        if not protected_selectors:
            raise ValueError("protected selector authority is absent")
        protected_selected = protected_selectors[0]
        protected_offset = protected_base + \
            int(protected_selected["record_slot"]) * 512
        protected_wire = disk[protected_offset:protected_offset + 160]
        protected = protected_record(protected_wire)
        if (
            hashlib.sha256(protected_wire).digest() !=
                protected_selected["record_digest"]
            or protected["generation"] != protected_selected["generation"]
            or protected["generation"] != 4
            or protected["kind"] != 1
            or protected["confirmed_floor"] != 2
            or protected["pending_sequence"] != 0
            or protected["attempts_remaining"] != 0
            or protected["attempt_sequence"] != 1
            or not any(protected["binding_digest"])
        ):
            raise ValueError("protected journal is not exact confirmed attempt")
        report = {
            "schema": "ribon-qemu-boot-confirmation-inspection-v1",
            "disk_sha256": hashlib.sha256(disk).hexdigest(),
            "manifest_sha256": manifest_sha,
            "update": {
                "active_slot": 1,
                "journal_generation": 5,
                "state": "CONFIRMED",
            },
            "protected": {
                "attempt_sequence": protected["attempt_sequence"],
                "confirmed_floor": protected["confirmed_floor"],
                "generation": protected["generation"],
                "provider_class": "reference",
            },
            "active_predecessor_bytes_unchanged": True,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print("RIBON-D06-QEMU-CONFIRMED-JOURNALS-OK")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"qemu-boot-confirmation-inspector: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
