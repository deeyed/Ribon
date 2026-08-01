#!/usr/bin/env python3
"""Compose and independently inspect deterministic Ribon update-storage objects."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from typing import Any


SOURCE_SCHEMA = "ribon-update-layout-source-v1"
PROVENANCE_SCHEMA = "ribon-update-layout-provenance-v1"
LAYOUT_MAGIC = b"RIBON-UPDATE-LAYOUT-V1".ljust(32, b"\0")
METADATA_MAGIC = b"RIBON-SLOT-METADATA-V1".ljust(32, b"\0")
LAYOUT_BYTES = 512
METADATA_BYTES = 512
ALIGNMENT_MAXIMUM = 1 << 30
U64_MAXIMUM = (1 << 64) - 1
REGION_NAMES = (
    "bootloader",
    "guard-boot-recovery",
    "immutable-recovery",
    "guard-recovery-slot-a",
    "slot-a",
    "guard-slot-a-slot-b",
    "slot-b",
    "guard-slot-b-metadata",
    "slot-metadata",
    "update-journal",
    "trailing-reserved",
)
SOURCE_KEYS = {
    "allocation_alignment",
    "bootloader_bytes",
    "guard_gap_bytes",
    "immutable_recovery_bytes",
    "media_capacity_bytes",
    "minimum_trailing_reserved_bytes",
    "schema",
    "slot_metadata_bytes",
    "slot_payload_bytes",
    "update_journal_bytes",
}


def exact_object(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    """Require one JSON object with exactly the selected keys."""

    if not isinstance(value, dict) or set(value) != keys:
        raise ValueError(f"{label} must contain exactly {sorted(keys)}")
    return value


def positive_u64(value: Any, label: str) -> int:
    """Return one positive u64 without accepting bool."""

    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not 1 <= value <= U64_MAXIMUM
    ):
        raise ValueError(f"{label} must be a positive u64")
    return value


def power_of_two(value: int) -> bool:
    """Return whether one integer is a nonzero power of two."""

    return value > 0 and value & (value - 1) == 0


def align_up(value: int, alignment: int) -> int:
    """Align one u64 while rejecting arithmetic overflow."""

    if value > U64_MAXIMUM - (alignment - 1):
        raise ValueError("aligned layout scalar wraps u64")
    return (value + alignment - 1) & ~(alignment - 1)


def parse_source(path: Path) -> dict[str, int]:
    """Read and exact-shape-check one source-neutral layout input."""

    document = exact_object(
        json.loads(path.read_text(encoding="utf-8")), SOURCE_KEYS, "layout source"
    )
    if document["schema"] != SOURCE_SCHEMA:
        raise ValueError("unsupported layout source schema")
    model = {
        key: positive_u64(document[key], key)
        for key in SOURCE_KEYS
        if key != "schema"
    }
    alignment = model["allocation_alignment"]
    if not power_of_two(alignment) or alignment > ALIGNMENT_MAXIMUM:
        raise ValueError("allocation_alignment must be a bounded power of two")
    if model["media_capacity_bytes"] % alignment:
        raise ValueError("media capacity must be allocation aligned")
    if model["slot_metadata_bytes"] < 2 * METADATA_BYTES:
        raise ValueError("slot metadata must hold two exact metadata objects")
    return model


def calculate_layout(model: dict[str, int]) -> list[dict[str, int | str]]:
    """Calculate the canonical eleven-region A/B media layout."""

    alignment = model["allocation_alignment"]
    requested = (
        model["bootloader_bytes"],
        model["guard_gap_bytes"],
        model["immutable_recovery_bytes"],
        model["guard_gap_bytes"],
        model["slot_payload_bytes"],
        model["guard_gap_bytes"],
        model["slot_payload_bytes"],
        model["guard_gap_bytes"],
        model["slot_metadata_bytes"],
        model["update_journal_bytes"],
    )
    lengths = [align_up(value, alignment) for value in requested]
    minimum_tail = align_up(model["minimum_trailing_reserved_bytes"], alignment)
    prefix = sum(lengths)
    if prefix > U64_MAXIMUM or minimum_tail > U64_MAXIMUM - prefix:
        raise ValueError("layout total wraps u64")
    capacity = model["media_capacity_bytes"]
    if prefix + minimum_tail > capacity:
        raise ValueError("layout does not fit media capacity")
    lengths.append(capacity - prefix)
    regions: list[dict[str, int | str]] = []
    offset = 0
    for index, (name, length) in enumerate(zip(REGION_NAMES, lengths), start=1):
        regions.append(
            {"kind": index, "length": length, "name": name, "offset": offset}
        )
        offset += length
    if offset != capacity:
        raise ValueError("layout does not consume exact media capacity")
    return regions


def encode_identity(model: dict[str, int], regions: list[dict[str, int | str]]) -> bytes:
    """Encode the exact C-compatible 512-byte little-endian layout identity."""

    if len(regions) != len(REGION_NAMES):
        raise ValueError("layout region count is not canonical")
    output = bytearray(LAYOUT_BYTES)
    output[:32] = LAYOUT_MAGIC
    struct.pack_into(
        "<HHIQQII",
        output,
        32,
        1,
        128,
        LAYOUT_BYTES,
        model["media_capacity_bytes"],
        model["allocation_alignment"],
        len(regions),
        0,
    )
    for index, region in enumerate(regions):
        struct.pack_into(
            "<IIQQ",
            output,
            128 + index * 24,
            int(region["kind"]),
            0,
            int(region["offset"]),
            int(region["length"]),
        )
    return bytes(output)


def load_update_manifest_validator() -> Any:
    """Load the sibling D01 host codec without creating a package dependency."""

    path = Path(__file__).with_name("update_manifest.py")
    spec = importlib.util.spec_from_file_location("ribon_update_manifest_tool", path)
    if spec is None or spec.loader is None:
        raise ValueError("cannot load update manifest validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def manifest_projection(path: Path, alignment: int) -> dict[str, Any]:
    """Validate a D01 manifest and derive its worst-case slot byte requirement."""

    data = path.read_bytes()
    view = load_update_manifest_validator().validate_manifest(data)
    required = max(
        int(component["bundle_offset"]) + int(component["maximum_size"])
        for component in view["components"]
    )
    required = align_up(required, alignment)
    return {
        "component_count": int(view["component_count"]),
        "manifest_sha256": hashlib.sha256(data).hexdigest(),
        "required_slot_bytes": required,
    }


def compose(source: Path, manifest: Path | None) -> dict[str, Any]:
    """Compose one reproducible layout provenance object."""

    source_bytes = source.read_bytes()
    model = parse_source(source)
    regions = calculate_layout(model)
    identity = encode_identity(model, regions)
    slot_bytes = int(regions[4]["length"])
    projection = None if manifest is None else manifest_projection(
        manifest, model["allocation_alignment"]
    )
    if projection is not None and int(projection["required_slot_bytes"]) > slot_bytes:
        raise ValueError("update manifest maximum range exceeds both canonical slots")
    return {
        "allocation_alignment": model["allocation_alignment"],
        "identity_bytes": len(identity),
        "layout_identity_sha256": hashlib.sha256(identity).hexdigest(),
        "manifest": projection,
        "media_capacity_bytes": model["media_capacity_bytes"],
        "regions": regions,
        "schema": PROVENANCE_SCHEMA,
        "slot_payload_bytes": slot_bytes,
        "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
    }


def crc32c(data: bytes) -> int:
    """Calculate Castagnoli CRC32C without host-specific extensions."""

    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            mask = -(crc & 1) & 0xFFFFFFFF
            crc = (crc >> 1) ^ (0x82F63B78 & mask)
    return (~crc) & 0xFFFFFFFF


def inspect_metadata(path: Path) -> dict[str, Any]:
    """Independently validate and derive one exact LE slot metadata object."""

    data = path.read_bytes()
    if len(data) != METADATA_BYTES or data[:32] != METADATA_MAGIC:
        raise ValueError("metadata magic or exact size is invalid")
    version, header, total = struct.unpack_from("<HHI", data, 32)
    generation = struct.unpack_from("<Q", data, 40)[0]
    active, pending, count, flags = struct.unpack_from("<IIII", data, 48)
    if (version, header, total, count, flags) != (1, 64, 512, 2, 0):
        raise ValueError("metadata header is noncanonical")
    if generation == 0 or active >= 2 or (pending != 0xFFFFFFFF and pending >= 2):
        raise ValueError("metadata global slot state is invalid")
    body_digest = hashlib.sha256(data[:384]).digest()
    if data[384:416] != body_digest or struct.unpack_from("<I", data, 416)[0] != crc32c(data[:416]):
        raise ValueError("metadata digest or CRC32C does not match")
    if any(data[420:]):
        raise ValueError("metadata trailing reserved bytes are nonzero")
    slots: list[dict[str, Any]] = []
    pending_count = 0
    for index in range(2):
        row = 64 + index * 160
        slot_id, state = struct.unpack_from("<II", data, row)
        row_generation, image_generation = struct.unpack_from("<QQ", data, row + 8)
        boot_attempts, row_flags = struct.unpack_from("<II", data, row + 120)
        manifest_digest = data[row + 24:row + 56]
        image_set_digest = data[row + 56:row + 88]
        layout_digest = data[row + 88:row + 120]
        identities = (manifest_digest, image_set_digest, layout_digest)
        if slot_id != index or state > 5 or row_flags != 0 or any(data[row + 128:row + 160]):
            raise ValueError(f"metadata slot {index} shape is invalid")
        if state == 0:
            if row_generation or image_generation or boot_attempts or any(any(value) for value in identities):
                raise ValueError(f"empty metadata slot {index} carries identity")
        elif (
            row_generation == 0
            or row_generation > generation
            or image_generation == 0
            or any(not any(value) for value in identities)
            or boot_attempts > 32
            or (state == 3) != (boot_attempts > 0)
        ):
            raise ValueError(f"metadata slot {index} lifecycle is invalid")
        pending_count += state == 3
        slots.append(
            {
                "boot_attempts": boot_attempts,
                "image_generation": image_generation,
                "layout_sha256": layout_digest.hex(),
                "manifest_sha256": manifest_digest.hex(),
                "metadata_generation": row_generation,
                "slot": index,
                "state": state,
            }
        )
    if slots[active]["state"] != 4:
        raise ValueError("active slot is not confirmed")
    if pending == 0xFFFFFFFF:
        if pending_count:
            raise ValueError("unmarked pending slot exists")
    elif pending_count != 1 or pending == active or slots[pending]["state"] != 3:
        raise ValueError("pending marker is not singleton")
    return {
        "active_slot": active,
        "metadata_generation": generation,
        "pending_slot": None if pending == 0xFFFFFFFF else pending,
        "schema": "ribon-update-slot-metadata-inspection-v1",
        "slots": slots,
        "wire_sha256": hashlib.sha256(data).hexdigest(),
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    """Write deterministic JSON below an explicitly selected build output."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    """Run the compose or independent metadata-inspection command."""

    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    compose_parser = subparsers.add_parser("compose")
    compose_parser.add_argument("--source", type=Path, required=True)
    compose_parser.add_argument("--manifest", type=Path)
    compose_parser.add_argument("--identity-output", type=Path)
    compose_parser.add_argument("--output", type=Path, required=True)
    inspect_parser = subparsers.add_parser("inspect-metadata")
    inspect_parser.add_argument("--metadata", type=Path, required=True)
    inspect_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "compose":
            result = compose(args.source, args.manifest)
            if args.identity_output is not None:
                model = parse_source(args.source)
                identity = encode_identity(model, calculate_layout(model))
                args.identity_output.parent.mkdir(parents=True, exist_ok=True)
                args.identity_output.write_bytes(identity)
        else:
            result = inspect_metadata(args.metadata)
        write_json(args.output, result)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"update-layout: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
