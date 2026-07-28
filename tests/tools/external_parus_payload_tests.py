#!/usr/bin/env python3
"""Unit tests for the external Parus payload product boundary."""

from __future__ import annotations

import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = ROOT / "tools" / "validate_external_parus_payload.py"
ARM64_MANIFEST = (
    ROOT
    / "products/bootmgr/manifests/qemu-aarch64-virt-parus-external.json"
)
RISCV64_MANIFEST = (
    ROOT
    / "products/bootmgr/manifests/qemu-riscv64-virt-parus-external.json"
)


def load_validator():
    """Load the source-owned validator without a package dependency."""

    spec = importlib.util.spec_from_file_location(
        "ribon_external_parus_payload",
        VALIDATOR_PATH,
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_elf(path: Path, machine: int = 183, base: int = 0x41000000) -> None:
    """Write one minimal ELF64 image with an executable PT_LOAD."""

    ident = b"\x7fELF" + bytes((2, 1, 1, 0)) + bytes(8)
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        ident,
        2,
        machine,
        1,
        base,
        64,
        0,
        0,
        64,
        56,
        1,
        0,
        0,
        0,
    )
    program = struct.pack(
        "<IIQQQQQQ",
        1,
        5,
        0x100,
        base,
        base,
        4,
        0x1000,
        0x1000,
    )
    path.write_bytes(header + program + bytes(0x100 - 120) + b"\0\0\0\0")


class ExternalParusPayloadTests(unittest.TestCase):
    """Reject wrong ISA/window tuples and preserve positive provenance."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.validator = load_validator()

    def test_accepts_aarch64_rph1_payload_in_product_window(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            payload = Path(raw) / "parus.elf"
            write_elf(payload)
            report = self.validator.validate(ARM64_MANIFEST, payload)
            self.assertTrue(report["success"])
            self.assertEqual(report["entry_abi"], "arm64-rph1-v1")
            self.assertTrue(report["payload"]["immutable"])

    def test_accepts_riscv64_rph1_payload_in_product_window(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            payload = Path(raw) / "parus.elf"
            write_elf(payload, machine=243, base=0x80400000)
            report = self.validator.validate(RISCV64_MANIFEST, payload)
            self.assertTrue(report["success"])
            self.assertEqual(report["architecture"], "riscv64")
            self.assertEqual(report["entry_abi"], "riscv-rph1-v1")
            self.assertTrue(report["payload"]["immutable"])

    def test_rejects_wrong_architecture(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            payload = Path(raw) / "wrong-arch.elf"
            write_elf(payload, machine=62)
            with self.assertRaisesRegex(ValueError, "machine does not match"):
                self.validator.validate(ARM64_MANIFEST, payload)

    def test_rejects_segment_outside_product_window(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            payload = Path(raw) / "outside.elf"
            write_elf(payload, base=0x42000000)
            with self.assertRaisesRegex(ValueError, "product window"):
                self.validator.validate(ARM64_MANIFEST, payload)

    def test_rejects_manifest_contract_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw)
            payload = directory / "parus.elf"
            manifest = directory / "manifest.json"
            write_elf(payload)
            document = json.loads(ARM64_MANIFEST.read_text(encoding="utf-8"))
            document["payload"]["entry_abi"] = "arm64-fdt-v1"
            manifest.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "RPH1 tuple"):
                self.validator.validate(manifest, payload)


if __name__ == "__main__":
    unittest.main()
