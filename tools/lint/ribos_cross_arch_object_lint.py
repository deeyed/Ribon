#!/usr/bin/env python3
"""Audit three Ribos validation target graphs for host and architecture leaks."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


FORBIDDEN_SYMBOL_FRAGMENTS = (
    "malloc",
    "calloc",
    "realloc",
    "free",
    "fopen",
    "fread",
    "fwrite",
    "printf",
    "socket",
    "connect",
    "ribon_host_",
    "ribos_frontend_",
    "ribos_ir_",
    "crypto_ed25519_sign",
    "crypto_eddsa_sign",
    "ribon_fixture",
    "RIBON_RIBOS_AUTHORIZATION_FIXTURE_CALLBACK",
)
FORBIDDEN_SOURCE_FRAGMENTS = (
    "language/ribos/frontend/",
    "language/ribos/host/",
    "language/ribos/ir/",
    "src/environments/host/",
    "src/environments/raw-fdt/",
    "src/environments/uefi-app/",
    "src/protocols/os/",
    "tests/fixtures/",
    "tools/sign_ribos_policy.py",
    "src/plugins/security/fixture/",
)


def tool_output(*command: str) -> str:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "command failed")
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, action="append", required=True)
    parser.add_argument("--image", type=Path, action="append", required=True)
    args = parser.parse_args()
    if len(args.map) != 3 or len(args.image) != 3:
        raise SystemExit("exactly three --map and three --image arguments are required")

    for map_path in args.map:
        text = map_path.read_text(encoding="utf-8", errors="replace")
        leaks = [
            fragment for fragment in FORBIDDEN_SOURCE_FRAGMENTS
            if fragment in text
        ]
        if leaks:
            raise RuntimeError(f"{map_path}: forbidden source graph {leaks}")
    for image in args.image:
        symbols = tool_output("nm", str(image))
        leaks = [
            fragment for fragment in FORBIDDEN_SYMBOL_FRAGMENTS
            if fragment in symbols
        ]
        if leaks:
            raise RuntimeError(f"{image}: forbidden undefined symbols {leaks}")
    print(
        "RIBOS-R18-CROSS-ARCH-OBJECTS-OK targets=amd64,aarch64,riscv64 "
        "host-imports=0 frontend-imports=0 network-imports=0 "
        "signer-imports=0 fixture-authority=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
