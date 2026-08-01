#!/usr/bin/env python3
"""Require recovery-network graph closure and hostile manifest rejection."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(composer: Path, manifest: Path, output: Path, succeed: bool) -> None:
    """Run one product composition and require the selected result class."""

    completed = subprocess.run(
        [sys.executable, str(composer), "--manifest", str(manifest),
         "--architecture", "x86_64", "--output", str(output)],
        text=True, capture_output=True, check=False,
    )
    if (completed.returncode == 0) != succeed:
        raise RuntimeError(
            f"unexpected graph status {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )


def write(path: Path, document: object) -> None:
    """Write canonical temporary hostile JSON."""

    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")


def main() -> int:
    """Exercise mode, capability, role, endpoint, path and budget failures."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--composer", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.manifest.read_text(encoding="utf-8"))
    try:
        with tempfile.TemporaryDirectory(prefix="ribon-d05-network-graph-") as directory:
            root = Path(directory)
            manifest = root / "product.json"
            output = root / "registry.c"
            write(manifest, source)
            invoke(args.composer, manifest, output, True)

            hostile = json.loads(json.dumps(source))
            hostile["mode"] = "normal"
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)

            hostile = json.loads(json.dumps(source))
            del hostile["recovery_network"]
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)

            hostile = json.loads(json.dumps(source))
            hostile["required_capabilities"].remove("NETWORK_TRANSPORT")
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)

            hostile = json.loads(json.dumps(source))
            hostile["recovery_network"]["service_id"] = \
                "service.uefi-app.boot-source"
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)

            hostile = json.loads(json.dumps(source))
            hostile["recovery_network"]["objects"][0]["path"] = "../update.man"
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)

            hostile = json.loads(json.dumps(source))
            hostile["recovery_network"]["retry_count"] = 3
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)

            hostile = json.loads(json.dumps(source))
            hostile["recovery_network"]["objects"][2]["maximum_bytes"] = \
                64 * 1024 * 1024 + 1
            write(manifest, hostile)
            invoke(args.composer, manifest, output, False)
        print(
            "RIBON-D05-RECOVERY-NETWORK-GRAPH-OK "
            "normal=reject binding=required capability=required endpoint=typed "
            "path=canonical retry=bounded output=bounded"
        )
        return 0
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"recovery-network-graph-test: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
