#!/usr/bin/env python3
"""Regenerate the tracked trust-message signature with independent OpenSSL."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    """Load one Ribon host tool without adding the tools directory to sys.path."""

    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    """Cross-check exact public key and signature bytes without logging secrets."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openssl", required=True)
    parser.add_argument("--message-vector", type=Path, required=True)
    parser.add_argument("--signature-vector", type=Path, required=True)
    parser.add_argument("--seed", type=Path, required=True)
    args = parser.parse_args()

    inspector = load_module(
        "ribon_trust_inspector", ROOT / "tools/inspect_ribos_trust_message.py"
    )
    signer = load_module(
        "ribon_policy_signer", ROOT / "tools/sign_ribos_policy.py"
    )
    _, message = inspector.load_vector(args.message_vector)
    document = json.loads(args.signature_vector.read_text(encoding="utf-8"))
    if set(document) != {
        "message_sha256",
        "public_key_hex",
        "schema",
        "signature_hex",
        "signer",
        "source_seed",
    } or document["schema"] != "ribon-ed25519-cross-tool-vector-v1":
        raise RuntimeError("signature vector schema is not exact")
    public_key, signature = signer.openssl_sign(
        args.openssl,
        signer.load_seed(args.seed),
        message,
    )
    if (
        public_key.hex() != document["public_key_hex"]
        or signature.hex() != document["signature_hex"]
        or hashlib.sha256(message).hexdigest()
        != document["message_sha256"]
    ):
        raise RuntimeError("OpenSSL output differs from the tracked vector")
    print(
        "RIBON-ED25519-CROSS-TOOL-OK signer=openssl "
        "message=RIBON-TRUST-MESSAGE-V1 deterministic=yes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
