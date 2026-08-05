#!/usr/bin/env python3
"""Validate the explicit tools and firmware required by one Make build lane."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import sys


def split_binding(binding: str) -> tuple[str, str]:
    """Split NAME=VALUE while preserving path punctuation in VALUE."""

    name, separator, value = binding.partition("=")
    if not separator or not name:
        raise ValueError(f"invalid binding: {binding!r}")
    return name, value


def resolve_tool(value: str) -> str | None:
    """Resolve one executable name or path without invoking a shell."""

    if not value:
        return None
    candidate = Path(value).expanduser()
    if candidate.parent != Path("."):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate.resolve())
        return None
    return shutil.which(value)


def main() -> int:
    """Report every resolved dependency and fail once after full inspection."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", required=True)
    parser.add_argument("--tool", action="append", default=[])
    parser.add_argument("--file", action="append", default=[])
    args = parser.parse_args()
    failures: list[str] = []
    print(
        "RIBON-BUILD-DOCTOR "
        f"scope={args.scope} host={platform.system().lower()}-"
        f"{platform.machine().lower()}"
    )
    for binding in args.tool:
        name, value = split_binding(binding)
        resolved = resolve_tool(value)
        if resolved is None:
            failures.append(f"tool:{name}={value or '<empty>'}")
        else:
            print(f"RIBON-BUILD-TOOL name={name} path={resolved}")
    for binding in args.file:
        name, value = split_binding(binding)
        candidate = Path(value).expanduser() if value else None
        if candidate is None or not candidate.is_file():
            failures.append(f"file:{name}={value or '<empty>'}")
        else:
            print(f"RIBON-BUILD-FILE name={name} path={candidate.resolve()}")
    if failures:
        for failure in failures:
            print(f"RIBON-BUILD-MISSING {failure}", file=sys.stderr)
        return 1
    print(f"RIBON-BUILD-DOCTOR-OK scope={args.scope}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as error:
        print(f"RIBON-BUILD-DOCTOR-FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
