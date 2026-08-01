#!/usr/bin/env python3
"""Require product composer rejection for malformed D02 writer graphs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def invoke(
    python: str,
    composer: Path,
    manifest: Path,
    output: Path,
    succeed: bool,
) -> None:
    """Run one product composition and require its selected result class."""

    result = subprocess.run(
        [
            python,
            str(composer),
            "--manifest",
            str(manifest),
            "--architecture",
            "x86_64",
            "--output",
            str(output),
        ],
        text=True,
        capture_output=True,
        check=False,
    )
    if (result.returncode == 0) != succeed:
        raise RuntimeError(
            f"unexpected graph result {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def write(path: Path, document: object) -> None:
    """Write one temporary hostile product manifest."""

    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")


def main() -> int:
    """Exercise missing binding, wrong mode, role, digest and capability cases."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--composer", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.manifest.read_text(encoding="utf-8"))
    python = sys.executable

    with tempfile.TemporaryDirectory(prefix="ribon-d02-graph-") as directory:
        root = Path(directory)
        manifest = root / "product.json"
        output = root / "registry.c"
        write(manifest, source)
        invoke(python, args.composer, manifest, output, True)

        hostile = json.loads(json.dumps(source))
        hostile["mode"] = "normal"
        write(manifest, hostile)
        invoke(python, args.composer, manifest, output, False)

        hostile = json.loads(json.dumps(source))
        del hostile["update_storage"]
        write(manifest, hostile)
        invoke(python, args.composer, manifest, output, False)

        hostile = json.loads(json.dumps(source))
        hostile["update_storage"]["writer_service_id"] = \
            hostile["update_storage"]["read_service_id"]
        write(manifest, hostile)
        invoke(python, args.composer, manifest, output, False)

        hostile = json.loads(json.dumps(source))
        hostile["required_capabilities"].remove("INACTIVE_SLOT_ERASE")
        write(manifest, hostile)
        invoke(python, args.composer, manifest, output, False)

        hostile = json.loads(json.dumps(source))
        hostile["update_storage"]["layout_digest_sha256"] = "0" * 64
        write(manifest, hostile)
        invoke(python, args.composer, manifest, output, False)

        hostile = json.loads(json.dumps(source))
        hostile["update_storage"]["media_identity_digest_sha256"] = "0" * 64
        write(manifest, hostile)
        invoke(python, args.composer, manifest, output, False)

    print(
        "RIBON-UPDATE-STORAGE-GRAPH-HOSTILE-OK "
        "mode=1 binding=1 role=1 cap=1 layout-digest=1 media-digest=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
