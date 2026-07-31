#!/usr/bin/env python3
"""Hard-gate network and flash out of normal-mode Ribos product graphs."""

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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--architecture", required=True)
    args = parser.parse_args()
    generator = load_generator()
    manifest = generator.load_manifest(args.manifest, args.architecture)
    policy = manifest.get("ribos_policy")
    if manifest.get("mode") != "normal" or not isinstance(policy, dict):
        raise RuntimeError("normal-mode Ribos product graph is required")
    forbidden = {"NETWORK", "FLASH"}
    capabilities = set(policy["capabilities"])
    if capabilities & forbidden:
        raise RuntimeError("normal policy grants network or flash")
    for helper in policy["helpers"]:
        if set(helper["ribos_capabilities"]) & forbidden:
            raise RuntimeError("normal helper imports network or flash")
        if helper["service_kind"] in {
            "network-transport",
            "inactive-slot-storage",
        }:
            raise RuntimeError("normal helper routes to update authority")

    hostile = json.loads(args.manifest.read_text(encoding="utf-8"))
    hostile["ribos_policy"]["capabilities"].append("NETWORK")
    hostile["ribos_policy"]["capabilities"].sort()
    with tempfile.TemporaryDirectory(prefix="ribon-ribos-normal-") as directory:
        path = Path(directory) / "hostile.json"
        path.write_text(
            json.dumps(hostile, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        try:
            generator.load_manifest(path, args.architecture)
        except ValueError:
            print(
                "RIBOS-NORMAL-NO-NETWORK-OK network=absent flash=absent "
                "update-services=absent negative-gate=closed"
            )
            return 0
    raise RuntimeError("normal product generator accepted NETWORK capability")


if __name__ == "__main__":
    raise SystemExit(main())
