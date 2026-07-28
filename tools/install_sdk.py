#!/usr/bin/env python3
"""Stage the public Ribon SDK with deterministic content metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil


def digest(path: Path) -> str:
    """Return the SHA-256 digest of one installed file."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    """Copy only public headers, libraries, schemas, templates, and an ABI manifest."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--public-include", type=Path, required=True)
    parser.add_argument("--library", type=Path, action="append", required=True)
    parser.add_argument("--schemas", type=Path, required=True)
    parser.add_argument("--templates", type=Path, required=True)
    args = parser.parse_args()

    root = args.root
    if root.exists():
        shutil.rmtree(root)
    include_destination = root / "include" / "Ribon"
    library_destination = root / "lib"
    share_destination = root / "share" / "ribon"
    shutil.copytree(args.public_include, include_destination)
    library_destination.mkdir(parents=True)
    for library in args.library:
        shutil.copy2(library, library_destination / library.name)
    shutil.copytree(args.schemas, share_destination / "schemas")
    shutil.copytree(args.templates, share_destination / "templates")

    pkgconfig = library_destination / "pkgconfig"
    pkgconfig.mkdir()
    (pkgconfig / "ribon-sdk.pc").write_text(
        "prefix=${pcfiledir}/../..\n"
        "includedir=${prefix}/include\n"
        "libdir=${prefix}/lib\n"
        "\n"
        "Name: Ribon SDK\n"
        "Description: Deterministic boot and firmware plugin SDK\n"
        "Version: 0.4.0\n"
        "Cflags: -I${includedir}\n"
        "Libs: -L${libdir} -lribon-sdk -lribon-boot -lribon-core\n",
        encoding="utf-8",
    )

    installed = sorted(path for path in root.rglob("*") if path.is_file())
    manifest = {
        "schema_version": 1,
        "sdk_abi": 4,
        "core_abi": 4,
        "plugin_abi": {"major": 4, "minor": 0},
        "source_version": "0.4.0",
        "files": {
            path.relative_to(root).as_posix(): digest(path)
            for path in installed
        },
    }
    share_destination.mkdir(parents=True, exist_ok=True)
    (share_destination / "sdk-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
