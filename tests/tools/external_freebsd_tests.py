#!/usr/bin/env python3
"""Hostile tests for the pinned FreeBSD input and deterministic ESP composer."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def load(name: str, relative: str):
    """Load one source-owned tool without creating an installed package."""

    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PREPARE = load("prepare_external_freebsd", "tools/prepare_external_freebsd.py")
COMPOSE = load("compose_freebsd_uefi", "tools/compose_freebsd_uefi.py")


def valid_pe(marker: bytes = b"") -> bytearray:
    """Create a minimal PE32+ EFI application for parser hostility tests."""

    image = bytearray(0x300)
    image[0:2] = b"MZ"
    image[0x3C:0x40] = (0x80).to_bytes(4, "little")
    image[0x80:0x84] = b"PE\0\0"
    image[0x84:0x86] = (0x8664).to_bytes(2, "little")
    optional = 0x80 + 24
    image[optional:optional + 2] = (0x20B).to_bytes(2, "little")
    image[optional + 16:optional + 20] = (0x1000).to_bytes(4, "little")
    image[optional + 68:optional + 70] = (10).to_bytes(2, "little")
    if marker:
        image[-len(marker):] = marker
    return image


def must_reject(callback) -> None:
    """Require one hostile input to fail closed."""

    try:
        callback()
    except (OSError, ValueError, json.JSONDecodeError):
        return
    raise AssertionError("hostile FreeBSD package input was accepted")


def main() -> int:
    """Exercise descriptor, PE, path and produced-package invariants."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--product",
        type=Path,
        default=ROOT / "build/targets/x86_64-uefi-freebsd",
    )
    args = parser.parse_args()

    descriptor_path = ROOT / "external/inputs/freebsd-amd64-15.1-release.json"
    descriptor = PREPARE.validate_descriptor(descriptor_path)
    assert descriptor["artifact"]["raw_sha256"] == PREPARE.RAW_SHA256
    assert COMPOSE.short_name("loader.efi") == b"LOADER  EFI"
    assert COMPOSE.short_name("RIBON") == b"RIBON      "
    loader = valid_pe(COMPOSE.FREEBSD_LOADER_MARKER)
    facts = COMPOSE.validate_pe(bytes(loader), COMPOSE.FREEBSD_LOADER_MARKER)
    assert facts == {"entry_rva": 0x1000, "machine": 0x8664, "subsystem": 10}

    for hostile in ("", ".", "..", "../RIBON", "TOO-LONG-NAME.EFI", "A/B"):
        must_reject(lambda value=hostile: COMPOSE.short_name(value))
    wrong_machine = valid_pe(COMPOSE.FREEBSD_LOADER_MARKER)
    wrong_machine[0x84:0x86] = (0xAA64).to_bytes(2, "little")
    must_reject(lambda: COMPOSE.validate_pe(bytes(wrong_machine), COMPOSE.FREEBSD_LOADER_MARKER))
    missing_marker = valid_pe()
    must_reject(lambda: COMPOSE.validate_pe(bytes(missing_marker), COMPOSE.FREEBSD_LOADER_MARKER))

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        hostile_descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
        hostile_descriptor["artifact"]["raw_sha256"] = "0" * 64
        bad = directory / "descriptor.json"
        bad.write_text(json.dumps(hostile_descriptor), encoding="utf-8")
        must_reject(lambda: PREPARE.validate_descriptor(bad))
        short = directory / "short.img"
        short.write_bytes(bytes(512))
        must_reject(lambda: PREPARE.validate_disk_layout(short))

    product = args.product.resolve()
    report_path = product / "results/package.json"
    loader_path = product / "payload/loader.efi"
    disk_path = product / "FreeBSD-15.1-Ribon-amd64.img"
    if not report_path.is_file() or not loader_path.is_file() or not disk_path.is_file():
        raise AssertionError("FreeBSD product outputs are absent")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert report["schema"] == "ribon-freebsd-uefi-package-v1"
    assert report["official_source"]["immutable"] is True
    assert report["official_source"]["sha256"] == PREPARE.RAW_SHA256
    assert report["files"]["/EFI/FREEBSD/LOADER.EFI"]["sha256"] == COMPOSE.sha256_file(loader_path)
    assert report["composed"]["sha256"] == COMPOSE.sha256_file(disk_path)
    print("RIBON-EXTERNAL-FREEBSD-TESTS-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
