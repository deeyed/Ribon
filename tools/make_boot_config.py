#!/usr/bin/env python3
"""Write one deterministic Ribon boot configuration candidate."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--entry", required=True)
    parser.add_argument("--priority", type=int, required=True)
    parser.add_argument("--protocol", required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--cmdline", default="")
    args = parser.parse_args()
    if args.priority < 0 or args.priority > 0xFFFFFFFF:
        raise SystemExit("priority must fit uint32")
    if not args.kernel.startswith("/"):
        raise SystemExit("kernel must be an absolute canonical path")
    lines = [
        "version=1",
        f"entry={args.entry}",
        f"priority={args.priority}",
        f"protocol={args.protocol}",
        f"image={args.image}",
        f"kernel={args.kernel}",
    ]
    if args.cmdline:
        lines.append(f"cmdline={args.cmdline}")
    lines.append("end")
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
