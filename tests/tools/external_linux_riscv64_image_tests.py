#!/usr/bin/env python3
"""Hostile corpus for the pinned Linux RISC-V64 external-input contract."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_external_linux_riscv64_image",
    ROOT / "tools" / "prepare_external_linux_riscv64_image.py",
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
    image[8:16] = (2 * 1024 * 1024).to_bytes(8, "little")
    image[16:24] = (4096).to_bytes(8, "little")
    image[32:36] = (2).to_bytes(4, "little")
    image[48:56] = b"RISCV\0\0\0"
    image[56:60] = b"RSC\x05"
    image[60:64] = (64).to_bytes(4, "little")
    image[64:68] = b"PE\0\0"
    digest = hashlib.sha256(image).hexdigest()
    descriptor = {
        "schema": "ribon-external-linux-riscv64-image-v1",
        "source": {
            "distribution": "fixture",
            "release": "1",
            "target": "riscv64/netboot",
            "url": "https://example.invalid/Image",
        },
        "artifact": {
            "architecture": "riscv64",
            "class": "linux-riscv64-image",
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
        "product_id": "fixture.linux-riscv64",
        "architecture": "riscv64",
        "environment": "raw-fdt",
        "boot_protocols": ["linux"],
        "payload": {
            "architecture": "riscv64",
            "format": "linux-riscv64-image",
            "entry_abi": "riscv64-linux-fdt-v1",
            "load_base": 0x80400000,
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
        assert MODULE.validate_product(product_path, loaded)["product_id"] == \
            "fixture.linux-riscv64"

        for offset, label in (
            (8, "wrong text offset"),
            (16, "zero image size"),
            (32, "wrong header version"),
            (48, "wrong primary magic"),
            (56, "wrong secondary magic"),
            (64, "wrong PE signature"),
        ):
            corrupt = bytearray(image)
            corrupt[offset] ^= 1
            hostile = json.loads(json.dumps(loaded))
            hostile["artifact"]["expected_sha256"] = hashlib.sha256(corrupt).hexdigest()
            expect_failure(
                lambda data=bytes(corrupt), spec=hostile: MODULE.validate_image(
                    data, spec
                ),
                label,
            )

        wrong_product = json.loads(json.dumps(product))
        wrong_product["payload"]["load_base"] += 4096
        product_path.write_text(json.dumps(wrong_product), encoding="utf-8")
        expect_failure(
            lambda: MODULE.validate_product(product_path, loaded),
            "misaligned placement",
        )
        wrong_product = json.loads(json.dumps(product))
        wrong_product["payload"]["entry_abi"] = "arm64-linux-fdt-v1"
        product_path.write_text(json.dumps(wrong_product), encoding="utf-8")
        expect_failure(
            lambda: MODULE.validate_product(product_path, loaded),
            "wrong entry ABI",
        )
    print("RIBON-R04-EXTERNAL-LINUX-RISCV64-INPUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
