#!/usr/bin/env python3
"""Audit the architecture-neutral Ribos VM and Ribon adapter object boundary."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def run(*command: str) -> str:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "command failed")
    return result.stdout


def symbols(path: Path) -> list[str]:
    result = []
    for line in run("nm", "-g", str(path)).splitlines():
        fields = line.split()
        if not fields:
            continue
        symbol = fields[-1]
        if symbol.startswith("_"):
            symbol = symbol[1:]
        result.append(symbol)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter", type=Path, required=True)
    parser.add_argument("--vm-core", type=Path, required=True)
    args = parser.parse_args()
    adapter_members = {
        Path(line).name
        for line in run("ar", "t", str(args.adapter)).splitlines()
        if line and not line.startswith("__.SYMDEF")
    }
    if adapter_members != {"adapter.o", "extension.o"}:
        raise RuntimeError(f"unexpected Ribos adapter members: {adapter_members}")
    vm_symbols = symbols(args.vm_core)
    adapter_symbols = symbols(args.adapter)
    if any(symbol.startswith("ribon_") for symbol in vm_symbols):
        raise RuntimeError("architecture-neutral VM imports Ribon symbols")
    required = (
        "ribon_ribos_policy_execute",
        "ribon_ribos_policy_helper_dispatch",
        "ribon_ribos_policy_plugin_descriptor",
        "ribon_ribos_extension_validate_v1",
    )
    if any(symbol not in adapter_symbols for symbol in required):
        raise RuntimeError("Ribon adapter archive omits required symbols")
    forbidden = (
        "ribon_host_",
        "ribon_luca_",
        "ribon_parus_",
        "ribon_network_",
        "ribon_flash_",
    )
    if any(
        symbol.startswith(prefix)
        for symbol in adapter_symbols
        for prefix in forbidden
    ):
        raise RuntimeError("generic adapter leaks product or raw update semantics")
    print(
        "RIBOS-OBJECT-GRAPH-OK vm-ribon-imports=0 "
        "adapter=isolated product-semantics=external"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
