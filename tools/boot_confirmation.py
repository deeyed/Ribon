#!/usr/bin/env python3
"""Encode the canonical Ribon boot-confirmation envelope v1."""

from __future__ import annotations

import hashlib
import struct


HEADER_BYTES = 256
SIGNATURE_BYTES = 64
MAX_WIRE_BYTES = 2048
MAGIC = b"RIBON-BOOT-CONFIRM-ENV-V1".ljust(32, b"\0")


def encode(
    *,
    product_id: bytes,
    protocol_id: bytes,
    key_id: bytes,
    health_payload: bytes,
    slot_id: int,
    protocol_major: int,
    protocol_minor: int,
    policy_version: int,
    image_generation: int,
    manifest_sequence: int,
    attempt_sequence: int,
    manifest_digest: bytes,
    nonce: bytes,
    signature: bytes,
) -> bytes:
    """Encode one exact little-endian envelope without native-layout reuse."""

    if not (1 <= len(product_id) <= 128 and b"\0" not in product_id):
        raise ValueError("product_id must be 1..128 non-NUL bytes")
    if not (1 <= len(protocol_id) <= 64 and b"\0" not in protocol_id):
        raise ValueError("protocol_id must be 1..64 non-NUL bytes")
    if not (1 <= len(key_id) <= 64 and b"\0" not in key_id):
        raise ValueError("key_id must be 1..64 non-NUL bytes")
    if not (1 <= len(health_payload) <= 1024):
        raise ValueError("health_payload must be 1..1024 bytes")
    if slot_id not in (0, 1):
        raise ValueError("slot_id must be 0 or 1")
    if min(protocol_major, policy_version, image_generation,
           manifest_sequence, attempt_sequence) <= 0:
        raise ValueError("version, generation, and sequence fields must be positive")
    if len(manifest_digest) != 32 or not any(manifest_digest):
        raise ValueError("manifest_digest must be one nonzero SHA-256")
    if len(nonce) != 32 or not any(nonce):
        raise ValueError("nonce must be one nonzero 32-byte value")
    if len(signature) != SIGNATURE_BYTES:
        raise ValueError("signature must be exactly 64 bytes")
    product_offset = HEADER_BYTES
    protocol_offset = product_offset + len(product_id)
    key_offset = protocol_offset + len(protocol_id)
    health_offset = key_offset + len(key_id)
    signature_offset = health_offset + len(health_payload)
    total = signature_offset + SIGNATURE_BYTES
    if total > MAX_WIRE_BYTES:
        raise ValueError("envelope exceeds canonical capacity")
    output = bytearray(total)
    output[:32] = MAGIC
    struct.pack_into("<HHIIIIIII", output, 32, 1, 0, HEADER_BYTES, total,
                     slot_id, protocol_major, protocol_minor, policy_version, 0)
    struct.pack_into("<QQQ", output, 64, image_generation,
                     manifest_sequence, attempt_sequence)
    struct.pack_into("<HHHHIIIIIII", output, 88,
                     len(product_id), len(protocol_id), len(key_id), 0,
                     len(health_payload), product_offset, protocol_offset,
                     key_offset, health_offset, signature_offset, SIGNATURE_BYTES)
    output[128:160] = manifest_digest
    output[160:192] = nonce
    output[192:224] = hashlib.sha256(health_payload).digest()
    output[product_offset:protocol_offset] = product_id
    output[protocol_offset:key_offset] = protocol_id
    output[key_offset:health_offset] = key_id
    output[health_offset:signature_offset] = health_payload
    output[signature_offset:] = signature
    return bytes(output)


def authenticated_message(envelope: bytes) -> bytes:
    """Return the exact signature message after structural bounds checks."""

    if len(envelope) < HEADER_BYTES or envelope[:32] != MAGIC:
        raise ValueError("malformed boot-confirmation envelope")
    total = struct.unpack_from("<I", envelope, 40)[0]
    signature_offset, signature_size = struct.unpack_from("<II", envelope, 116)
    if total != len(envelope) or signature_size != SIGNATURE_BYTES or \
            signature_offset + signature_size != len(envelope):
        raise ValueError("noncanonical signature range")
    return envelope[:signature_offset]


def attach_signature(envelope: bytes, signature: bytes) -> bytes:
    """Replace only the canonical final signature bytes."""

    if len(signature) != SIGNATURE_BYTES:
        raise ValueError("signature must be exactly 64 bytes")
    message = authenticated_message(envelope)
    return message + signature
