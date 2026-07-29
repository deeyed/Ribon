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
    parser.add_argument("--init-image", default="")
    parser.add_argument("--module", action="append", default=[])
    args = parser.parse_args()
    if args.priority < 0 or args.priority > 0xFFFFFFFF:
        raise SystemExit("priority must fit uint32")
    paths = [args.kernel]
    if args.init_image:
        paths.append(args.init_image)
    paths.extend(args.module)
    if len(args.module) + (1 if args.init_image else 0) > 8:
        raise SystemExit("boot module count exceeds 8")
    if any(not path.startswith("/") for path in paths):
        raise SystemExit("all paths must be absolute canonical paths")
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
    if args.init_image:
        lines.append(f"init_image={args.init_image}")
    lines.extend(f"module={path}" for path in args.module)
    lines.append("end")
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
