#!/usr/bin/env python3
"""Contract tests for payload-aware QEMU evidence classification."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools" / "qemu_target_smoke.py"
TRANSFER_MARKERS = (
    "RIBON-R4-RAW-FDT-ENTRY",
    "RIBON-R4-FDT-ACCEPTED",
    "RIBON-R4-PRODUCT-GRAPH-OK",
    "RIBON-R4-PARUS-RPH1-OK",
    "RIBON-R4-PAYLOAD-LOADED",
    "RIBON-R4-RAW-FDT-TRANSFER",
)


class QemuTargetSmokeTests(unittest.TestCase):
    """Verify actual-kernel and fixture evidence cannot masquerade."""

    def run_harness(
        self,
        directory: Path,
        payload: Path,
        payload_class: str,
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        """Run the harness against a deterministic fake QEMU process."""
        fake_qemu = directory / "fake-qemu.py"
        fake_qemu.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            "if '--version' in sys.argv:\n"
            "    print('QEMU emulator version test')\n"
            "else:\n"
            f"    print({' '.join(TRANSFER_MARKERS)!r})\n",
            encoding="utf-8",
        )
        fake_qemu.chmod(0o755)
        image = directory / "ribon.bin"
        image.write_bytes(b"RIBON")
        log = directory / "serial.log"
        result = directory / "result.json"
        completed = subprocess.run(
            [
                sys.executable,
                str(HARNESS),
                "--target",
                "aarch64-virt-raw-fdt",
                "--qemu",
                str(fake_qemu),
                "--image",
                str(image),
                "--payload",
                str(payload),
                "--expected-payload-class",
                payload_class,
                "--source-revision",
                "test-revision",
                "--timeout",
                "1",
                "--log",
                str(log),
                "--result",
                str(result),
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        return completed, json.loads(result.read_text(encoding="utf-8"))

    def test_external_kernel_payload_records_positive_provenance(self) -> None:
        """An immutable non-fixture ELF may open external-kernel evidence."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            self.assertEqual(result["outcome"], "passed")
            self.assertEqual(result["observed_payload_class"], "kernel")
            self.assertTrue(result["payload"]["immutable"])
            self.assertTrue(result["cleanup"]["complete"])
            self.assertFalse(result["cleanup"]["forced_kill"])

    def test_fixture_cannot_masquerade_as_external_kernel(self) -> None:
        """A fixture marker rejects an external-kernel product claim."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "fixture.elf"
            payload.write_bytes(
                b"\x7fELF" + b"PARUS-FIXTURE-ENTRY-OK" + b"\0" * 64
            )
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "payload-class-mismatch")
            self.assertEqual(result["observed_payload_class"], "fixture")
            self.assertFalse(result["cleanup"]["launched"])
            self.assertTrue(result["cleanup"]["complete"])


if __name__ == "__main__":
    unittest.main()
