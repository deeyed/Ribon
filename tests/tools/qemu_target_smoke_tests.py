#!/usr/bin/env python3
"""Contract tests for payload-aware QEMU evidence classification."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools" / "qemu_target_smoke.py"


def load_harness_module():
    """Load the evidence harness for bounded classifier unit tests."""

    spec = importlib.util.spec_from_file_location("qemu_target_smoke", HARNESS)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
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

    def test_freebsd_loader_has_distinct_observed_class(self) -> None:
        """An official-loader marker cannot be reported as a Linux EFI stub."""

        with tempfile.TemporaryDirectory() as temporary:
            loader = Path(temporary) / "loader.efi"
            loader.write_bytes(
                b"MZ" + bytes(62)
                + b"FreeBSD/amd64 EFI loader, Revision 3.0"
            )
            self.assertEqual(
                load_harness_module().observed_payload_class(loader),
                "freebsd-efi",
            )

    def test_efi_without_freebsd_marker_remains_linux_class(self) -> None:
        """Expected class declarations cannot relabel an arbitrary EFI image."""

        with tempfile.TemporaryDirectory() as temporary:
            loader = Path(temporary) / "other.efi"
            loader.write_bytes(b"MZ" + bytes(128))
            self.assertEqual(
                load_harness_module().observed_payload_class(loader),
                "linux-efi",
            )

    def run_harness(
        self,
        directory: Path,
        payload: Path,
        payload_class: str,
        target: str = "aarch64-virt-raw-fdt",
        required_markers: tuple[str, ...] = (),
        required_markers_anywhere: tuple[str, ...] = (),
        extra_output: str = "",
        module_provenance: Path | None = None,
        product_manifest: Path | None = None,
        mutate_module_provenance: bool = False,
        mutate_composed_artifact: bool = False,
        corrupt_module_image: bool = False,
        tail_output_on_terminate: str = "",
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        """Run the harness against a deterministic fake QEMU process."""
        fake_qemu = directory / "fake-qemu.py"
        image = directory / "ribon.bin"
        mutation = (
            "    from pathlib import Path\n"
            f"    p = Path({str(module_provenance)!r})\n"
            "    p.write_bytes(p.read_bytes() + b' ')\n"
            if mutate_module_provenance and module_provenance is not None
            else ""
        )
        image_mutation = (
            "    from pathlib import Path\n"
            f"    p = Path({str(image)!r})\n"
            "    p.write_bytes(p.read_bytes() + b'changed')\n"
            if mutate_composed_artifact
            else ""
        )
        tail_handler = (
            "import signal\n"
            "import time\n"
            "def emit_tail(_signum, _frame):\n"
            f"    print({tail_output_on_terminate!r}, flush=True)\n"
            "    raise SystemExit(0)\n"
            "signal.signal(signal.SIGTERM, emit_tail)\n"
            if tail_output_on_terminate
            else ""
        )
        tail_wait = (
            "    sys.stdout.flush()\n"
            "    while True:\n"
            "        time.sleep(1)\n"
            if tail_output_on_terminate
            else ""
        )
        fake_qemu.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            f"{tail_handler}"
            "if '--version' in sys.argv:\n"
            "    print('QEMU emulator version test')\n"
            "else:\n"
            f"{mutation}"
            f"{image_mutation}"
            f"    print({' '.join(TRANSFER_MARKERS)!r})\n"
            f"    print({extra_output!r})\n"
            f"{tail_wait}",
            encoding="utf-8",
        )
        fake_qemu.chmod(0o755)
        image.write_bytes(b"RIBON")
        if module_provenance is not None:
            try:
                provenance = json.loads(
                    module_provenance.read_text(encoding="utf-8")
                )
                module_image = bytearray(4096)
                product_root = module_provenance.parent.parent
                for component in provenance["components"]:
                    data = (
                        product_root / component["snapshot"]
                    ).read_bytes()
                    module_image.extend(data)
                    module_image.extend(
                        bytes((-len(data)) % 4096)
                    )
                if corrupt_module_image:
                    module_image[4096] ^= 0xff
                image.write_bytes(module_image)
            except (KeyError, OSError, json.JSONDecodeError, TypeError):
                pass
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
            "3",
            "--log",
            str(log),
            "--result",
            str(result),
        ]
        if target == "riscv64-virt-opensbi":
            command.extend(("--firmware", str(firmware)))
        for marker in required_markers:
            command.extend(("--required-marker", marker))
        for marker in required_markers_anywhere:
            command.extend(("--required-marker-anywhere", marker))
        if module_provenance is not None:
            command.extend(("--module-provenance", str(module_provenance)))
        if product_manifest is not None:
            command.extend(("--product-manifest", str(product_manifest)))
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

    def write_module_provenance(self, directory: Path) -> tuple[Path, Path]:
        """Write one valid generated-bundle-shaped report and snapshot."""

        data = b"initial-module"
        snapshot = (
            directory
            / "generated"
            / "boot-modules"
            / "boot-module-components"
            / "000.bin"
        )
        snapshot.parent.mkdir(parents=True)
        snapshot.write_bytes(data)
        digest = hashlib.sha256()
        digest.update(b"ribon-boot-module-bundle-provenance-v1\0")
        digest.update(b"initial-user\0")
        digest.update(b"initial-image\0")
        digest.update(len(data).to_bytes(8, "little"))
        digest.update(data)
        provenance = directory / "results" / "boot-modules.json"
        provenance.parent.mkdir(parents=True)
        product = (
            ROOT
            / "products/bootmgr/manifests/"
            / "qemu-aarch64-virt-modules-fixture.json"
        )
        product_document = json.loads(product.read_text(encoding="utf-8"))
        provenance.write_text(
            json.dumps(
                {
                    "bundle_sha256": digest.hexdigest(),
                    "component_count": 1,
                    "components": [
                        {
                            "index": 0,
                            "maximum_size": 4096,
                            "name": "initial-user",
                            "role": "initial-image",
                            "sha256": hashlib.sha256(data).hexdigest(),
                            "size": len(data),
                            "snapshot": snapshot.relative_to(directory).as_posix(),
                            "source": "initial.bin",
                        }
                    ],
                    "product_id": product_document["product_id"],
                    "product_manifest_sha256": hashlib.sha256(
                        product.read_bytes()
                    ).hexdigest(),
                    "schema": "ribon-boot-module-bundle-provenance-v1",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        return provenance, product

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

    def test_anywhere_marker_is_unique_and_position_independent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                required_markers_anywhere=("ASYNC-RECEIPT",),
                extra_output="ASYNC-RECEIPT",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            self.assertEqual(result["outcome"], "passed")
            self.assertEqual(
                result["marker_observations_anywhere"][0]["count"], 1
            )

    def test_anywhere_marker_missing_or_duplicate_is_rejected(self) -> None:
        for output, divergence in (
            ("", "missing-anywhere:ASYNC-RECEIPT"),
            ("ASYNC-RECEIPT ASYNC-RECEIPT", "duplicate-anywhere:ASYNC-RECEIPT"),
        ):
            with self.subTest(output=output), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                payload = directory / "parus.elf"
                payload.write_bytes(b"\x7fELF" + b"\0" * 128)
                completed, result = self.run_harness(
                    directory,
                    payload,
                    "kernel",
                    required_markers_anywhere=("ASYNC-RECEIPT",),
                    extra_output=output,
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertEqual(result["first_divergence"], divergence)

    def test_malformed_module_provenance_is_rejected_before_launch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            provenance = directory / "bad.json"
            provenance.write_text("{", encoding="utf-8")
            product = directory / "product.json"
            product.write_text('{"product_id":"test.product"}\n', encoding="utf-8")
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                module_provenance=provenance,
                product_manifest=product,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "module-provenance-invalid")
            self.assertFalse(result["cleanup"]["launched"])

    def test_module_provenance_mutation_invalidates_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            provenance, product = self.write_module_provenance(directory)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                module_provenance=provenance,
                product_manifest=product,
                mutate_module_provenance=True,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "module-provenance-mutated")
            self.assertFalse(result["boot_module_bundle"]["immutable"])

    def test_module_provenance_requires_product_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            provenance, _product = self.write_module_provenance(directory)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                module_provenance=provenance,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(
                result["outcome"], "module-product-manifest-required"
            )
            self.assertFalse(result["cleanup"]["launched"])

    def test_module_product_requires_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            _provenance, product = self.write_module_provenance(directory)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                product_manifest=product,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "module-provenance-required")
            self.assertFalse(result["cleanup"]["launched"])

    def test_module_product_requires_exact_raw_fdt_authority(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            provenance, _product = self.write_module_provenance(directory)
            forged_product = directory / "forged-product.json"
            forged_product.write_text(
                json.dumps(
                    {
                        "product_id":
                            "bootmgr.qemu-aarch64-virt-modules-fixture"
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            report = json.loads(provenance.read_text(encoding="utf-8"))
            report["product_manifest_sha256"] = hashlib.sha256(
                forged_product.read_bytes()
            ).hexdigest()
            provenance.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                module_provenance=provenance,
                product_manifest=forged_product,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "module-product-mismatch")
            self.assertFalse(result["cleanup"]["launched"])

    def test_module_provenance_must_match_composed_image(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            provenance, product = self.write_module_provenance(directory)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                module_provenance=provenance,
                product_manifest=product,
                corrupt_module_image=True,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "module-image-mismatch")
            self.assertFalse(result["cleanup"]["launched"])

    def test_fatal_kernel_markers_are_terminal_failures(self) -> None:
        for marker in ("PANIC", "Unhandled exception"):
            with self.subTest(marker=marker), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                payload = directory / "parus.elf"
                payload.write_bytes(b"\x7fELF" + b"\0" * 128)
                completed, result = self.run_harness(
                    directory,
                    payload,
                    "kernel",
                    extra_output=marker,
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertEqual(result["outcome"], "payload-failure")

    def test_fatal_tail_after_required_markers_revokes_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                tail_output_on_terminate="PANIC: after terminal marker",
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "payload-failure")
            self.assertEqual(
                result["first_divergence"],
                "payload-failure-after-required-evidence",
            )

    def test_composed_artifact_mutation_revokes_pass(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            payload = directory / "parus.elf"
            payload.write_bytes(b"\x7fELF" + b"\0" * 128)
            completed, result = self.run_harness(
                directory,
                payload,
                "kernel",
                mutate_composed_artifact=True,
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(result["outcome"], "composed-artifact-mutated")
            self.assertFalse(result["composed_artifact"]["immutable"])


if __name__ == "__main__":
    unittest.main()
