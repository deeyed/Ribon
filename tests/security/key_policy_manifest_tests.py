#!/usr/bin/env python3
"""Exercise the product manifest to immutable key-store composition boundary."""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile


def load_module(name: str, path: Path):
    """Load one source-owned tool without requiring a Python package layout."""

    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_manifest(directory: Path, document: dict[str, object]) -> Path:
    """Write one deterministic source manifest inside a disposable directory."""

    path = directory / "product.json"
    path.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return path


def reject(composer, document: dict[str, object]) -> None:
    """Require one manifest mutation to fail at composition time."""

    with tempfile.TemporaryDirectory(prefix="ribon-key-policy-negative-") as name:
        path = write_manifest(Path(name), document)
        try:
            composer.load_manifest(path, "x86_64")
        except ValueError:
            return
    raise RuntimeError("invalid key-policy manifest mutation was accepted")


def independent_digest(loaded: dict[str, object]) -> bytes:
    """Reproduce the normative pointer-free serialization independently."""

    policy = loaded["key_policy"]
    records = loaded["_key_policy_records"]
    product_digest = loaded["_source_manifest_digest"]
    assert isinstance(policy, dict)
    assert isinstance(records, list)
    assert isinstance(product_digest, bytes)
    payload = bytearray(b"RIBON-KEY-STORE-V1".ljust(32, b"\0"))
    payload.extend(struct.pack("<HHIQ", 1, 0, len(records), policy["generation"]))
    payload.extend(hashlib.sha256(str(policy["id"]).encode("ascii")).digest())
    lifecycle = {"active": 1, "retiring": 2, "revoked": 3}
    for record in records:
        assert isinstance(record, dict)
        public_key = bytes.fromhex(str(record["public_key_hex"]))
        issuer = record["issuer"]
        domains = record["domain_digests"]
        assert isinstance(domains, list)
        payload.extend(hashlib.sha256(str(record["id"]).encode("ascii")).digest())
        payload.extend(public_key)
        payload.extend(hashlib.sha256(public_key).digest())
        payload.extend(product_digest)
        payload.extend(struct.pack("<Q", record["usage_mask"]))
        payload.extend(struct.pack("<I", record["mode_mask"]))
        payload.extend(struct.pack("<I", lifecycle[str(record["status"])]))
        payload.extend(struct.pack("<I", 1 if issuer is None else 0))
        payload.extend(struct.pack(
            "<QQ",
            record["minimum_sequence"],
            record["maximum_sequence"],
        ))
        payload.extend(
            bytes(32) if issuer is None
            else hashlib.sha256(str(issuer).encode("ascii")).digest()
        )
        payload.extend(struct.pack(
            "<II",
            record["delegation_depth"],
            len(domains),
        ))
        for digest in domains:
            assert isinstance(digest, bytes)
            payload.extend(digest)
    return hashlib.sha256(payload).digest()


def record(
    key_id: str,
    public_byte: int,
    issuer: str | None,
    status: str = "active",
    minimum: int = 18,
    maximum: int = 40,
) -> dict[str, object]:
    """Build one test-only normal-policy record with a unique public key."""

    return {
        "id": key_id,
        "issuer": issuer,
        "maximum_sequence": maximum,
        "minimum_sequence": minimum,
        "modes": ["normal"],
        "public_key_hex": (bytes([public_byte]) * 32).hex(),
        "rollback_domains": ["ribon.policy.ribos-qemu-validation.v1"],
        "status": status,
        "usages": ["policy-normal"],
    }


