#!/usr/bin/env python3
"""Hostile tests for the typed multi-OS runtime evidence closure."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_multi_os_runtime", ROOT / "tools" / "check_multi_os_runtime.py"
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MultiOsRuntimeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.revision = "1" * 40

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_result(self, label: str) -> Path:
        expected = MODULE.EXPECTED_ROWS[label]
        payload = self.root / f"{label}.payload"
        composed = self.root / f"{label}.composed"
        serial = self.root / f"{label}.log"
        payload.write_bytes(label.encode("ascii"))
        composed.write_bytes(b"composed-" + label.encode("ascii"))
        serial.write_bytes(b"serial-" + label.encode("ascii"))
        payload_hash = MODULE.sha256_file(payload)
        composed_hash = MODULE.sha256_file(composed)
        result = {
            "schema": "ribon-qemu-payload-evidence-v1",
            "outcome": "passed",
            "first_divergence": None,
            "source_revision": self.revision,
            "target": expected["target"],
            "observed_payload_class": expected["payload_class"],
            "terminal": expected["terminal"],
            "cleanup": {
                "complete": True,
                "forced_kill": False,
                "process_group_alive_after_cleanup": False,
            },
            "raw_serial": {
                "path": str(serial),
                "preserved": True,
                "sha256": MODULE.sha256_file(serial),
            },
            "payload": {
                "path": str(payload),
                "immutable": True,
                "sha256": payload_hash,
                "sha256_after_run": payload_hash,
            },
            "composed_artifact": {
                "path": str(composed),
                "immutable": True,
                "sha256": composed_hash,
                "sha256_after_run": composed_hash,
            },
            "product_manifest": {"product_id": f"test.{label}"},
            "qemu": {"version": "test-qemu"},
        }
        path = self.root / f"{label}.json"
        path.write_text(json.dumps(result), encoding="utf-8")
        return path

    def test_all_typed_rows_validate(self) -> None:
        for label in MODULE.EXPECTED_ROWS:
            path = self.write_result(label)
            row = MODULE.validate_result(label, path, self.revision)
            self.assertEqual(row["evidence_class"], MODULE.EXPECTED_ROWS[label]["evidence_class"])

    def test_revision_mismatch_is_rejected(self) -> None:
        label = "linux-riscv64-opensbi"
        with self.assertRaisesRegex(ValueError, "result contract mismatch"):
            MODULE.validate_result(label, self.write_result(label), "2" * 40)

    def test_forced_cleanup_is_rejected(self) -> None:
        label = "freebsd-amd64-uefi"
        path = self.write_result(label)
        report = json.loads(path.read_text(encoding="utf-8"))
        report["cleanup"]["forced_kill"] = True
        path.write_text(json.dumps(report), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "result contract mismatch"):
            MODULE.validate_result(label, path, self.revision)


if __name__ == "__main__":
    unittest.main()
