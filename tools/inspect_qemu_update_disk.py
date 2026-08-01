#!/usr/bin/env python3
"""Independently inspect the D03 GPT media, metadata, and installed component bytes."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib


BLOCK_BYTES = 512
ANCHOR_OFFSET = 64 * 1024
ANCHOR_BYTES = 1024
LAYOUT_BYTES = 512
MEDIA_ID = hashlib.sha256(b"ribon.qemu.q35.update-media.v1").digest()


def load_tool(name: str):
    """Load a sibling independent codec."""

    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(f"ribon_inspect_{name}", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def layout_regions(identity: bytes) -> list[dict[str, int]]:
    """Validate canonical layout identity and derive all eleven exact ranges."""

    if (
        len(identity) != LAYOUT_BYTES
        or identity[:32] != b"RIBON-UPDATE-LAYOUT-V1".ljust(32, b"\0")
        or struct.unpack_from("<HHI", identity, 32) != (1, 128, 512)
        or struct.unpack_from("<I", identity, 56)[0] != 11
        or any(identity[60:128])
        or any(identity[128 + 11 * 24:])
    ):
        raise ValueError("layout identity is not canonical")
    capacity, alignment = struct.unpack_from("<QQ", identity, 40)
    regions = []
    cursor = 0
    for index in range(11):
        kind, flags, offset, length = struct.unpack_from("<IIQQ", identity, 128 + index * 24)
        if (
            kind != index + 1 or flags != 0 or length == 0 or offset != cursor
            or offset % alignment or length % alignment or offset + length > capacity
        ):
            raise ValueError("layout region sequence is invalid")
        regions.append({"kind": kind, "offset": offset, "length": length})
        cursor += length
    if cursor != capacity or regions[4]["length"] != regions[6]["length"]:
        raise ValueError("layout capacity closure is invalid")
    return regions


def inspect_gpt(disk: bytes) -> dict[str, object]:
    """Validate mirrored GPT headers/tables and return exact used partitions."""

    blocks = len(disk) // BLOCK_BYTES
    if len(disk) % BLOCK_BYTES or disk[510:512] != b"\x55\xaa" or disk[450] != 0xEE:
        raise ValueError("protective MBR is invalid")

    def header(lba: int, expected_backup: int) -> tuple[int, bytes]:
        sector = bytearray(disk[lba * BLOCK_BYTES:(lba + 1) * BLOCK_BYTES])
        if sector[:8] != b"EFI PART" or len(sector) != BLOCK_BYTES:
            raise ValueError("GPT header magic is invalid")
        revision, size, expected_crc, reserved = struct.unpack_from("<IIII", sector, 8)
        current, backup, first, last = struct.unpack_from("<QQQQ", sector, 24)
        table_lba, count, entry_size, table_crc = struct.unpack_from("<QIII", sector, 72)
        if (
            revision != 0x00010000 or size != 92 or reserved != 0
            or current != lba or backup != expected_backup or first != 34
            or last != blocks - 34 or count != 128 or entry_size != 128
        ):
            raise ValueError("GPT header scalar is invalid")
        struct.pack_into("<I", sector, 16, 0)
        if zlib.crc32(sector[:size]) & 0xFFFFFFFF != expected_crc:
            raise ValueError("GPT header CRC is invalid")
        table = disk[table_lba * BLOCK_BYTES:table_lba * BLOCK_BYTES + count * entry_size]
        if len(table) != count * entry_size or zlib.crc32(table) & 0xFFFFFFFF != table_crc:
            raise ValueError("GPT table CRC is invalid")
        return table_lba, table

    primary_lba, primary = header(1, blocks - 1)
    backup_lba, backup = header(blocks - 1, 1)
    if primary_lba != 2 or backup_lba != blocks - 33 or primary != backup:
        raise ValueError("GPT mirror is not exact")
    partitions = []
    for index in range(128):
        row = primary[index * 128:(index + 1) * 128]
        if row[:16] == bytes(16):
            if any(row):
                raise ValueError("unused GPT entry is nonzero")
            continue
        first, last, attributes = struct.unpack_from("<QQQ", row, 32)
        if first > last or attributes != 0:
            raise ValueError("GPT partition range is invalid")
        name = row[56:128].decode("utf-16-le").rstrip("\0")
        partitions.append({"name": name, "first_lba": first, "last_lba": last})
    return {"partition_count": len(partitions), "partitions": partitions}


def inspect_metadata(data: bytes, layout_digest: bytes) -> dict[str, object]:
    """Validate redundant canonical metadata and require B=VERIFIED."""

    layout_tool = load_tool("update_layout.py")
    if len(data) != 1024 or data[:512] != data[512:]:
        raise ValueError("metadata copies differ or are truncated")
    wire = data[:512]
    if (
        wire[:32] != b"RIBON-SLOT-METADATA-V1".ljust(32, b"\0")
        or struct.unpack_from("<HHI", wire, 32) != (1, 64, 512)
        or wire[384:416] != hashlib.sha256(wire[:384]).digest()
        or struct.unpack_from("<I", wire, 416)[0] != layout_tool.crc32c(wire[:416])
        or any(wire[420:])
    ):
        raise ValueError("metadata wire integrity is invalid")
    generation = struct.unpack_from("<Q", wire, 40)[0]
    active, pending, count, flags = struct.unpack_from("<IIII", wire, 48)
    if generation != 3 or active != 0 or pending != 0xFFFFFFFF or count != 2 or flags != 0:
        raise ValueError("metadata global VERIFIED state is invalid")
    slots = []
    for index in range(2):
        row = 64 + index * 160
        slot_id, state = struct.unpack_from("<II", wire, row)
        row_generation, image_generation = struct.unpack_from("<QQ", wire, row + 8)
        manifest_digest = wire[row + 24:row + 56]
        image_digest = wire[row + 56:row + 88]
        row_layout = wire[row + 88:row + 120]
        attempts, row_flags = struct.unpack_from("<II", wire, row + 120)
        if slot_id != index or row_flags != 0 or attempts != 0 or any(wire[row + 128:row + 160]):
            raise ValueError("metadata slot row shape is invalid")
        slots.append({
            "slot": index, "state": state, "metadata_generation": row_generation,
            "image_generation": image_generation, "manifest_sha256": manifest_digest.hex(),
            "image_set_sha256": image_digest.hex(), "layout_sha256": row_layout.hex(),
        })
    if slots[0]["state"] != 4 or slots[1]["state"] != 2:
        raise ValueError("metadata is not A-CONFIRMED/B-VERIFIED")
    if bytes.fromhex(str(slots[1]["layout_sha256"])) != layout_digest:
        raise ValueError("verified slot layout identity differs")
    return {"generation": generation, "active_slot": active, "slots": slots}


def main() -> int:
    """Produce independent post-install evidence JSON."""

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
        manifest = manifest_tool.validate_manifest(manifest_bytes)
        if len(disk) != 64 * 1024 * 1024:
            raise ValueError("disk capacity is not the D03 reference capacity")
        anchor = disk[ANCHOR_OFFSET:ANCHOR_OFFSET + ANCHOR_BYTES]
        if (
            anchor[:32] != b"RIBON-UEFI-UPDATE-MEDIA-V1".ljust(32, b"\0")
            or struct.unpack_from("<HHIQQII", anchor, 32) !=
                (1, 1024, 1024, ANCHOR_OFFSET, len(disk), 512, 0)
            or anchor[64:96] != MEDIA_ID
            or anchor[640:672] != hashlib.sha256(anchor[:640]).digest()
            or struct.unpack_from("<I", anchor, 672)[0] !=
                load_tool("update_layout.py").crc32c(anchor[:672])
            or any(anchor[676:])
        ):
            raise ValueError("media anchor is invalid")
        identity = anchor[128:640]
        layout_digest = hashlib.sha256(identity).digest()
        if anchor[96:128] != layout_digest:
            raise ValueError("layout anchor digest differs")
        regions = layout_regions(identity)
        if struct.unpack_from("<Q", identity, 40)[0] != len(disk):
            raise ValueError("layout media capacity differs")
        gpt = inspect_gpt(disk)
        slot_a, slot_b, metadata_region = regions[4], regions[6], regions[8]
        active = disk[slot_a["offset"]:slot_a["offset"] + slot_a["length"]]
        active_sha = hashlib.sha256(active).hexdigest()
        if active_sha != args.expected_active_sha256:
            raise ValueError("active slot changed during inactive install")
        metadata = inspect_metadata(
            disk[metadata_region["offset"]:metadata_region["offset"] + 1024],
            layout_digest,
        )
        if metadata["slots"][1]["manifest_sha256"] != hashlib.sha256(manifest_bytes).hexdigest():
            raise ValueError("metadata manifest digest differs")
        installed = []
        for component in manifest["components"]:
            offset = slot_b["offset"] + int(component["bundle_offset"])
            size = int(component["exact_size"])
            digest = hashlib.sha256(disk[offset:offset + size]).hexdigest()
            if digest != component["content_sha256"]:
                raise ValueError("installed component content digest differs")
            installed.append({"offset": offset, "bytes": size, "sha256": digest})
        report = {
            "schema": "ribon-qemu-update-disk-inspection-v1",
            "disk_sha256": hashlib.sha256(disk).hexdigest(),
            "media_identity_sha256": MEDIA_ID.hex(),
            "layout_identity_sha256": layout_digest.hex(),
            "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "active_slot_sha256": active_sha,
            "active_slot_unchanged": True,
            "metadata": metadata,
            "installed_components": installed,
            "gpt": gpt,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("RIBON-D03-QEMU-UPDATE-DISK-VERIFIED")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"qemu-update-inspector: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
