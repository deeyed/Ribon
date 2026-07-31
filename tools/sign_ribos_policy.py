#!/usr/bin/env python3
"""Sign one canonical Ribos policy trust message with an offline Ed25519 seed."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ENVELOPE_BYTES = 128
PAYLOAD_SCHEMA_DIGEST_OFFSET = 96
SIGNED_FLAG = 1
HASH_SHA256 = 1
ED25519_ALGORITHM = 1
SIGNATURE_BYTES = 64
TRUST_MESSAGE_BYTES = 232
TRUST_DOMAIN = b"RIBON-TRUST-MESSAGE-V1".ljust(32, b"\0")
PKCS8_ED25519_SEED_PREFIX = bytes.fromhex("302e020100300506032b657004220420")
SPKI_ED25519_PREFIX = bytes.fromhex("302a300506032b6570032100")
MODES = {
    "normal": 1,
    "recovery": 2,
    "provisioning": 3,
    "diagnostic": 4,
}


def u16(data: bytes, offset: int) -> int:
    """Read one little-endian u16."""

    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    """Read one little-endian u32."""

    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    """Read one little-endian u64."""

    return struct.unpack_from("<Q", data, offset)[0]


def load_seed(path: Path) -> bytes:
    """Load exactly one host-only 32-byte hexadecimal Ed25519 seed."""

    try:
        seed = bytes.fromhex("".join(path.read_text(encoding="ascii").split()))
    except ValueError as error:
        raise ValueError("private seed fixture is not hexadecimal") from error
    if len(seed) != 32:
        raise ValueError("private seed fixture must contain exactly 32 bytes")
    return seed


def unsigned_view(unsigned: bytes) -> tuple[bytes, bytes, tuple[int, int, int, int]]:
    """Validate an unsigned artifact and return payload identities and versions."""

    if (
        len(unsigned) < ENVELOPE_BYTES + 160
        or unsigned[:8] != b"RIBOSA1\0"
        or u16(unsigned, 8) != 1
        or u16(unsigned, 10) != 0
        or u32(unsigned, 12) != ENVELOPE_BYTES
        or u32(unsigned, 16) != 0
        or u16(unsigned, 20) != HASH_SHA256
        or u16(unsigned, 22) != 0
        or u64(unsigned, 24) != ENVELOPE_BYTES
        or u64(unsigned, 40) != len(unsigned)
        or u32(unsigned, 48) != 0
        or u32(unsigned, 52) != 0
        or u64(unsigned, 56) != len(unsigned)
        or u64(unsigned, 64) != len(unsigned)
        or any(unsigned[104:128])
    ):
        raise ValueError("input is not a canonical unsigned Ribos artifact")
    payload_length = u64(unsigned, 32)
    if ENVELOPE_BYTES + payload_length != len(unsigned):
        raise ValueError("unsigned artifact payload bounds are inconsistent")
    payload = unsigned[ENVELOPE_BYTES:]
    if payload[:8] != b"RIBBC01\0" or u32(payload, 16) != 160:
        raise ValueError("input payload is not Ribos bytecode artifact v1")
    payload_digest = hashlib.sha256(payload).digest()
    if payload_digest != unsigned[72:104]:
        raise ValueError("input payload digest does not match its envelope")
    schema_digest = payload[
        PAYLOAD_SCHEMA_DIGEST_OFFSET:PAYLOAD_SCHEMA_DIGEST_OFFSET + 32
    ]
    if schema_digest == bytes(32):
        raise ValueError("input payload schema digest is zero")
    return payload_digest, schema_digest, (
        u16(payload, 8),
        u16(payload, 10),
        u16(payload, 12),
        u16(payload, 14),
    )


def trust_message(
    unsigned: bytes,
    product_manifest: bytes,
    key_id: bytes,
    rollback_domain: bytes,
    sequence: int,
    mode: int,
) -> bytes:
    """Encode the frozen RIBON-TRUST-MESSAGE-V1 bytes independently of C."""

    artifact_digest, schema_digest, versions = unsigned_view(unsigned)
    if not 1 <= len(key_id) <= 64 or b"\0" in key_id:
        raise ValueError("key ID must be 1..64 non-NUL bytes")
    if not product_manifest or not rollback_domain:
        raise ValueError("product manifest and rollback domain must be non-empty")
    if not 0 <= sequence <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("sequence must be a u64")
    payload_length = u64(unsigned, 32)
    vm_major, vm_minor, isa_major, isa_minor = versions
    message = bytearray(TRUST_DOMAIN)
    message.extend(
        struct.pack(
            "<12H2Q",
            1,
            0,
            1,
            0,
            vm_major,
            vm_minor,
            isa_major,
            isa_minor,
            HASH_SHA256,
            ED25519_ALGORITHM,
            mode,
            mode,
            sequence,
            payload_length,
        )
    )
    message.extend(artifact_digest)
    message.extend(hashlib.sha256(product_manifest).digest())
    message.extend(schema_digest)
    message.extend(hashlib.sha256(rollback_domain).digest())
    message.extend(hashlib.sha256(key_id).digest())
    if len(message) != TRUST_MESSAGE_BYTES:
        raise AssertionError("trust-message size drift")
    return bytes(message)


def openssl_sign(
    openssl: str,
    seed: bytes,
    message: bytes,
) -> tuple[bytes, bytes]:
    """Use the independent OpenSSL Ed25519 implementation offline."""

    with tempfile.TemporaryDirectory(prefix="ribon-ed25519-sign-") as directory:
        root = Path(directory)
        private_key = root / "private.der"
        public_key = root / "public.der"
        message_path = root / "message.bin"
        signature_path = root / "signature.bin"
        private_key.write_bytes(PKCS8_ED25519_SEED_PREFIX + seed)
        message_path.write_bytes(message)
        subprocess.run(
            [
                openssl,
                "pkey",
                "-in",
                str(private_key),
                "-inform",
                "DER",
                "-pubout",
                "-outform",
                "DER",
                "-out",
                str(public_key),
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [
                openssl,
                "pkeyutl",
                "-sign",
                "-rawin",
                "-inkey",
                str(private_key),
                "-keyform",
                "DER",
                "-in",
                str(message_path),
                "-out",
                str(signature_path),
            ],
            check=True,
            capture_output=True,
        )
        public_der = public_key.read_bytes()
        signature = signature_path.read_bytes()
        if not public_der.startswith(SPKI_ED25519_PREFIX) or len(public_der) != 44:
            raise ValueError("OpenSSL returned an unexpected Ed25519 public key")
        if len(signature) != SIGNATURE_BYTES:
            raise ValueError("OpenSSL returned an unexpected Ed25519 signature")
        subprocess.run(
            [
                openssl,
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                str(public_key),
                "-keyform",
                "DER",
                "-sigfile",
                str(signature_path),
                "-in",
                str(message_path),
            ],
            check=True,
            capture_output=True,
        )
        return public_der[len(SPKI_ED25519_PREFIX):], signature


def signed_artifact(unsigned: bytes, key_id: bytes, signature: bytes) -> bytes:
    """Attach one verified Ed25519 signature without changing payload bytes."""

    output = bytearray(unsigned)
    key_offset = len(unsigned)
    signature_offset = key_offset + len(key_id)
    total_length = signature_offset + len(signature)
    struct.pack_into("<I", output, 16, SIGNED_FLAG)
    struct.pack_into("<H", output, 22, ED25519_ALGORITHM)
    struct.pack_into("<Q", output, 40, key_offset)
    struct.pack_into("<I", output, 48, len(key_id))
    struct.pack_into("<I", output, 52, len(signature))
    struct.pack_into("<Q", output, 56, signature_offset)
    struct.pack_into("<Q", output, 64, total_length)
    output.extend(key_id)
    output.extend(signature)
    return bytes(output)


def main() -> int:
    """Sign without logging the seed, private DER, message, or signature bytes."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--private-seed", type=Path, required=True)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--rollback-domain", required=True)
    parser.add_argument("--sequence", type=int, required=True)
    parser.add_argument("--mode", choices=sorted(MODES), required=True)
    parser.add_argument("--expected-public-key")
    parser.add_argument("--expected-sha256", type=Path)
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()

    unsigned = args.input.read_bytes()
    key_id = args.key_id.encode("utf-8")
    message = trust_message(
        unsigned,
        args.product_manifest.read_bytes(),
        key_id,
        args.rollback_domain.encode("utf-8"),
        args.sequence,
        MODES[args.mode],
    )
    public_key, signature = openssl_sign(
        args.openssl,
        load_seed(args.private_seed),
        message,
    )
    if args.expected_public_key is not None and (
        public_key.hex() != args.expected_public_key.lower()
    ):
        raise SystemExit("RIBOS-SIGN-PUBLIC-KEY-MISMATCH")
    output = signed_artifact(unsigned, key_id, signature)
    digest = hashlib.sha256(output).hexdigest()
    if args.expected_sha256 is not None:
        expected = args.expected_sha256.read_text(encoding="ascii").strip()
        if digest != expected:
            raise SystemExit(
                f"RIBOS-R18-GOLDEN-FAIL expected={expected} observed={digest}"
            )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(
        f"RIBOS-POLICY-SIGN-OK sha256={digest} bytes={len(output)} "
        "algorithm=ed25519 signer=openssl-offline"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
