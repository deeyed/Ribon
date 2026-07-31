#!/usr/bin/env python3
"""Unit tests for deterministic boot-module component bundle generation."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "generate_boot_module_bundle.py"
PRODUCT_MANIFEST = (
    ROOT / "products/bootmgr/manifests/qemu-aarch64-virt-modules-fixture.json"
)
SPEC = importlib.util.spec_from_file_location("ribon_boot_module_bundle", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def component(name: str, role: str, source: str, data: bytes) -> dict[str, object]:
    """Return one complete exact component manifest entry."""

    return {
        "expected_sha256": hashlib.sha256(data).hexdigest(),
        "expected_size": len(data),
        "maximum_size": max(len(data), 4096),
        "name": name,
        "role": role,
        "source": source,
    }


class BootModuleBundleTests(unittest.TestCase):
    """Exercise manifest closure, order, and hermetic generated outputs."""

    def write_manifest(
        self,
        root: Path,
        entries: list[dict[str, object]],
    ) -> Path:
        """Write one source-owned manifest and its caller-prepared files."""

        path = root / "manifest.json"
        path.write_text(
            json.dumps(
                {
                    "components": entries,
                    "schema": "ribon-boot-module-components-v1",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        return path

    def generate(self, manifest: Path, root: Path) -> tuple[Path, Path, Path]:
        """Generate the three public artifacts under one product root."""

        assembly = root / "generated" / "boot-modules" / "bundle.S"
        descriptors = root / "generated" / "boot-modules" / "descriptor.c"
        provenance = root / "results" / "boot-modules.json"
        GENERATOR.generate(
            manifest,
            PRODUCT_MANIFEST,
            root,
            assembly,
            descriptors,
            provenance,
        )
        return assembly, descriptors, provenance

    def test_initial_auxiliary_and_order_are_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source"
            source.mkdir()
            initial = b"initial-image\x00"
            auxiliary = b"auxiliary-firmware\x00"
            (source / "initial.bin").write_bytes(initial)
            (source / "aux.bin").write_bytes(auxiliary)
            manifest = self.write_manifest(
                source,
                [
                    component("initial-user", "initial-image", "initial.bin", initial),
                    component("device-fw", "auxiliary", "aux.bin", auxiliary),
                ],
            )
            assembly, descriptors, provenance = self.generate(
                manifest, Path(raw) / "out"
            )
            report = json.loads(provenance.read_text(encoding="utf-8"))
            self.assertEqual(report["component_count"], 2)
            self.assertEqual(
                report["product_id"],
                "bootmgr.qemu-aarch64-virt-modules-fixture",
            )
            self.assertEqual(
                [item["name"] for item in report["components"]],
                ["initial-user", "device-fw"],
            )
            self.assertEqual(
                [item["role"] for item in report["components"]],
                ["initial-image", "auxiliary"],
            )
            self.assertIn('.incbin "boot-module-components/000.bin"', assembly.read_text())
            self.assertIn("RIBON_BOOT_MODULE_ROLE_INITIAL_IMAGE", descriptors.read_text())

    def test_auxiliary_only_is_valid(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source"
            source.mkdir()
            data = b"firmware"
            (source / "fw.bin").write_bytes(data)
            manifest = self.write_manifest(
                source, [component("firmware", "auxiliary", "fw.bin", data)]
            )
            _assembly, _descriptors, provenance = self.generate(
                manifest, Path(raw) / "out"
            )
            report = json.loads(provenance.read_text(encoding="utf-8"))
            self.assertEqual(report["components"][0]["role"], "auxiliary")

    def test_outputs_are_root_independent_and_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source"
            source.mkdir()
            data = b"deterministic-component"
            (source / "input.bin").write_bytes(data)
            manifest = self.write_manifest(
                source, [component("stable", "initial-image", "input.bin", data)]
            )
            first = self.generate(manifest, Path(raw) / "first")
            second = self.generate(manifest, Path(raw) / "second")
            for left, right in zip(first, second):
                self.assertEqual(left.read_bytes(), right.read_bytes())
            self.assertEqual(
                (Path(raw) / "first/generated/boot-modules/boot-module-components/000.bin").read_bytes(),
                (Path(raw) / "second/generated/boot-modules/boot-module-components/000.bin").read_bytes(),
            )

    def test_duplicate_initial_ninth_and_unknown_role_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw)
            data = b"x"
            (source / "x.bin").write_bytes(data)
            duplicate = [
                component("a", "initial-image", "x.bin", data),
                component("b", "initial-image", "x.bin", data),
            ]
            with self.assertRaisesRegex(ValueError, "singleton"):
                GENERATOR._validate_manifest(self.write_manifest(source, duplicate))
            nine = [
                component(f"m{index}", "auxiliary", "x.bin", data)
                for index in range(9)
            ]
            with self.assertRaisesRegex(ValueError, "1..8"):
                GENERATOR._validate_manifest(self.write_manifest(source, nine))
            invalid = [component("bad", "unknown", "x.bin", data)]
            with self.assertRaisesRegex(ValueError, "unknown role"):
                GENERATOR._validate_manifest(self.write_manifest(source, invalid))

    def test_zero_short_corrupt_and_oversize_inputs_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw)
            (source / "empty.bin").write_bytes(b"")
            empty = component("empty", "auxiliary", "empty.bin", b"")
            with self.assertRaisesRegex(ValueError, "size"):
                GENERATOR._validate_manifest(self.write_manifest(source, [empty]))

            data = b"abc"
            (source / "data.bin").write_bytes(data)
            short = component("short", "auxiliary", "data.bin", data)
            short["expected_size"] = len(data) + 1
            with self.assertRaisesRegex(ValueError, "size or identity"):
                GENERATOR._validate_manifest(self.write_manifest(source, [short]))

            corrupt = component("corrupt", "auxiliary", "data.bin", data)
            corrupt["expected_sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                GENERATOR._validate_manifest(self.write_manifest(source, [corrupt]))

            oversize = component("oversize", "auxiliary", "data.bin", data)
            oversize["maximum_size"] = 2
            with self.assertRaisesRegex(ValueError, "maximum size"):
                GENERATOR._validate_manifest(self.write_manifest(source, [oversize]))

    def test_duplicate_name_and_path_escape_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source"
            source.mkdir()
            data = b"x"
            (source / "x.bin").write_bytes(data)
            duplicate = [
                component("same", "auxiliary", "x.bin", data),
                component("same", "auxiliary", "x.bin", data),
            ]
            with self.assertRaisesRegex(ValueError, "duplicate"):
                GENERATOR._validate_manifest(self.write_manifest(source, duplicate))
            escaped = [component("escape", "auxiliary", "../x.bin", data)]
            with self.assertRaisesRegex(ValueError, "traverse"):
                GENERATOR._validate_manifest(self.write_manifest(source, escaped))

    def test_intermediate_source_directory_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw) / "source"
            real = source / "real"
            real.mkdir(parents=True)
            data = b"x"
            (real / "x.bin").write_bytes(data)
            (source / "linked").symlink_to(real, target_is_directory=True)
            manifest = self.write_manifest(
                source,
                [component("linked", "auxiliary", "linked/x.bin", data)],
            )
            with self.assertRaisesRegex(ValueError, "symlink"):
                GENERATOR._validate_manifest(manifest)

    def test_name_length_matches_runtime_bound(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            source = Path(raw)
            data = b"x"
            (source / "x.bin").write_bytes(data)
            accepted = [component("n" * 63, "auxiliary", "x.bin", data)]
            self.assertEqual(
                len(GENERATOR._validate_manifest(
                    self.write_manifest(source, accepted)
                )[0]["name"]),
                63,
            )
            rejected = [component("n" * 64, "auxiliary", "x.bin", data)]
            with self.assertRaisesRegex(ValueError, "invalid"):
                GENERATOR._validate_manifest(
                    self.write_manifest(source, rejected)
                )

    def test_product_manifest_must_authorize_exact_bundle_service(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            product = json.loads(PRODUCT_MANIFEST.read_text(encoding="utf-8"))
            product["required_capabilities"].remove("BOOT_MODULE_BUNDLE")
            path = Path(raw) / "product.json"
            path.write_text(json.dumps(product), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "does not authorize"):
                GENERATOR._validate_product_manifest(path)
            with self.assertRaisesRegex(ValueError, "does not authorize"):
                GENERATOR._validate_product_manifest(
                    ROOT / "products/bootmgr/manifests/qemu-aarch64-virt-parus.json"
                )

    def test_generated_output_cannot_escape_build_root(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "source"
            source.mkdir()
            data = b"x"
            (source / "x.bin").write_bytes(data)
            manifest = self.write_manifest(
                source, [component("x", "auxiliary", "x.bin", data)]
            )
            with self.assertRaisesRegex(ValueError, "inside --output-root"):
                GENERATOR.generate(
                    manifest,
                    PRODUCT_MANIFEST,
                    root / "out",
                    root / "escaped.S",
                    root / "out/descriptor.c",
                    root / "out/provenance.json",
                )


if __name__ == "__main__":
    unittest.main()
