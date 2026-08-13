#!/usr/bin/env python3
"""Audit the Core and generic boot-library object ownership boundary."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


CORE_MEMBERS = {"arena.o", "context.o", "plugin.o", "registry.o", "service_directory.o"}
BOOT_MEMBERS = {
    "block.o",
    "boot.o",
    "boot_config.o",
    "environment.o",
    "fat32.o",
    "gpt.o",
    "image.o",
    "memory.o",
    "module_bundle.o",
    "port.o",
    "protocol.o",
}
SDK_MEMBERS = {
    "personality.o",
    "sdk.o",
}
CORE_FORBIDDEN_SYMBOLS = (
    "ribon_arch_",
    "ribon_boot_protocol_",
    "ribon_luca_",
    "ribon_parus_",
    "ribon_uefi_",
    "ribon_rpi_",
)
BOOT_FORBIDDEN_SYMBOLS = (
    "ribon_generated_boot_module_",
    "ribon_luca_",
    "ribon_parus_",
    "ribon_uefi_",
    "ribon_bios_",
    "ribon_rpi_",
)


def run(*command: str) -> str:
    """Run a read-only object inspection command."""

    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "command failed")
    return result.stdout


def members(path: Path) -> set[str]:
    """Return archive member basenames."""

    return {
        Path(line.strip()).name
        for line in run("ar", "t", str(path)).splitlines()
        if line.strip() and not line.strip().startswith("__.SYMDEF")
    }


def symbols(path: Path) -> str:
    """Return defined and undefined archive symbol text."""

    return run("nm", "-g", str(path))


def main() -> int:
    """Validate exact members and forbidden cross-boundary symbols."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    args = parser.parse_args()
    failures: list[str] = []
    try:
        core_members = members(args.core)
        boot_members = members(args.boot)
        sdk_members = members(args.sdk)
        core_symbols = symbols(args.core)
        boot_symbols = symbols(args.boot)
    except RuntimeError as error:
        print(f"object_graph_lint: {error}")
        return 2

    if core_members != CORE_MEMBERS:
        failures.append(
            f"core members mismatch: expected={sorted(CORE_MEMBERS)} actual={sorted(core_members)}"
        )
    if boot_members != BOOT_MEMBERS:
        failures.append(
            f"boot members mismatch: expected={sorted(BOOT_MEMBERS)} actual={sorted(boot_members)}"
        )
    if sdk_members != SDK_MEMBERS:
        failures.append(
            f"sdk members mismatch: expected={sorted(SDK_MEMBERS)} "
            f"actual={sorted(sdk_members)}"
        )
    for symbol in CORE_FORBIDDEN_SYMBOLS:
        if symbol in core_symbols:
            failures.append(f"core archive leaks forbidden symbol prefix: {symbol}")
    for symbol in BOOT_FORBIDDEN_SYMBOLS:
        if symbol in boot_symbols:
            failures.append(f"boot archive contains OS or native environment symbol: {symbol}")

    if failures:
        for failure in failures:
            print(f"object_graph_lint: {failure}")
        return 1
    print("RIBON-R5-LIBRARY-OBJECT-GRAPHS-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
