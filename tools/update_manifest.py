#!/usr/bin/env python3
"""Assemble and inspect the canonical Ribon update-manifest v1 format."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


MANIFEST_MAGIC = b"RIBON-UPDATE-MANIFEST-V1".ljust(32, b"\0")
MESSAGE_MAGIC = b"RIBON-UPDATE-MESSAGE-V1".ljust(32, b"\0")
ENVELOPE_MAGIC = b"RIBON-UPDATE-SIGNATURE-V1".ljust(32, b"\0")
MANIFEST_HEADER_BYTES = 256
BINDING_BYTES = 256
COMPONENT_BYTES = 192
COMPONENTS_OFFSET = MANIFEST_HEADER_BYTES + BINDING_BYTES
MAX_COMPONENTS = 16
SIGNED_MESSAGE_BYTES = 256
ENVELOPE_HEADER_BYTES = 160
MAX_KEY_ID_BYTES = 64
ED25519_SIGNATURE_BYTES = 64
HASH_SHA256 = 1
SIGNATURE_ED25519 = 1
UPDATE_MANIFEST_USAGE = 5
SOURCE_SCHEMA = "ribon-update-manifest-source-v1"

MODES = {
    "normal": 1,
    "recovery": 2,
    "provisioning": 3,
    "diagnostic": 4,
}
ROLES = {
    "kernel": 1,
    "boot-module": 2,
    "policy": 3,
    "firmware": 4,
    "recovery-image": 5,
}
DESTINATIONS = {
    "kernel-slot": 1,
    "module-slot": 2,
    "policy-slot": 3,
    "firmware-slot": 4,
    "recovery-slot": 5,
}
ROLE_DESTINATION = {
    1: 1,
    2: 2,
    3: 3,
    4: 4,
    5: 5,
}
IMAGE_FORMATS = {
    "opaque": 1,
    "elf64": 2,
    "pe-coff": 3,
    "linux-image": 4,
    "raw": 5,
}
SOURCE_KEYS = {
    "architecture_id",
    "bundle_generation",
    "components",
    "creation_policy_version",
    "environment_id",
    "hardware_revision",
    "manifest_schema_id",
    "mode",
    "platform_id",
    "predecessor_generation",
    "product_id",
    "protocol",
    "rollback_domain",
    "rollback_sequence",
    "schema",
}
COMPONENT_KEYS = {
    "bundle_offset",
    "destination_class",
    "destination_id",
    "entry_contract_id",
    "expected_sha256",
    "image_format",
    "logical_id",
    "maximum_size",
    "required",
    "role",
    "source",
}


def exact_object(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    """Require one JSON object with exactly the selected keys."""

    if not isinstance(value, dict) or set(value) != keys:
        raise ValueError(f"{label} must contain exactly {sorted(keys)}")
    return value


def integer(value: Any, maximum: int, label: str) -> int:
    """Return one non-negative bounded integer without accepting bool."""

    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > maximum
    ):
        raise ValueError(f"{label} must be an unsigned integer <= {maximum}")
    return value


def text(value: Any, label: str, maximum: int = 128) -> str:
    """Return one bounded non-NUL UTF-8 identity spelling."""

    if not isinstance(value, str):
        raise ValueError(f"{label} must be a string")
    encoded = value.encode("utf-8")
    if not 1 <= len(encoded) <= maximum or b"\0" in encoded:
        raise ValueError(f"{label} must encode to 1..{maximum} non-NUL bytes")
    return value


def digest_id(value: Any, label: str) -> bytes:
    """Hash one bounded stable UTF-8 identity into its wire identity."""

    return hashlib.sha256(text(value, label).encode("utf-8")).digest()


def digest_hex(value: Any, label: str) -> bytes:
    """Read one canonical nonzero lowercase SHA-256 spelling."""

    if not isinstance(value, str) or len(value) != 64 or value.lower() != value:
        raise ValueError(f"{label} must be 64 lowercase hexadecimal characters")
    try:
        result = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError(f"{label} is not hexadecimal") from error
    if result == bytes(32):
        raise ValueError(f"{label} must not be zero")
    return result


def key_id_bytes(value: str) -> bytes:
    """Encode one bounded opaque key ID."""

    encoded = text(value, "key_id", MAX_KEY_ID_BYTES).encode("utf-8")
    return encoded


def read_exact_component(root: Path, component: dict[str, Any], index: int) -> bytes:
    """Read one component once and require its declared SHA-256 and maximum size."""

    source_value = text(component["source"], f"components[{index}].source", 512)
    source = Path(source_value)
    if not source.is_absolute():
        source = root / source
    data = source.read_bytes()
    expected = digest_hex(
        component["expected_sha256"], f"components[{index}].expected_sha256"
    )
    maximum = integer(
        component["maximum_size"],
        0xFFFFFFFFFFFFFFFF,
        f"components[{index}].maximum_size",
    )
    if not data or len(data) > maximum:
        raise ValueError(f"components[{index}] has zero or oversized content")
    if hashlib.sha256(data).digest() != expected:
        raise ValueError(f"components[{index}] SHA-256 does not match exact input")
    return data


def parse_source(path: Path) -> dict[str, Any]:
    """Load and exact-shape-check one source-neutral component manifest."""

    document = exact_object(
        json.loads(path.read_text(encoding="utf-8")), SOURCE_KEYS, "source manifest"
    )
    if document["schema"] != SOURCE_SCHEMA:
        raise ValueError("unsupported source manifest schema")
    if document["mode"] not in MODES:
        raise ValueError("mode is not in the stable registry")
    protocol = exact_object(
        document["protocol"], {"id", "major", "minor"}, "protocol"
    )
    hardware = exact_object(
        document["hardware_revision"], {"minimum", "maximum"}, "hardware_revision"
    )
    minimum_hardware = integer(hardware["minimum"], 0xFFFFFFFF, "hardware minimum")
    maximum_hardware = integer(hardware["maximum"], 0xFFFFFFFF, "hardware maximum")
    if minimum_hardware > maximum_hardware:
        raise ValueError("hardware revision range is reversed")
    components_value = document["components"]
    if not isinstance(components_value, list) or not 1 <= len(components_value) <= MAX_COMPONENTS:
        raise ValueError(f"components must contain 1..{MAX_COMPONENTS} entries")
    components: list[dict[str, Any]] = []
    singleton: set[int] = set()
    ranges: list[tuple[int, int]] = []
    logical_ids: set[bytes] = set()
    for index, value in enumerate(components_value):
        item = exact_object(value, COMPONENT_KEYS, f"components[{index}]")
        role_name = item["role"]
        destination_name = item["destination_class"]
        format_name = item["image_format"]
        if role_name not in ROLES or destination_name not in DESTINATIONS:
            raise ValueError(f"components[{index}] has an unknown role or destination")
        role = ROLES[role_name]
        destination = DESTINATIONS[destination_name]
        if ROLE_DESTINATION[role] != destination:
            raise ValueError(f"components[{index}] role/destination do not match")
        if format_name not in IMAGE_FORMATS:
            raise ValueError(f"components[{index}] has an unknown image format")
        if not isinstance(item["required"], bool):
            raise ValueError(f"components[{index}].required must be bool")
        logical_digest = digest_id(item["logical_id"], f"components[{index}].logical_id")
        if logical_digest in logical_ids:
            raise ValueError("duplicate logical component identity")
        logical_ids.add(logical_digest)
        if role in {1, 3, 5}:
            if role in singleton:
                raise ValueError("duplicate singleton component role")
            singleton.add(role)
        data = read_exact_component(path.parent, item, index)
        offset = integer(
            item["bundle_offset"],
            0xFFFFFFFFFFFFFFFF,
            f"components[{index}].bundle_offset",
        )
        if offset > 0xFFFFFFFFFFFFFFFF - len(data):
            raise ValueError(f"components[{index}] byte range wraps")
        end = offset + len(data)
        if any(offset < previous_end and previous_offset < end for previous_offset, previous_end in ranges):
            raise ValueError("component byte ranges overlap")
        ranges.append((offset, end))
        components.append(
            {
                "bundle_offset": offset,
                "content_digest": hashlib.sha256(data).digest(),
                "destination": destination,
                "destination_id_digest": digest_id(
                    item["destination_id"], f"components[{index}].destination_id"
                ),
                "entry_contract_digest": digest_id(
                    item["entry_contract_id"],
                    f"components[{index}].entry_contract_id",
                ),
                "exact_size": len(data),
                "flags": 1 if item["required"] else 0,
                "format": IMAGE_FORMATS[format_name],
                "install_order": index,
                "logical_id_digest": logical_digest,
                "maximum_size": integer(
                    item["maximum_size"],
                    0xFFFFFFFFFFFFFFFF,
                    f"components[{index}].maximum_size",
                ),
                "role": role,
            }
        )
    return {
        "architecture_digest": digest_id(document["architecture_id"], "architecture_id"),
        "bundle_generation": integer(
            document["bundle_generation"], 0xFFFFFFFFFFFFFFFF, "bundle_generation"
        ),
        "components": components,
        "creation_policy_version": integer(
            document["creation_policy_version"],
            0xFFFFFFFFFFFFFFFF,
            "creation_policy_version",
        ),
        "environment_digest": digest_id(document["environment_id"], "environment_id"),
        "hardware_maximum": maximum_hardware,
        "hardware_minimum": minimum_hardware,
        "mode": MODES[document["mode"]],
        "platform_digest": digest_id(document["platform_id"], "platform_id"),
        "predecessor_generation": integer(
            document["predecessor_generation"],
            0xFFFFFFFFFFFFFFFF,
            "predecessor_generation",
        ),
        "product_digest": digest_id(document["product_id"], "product_id"),
        "protocol_digest": digest_id(protocol["id"], "protocol.id"),
        "protocol_major": integer(protocol["major"], 0xFFFF, "protocol.major"),
        "protocol_minor": integer(protocol["minor"], 0xFFFF, "protocol.minor"),
        "rollback_domain_digest": digest_id(document["rollback_domain"], "rollback_domain"),
        "rollback_sequence": integer(
            document["rollback_sequence"],
            0xFFFFFFFFFFFFFFFF,
            "rollback_sequence",
        ),
        "schema_digest": digest_id(document["manifest_schema_id"], "manifest_schema_id"),
    }


def encode_manifest(model: dict[str, Any]) -> bytes:
    """Encode one validated model into the canonical little-endian manifest."""

    components = model["components"]
    generation = model["bundle_generation"]
    predecessor = model["predecessor_generation"]
    if generation == 0 or predecessor >= generation:
        raise ValueError("bundle generation must be positive and exceed predecessor")
    total_size = COMPONENTS_OFFSET + len(components) * COMPONENT_BYTES
    output = bytearray(total_size)
    output[:32] = MANIFEST_MAGIC
    struct.pack_into(
        "<HHIQIIQQQQHHHH",
        output,
        32,
        1,
        0,
        MANIFEST_HEADER_BYTES,
        total_size,
        2,
        len(components),
        generation,
        predecessor,
        model["rollback_sequence"],
        model["creation_policy_version"],
        HASH_SHA256,
        SIGNATURE_ED25519,
        model["mode"],
        0,
    )
    output[96:128] = model["schema_digest"]
    struct.pack_into("<IIQQII", output, 128, 1, 0, 256, 256, 1, 256)
    struct.pack_into(
        "<IIQQII",
        output,
        160,
        2,
        0,
        COMPONENTS_OFFSET,
        len(components) * COMPONENT_BYTES,
        len(components),
        COMPONENT_BYTES,
    )
    binding = MANIFEST_HEADER_BYTES
    output[binding:binding + 32] = model["product_digest"]
    output[binding + 32:binding + 64] = model["architecture_digest"]
    output[binding + 64:binding + 96] = model["platform_digest"]
    output[binding + 96:binding + 128] = model["environment_digest"]
    output[binding + 128:binding + 160] = model["protocol_digest"]
    output[binding + 160:binding + 192] = model["rollback_domain_digest"]
    struct.pack_into(
        "<HHII",
        output,
        binding + 192,
        model["protocol_major"],
        model["protocol_minor"],
        model["hardware_minimum"],
        model["hardware_maximum"],
    )
    for index, component in enumerate(components):
        row = COMPONENTS_OFFSET + index * COMPONENT_BYTES
        output[row:row + 32] = component["logical_id_digest"]
        output[row + 32:row + 64] = component["content_digest"]
        output[row + 64:row + 96] = component["destination_id_digest"]
        output[row + 96:row + 128] = component["entry_contract_digest"]
        struct.pack_into(
            "<QQQHHHHI",
            output,
            row + 128,
            component["bundle_offset"],
            component["exact_size"],
            component["maximum_size"],
            component["role"],
            component["destination"],
            component["format"],
            component["flags"],
            component["install_order"],
        )
    validate_manifest(bytes(output))
    return bytes(output)


def u16(data: bytes, offset: int) -> int:
    """Read one little-endian u16."""

    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    """Read one little-endian u32."""

    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    """Read one little-endian u64."""

    return struct.unpack_from("<Q", data, offset)[0]


def nonzero_digest(data: bytes, offset: int, label: str) -> bytes:
    """Read one nonzero 32-byte identity from a checked range."""

    value = data[offset:offset + 32]
    if len(value) != 32 or value == bytes(32):
        raise ValueError(f"{label} is zero or truncated")
    return value


def validate_manifest(data: bytes) -> dict[str, Any]:
    """Independently derive and validate every manifest range and scalar."""

    minimum = COMPONENTS_OFFSET + COMPONENT_BYTES
    maximum = COMPONENTS_OFFSET + MAX_COMPONENTS * COMPONENT_BYTES
    if not minimum <= len(data) <= maximum or data[:32] != MANIFEST_MAGIC:
        raise ValueError("manifest magic or total bound is invalid")
    if (u16(data, 32), u16(data, 34)) != (1, 0):
        raise ValueError("unsupported manifest version")
    if u32(data, 36) != MANIFEST_HEADER_BYTES or u64(data, 40) != len(data):
        raise ValueError("manifest header or total size is noncanonical")
    count = u32(data, 52)
    expected_size = COMPONENTS_OFFSET + count * COMPONENT_BYTES
    if u32(data, 48) != 2 or not 1 <= count <= MAX_COMPONENTS or expected_size != len(data):
        raise ValueError("manifest section or component count is invalid")
    if u64(data, 56) == 0 or u64(data, 64) >= u64(data, 56):
        raise ValueError("manifest generation relation is invalid")
    if u16(data, 88) != HASH_SHA256 or u16(data, 90) != SIGNATURE_ED25519:
        raise ValueError("unsupported manifest algorithm")
    if u16(data, 92) not in MODES.values() or u16(data, 94) != 0:
        raise ValueError("manifest mode or flags are invalid")
    schema_digest = nonzero_digest(data, 96, "schema digest")
    if data[128:160] != struct.pack("<IIQQII", 1, 0, 256, 256, 1, 256):
        raise ValueError("binding directory entry is noncanonical")
    if data[160:192] != struct.pack(
        "<IIQQII", 2, 0, COMPONENTS_OFFSET, count * COMPONENT_BYTES, count, COMPONENT_BYTES
    ):
        raise ValueError("component directory entry is noncanonical")
    if any(data[192:256]):
        raise ValueError("unused directory entries are nonzero")
    binding = MANIFEST_HEADER_BYTES
    product_digest = nonzero_digest(data, binding, "product digest")
    architecture_digest = nonzero_digest(data, binding + 32, "architecture digest")
    platform_digest = nonzero_digest(data, binding + 64, "platform digest")
    environment_digest = nonzero_digest(data, binding + 96, "environment digest")
    protocol_digest = nonzero_digest(data, binding + 128, "protocol digest")
    rollback_domain_digest = nonzero_digest(data, binding + 160, "domain digest")
    if u32(data, binding + 196) > u32(data, binding + 200) or any(data[binding + 204:binding + 256]):
        raise ValueError("binding hardware range or reserved bytes are invalid")
    logical_ids: set[bytes] = set()
    singleton: set[int] = set()
    ranges: list[tuple[int, int]] = []
    components: list[dict[str, Any]] = []
    for index in range(count):
        row = COMPONENTS_OFFSET + index * COMPONENT_BYTES
        logical_id = nonzero_digest(data, row, "logical ID")
        content = nonzero_digest(data, row + 32, "content digest")
        destination_id = nonzero_digest(data, row + 64, "destination ID")
        entry_contract = nonzero_digest(data, row + 96, "entry contract")
        offset, exact_size, maximum_size = struct.unpack_from("<QQQ", data, row + 128)
        role, destination, image_format, flags, install_order = struct.unpack_from(
            "<HHHHI", data, row + 152
        )
        if (
            role not in ROLE_DESTINATION
            or ROLE_DESTINATION[role] != destination
            or image_format not in IMAGE_FORMATS.values()
            or flags & ~1
            or install_order != index
            or exact_size == 0
            or exact_size > maximum_size
            or offset > 0xFFFFFFFFFFFFFFFF - exact_size
            or any(data[row + 164:row + COMPONENT_BYTES])
        ):
            raise ValueError(f"component {index} scalar or reserved bytes are invalid")
        if logical_id in logical_ids:
            raise ValueError("duplicate logical component identity")
        logical_ids.add(logical_id)
        if role in {1, 3, 5}:
            if role in singleton:
                raise ValueError("duplicate singleton component role")
            singleton.add(role)
        end = offset + exact_size
        if any(offset < old_end and old_offset < end for old_offset, old_end in ranges):
            raise ValueError("component byte ranges overlap")
        ranges.append((offset, end))
        components.append(
            {
                "bundle_offset": offset,
                "content_sha256": content.hex(),
                "destination_class": destination,
                "destination_id_sha256": destination_id.hex(),
                "entry_contract_sha256": entry_contract.hex(),
                "exact_size": exact_size,
                "flags": flags,
                "image_format": image_format,
                "install_order": install_order,
                "logical_id_sha256": logical_id.hex(),
                "maximum_size": maximum_size,
                "role": role,
            }
        )
    return {
        "architecture_sha256": architecture_digest.hex(),
        "bundle_generation": u64(data, 56),
        "component_count": count,
        "components": components,
        "creation_policy_version": u64(data, 80),
        "environment_sha256": environment_digest.hex(),
        "hardware_revision": {
            "maximum": u32(data, binding + 200),
            "minimum": u32(data, binding + 196),
        },
        "manifest_bytes": len(data),
        "manifest_sha256": hashlib.sha256(data).hexdigest(),
        "mode": u16(data, 92),
        "platform_sha256": platform_digest.hex(),
        "predecessor_generation": u64(data, 64),
        "product_sha256": product_digest.hex(),
        "protocol": {
            "id_sha256": protocol_digest.hex(),
            "major": u16(data, binding + 192),
            "minor": u16(data, binding + 194),
        },
        "rollback_domain_sha256": rollback_domain_digest.hex(),
        "rollback_sequence": u64(data, 72),
        "schema_sha256": schema_digest.hex(),
    }


def signed_message(data: bytes, key_id: bytes) -> bytes:
    """Build the update-only 256-byte signature input independently of C."""

    view = validate_manifest(data)
    output = bytearray(SIGNED_MESSAGE_BYTES)
    output[:32] = MESSAGE_MAGIC
    struct.pack_into(
        "<8HII5Q",
        output,
        32,
        1,
        0,
        1,
        0,
        HASH_SHA256,
        SIGNATURE_ED25519,
        view["mode"],
        UPDATE_MANIFEST_USAGE,
        0,
        0,
        view["rollback_sequence"],
        len(data),
        view["bundle_generation"],
        view["predecessor_generation"],
        view["creation_policy_version"],
    )
    output[96:128] = hashlib.sha256(data).digest()
    output[128:160] = bytes.fromhex(view["product_sha256"])
    output[160:192] = bytes.fromhex(view["schema_sha256"])
    output[192:224] = bytes.fromhex(view["rollback_domain_sha256"])
    output[224:256] = hashlib.sha256(key_id).digest()
    return bytes(output)


def encode_envelope(data: bytes, key_id: bytes, signature: bytes) -> bytes:
    """Attach one detached signature without interpreting private-key material."""

    view = validate_manifest(data)
    if len(signature) != ED25519_SIGNATURE_BYTES:
        raise ValueError("Ed25519 signature must contain exactly 64 bytes")
    message = signed_message(data, key_id)
    signature_offset = ENVELOPE_HEADER_BYTES + len(key_id)
    total_size = signature_offset + len(signature)
    output = bytearray(total_size)
    output[:32] = ENVELOPE_MAGIC
    struct.pack_into(
        "<HHIQQ6H I QQ",
        output,
        32,
        1,
        0,
        ENVELOPE_HEADER_BYTES,
        total_size,
        len(data),
        HASH_SHA256,
        SIGNATURE_ED25519,
        UPDATE_MANIFEST_USAGE,
        view["mode"],
        len(key_id),
        len(signature),
        0,
        ENVELOPE_HEADER_BYTES,
        signature_offset,
    )
    output[88:120] = hashlib.sha256(data).digest()
    output[120:152] = hashlib.sha256(message).digest()
    output[ENVELOPE_HEADER_BYTES:signature_offset] = key_id
    output[signature_offset:] = signature
    return bytes(output)


def validate_envelope(data: bytes) -> dict[str, Any]:
    """Read one detached envelope without trusting its encoded offsets."""

    if (
        not ENVELOPE_HEADER_BYTES + 1 + ED25519_SIGNATURE_BYTES
        <= len(data)
        <= ENVELOPE_HEADER_BYTES + MAX_KEY_ID_BYTES + ED25519_SIGNATURE_BYTES
        or data[:32] != ENVELOPE_MAGIC
    ):
        raise ValueError("signature envelope magic or size is invalid")
    if (u16(data, 32), u16(data, 34)) != (1, 0):
        raise ValueError("unsupported signature envelope version")
    if (
        u32(data, 36) != ENVELOPE_HEADER_BYTES
        or u64(data, 40) != len(data)
        or u64(data, 48) == 0
        or u16(data, 56) != HASH_SHA256
        or u16(data, 58) != SIGNATURE_ED25519
        or u16(data, 60) != UPDATE_MANIFEST_USAGE
        or u16(data, 62) not in MODES.values()
        or u32(data, 68) != 0
        or any(data[152:160])
    ):
        raise ValueError("signature envelope header is noncanonical")
    key_size = u16(data, 64)
    signature_size = u16(data, 66)
    key_offset = u64(data, 72)
    signature_offset = u64(data, 80)
    if (
        not 1 <= key_size <= MAX_KEY_ID_BYTES
        or signature_size != ED25519_SIGNATURE_BYTES
        or key_offset != ENVELOPE_HEADER_BYTES
        or signature_offset != key_offset + key_size
        or signature_offset + signature_size != len(data)
    ):
        raise ValueError("signature envelope ranges are noncanonical")
    key_id = data[key_offset:signature_offset]
    if b"\0" in key_id or data[88:120] == bytes(32) or data[120:152] == bytes(32):
        raise ValueError("signature envelope identities are invalid")
    return {
        "envelope_bytes": len(data),
        "envelope_sha256": hashlib.sha256(data).hexdigest(),
        "key_id_utf8": key_id.decode("utf-8", errors="strict"),
        "manifest_bytes": u64(data, 48),
        "manifest_sha256": data[88:120].hex(),
        "message_sha256": data[120:152].hex(),
        "mode": u16(data, 62),
        "signature_hex": data[signature_offset:].hex(),
    }


def command_assemble(args: argparse.Namespace) -> None:
    """Assemble one source-neutral manifest and report its immutable identity."""

    output = encode_manifest(parse_source(args.source))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(
        "RIBON-UPDATE-MANIFEST-ASSEMBLE-OK "
        f"bytes={len(output)} sha256={hashlib.sha256(output).hexdigest()}"
    )


def command_inspect(args: argparse.Namespace) -> None:
    """Inspect one manifest without modifying it."""

    data = args.manifest.read_bytes()
    view = validate_manifest(data)
    if args.format == "hex":
        print(data.hex())
    else:
        print(json.dumps(view, indent=2, sort_keys=True))


def command_message(args: argparse.Namespace) -> None:
    """Emit the exact offline-signer input for one manifest and key ID."""

    message = signed_message(args.manifest.read_bytes(), key_id_bytes(args.key_id))
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(message)
    else:
        print(message.hex())


def command_envelope(args: argparse.Namespace) -> None:
    """Attach a caller-supplied detached signature to one canonical manifest."""

    if (args.signature_file is None) == (args.signature_hex is None):
        raise ValueError("choose exactly one of --signature-file or --signature-hex")
    signature = (
        args.signature_file.read_bytes()
        if args.signature_file is not None
        else bytes.fromhex(args.signature_hex)
    )
    envelope = encode_envelope(
        args.manifest.read_bytes(), key_id_bytes(args.key_id), signature
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(envelope)
    print(
        "RIBON-UPDATE-SIGNATURE-ENVELOPE-OK "
        f"bytes={len(envelope)} sha256={hashlib.sha256(envelope).hexdigest()}"
    )


def command_inspect_envelope(args: argparse.Namespace) -> None:
    """Inspect one detached envelope without verifying its signature."""

    print(json.dumps(validate_envelope(args.envelope.read_bytes()), indent=2, sort_keys=True))


def parser() -> argparse.ArgumentParser:
    """Build the bounded host-tool command surface."""

    result = argparse.ArgumentParser(description=__doc__)
    subcommands = result.add_subparsers(required=True)
    assemble = subcommands.add_parser("assemble", help="assemble a canonical manifest")
    assemble.add_argument("--source", type=Path, required=True)
    assemble.add_argument("--output", type=Path, required=True)
    assemble.set_defaults(function=command_assemble)
    inspect = subcommands.add_parser("inspect", help="inspect a canonical manifest")
    inspect.add_argument("--manifest", type=Path, required=True)
    inspect.add_argument("--format", choices=("hex", "json"), default="json")
    inspect.set_defaults(function=command_inspect)
    message = subcommands.add_parser("message", help="emit offline-signer input")
    message.add_argument("--manifest", type=Path, required=True)
    message.add_argument("--key-id", required=True)
    message.add_argument("--output", type=Path)
    message.set_defaults(function=command_message)
    envelope = subcommands.add_parser("envelope", help="attach a detached signature")
    envelope.add_argument("--manifest", type=Path, required=True)
    envelope.add_argument("--key-id", required=True)
    envelope.add_argument("--signature-file", type=Path)
    envelope.add_argument("--signature-hex")
    envelope.add_argument("--output", type=Path, required=True)
    envelope.set_defaults(function=command_envelope)
    inspect_envelope = subcommands.add_parser(
        "inspect-envelope", help="inspect a detached signature envelope"
    )
    inspect_envelope.add_argument("--envelope", type=Path, required=True)
    inspect_envelope.set_defaults(function=command_inspect_envelope)
    return result


def main() -> int:
    """Run one deterministic host-tool operation with fail-closed diagnostics."""

    arguments = parser().parse_args()
    try:
        arguments.function(arguments)
    except (OSError, UnicodeError, ValueError, struct.error) as error:
        print(f"RIBON-UPDATE-MANIFEST-FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
