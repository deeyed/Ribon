#!/usr/bin/env python3
"""Negative corpus for the pinned Linux AArch64 external-input contract."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_external_linux_image",
    ROOT / "tools" / "prepare_external_linux_image.py",
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def expect_failure(callback, label: str) -> None:
    try:
        callback()
    except ValueError:
        return
    raise AssertionError(f"{label} was accepted")


def main() -> int:
    image = bytearray(128)
    image[56:60] = b"ARMd"
    image[16:24] = (4096).to_bytes(8, "little")
    digest = hashlib.sha256(image).hexdigest()
    descriptor = {
        "schema": "ribon-external-linux-image-v1",
        "source": {
            "distribution": "fixture",
            "release": "1",
            "target": "armsr/armv8",
            "url": "https://example.invalid/Image",
        },
        "artifact": {
            "architecture": "aarch64",
            "class": "linux-aarch64-image",
            "filename": "Image",
            "expected_sha256": digest,
            "expected_size": len(image),
            "maximum_size": 4096,
        },
        "license": {"notice": "fixture", "spdx": "GPL-2.0-only"},
        "provenance": {
            "kind": "pinned-upstream-release-artifact",
            "verification": "fixture",
        },
    }
    product = {
        "product_id": "fixture.linux",
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "boot_protocols": ["linux"],
        "payload": {
            "architecture": "aarch64",
            "format": "linux-aarch64-image",
            "entry_abi": "arm64-linux-fdt-v1",
            "load_base": 0x41000000,
            "load_size": 4096,
        },
    }
    with tempfile.TemporaryDirectory() as directory_text:
        directory = Path(directory_text)
        descriptor_path = directory / "input.json"
        product_path = directory / "product.json"
        descriptor_path.write_text(json.dumps(descriptor), encoding="utf-8")
        product_path.write_text(json.dumps(product), encoding="utf-8")
        loaded = MODULE.load_descriptor(descriptor_path)
        assert MODULE.validate_image(bytes(image), loaded)["sha256"] == digest
        assert MODULE.validate_product(product_path, loaded)["product_id"] == "fixture.linux"

        corrupt = bytearray(image)
        corrupt[0] ^= 1
        expect_failure(
            lambda: MODULE.validate_image(bytes(corrupt), loaded),
            "digest mismatch",
        )
        wrong_magic = bytearray(image)
        wrong_magic[56:60] = b"NOPE"
        wrong_magic_digest = hashlib.sha256(wrong_magic).hexdigest()
        wrong_magic_descriptor = json.loads(json.dumps(loaded))
        wrong_magic_descriptor["artifact"]["expected_sha256"] = wrong_magic_digest
        expect_failure(
            lambda: MODULE.validate_image(bytes(wrong_magic), wrong_magic_descriptor),
            "wrong image class",
        )
        oversized_descriptor = json.loads(json.dumps(loaded))
        oversized_descriptor["artifact"]["maximum_size"] = len(image) - 1
        expect_failure(
            lambda: MODULE.validate_image(bytes(image), oversized_descriptor),
            "oversized image",
        )
        wrong_arch_product = json.loads(json.dumps(product))
        wrong_arch_product["architecture"] = "riscv64"
        product_path.write_text(json.dumps(wrong_arch_product), encoding="utf-8")
        expect_failure(
            lambda: MODULE.validate_product(product_path, loaded),
            "wrong architecture",
        )
        wrong_class_descriptor = json.loads(json.dumps(descriptor))
        wrong_class_descriptor["artifact"]["class"] = "elf64"
        descriptor_path.write_text(
            json.dumps(wrong_class_descriptor), encoding="utf-8"
        )
        expect_failure(
            lambda: MODULE.load_descriptor(descriptor_path),
            "wrong descriptor class",
        )
    print("RIBON-D07-EXTERNAL-LINUX-INPUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
