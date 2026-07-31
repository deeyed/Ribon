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
    "RIBON-R4-PROTOCOL-HANDOFF-OK",
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
        target: str = "aarch64-virt-raw-fdt",
        required_markers: tuple[str, ...] = (),
        extra_output: str = "",
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        """Run the harness against a deterministic fake QEMU process."""
        fake_qemu = directory / "fake-qemu.py"
        fake_qemu.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            "if '--version' in sys.argv:\n"
            "    print('QEMU emulator version test')\n"
            "else:\n"
            f"    print({' '.join(TRANSFER_MARKERS)!r})\n"
            f"    print({extra_output!r})\n",
            encoding="utf-8",
        )
        fake_qemu.chmod(0o755)
        image = directory / "ribon.bin"
        image.write_bytes(b"RIBON")
        firmware = directory / "opensbi.bin"
        firmware.write_bytes(b"OpenSBI")
        log = directory / "serial.log"
        result = directory / "result.json"
        command = [
            sys.executable,
            str(HARNESS),
            "--target",
            target,
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
        ]
        if target == "riscv64-virt-opensbi":
            command.extend(("--firmware", str(firmware)))
        for marker in required_markers:
            command.extend(("--required-marker", marker))
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.assertTrue(
            result.is_file(),
            "QEMU evidence harness did not publish its result:\n"
            f"{completed.stdout}",
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

    def test_riscv64_records_opensbi_firmware_provenance(self) -> None:
        """The RISC-V lane requires and hashes its firmware authority."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                target="riscv64-virt-opensbi",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            self.assertEqual(result["outcome"], "passed")
            self.assertEqual(result["target"], "riscv64-virt-opensbi")
            self.assertEqual(
                result["firmware"]["path"],
                str(directory / "opensbi.bin"),
            )
            self.assertEqual(len(result["firmware"]["sha256"]), 64)

    def test_riscv64_rph1_fixture_uses_its_own_marker_graph(self) -> None:
        """The Ribon-owned RPH1 fixture remains distinct from a Parus kernel."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "rph1-fixture.elf"
            payload.write_bytes(
                b"\x7fELF"
                + b"RIBON-RISCV64-RPH1-FIXTURE-V1"
                + b"\0" * 128
            )
            markers = (
                "RIBON-RPH1-RISCV64-FIXTURE-ENTRY",
                "RIBON-RPH1-RISCV64-FIXTURE-MMU-OFF",
                "RIBON-RPH1-RISCV64-FIXTURE-RPH1-OK",
                "RIBON-RPH1-RISCV64-FIXTURE-BOOT-CPU-OK",
            )
            completed, result = self.run_harness(
                directory,
                payload,
                "fixture",
                target="riscv64-virt-opensbi",
                required_markers=markers,
                extra_output="\n".join(
                    (*markers, "RIBON-RPH1-RISCV64-FIXTURE-OK")
                ),
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            self.assertEqual(result["outcome"], "passed")
            self.assertEqual(result["observed_payload_class"], "fixture")
            self.assertEqual(result["expected_product_class"], "fixture-smoke")
            self.assertNotIn(
                "PARUS-FIXTURE-ENTRY-OK",
                result["required_markers"],
            )

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

    def test_riscv64_fixture_failure_marker_is_terminal(self) -> None:
        """A Ribon RPH1 fixture failure cannot wait until generic timeout."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "rph1-fixture.elf"
            payload.write_bytes(
                b"\x7fELF"
                + b"RIBON-RISCV64-RPH1-FIXTURE-V1"
                + b"\0" * 128
            )
            completed, result = self.run_harness(
                directory,
                payload,
                "fixture",
                target="riscv64-virt-opensbi",
                extra_output="RIBON-RPH1-RISCV64-FIXTURE-FAIL:crc32c",
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "payload-abi-failure")
            self.assertEqual(result["terminal"], "payload-abi-failure")

    def test_external_kernel_requires_payload_terminal_receipt(self) -> None:
        """A transfer marker alone cannot satisfy an external-kernel claim."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                required_markers=("PARUS:BM:v0:06000200:IDLE:OK:NONE",),
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "early-exit")
            self.assertEqual(
                result["first_divergence"],
                "missing:PARUS:BM:v0:06000200:IDLE:OK:NONE",
            )

    def test_repeated_required_marker_is_canonicalized(self) -> None:
        """Repeated CLI requirements do not create a false ordering failure."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                required_markers=(
                    "RIBON-R4-RAW-FDT-TRANSFER",
                    "RIBON-R4-RAW-FDT-TRANSFER",
                ),
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            self.assertEqual(
                result["required_markers"].count(
                    "RIBON-R4-RAW-FDT-TRANSFER"
                ),
                1,
            )

    def test_zero_failure_counter_is_not_terminal_failure(self) -> None:
        """A diagnostic field named FAIL with zero count is not a boot failure."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                extra_output=(
                    "PARUS:ATTACH:v0:ATTACH:FAIL:COUNT="
                    "0x0000000000000000"
                ),
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            self.assertEqual(result["outcome"], "passed")


if __name__ == "__main__":
    unittest.main()
