#!/usr/bin/env python3
"""Create a deterministic RPi5 raw-FDT boot-partition package."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--cmdline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        shutil.rmtree(args.output)
    (args.output / "boot").mkdir(parents=True)
    files = {
        "kernel8.img": args.image,
        "boot/payload.elf": args.payload,
        "config.txt": args.config,
        "cmdline.txt": args.cmdline,
    }
    for relative, source in files.items():
        destination = args.output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    manifest = {
        "claim": "package-only; no live RPi5 execution",
        "environment": "raw-fdt",
        "files": {
            relative: {
                "sha256": sha256(args.output / relative),
                "size": (args.output / relative).stat().st_size,
            }
            for relative in sorted(files)
        },
        "port": "raspberrypi-rpi5",
        "schema": "ribon-rpi5-package-v1",
    }
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("RIBON-R4-RPI5-PACKAGE-CREATED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
