#!/usr/bin/env python3
"""Compare two generated SDK trees byte-for-byte."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def tree(root: Path) -> dict[str, str]:
    """Return sorted relative-path to SHA-256 mapping."""

    return {
        path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    args = parser.parse_args()
    first = tree(args.first)
    second = tree(args.second)
    if first != second:
        first_keys = set(first)
        second_keys = set(second)
        print(f"only-first={sorted(first_keys - second_keys)}")
        print(f"only-second={sorted(second_keys - first_keys)}")
        print(
            "changed="
            f"{sorted(key for key in first_keys & second_keys if first[key] != second[key])}"
        )
        return 1
    print(f"RIBON-R5-SDK-REPRODUCIBLE-OK files={len(first)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
