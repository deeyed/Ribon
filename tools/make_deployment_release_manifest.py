#!/usr/bin/env python3
"""Assemble one path-free deployment evidence manifest from D01-D08 artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys


def sha256(path: Path) -> str:
    """Return one canonical file digest."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict[str, object]:
    """Read one JSON object."""

    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON artifact is not an object: {path}")
    return value


def tree_identity(root: Path) -> dict[str, object]:
    """Derive a canonical file map and aggregate digest without host paths."""

    if not root.is_dir():
        raise ValueError(f"artifact tree is missing: {root}")
    files = {
        path.relative_to(root).as_posix(): {
            "sha256": sha256(path),
            "size": path.stat().st_size,
        }
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }
    if not files:
        raise ValueError(f"artifact tree is empty: {root}")
    encoded = json.dumps(files, separators=(",", ":"), sort_keys=True).encode("utf-8")
    return {"digest": hashlib.sha256(encoded).hexdigest(), "files": files}


def tool_version(command: list[str]) -> str:
    """Return one stable first-line tool identity."""

    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise ValueError(f"tool version query failed: {' '.join(command)}")
    lines = (result.stdout + result.stderr).splitlines()
    if not lines:
        raise ValueError(f"tool version query was empty: {' '.join(command)}")
    return lines[0].strip()


def write_json(path: Path, value: object) -> None:
    """Write deterministic presentation JSON."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--consumer-report", type=Path, required=True)
    parser.add_argument("--rpi5-package", type=Path, required=True)
    parser.add_argument("--rpi5-prehardware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    sdk = read_json(args.sdk_root / "share/ribon/sdk-manifest.json")
    consumer = read_json(args.consumer_report)
    package = read_json(args.rpi5_package / "manifest.json")
    prehardware = read_json(args.rpi5_prehardware)
    revisions = {
        sdk.get("source_revision"),
        consumer.get("source_revision"),
        prehardware.get("source_revision"),
        args.source_revision,
    }
    if (
        len(revisions) != 1
        or None in revisions
        or sdk.get("schema") != "ribon-sdk-install-v2"
        or consumer.get("schema") != "ribon-sdk-deployment-consumer-v1"
        or consumer.get("source_private_dependencies") != 0
        or package.get("claim") != "package-only; no live RPi5 execution"
        or prehardware.get("schema") != "ribon-rpi5-prehardware-v1"
        or prehardware.get("hardware_execution") != "not-run"
    ):
        raise ValueError("deployment evidence identities or boundaries do not close")

    product = read_json(args.rpi5_package / "metadata/product.json")
    graph = read_json(args.consumer_report.parent / "object-graph.json")
    result = {
        "schema": "ribon-deployment-release-v1",
        "source_revision": args.source_revision,
        "toolchain": {
            "c_compiler": tool_version([args.cc, "--version"]),
            "host_architecture": platform.machine(),
            "python": platform.python_version(),
        },
        "contracts": {
            "core_abi": sdk["core_abi"],
            "plugin_abi": sdk["plugin_abi"],
            "product_graph_schema": "ribon-product-object-graph-v1",
            "product_manifest_schema": product.get("schema_version"),
            "sdk_abi": sdk["sdk_abi"],
            "sdk_install_schema": sdk["schema"],
            "update_manifest_schema": "ribon.update.manifest.v1",
        },
        "artifacts": {
            "installed_sdk": tree_identity(args.sdk_root),
            "external_consumer": tree_identity(args.consumer_report.parent.parent),
            "rpi5_package": tree_identity(args.rpi5_package),
            "rpi5_prehardware": tree_identity(args.rpi5_prehardware.parent),
        },
        "evidence": {
            "host_build_unit": "passed",
            "qemu_aarch64_update_recovery_confirmation_linux": "passed-by-aggregate-gate",
            "rpi5_package_prehardware": "passed",
            "rpi5_physical": "not-run",
        },
        "claims": [
            "installed SDK can compose, compile, verify, assemble, inspect, and link an out-of-tree recovery/update consumer",
            "signed update install is transactional and recovery networking is bounded by prior D01-D07 gates",
            "AArch64 QEMU exercises OS-neutral boot confirmation and a Linux Image boot",
            "RPi5 package and signed update inputs are deterministic prehardware artifacts",
        ],
        "nonclaims": [
            "physical RPi5 execution",
            "production key custody or monotonic hardware rollback state",
            "UEFI specification conformance or fleet deployment",
            "Parus integration or user-process execution",
            "Linux boot on architectures other than AArch64",
        ],
    }
    write_json(args.output, result)
    print(
        "RIBON-D08-DEPLOYMENT-RELEASE-OK "
        "sdk=installed consumer=out-of-tree rpi5=prehardware hardware=not-run"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"RIBON-D08-DEPLOYMENT-RELEASE-FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
