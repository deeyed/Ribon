#!/usr/bin/env python3
"""Inspect one canonical product-bound Ribon trust-message vector."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
from typing import Any


DOMAIN = b"RIBON-TRUST-MESSAGE-V1".ljust(32, b"\0")
MESSAGE_BYTES = 232
SCHEMA = "ribon-trust-message-vector-v1"
EXPECTED_KEYS = {
    "artifact_sha256",
    "envelope_version",
    "expected_message_hex",
    "expected_message_sha256",
    "hash_algorithm",
    "isa_version",
    "key_id_utf8",
    "key_usage",
    "mode",
    "payload_length",
    "product_sha256",
    "rollback_domain_sha256",
    "schema",
    "schema_sha256",
    "sequence",
    "signature_algorithm",
    "trust_version",
    "vm_abi_version",
}


def exact_object(value: Any, keys: set[str], label: str) -> dict[str, Any]:
    """Require a JSON object with one exact key set."""

    if not isinstance(value, dict) or set(value) != keys:
        raise ValueError(f"{label} must contain exactly {sorted(keys)}")
    return value


def u16(value: Any, label: str) -> int:
    """Validate one unsigned 16-bit integer."""

    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xFFFF:
        raise ValueError(f"{label} must be a u16")
    return value


def u64(value: Any, label: str) -> int:
    """Validate one unsigned 64-bit integer."""

    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError(f"{label} must be a u64")
    return value


def version(value: Any, label: str) -> tuple[int, int]:
    """Read an exact major/minor version object."""

    item = exact_object(value, {"major", "minor"}, label)
    return u16(item["major"], f"{label}.major"), u16(
        item["minor"], f"{label}.minor"
    )


def digest(value: Any, label: str) -> bytes:
    """Read one nonzero lowercase SHA-256 spelling."""

    if not isinstance(value, str) or len(value) != 64 or value.lower() != value:
        raise ValueError(f"{label} must be 64 lowercase hex characters")
    try:
        result = bytes.fromhex(value)
    except ValueError as error:
        raise ValueError(f"{label} is not hexadecimal") from error
    if result == bytes(32):
        raise ValueError(f"{label} must not be zero")
    return result


def encode(document: dict[str, Any]) -> bytes:
    """Encode the frozen 232-byte trust-message v1 format independently of C."""

    trust_major, trust_minor = version(document["trust_version"], "trust_version")
    envelope_major, envelope_minor = version(
        document["envelope_version"], "envelope_version"
    )
    vm_major, vm_minor = version(document["vm_abi_version"], "vm_abi_version")
    isa_major, isa_minor = version(document["isa_version"], "isa_version")
    mode = u16(document["mode"], "mode")
    key_usage = u16(document["key_usage"], "key_usage")
    hash_algorithm = u16(document["hash_algorithm"], "hash_algorithm")
    signature_algorithm = u16(
        document["signature_algorithm"], "signature_algorithm"
    )
    if (trust_major, trust_minor) != (1, 0):
        raise ValueError("unsupported trust-message version")
    if (envelope_major, envelope_minor) != (1, 0):
        raise ValueError("unsupported artifact envelope version")
    if (vm_major, vm_minor) != (1, 0) or (isa_major, isa_minor) != (1, 0):
        raise ValueError("unsupported VM ABI or ISA version")
    if hash_algorithm != 1 or signature_algorithm != 1:
        raise ValueError("unsupported hash or signature algorithm")
    if mode not in {1, 2, 3, 4}:
        raise ValueError("invalid mode")
    if key_usage not in {1, 2, 3, 4, 5, 6}:
        raise ValueError("invalid key usage")
    if key_usage > 4 or key_usage != mode:
        raise ValueError("Ribos policy mode and key usage do not match")

    key_id_value = document["key_id_utf8"]
    if not isinstance(key_id_value, str):
        raise ValueError("key_id_utf8 must be a string")
    key_id = key_id_value.encode("utf-8")
    if not 1 <= len(key_id) <= 64 or b"\0" in key_id:
        raise ValueError("key_id_utf8 must encode to 1..64 non-NUL bytes")

    message = bytearray(DOMAIN)
    message.extend(
        struct.pack(
            "<12H2Q",
            trust_major,
            trust_minor,
            envelope_major,
            envelope_minor,
            vm_major,
            vm_minor,
            isa_major,
            isa_minor,
            hash_algorithm,
            signature_algorithm,
            mode,
            key_usage,
            u64(document["sequence"], "sequence"),
            u64(document["payload_length"], "payload_length"),
        )
    )
    message.extend(digest(document["artifact_sha256"], "artifact_sha256"))
    message.extend(digest(document["product_sha256"], "product_sha256"))
    message.extend(digest(document["schema_sha256"], "schema_sha256"))
    message.extend(
        digest(document["rollback_domain_sha256"], "rollback_domain_sha256")
    )
    message.extend(hashlib.sha256(key_id).digest())
    if len(message) != MESSAGE_BYTES:
        raise AssertionError("trust-message encoder size drift")
    return bytes(message)


def load_vector(path: Path) -> tuple[dict[str, Any], bytes]:
    """Load, shape-check, encode and verify one tracked canonical vector."""

    document = exact_object(
        json.loads(path.read_text(encoding="utf-8")), EXPECTED_KEYS, "vector"
    )
    if document["schema"] != SCHEMA:
        raise ValueError("unsupported vector schema")
    message = encode(document)
    expected_hex = document["expected_message_hex"]
    if not isinstance(expected_hex, str) or message.hex() != expected_hex:
        raise ValueError("canonical message does not match expected_message_hex")
    expected_sha256 = document["expected_message_sha256"]
    if hashlib.sha256(message).hexdigest() != expected_sha256:
        raise ValueError("canonical message digest does not match vector")
    return document, message


def main() -> int:
    """Inspect the canonical message without performing cryptographic verification."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vector", type=Path, required=True)
    parser.add_argument("--format", choices=("hex", "json"), default="json")
    args = parser.parse_args()
    document, message = load_vector(args.vector)
    if args.format == "hex":
        print(message.hex())
    else:
        print(
            json.dumps(
                {
                    "key_usage": document["key_usage"],
                    "message_bytes": len(message),
                    "message_hex": message.hex(),
                    "message_sha256": hashlib.sha256(message).hexdigest(),
                    "mode": document["mode"],
                    "schema": SCHEMA,
                    "sequence": document["sequence"],
                },
                indent=2,
                sort_keys=True,
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
