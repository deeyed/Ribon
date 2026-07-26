#!/usr/bin/env python3
"""Run the QStar-hosted Ribon Core smoke path."""

from __future__ import annotations

import pathlib
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: qstar_host_smoke.py <arch> <ribon-boot> <fixture>",
            file=sys.stderr,
        )
        return 2

    arch = sys.argv[1]
    bootloader = pathlib.Path(sys.argv[2])
    fixture = pathlib.Path(sys.argv[3])
    fixture.parent.mkdir(parents=True, exist_ok=True)

    generator = pathlib.Path("tools/make_elf64_fixture.py")
    gen_result = subprocess.run(
        [
            sys.executable,
            str(generator),
            "--arch",
            arch,
            "--output",
            str(fixture),
        ],
        check=False,
    )
    if gen_result.returncode != 0:
        return gen_result.returncode

    run_result = subprocess.run(
        [
            str(bootloader),
            "--profile",
            "parus",
            "--kernel",
            str(fixture),
        ],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if run_result.stdout:
        print(run_result.stdout, end="")
    if run_result.stderr:
        print(run_result.stderr, end="", file=sys.stderr)
    return run_result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
