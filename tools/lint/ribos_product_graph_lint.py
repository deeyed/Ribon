#!/usr/bin/env python3
"""Validate deterministic product-generated Ribos schema/helper bindings."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def load_generator():
    path = ROOT / "tools" / "generate_plugin_registry.py"
    spec = importlib.util.spec_from_file_location("ribon_registry_generator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load product graph generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def reject_mutation(generator, manifest: dict[str, object], architecture: str) -> None:
    with tempfile.TemporaryDirectory(prefix="ribon-ribos-graph-") as directory:
        path = Path(directory) / "product.json"
        path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        try:
            generator.load_manifest(path, architecture)
        except ValueError:
            return
    raise RuntimeError("invalid Ribos product graph mutation was accepted")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--architecture", required=True)
    args = parser.parse_args()
    generator = load_generator()
    manifest = generator.load_manifest(args.manifest, args.architecture)
    policy = manifest.get("ribos_policy")
    if not isinstance(policy, dict):
        raise RuntimeError("selected product has no Ribos policy binding")
    first = generator.render(manifest)
    second = generator.render(
        generator.load_manifest(args.manifest, args.architecture)
    )
    if first != second:
        raise RuntimeError("Ribos product binding generation is not deterministic")
    required_fragments = (
        "generated_ribos_helper_contract",
        "generated_ribos_routes",
        "generated_ribos_limits",
        "ribon_generated_ribos_policy_binding",
        "ribon_ribos_policy_helper_dispatch",
    )
    if any(fragment not in first for fragment in required_fragments):
        raise RuntimeError("generated product source omits a Ribos binding section")
    helpers = policy.get("helpers")
    if not isinstance(helpers, list) or len(helpers) != 5:
        raise RuntimeError("host reference helper closure is not exact")
    ids = [helper["stable_id"] for helper in helpers]
    if ids != [2, 8, 11, 21, 22]:
        raise RuntimeError(f"unexpected host reference helper closure: {ids}")

    duplicate = json.loads(args.manifest.read_text(encoding="utf-8"))
    duplicate["ribos_policy"]["helpers"][1]["stable_id"] = 2
    reject_mutation(generator, duplicate, args.architecture)
    inverted = json.loads(args.manifest.read_text(encoding="utf-8"))
    inverted["ribos_policy"]["helpers"][0]["allowed_phases"] = ["driver"]
    reject_mutation(generator, inverted, args.architecture)
    missing_watchdog = json.loads(args.manifest.read_text(encoding="utf-8"))
    missing_watchdog["ribos_policy"]["watchdog_service_id"] = "service.missing"
    reject_mutation(generator, missing_watchdog, args.architecture)
    print(
        "RIBOS-PRODUCT-GRAPH-OK schema=selected helper-table=generated "
        "digest=canonical routes=typed deterministic=yes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