def main() -> int:
    """Validate deterministic generation and hostile manifest graph rejection."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--composer", type=Path, required=True)
    parser.add_argument("--manifest-tool", type=Path, required=True)
    parser.add_argument("--host-manifest", type=Path, required=True)
    args = parser.parse_args()
    composer = load_module("ribon_key_policy_composer", args.composer)
    maker = load_module("ribon_key_policy_manifest_maker", args.manifest_tool)
    source = json.loads(args.host_manifest.read_text(encoding="utf-8"))
    if not isinstance(source, dict):
        raise RuntimeError("host source manifest must be an object")
    base = maker.derive_manifest(source)

    with tempfile.TemporaryDirectory(prefix="ribon-key-policy-positive-") as name:
        path = write_manifest(Path(name), base)
        loaded = composer.load_manifest(path, "x86_64")
        first = composer.render(loaded)
        second = composer.render(composer.load_manifest(path, "x86_64"))
        if first != second:
            raise RuntimeError("key-policy generated C is not deterministic")
        required = (
            "generated_key_policy_records",
            "generated_key_policy_store",
            "ribon_generated_key_policy_store",
            "RIBON_KEY_POLICY_LIFECYCLE_ACTIVE",
        )
        if any(fragment not in first for fragment in required):
            raise RuntimeError("generated C omits immutable key-policy closure")
        observed = loaded["_key_policy_digest"]
        if observed != independent_digest(loaded):
            raise RuntimeError("independent canonical trust-store digest mismatch")

    rotation = copy.deepcopy(base)
    rotation["key_policy"]["keys"] = [
        record("key-new", 2, "key-root", minimum=18, maximum=40),
        record("key-old", 3, "key-root", status="retiring", maximum=20),
        record("key-root", 1, None, minimum=0, maximum=40),
    ]
    with tempfile.TemporaryDirectory(prefix="ribon-key-policy-rotation-") as name:
        path = write_manifest(Path(name), rotation)
        loaded = composer.load_manifest(path, "x86_64")
        keys = loaded["key_policy"]["keys"]
        if [item["delegation_depth"] for item in keys] != [1, 1, 0]:
            raise RuntimeError("rotation graph depth was not derived deterministically")

    mutation = copy.deepcopy(rotation)
    mutation["key_policy"]["keys"][1]["id"] = "key-new"
    reject(composer, mutation)
    mutation = copy.deepcopy(rotation)
    mutation["key_policy"]["keys"][1]["public_key_hex"] = (
        mutation["key_policy"]["keys"][0]["public_key_hex"]
    )
    reject(composer, mutation)
    mutation = copy.deepcopy(rotation)
    mutation["key_policy"]["keys"][1]["status"] = "revoked"
    mutation["key_policy"]["keys"][1]["public_key_hex"] = (
        mutation["key_policy"]["keys"][0]["public_key_hex"]
    )
    reject(composer, mutation)
    mutation = copy.deepcopy(rotation)
    mutation["key_policy"]["keys"][2]["issuer"] = "key-new"
    reject(composer, mutation)
    mutation = copy.deepcopy(rotation)
    mutation["key_policy"]["keys"][0]["issuer"] = "missing"
    reject(composer, mutation)
    mutation = copy.deepcopy(base)
    mutation["key_policy"]["keys"] = [
        record("k0", 1, None, minimum=0),
        record("k1", 2, "k0"),
        record("k2", 3, "k1"),
        record("k3", 4, "k2"),
    ]
    reject(composer, mutation)
    mutation = copy.deepcopy(rotation)
    mutation["key_policy"]["keys"][0]["maximum_sequence"] = 41
    reject(composer, mutation)
    mutation = copy.deepcopy(base)
    mutation["key_policy"]["keys"][0]["modes"] = ["recovery"]
    mutation["key_policy"]["keys"][0]["usages"] = ["policy-recovery"]
    reject(composer, mutation)
    mutation = copy.deepcopy(base)
    del mutation["key_policy"]
    reject(composer, mutation)
    mutation = copy.deepcopy(base)
    del mutation["signature_provider"]
    reject(composer, mutation)

    print(
        "RIBON-KEY-POLICY-MANIFEST-OK deterministic=yes records=1..32 "
        "duplicate=reject cycle=reject depth=2 authority=subset"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
