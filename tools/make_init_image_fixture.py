#!/usr/bin/env python3
"""Write a deterministic opaque initial-image fixture."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", type=int, default=4096)
    args = parser.parse_args()
    prefix = b"RIBON-OPAQUE-INITIAL-IMAGE-V1\n"
    if args.size < len(prefix):
        raise SystemExit("size is smaller than the fixture identity")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(prefix + bytes(args.size - len(prefix)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
