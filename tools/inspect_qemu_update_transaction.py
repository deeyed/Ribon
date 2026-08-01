#!/usr/bin/env python3
"""Independently inspect a D04 PENDING transaction journal and installed slot."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys


RECORD_BYTES = 1024
SELECTOR_BYTES = 512


def load_tool(name: str):
    """Load one sibling tool without sharing target implementation code."""

    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(f"ribon_d04_{name}", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def selector_open(wire: bytes, crc32c) -> dict[str, object] | None:
    """Return one valid selector or None for a torn/unused slot."""

    if len(wire) != SELECTOR_BYTES:
        raise ValueError("selector range is short")
    if wire == bytes(SELECTOR_BYTES):
        return None
    if (
        wire[:32] != b"RIBON-UPDATE-TXN-SELECT-V1".ljust(32, b"\0")
        or struct.unpack_from("<HHIIIQ", wire, 32)[:3] != (1, 128, 512)
        or struct.unpack_from("<I", wire, 44)[0] != 0
        or struct.unpack_from("<I", wire, 40)[0] >= 2
        or struct.unpack_from("<Q", wire, 48)[0] == 0
        or not any(wire[56:88])
        or wire[128:160] != hashlib.sha256(wire[:128]).digest()
        or struct.unpack_from("<I", wire, 160)[0] != crc32c(wire[:160])
        or any(wire[164:])
    ):
        return None
    return {
        "record_slot": struct.unpack_from("<I", wire, 40)[0],
        "generation": struct.unpack_from("<Q", wire, 48)[0],
        "record_digest": wire[56:88],
        "predecessor_generation": struct.unpack_from("<Q", wire, 88)[0],
        "predecessor_digest": wire[96:128],
    }


def metadata_open(wire: bytes, crc32c) -> dict[str, object]:
    """Decode the canonical slot metadata without assuming a target state."""

    if (
        len(wire) != 512
        or wire[:32] != b"RIBON-SLOT-METADATA-V1".ljust(32, b"\0")
        or struct.unpack_from("<HHI", wire, 32) != (1, 64, 512)
        or wire[384:416] != hashlib.sha256(wire[:384]).digest()
        or struct.unpack_from("<I", wire, 416)[0] != crc32c(wire[:416])
        or any(wire[420:])
    ):
        raise ValueError("transaction metadata is malformed")
    generation = struct.unpack_from("<Q", wire, 40)[0]
    active, pending, count, flags = struct.unpack_from("<IIII", wire, 48)
    if count != 2 or flags != 0 or generation == 0:
        raise ValueError("transaction metadata globals are invalid")
    slots = []
    for index in range(2):
        row = 64 + 160 * index
        slot_id, state = struct.unpack_from("<II", wire, row)
        row_generation, image_generation = struct.unpack_from("<QQ", wire, row + 8)
        attempts, row_flags = struct.unpack_from("<II", wire, row + 120)
        if slot_id != index or row_flags != 0 or any(wire[row + 128:row + 160]):
            raise ValueError("transaction metadata row is invalid")
        slots.append({
            "slot": slot_id,
            "state": state,
            "metadata_generation": row_generation,
            "image_generation": image_generation,
            "manifest_sha256": wire[row + 24:row + 56].hex(),
            "image_set_sha256": wire[row + 56:row + 88].hex(),
            "layout_sha256": wire[row + 88:row + 120].hex(),
            "attempts": attempts,
        })
    return {
        "generation": generation,
        "active_slot": active,
        "pending_slot": pending,
        "slots": slots,
    }


def record_open(wire: bytes, crc32c) -> dict[str, object]:
    """Decode one complete record and bind its embedded metadata."""

    if (
        len(wire) != RECORD_BYTES
        or wire[:32] != b"RIBON-UPDATE-TXN-RECORD-V1".ljust(32, b"\0")
        or struct.unpack_from("<HHI", wire, 32) != (1, 240, 1024)
        or struct.unpack_from("<I", wire, 40)[0] != 1
        or struct.unpack_from("<I", wire, 44)[0] >= 2
        or struct.unpack_from("<I", wire, 52)[0] != 0
        or any(wire[232:240])
        or wire[752:784] != hashlib.sha256(wire[:752]).digest()
        or struct.unpack_from("<I", wire, 784)[0] != crc32c(wire[:784])
        or any(wire[788:])
    ):
        raise ValueError("selected transaction record is malformed")
    metadata_wire = wire[240:752]
    if wire[104:136] != hashlib.sha256(metadata_wire).digest():
        raise ValueError("record metadata digest differs")
    metadata = metadata_open(metadata_wire, crc32c)
    target = struct.unpack_from("<I", wire, 44)[0]
    state = struct.unpack_from("<I", wire, 48)[0]
    generation = struct.unpack_from("<Q", wire, 56)[0]
    predecessor = struct.unpack_from("<Q", wire, 64)[0]
    slot = metadata["slots"][target]
    if (
        metadata["generation"] != generation
        or slot["state"] != state
        or bytes.fromhex(str(slot["manifest_sha256"])) != wire[136:168]
        or bytes.fromhex(str(slot["image_set_sha256"])) != wire[168:200]
        or bytes.fromhex(str(slot["layout_sha256"])) != wire[200:232]
        or (generation == 1 and (predecessor != 0 or any(wire[72:104])))
        or (generation > 1 and (predecessor != generation - 1 or not any(wire[72:104])))
    ):
        raise ValueError("record and metadata identities differ")
    return {
        "target_slot": target,
        "target_state": state,
        "generation": generation,
        "predecessor_generation": predecessor,
        "predecessor_digest": wire[72:104],
        "record_digest": wire[752:784],
        "metadata": metadata,
    }


def main() -> int:
    """Inspect GPT/layout, newest journal selector, PENDING state and payload."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--disk", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--expected-active-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        disk = args.disk.read_bytes()
        manifest_bytes = args.manifest.read_bytes()
        manifest_tool = load_tool("update_manifest.py")
        layout_tool = load_tool("update_layout.py")
        disk_tool = load_tool("inspect_qemu_update_disk.py")
        manifest = manifest_tool.validate_manifest(manifest_bytes)
        if len(disk) != 64 * 1024 * 1024:
            raise ValueError("disk capacity is not the D04 reference capacity")
        anchor = disk[
            disk_tool.ANCHOR_OFFSET:
            disk_tool.ANCHOR_OFFSET + disk_tool.ANCHOR_BYTES
        ]
        if (
            anchor[:32] != b"RIBON-UEFI-UPDATE-MEDIA-V1".ljust(32, b"\0")
            or struct.unpack_from("<HHIQQII", anchor, 32) != (
                1,
                disk_tool.ANCHOR_BYTES,
                disk_tool.ANCHOR_BYTES,
                disk_tool.ANCHOR_OFFSET,
                len(disk),
                disk_tool.BLOCK_BYTES,
                0,
            )
            or anchor[64:96] != disk_tool.MEDIA_ID
            or anchor[640:672] != hashlib.sha256(anchor[:640]).digest()
            or struct.unpack_from("<I", anchor, 672)[0] !=
                layout_tool.crc32c(anchor[:672])
            or any(anchor[676:])
        ):
            raise ValueError("media anchor is invalid")
        identity = anchor[128:640]
        layout_digest = hashlib.sha256(identity).digest()
        if anchor[96:128] != layout_digest:
            raise ValueError("layout anchor digest differs")
        regions = disk_tool.layout_regions(identity)
        if struct.unpack_from("<Q", identity, 40)[0] != len(disk):
            raise ValueError("layout media capacity differs")
        gpt = disk_tool.inspect_gpt(disk)
        by_kind = {int(region["kind"]): region for region in regions}
        slot_a = by_kind[5]
        slot_b = by_kind[7]
        journal = by_kind[10]
        active = disk[int(slot_a["offset"]):
                      int(slot_a["offset"]) + int(slot_a["length"])]
        active_sha = hashlib.sha256(active).hexdigest()
        if active_sha != args.expected_active_sha256:
            raise ValueError("confirmed predecessor bytes changed")
        journal_offset = int(journal["offset"])
        selectors = []
        for index in range(2):
            offset = journal_offset + 2 * RECORD_BYTES + index * SELECTOR_BYTES
            decoded = selector_open(
                disk[offset:offset + SELECTOR_BYTES], layout_tool.crc32c
            )
            if decoded is not None:
                decoded["selector_slot"] = index
                selectors.append(decoded)
        if not selectors:
            raise ValueError("transaction journal has no valid selector")
        selectors.sort(key=lambda value: int(value["generation"]), reverse=True)
        if len(selectors) > 1 and selectors[0]["generation"] == selectors[1]["generation"]:
            raise ValueError("transaction selectors conflict")
        selected = selectors[0]
        record_offset = journal_offset + int(selected["record_slot"]) * RECORD_BYTES
        record = record_open(
            disk[record_offset:record_offset + RECORD_BYTES], layout_tool.crc32c
        )
        if (
            record["generation"] != selected["generation"]
            or record["record_digest"] != selected["record_digest"]
            or record["predecessor_generation"] != selected["predecessor_generation"]
            or record["predecessor_digest"] != selected["predecessor_digest"]
        ):
            raise ValueError("selector does not bind selected record")
        metadata = record["metadata"]
        if (
            record["generation"] != 4
            or record["target_slot"] != 1
            or record["target_state"] != 3
            or metadata["active_slot"] != 0
            or metadata["pending_slot"] != 1
            or metadata["slots"][0]["state"] != 4
            or metadata["slots"][1]["state"] != 3
            or metadata["slots"][1]["attempts"] != 3
            or metadata["slots"][1]["manifest_sha256"] !=
                hashlib.sha256(manifest_bytes).hexdigest()
            or bytes.fromhex(str(metadata["slots"][1]["layout_sha256"])) !=
                layout_digest
        ):
            raise ValueError("newest transaction is not A-confirmed/B-pending generation 4")
        installed = []
        for component in manifest["components"]:
            offset = int(slot_b["offset"]) + int(component["bundle_offset"])
            size = int(component["exact_size"])
            digest = hashlib.sha256(disk[offset:offset + size]).hexdigest()
            if digest != component["content_sha256"]:
                raise ValueError("pending component digest differs")
            installed.append({"offset": offset, "bytes": size, "sha256": digest})
        report = {
            "schema": "ribon-qemu-update-transaction-inspection-v1",
            "disk_sha256": hashlib.sha256(disk).hexdigest(),
            "media_identity_sha256": disk_tool.MEDIA_ID.hex(),
            "layout_identity_sha256": layout_digest.hex(),
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "active_slot_unchanged": True,
            "active_slot_sha256": active_sha,
            "journal_generation": record["generation"],
            "predecessor_generation": record["predecessor_generation"],
            "target_slot": record["target_slot"],
            "target_state": "PENDING",
            "pending_attempts": metadata["slots"][1]["attempts"],
            "installed_components": installed,
            "metadata": metadata,
            "gpt": gpt,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        print("RIBON-D04-QEMU-TRANSACTION-DISK-PENDING")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"qemu-update-transaction-inspector: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
