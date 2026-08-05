#!/usr/bin/env python3
"""Resolve QEMU firmware from the selected tool prefix and standard data roots."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import sys


FIRMWARE_LAYOUTS = {
    "opensbi-riscv64": (
        "share/qemu/opensbi-riscv64-generic-fw_dynamic.bin",
        "share/opensbi/generic/firmware/fw_dynamic.bin",
        "share/opensbi/lp64/generic/firmware/fw_dynamic.bin",
        "lib/riscv64-linux-gnu/opensbi/generic/fw_dynamic.bin",
    ),
    "uefi-x86_64": (
        "share/qemu/edk2-x86_64-code.fd",
        "share/OVMF/OVMF_CODE_4M.fd",
        "share/qemu/OVMF_CODE.fd",
        "share/OVMF/OVMF_CODE.fd",
        "share/ovmf/OVMF_CODE.fd",
        "share/edk2/x64/OVMF_CODE.fd",
    ),
}


def executable_path(command: str) -> Path | None:
    """Resolve one command name or caller-provided executable path."""

    if not command:
        return None
    candidate = Path(command).expanduser()
    if candidate.parent != Path("."):
        return candidate.resolve() if candidate.is_file() else None
    resolved = shutil.which(command)
    return Path(resolved).resolve() if resolved else None


def candidate_prefixes(qemu: str, search_roots: list[Path]) -> list[Path]:
    """Return deterministic prefixes without embedding host package versions."""

    prefixes = [root.expanduser().resolve() for root in search_roots]
    qemu_path = executable_path(qemu)
    if qemu_path is not None:
        prefixes.append(qemu_path.parent.parent)
    prefixes.extend((Path("/usr"), Path("/usr/local")))
    result: list[Path] = []
    seen: set[Path] = set()
    for prefix in prefixes:
        if prefix not in seen:
            seen.add(prefix)
            result.append(prefix)
    return result


def resolve(kind: str, qemu: str, search_roots: list[Path]) -> Path | None:
    """Find the first exact firmware file in a stable layout order."""

    for prefix in candidate_prefixes(qemu, search_roots):
        for relative in FIRMWARE_LAYOUTS[kind]:
            candidate = prefix / relative
            if candidate.is_file():
                return candidate.resolve()
    return None


def main() -> int:
    """Print one path, or remain silent when an optional firmware is absent."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=sorted(FIRMWARE_LAYOUTS), required=True)
    parser.add_argument("--qemu", default="")
    parser.add_argument("--search-root", action="append", type=Path, default=[])
    parser.add_argument("--required", action="store_true")
    args = parser.parse_args()
    path = resolve(args.kind, args.qemu, args.search_root)
    if path is None:
        if args.required:
            print(
                f"RIBON-FIRMWARE-NOT-FOUND kind={args.kind}",
                file=sys.stderr,
            )
            return 1
        return 0
    print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
