#!/usr/bin/env python3
"""Validate the Ribon RPi5 boot partition package layout."""

from __future__ import annotations

import argparse
import pathlib
import sys


REQUIRED_FILES = (
    "kernel8.img",
    "ribon-rpi5.img",
    "config.txt",
    "cmdline.txt",
    "kernel/kernel.elf",
)


def fail(message: str) -> int:
    print(f"RIBON-RPI-PACKAGE-FAIL: {message}", file=sys.stderr)
    return 1


def read_text(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True, type=pathlib.Path)
    args = parser.parse_args()

    package = args.package
    if not package.is_dir():
        return fail(f"missing package directory: {package}")
    for relative in REQUIRED_FILES:
        path = package / relative
        if not path.is_file():
            return fail(f"missing file: {relative}")
        if path.stat().st_size == 0:
            return fail(f"empty file: {relative}")

    config = read_text(package / "config.txt")
    for required in ("arm_64bit=1", "kernel=kernel8.img", "enable_uart=1"):
        if required not in config:
            return fail(f"config.txt missing {required}")

    cmdline = read_text(package / "cmdline.txt").strip()
    if not cmdline:
        return fail("cmdline.txt is empty after trimming")
    if "\n" in cmdline or "\r" in cmdline:
        return fail("cmdline.txt must be a single command line")

    print("RIBON-RPI-PACKAGE-LAYOUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
