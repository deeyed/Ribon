#!/usr/bin/env python3
"""Audit immutable key-policy selection and the Ribos/native authority boundary."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REQUIRED_MAP_SYMBOLS = (
    "ribon_generated_key_policy_store",
    "ribon_key_policy_authorize",
    "ribon_key_policy_store_validate",
    "ribon_key_policy_verify",
)
FORBIDDEN_MUTATOR_NAMES = (
    "ribon_key_policy_add",
    "ribon_key_policy_import",
    "ribon_key_policy_revoke",
    "ribon_key_policy_rotate",
    "ribon_key_policy_set",
    "ribon_key_policy_update",
)


def load_generator():
    """Load the product composer as the sole source-manifest validator."""

    path = ROOT / "tools" / "generate_plugin_registry.py"
    spec = importlib.util.spec_from_file_location("ribon_key_policy_generator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load product graph generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_native_boundary() -> None:
    """Reject trust-store or public-key authority from the Ribos VM/adapter surface."""

    roots = [
        ROOT / "language" / "ribos",
        ROOT / "src" / "plugins" / "policy" / "ribos",
    ]
    files = [ROOT / "include" / "Ribon" / "policy" / "ribos.h"]
    for root in roots:
        files.extend(
            path for path in root.rglob("*")
            if path.suffix in {".c", ".h"}
        )
    forbidden = (
        "Ribon/security/key_policy.h",
        "RibonKeyPolicyRecord",
        "RibonKeyPolicyStore",
        "ribon_generated_key_policy_store",
    )
    for path in files:
        text = path.read_text(encoding="utf-8")
        leaked = [token for token in forbidden if token in text]
        if leaked:
            raise RuntimeError(f"{path}: Ribos boundary leaks key authority {leaked}")
    header = (ROOT / "include" / "Ribon" / "security" / "key_policy.h").read_text(
        encoding="utf-8"
    )
    leaked = [name for name in FORBIDDEN_MUTATOR_NAMES if name in header]
    if leaked:
        raise RuntimeError(f"public key-policy ABI exposes mutators: {leaked}")


def main() -> int:
    """Validate manifest, generated reports, final maps/images, and API direction."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--graph", type=Path, action="append", required=True)
    parser.add_argument("--map", dest="maps", type=Path, action="append", required=True)
    parser.add_argument("--image", type=Path, action="append", required=True)
    args = parser.parse_args()
    if not (len(args.graph) == len(args.maps) == len(args.image) == 3):
        raise RuntimeError("R05 key-policy graph gate requires exactly three targets")

    generator = load_generator()
    manifest = generator.load_manifest(args.manifest, "x86_64")
    policy = manifest.get("key_policy")
    digest = manifest.get("_key_policy_digest")
    if not isinstance(policy, dict) or not isinstance(digest, bytes):
        raise RuntimeError("selected signed product has no immutable key policy")
    keys = policy.get("keys")
    if not isinstance(keys, list) or not keys:
        raise RuntimeError("key policy has no bounded key records")
    for key in keys:
        if (
            not isinstance(key, dict)
            or key.get("modes") != ["normal"]
            or key.get("usages") != ["policy-normal"]
        ):
            raise RuntimeError("normal product leaks recovery/provisioning/debug key role")
    rendered = generator.render(manifest)
    digest_fragment = ", ".join(f"0x{value:02x}u" for value in digest)
    for fragment in (
        "generated_key_policy_records",
        "generated_key_policy_store",
        "ribon_generated_key_policy_store",
        digest_fragment,
    ):
        if fragment not in rendered:
            raise RuntimeError(f"generated key-policy closure omits {fragment}")

    for graph_path in args.graph:
        graph = json.loads(graph_path.read_text(encoding="utf-8"))
        if graph.get("key_policy") != policy:
            raise RuntimeError(f"{graph_path}: generated key policy report drift")
        if graph.get("key_policy_digest_sha256") != digest.hex():
            raise RuntimeError(f"{graph_path}: trust-store digest drift")

    for map_path in args.maps:
        text = map_path.read_text(encoding="utf-8", errors="strict")
        missing = [symbol for symbol in REQUIRED_MAP_SYMBOLS if symbol not in text]
        if missing:
            raise RuntimeError(f"{map_path}: missing key-policy closure {missing}")

    store_id = str(policy["id"]).encode("ascii")
    key_ids = [str(key["id"]).encode("ascii") for key in keys]
    public_keys = [bytes.fromhex(str(key["public_key_hex"])) for key in keys]
    for image_path in args.image:
        image = image_path.read_bytes()
        if store_id not in image:
            raise RuntimeError(f"{image_path}: trust-store identity is absent")
        if any(key_id not in image for key_id in key_ids):
            raise RuntimeError(f"{image_path}: selected key ID is absent")
        if any(public_key not in image for public_key in public_keys):
            raise RuntimeError(f"{image_path}: selected public key is absent")

    require_native_boundary()
    print(
        "RIBON-KEY-POLICY-GRAPHS-OK targets=3 store=immutable "
        "roles=normal-only mutators=absent ribos-authority=opaque"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
