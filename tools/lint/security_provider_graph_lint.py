#!/usr/bin/env python3
"""Reject signer, fixture-provider, and private-key leakage from R04 targets."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_PROVIDER = {
    "algorithm": "ed25519",
    "class": "production",
    "id": "security.signature.ed25519.monocypher-4.0.3",
    "symbol": "ribon_ed25519_signature_provider_descriptor",
}
REQUIRED_MAP_SYMBOLS = (
    "crypto_ed25519_check",
    "ribon_ed25519_signature_provider_descriptor",
    "ribon_generated_signature_provider",
)
FORBIDDEN_TARGET_NAMES = (
    "crypto_ed25519_key_pair",
    "crypto_ed25519_sign",
    "crypto_eddsa_key_pair",
    "crypto_eddsa_sign",
    "make_ribos_signed_fixture",
    "sign_ribos_policy.py",
    "rfc8032-test1-seed",
)


def load_generator():
    """Load the product composer as the one manifest validation authority."""

    path = ROOT / "tools" / "generate_plugin_registry.py"
    spec = importlib.util.spec_from_file_location("ribon_registry_generator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load product graph generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_production_provider(document: dict[str, object], source: Path) -> None:
    """Require the one selected provider to match the reviewed R04 closure."""

    if document.get("signature_provider") != EXPECTED_PROVIDER:
        raise RuntimeError(f"{source}: production signature provider is not exact")


def main() -> int:
    """Validate source graph, generated reports, maps, and linked image bytes."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--graph", type=Path, action="append", required=True)
    parser.add_argument("--map", dest="maps", type=Path, action="append", required=True)
    parser.add_argument("--image", type=Path, action="append", required=True)
    parser.add_argument("--private-seed", type=Path, required=True)
    args = parser.parse_args()
    if not (len(args.graph) == len(args.maps) == len(args.image) == 3):
        raise RuntimeError("R04 security graph gate requires exactly three targets")

    generator = load_generator()
    manifest = generator.load_manifest(args.manifest, "x86_64")
    require_production_provider(manifest, args.manifest)
    rendered = generator.render(manifest)
    for fragment in (
        EXPECTED_PROVIDER["symbol"],
        "ribon_generated_product_source_digest",
        "ribon_generated_signature_provider",
    ):
        if fragment not in rendered:
            raise RuntimeError(f"generated registry omits {fragment}")

    for graph_path in args.graph:
        graph = json.loads(graph_path.read_text(encoding="utf-8"))
        if not isinstance(graph, dict):
            raise RuntimeError(f"{graph_path}: graph report must be an object")
        require_production_provider(graph, graph_path)

    for map_path in args.maps:
        text = map_path.read_text(encoding="utf-8", errors="strict")
        missing = [symbol for symbol in REQUIRED_MAP_SYMBOLS if symbol not in text]
        forbidden = [symbol for symbol in FORBIDDEN_TARGET_NAMES if symbol in text]
        if missing:
            raise RuntimeError(f"{map_path}: missing verifier closure: {missing}")
        if forbidden:
            raise RuntimeError(f"{map_path}: signer/fixture closure leaked: {forbidden}")

    seed_hex = "".join(args.private_seed.read_text(encoding="ascii").split())
    if len(seed_hex) != 64:
        raise RuntimeError("test-only private seed must be exactly 32 bytes")
    private_seed = bytes.fromhex(seed_hex)
    provider_id = EXPECTED_PROVIDER["id"].encode("ascii")
    for image_path in args.image:
        image = image_path.read_bytes()
        if provider_id not in image:
            raise RuntimeError(f"{image_path}: selected verifier identity is absent")
        if private_seed in image:
            raise RuntimeError(f"{image_path}: private seed bytes leaked")
        leaked = [
            name for name in FORBIDDEN_TARGET_NAMES if name.encode("ascii") in image
        ]
        if leaked:
            raise RuntimeError(f"{image_path}: forbidden target bytes: {leaked}")

    fixture_mutation = dict(manifest)
    fixture_mutation["signature_provider"] = dict(EXPECTED_PROVIDER)
    fixture_mutation["signature_provider"]["class"] = "fixture"
    try:
        require_production_provider(fixture_mutation, args.manifest)
    except RuntimeError:
        pass
    else:
        raise RuntimeError("fixture provider mutation was accepted as production")

    print(
        "RIBON-SECURITY-PROVIDER-GRAPHS-OK targets=3 provider=production "
        "signer=absent private-key=absent fixture-provider=absent"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
