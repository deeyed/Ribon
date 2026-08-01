#!/usr/bin/env python3
"""Build one deterministic signed-bundle and GPT-backed q35 update medium."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import uuid
import zlib


MEDIA_BYTES = 64 * 1024 * 1024
BLOCK_BYTES = 512
ANCHOR_OFFSET = 64 * 1024
ANCHOR_BYTES = 1024
MEDIA_ID = hashlib.sha256(b"ribon.qemu.q35.update-media.v1").digest()
PRIVATE_SEED = bytes.fromhex(
    "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
)
KEY_ID = "ribon-update-release-2026q3"
LAYOUT_SOURCE = Path("tests/fixtures/update/update-layout-source-v1.json")
PROVENANCE_SCHEMA = "ribon-qemu-update-fixture-v1"


def load_tool(name: str):
    """Load one sibling deterministic codec without creating a package."""

    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(f"ribon_{name}", path)
    if spec is None or spec.loader is None:
        raise ValueError(f"cannot load {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(path: Path, data: bytes) -> None:
    """Write one generated binary below the selected output root."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_json(path: Path, value: object) -> None:
    """Write canonical presentation JSON for provenance and source inputs."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def component_bytes(tag: bytes) -> bytes:
    """Create exact page-sized deterministic component content."""

    output = bytearray(4096)
    for offset in range(0, len(output), 32):
        output[offset:offset + 32] = hashlib.sha256(tag + struct.pack("<I", offset)).digest()
    return bytes(output)


def sign_message(message: bytes, root: Path) -> bytes:
    """Use OpenSSL Ed25519 with the published RFC 8032 test seed."""

    private_der = bytes.fromhex("302e020100300506032b657004220420") + PRIVATE_SEED
    key = root / ".fixture-signing-key.der"
    message_path = root / ".fixture-message.bin"
    signature_path = root / ".fixture-signature.bin"
    write(key, private_der)
    write(message_path, message)
    try:
        completed = subprocess.run(
            [
                "openssl", "pkeyutl", "-sign", "-rawin",
                "-inkey", str(key), "-keyform", "DER",
                "-in", str(message_path), "-out", str(signature_path),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if completed.returncode != 0:
            raise ValueError(
                "OpenSSL Ed25519 signing failed: " +
                completed.stderr.decode("utf-8", errors="replace")
            )
        signature = signature_path.read_bytes()
        if len(signature) != 64:
            raise ValueError("OpenSSL returned a noncanonical Ed25519 signature")
        return signature
    finally:
        for path in (key, message_path, signature_path):
            path.unlink(missing_ok=True)


def metadata_wire(layout_digest: bytes) -> bytes:
    """Encode initial A-CONFIRMED/B-EMPTY redundant metadata fixture."""

    layout_tool = load_tool("update_layout.py")
    output = bytearray(512)
    output[:32] = b"RIBON-SLOT-METADATA-V1".ljust(32, b"\0")
    struct.pack_into("<HHIQIIII", output, 32, 1, 64, 512, 1, 0, 0xFFFFFFFF, 2, 0)
    manifest_digest = hashlib.sha256(b"ribon.factory.slot-a.manifest.v1").digest()
    image_digest = hashlib.sha256(b"ribon.factory.slot-a.image-set.v1").digest()
    struct.pack_into("<IIQQ", output, 64, 0, 4, 1, 1)
    output[88:120] = manifest_digest
    output[120:152] = image_digest
    output[152:184] = layout_digest
    struct.pack_into("<II", output, 184, 0, 0)
    struct.pack_into("<II", output, 224, 1, 0)
    output[384:416] = hashlib.sha256(output[:384]).digest()
    struct.pack_into("<I", output, 416, layout_tool.crc32c(output[:416]))
    return bytes(output)


def gpt_entry(type_id: uuid.UUID, unique_id: uuid.UUID,
              first: int, last: int, name: str) -> bytes:
    """Encode one UEFI GPT entry with deterministic GUIDs and UTF-16 name."""

    output = bytearray(128)
    output[:16] = type_id.bytes_le
    output[16:32] = unique_id.bytes_le
    struct.pack_into("<QQQ", output, 32, first, last, 0)
    encoded = name.encode("utf-16-le")
    output[56:56 + min(len(encoded), 72)] = encoded[:72]
    return bytes(output)


def gpt_header(current: int, backup: int, table_lba: int,
               table_crc: int, disk_id: uuid.UUID) -> bytes:
    """Encode one CRC-protected GPT header sector."""

    output = bytearray(BLOCK_BYTES)
    output[:8] = b"EFI PART"
    struct.pack_into(
        "<IIIIQQQQ16sQIII", output, 8,
        0x00010000, 92, 0, 0, current, backup, 34,
        MEDIA_BYTES // BLOCK_BYTES - 34, disk_id.bytes_le,
        table_lba, 128, 128, table_crc,
    )
    struct.pack_into("<I", output, 16, zlib.crc32(output[:92]) & 0xFFFFFFFF)
    return bytes(output)


def build_disk(identity: bytes, regions: list[dict[str, object]]) -> tuple[bytes, dict[str, object]]:
    """Build protective MBR, mirrored GPT, anchor, slots, and metadata."""

    disk = bytearray(MEDIA_BYTES)
    total_blocks = MEDIA_BYTES // BLOCK_BYTES
    partition_type = uuid.UUID("8f4f93a9-9cc8-4f55-a9aa-b9495f6f3d7a")
    namespace = uuid.UUID("a981be96-6b45-4f88-bf2b-86f706fbdc03")
    by_name = {str(region["name"]): region for region in regions}
    partitions = []
    for name in ("immutable-recovery", "slot-a", "slot-b", "slot-metadata"):
        region = by_name[name]
        first = int(region["offset"]) // BLOCK_BYTES
        last = (int(region["offset"]) + int(region["length"])) // BLOCK_BYTES - 1
        partitions.append((name, first, last, uuid.uuid5(namespace, name)))
    table = bytearray(128 * 128)
    for index, (name, first, last, unique) in enumerate(partitions):
        table[index * 128:(index + 1) * 128] = gpt_entry(
            partition_type, unique, first, last, name
        )
    table_crc = zlib.crc32(table) & 0xFFFFFFFF
    disk_id = uuid.uuid5(namespace, "ribon-qemu-q35-update-media-v1")
    disk[446 + 4] = 0xEE
    struct.pack_into("<II", disk, 446 + 8, 1, min(total_blocks - 1, 0xFFFFFFFF))
    disk[510:512] = b"\x55\xaa"
    disk[2 * BLOCK_BYTES:2 * BLOCK_BYTES + len(table)] = table
    backup_table_lba = total_blocks - 33
    disk[backup_table_lba * BLOCK_BYTES:(backup_table_lba * BLOCK_BYTES) + len(table)] = table
    disk[BLOCK_BYTES:2 * BLOCK_BYTES] = gpt_header(
        1, total_blocks - 1, 2, table_crc, disk_id
    )
    disk[(total_blocks - 1) * BLOCK_BYTES:] = gpt_header(
        total_blocks - 1, 1, backup_table_lba, table_crc, disk_id
    )
    anchor = bytearray(ANCHOR_BYTES)
    anchor[:32] = b"RIBON-UEFI-UPDATE-MEDIA-V1".ljust(32, b"\0")
    struct.pack_into("<HHIQQII", anchor, 32, 1, ANCHOR_BYTES,
                     ANCHOR_BYTES, ANCHOR_OFFSET, MEDIA_BYTES, BLOCK_BYTES, 0)
    anchor[64:96] = MEDIA_ID
    anchor[96:128] = hashlib.sha256(identity).digest()
    anchor[128:640] = identity
    anchor[640:672] = hashlib.sha256(anchor[:640]).digest()
    layout_tool = load_tool("update_layout.py")
    struct.pack_into("<I", anchor, 672, layout_tool.crc32c(anchor[:672]))
    disk[ANCHOR_OFFSET:ANCHOR_OFFSET + ANCHOR_BYTES] = anchor
    slot_a = by_name["slot-a"]
    active_pattern = component_bytes(b"ribon-active-slot-a-v1") * 2
    active_offset = int(slot_a["offset"])
    disk[active_offset:active_offset + len(active_pattern)] = active_pattern
    metadata = metadata_wire(hashlib.sha256(identity).digest())
    metadata_offset = int(by_name["slot-metadata"]["offset"])
    disk[metadata_offset:metadata_offset + 512] = metadata
    disk[metadata_offset + 512:metadata_offset + 1024] = metadata
    return bytes(disk), {
        "disk_guid": str(disk_id),
        "partitions": [
            {"name": name, "first_lba": first, "last_lba": last,
             "unique_guid": str(unique)}
            for name, first, last, unique in partitions
        ],
        "active_slot_sha256": hashlib.sha256(
            disk[active_offset:active_offset + int(slot_a["length"])]
        ).hexdigest(),
    }


def main() -> int:
    """Generate all D03 fixture artifacts below one explicit build root."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--layout-source", type=Path, default=LAYOUT_SOURCE)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    try:
        root = args.output_root
        root.mkdir(parents=True, exist_ok=True)
        update_tool = load_tool("update_manifest.py")
        layout_tool = load_tool("update_layout.py")
        kernel = component_bytes(b"ribon-qemu-update-kernel-v1")
        policy = component_bytes(b"ribon-qemu-update-policy-v1")
        write(root / "kernel.bin", kernel)
        write(root / "policy.bin", policy)
        bundle = kernel + policy
        write(root / "update.bin", bundle)
        source = {
            "architecture_id": "architecture.x86_64",
            "bundle_generation": 2,
            "components": [
                {
                    "bundle_offset": 0,
                    "destination_class": "kernel-slot",
                    "destination_id": "slot.inactive.kernel",
                    "entry_contract_id": "entry.x86_64.direct-v1",
                    "expected_sha256": hashlib.sha256(kernel).hexdigest(),
                    "image_format": "elf64",
                    "logical_id": "system.kernel",
                    "maximum_size": len(kernel),
                    "required": True,
                    "role": "kernel",
                    "source": "kernel.bin",
                },
                {
                    "bundle_offset": len(kernel),
                    "destination_class": "policy-slot",
                    "destination_id": "slot.inactive.policy",
                    "entry_contract_id": "policy.ribos.artifact-v1",
                    "expected_sha256": hashlib.sha256(policy).hexdigest(),
                    "image_format": "opaque",
                    "logical_id": "boot.policy",
                    "maximum_size": len(policy),
                    "required": True,
                    "role": "policy",
                    "source": "policy.bin",
                },
            ],
            "creation_policy_version": 1,
            "environment_id": "environment.uefi",
            "hardware_revision": {"maximum": 1, "minimum": 1},
            "manifest_schema_id": "ribon.update.manifest.v1",
            "mode": "recovery",
            "platform_id": "platform.qemu-q35",
            "predecessor_generation": 1,
            "product_digest_sha256": hashlib.sha256(
                args.product_manifest.read_bytes()
            ).hexdigest(),
            "product_id": "validation.x86_64-uefi-update-recovery",
            "protocol": {"id": "protocol.synthetic-v1", "major": 1, "minor": 0},
            "rollback_domain": "ribon.update.qemu-q35.v1",
            "rollback_sequence": 2,
            "schema": "ribon-update-manifest-source-v1",
        }
        source_path = root / "update-source.json"
        write_json(source_path, source)
        manifest = update_tool.encode_manifest(update_tool.parse_source(source_path))
        write(root / "update.man", manifest)
        message = update_tool.signed_message(manifest, KEY_ID.encode("utf-8"))
        signature = sign_message(message, root)
        envelope = update_tool.encode_envelope(manifest, KEY_ID.encode("utf-8"), signature)
        write(root / "update.sig", envelope)
        layout_model = layout_tool.parse_source(args.layout_source)
        regions = layout_tool.calculate_layout(layout_model)
        identity = layout_tool.encode_identity(layout_model, regions)
        write(root / "layout.bin", identity)
        write_json(root / "layout.json", layout_tool.compose(args.layout_source, root / "update.man"))
        disk, disk_report = build_disk(identity, regions)
        write(root / "update-disk.raw", disk)
        artifacts = {}
        for name in (
            "kernel.bin", "policy.bin", "update.bin", "update-source.json",
            "update.man", "update.sig", "layout.bin", "layout.json", "update-disk.raw",
        ):
            path = root / name
            artifacts[name] = {"bytes": path.stat().st_size,
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        write_json(root / "provenance.json", {
            "schema": PROVENANCE_SCHEMA,
            "artifacts": artifacts,
            "layout_identity_sha256": hashlib.sha256(identity).hexdigest(),
            "media_identity_sha256": MEDIA_ID.hex(),
            "product_manifest_sha256": hashlib.sha256(
                args.product_manifest.read_bytes()
            ).hexdigest(),
            **disk_report,
        })
        print("RIBON-D03-QEMU-UPDATE-FIXTURE-OK")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"qemu-update-fixture: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
