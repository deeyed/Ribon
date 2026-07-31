#!/usr/bin/env python3
"""Contract tests for RPi5 package-v2 boot-module provenance binding."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PACKAGER = ROOT / "tools" / "package_rpi5.py"
CHECKER = ROOT / "tools" / "check_rpi_package.py"
PRODUCT_MANIFEST = (
    ROOT / "products/bootmgr/manifests/rpi5-aarch64-modules-fixture.json"
)
PAGE_SIZE = 4096
PROVENANCE_FILE = "metadata/boot-modules.json"


def elf64_payload(path: Path) -> None:
    """Write a minimal AArch64 ELF64 with one disjoint PT_LOAD range."""

    identity = bytearray(16)
    identity[:4] = b"\x7fELF"
    identity[4] = 2
    identity[5] = 1
    identity[6] = 1
    data = bytearray(120)
    struct.pack_into(
        "<16sHHIQQQIHHHHHH",
        data,
        0,
        bytes(identity),
        2,
        183,
        1,
        0x04000000,
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
    struct.pack_into(
        "<IIQQQQQQ",
        data,
        64,
        1,
        5,
        0,
        0x04000000,
        0x04000000,
        1,
        PAGE_SIZE,
        PAGE_SIZE,
    )
    path.write_bytes(data)


def bundle_digest(
    components: list[tuple[str, str, bytes]],
) -> str:
    """Compute the canonical generated-bundle digest for fixture bytes."""

    digest = hashlib.sha256()
    digest.update(b"ribon-boot-module-bundle-provenance-v1\0")
    for name, role, data in components:
        digest.update(name.encode("ascii") + b"\0")
        digest.update(role.encode("ascii") + b"\0")
        digest.update(len(data).to_bytes(8, "little"))
        digest.update(data)
    return digest.hexdigest()


class RpiPackageTests(unittest.TestCase):
    """Ensure package module facts cannot diverge from copied provenance."""

    def run_checker(self, package: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), str(package)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

    def test_package_v2_exactly_binds_provenance_entries(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            product = root / "product"
            snapshots = (
                product
                / "generated"
                / "boot-modules"
                / "boot-module-components"
            )
            snapshots.mkdir(parents=True)
            components = [
                ("initial-user", "initial-image", b"shared-module-v1"),
                ("device-fw", "auxiliary", b"shared-module-v1"),
            ]
            image_bytes = bytearray(PAGE_SIZE * 4)
            struct.pack_into("<Q", image_bytes, 8, 0x00080000)
            struct.pack_into("<Q", image_bytes, 16, len(image_bytes))
            image_bytes[56:60] = b"ARM\x64"
            records = []
            for index, (name, role, data) in enumerate(components):
                snapshot = snapshots / f"{index:03d}.bin"
                snapshot.write_bytes(data)
                offset = PAGE_SIZE * (4 - len(components) + index)
                image_bytes[offset : offset + len(data)] = data
                records.append(
                    {
                        "index": index,
                        "maximum_size": PAGE_SIZE,
                        "name": name,
                        "role": role,
                        "sha256": hashlib.sha256(data).hexdigest(),
                        "size": len(data),
                        "snapshot": snapshot.relative_to(product).as_posix(),
                        "source": f"{index:03d}.bin",
                    }
                )
            image_bytes[PAGE_SIZE : PAGE_SIZE + len(components[0][2])] = (
                components[0][2]
            )
            image = root / "kernel8.img"
            image.write_bytes(image_bytes)
            payload = root / "payload.elf"
            elf64_payload(payload)
            config = root / "config.txt"
            config.write_text(
                "device_tree=bcm2712-rpi-5-b.dtb\n"
                "arm_64bit=1\n"
                "kernel=kernel8.img\n"
                "enable_uart=1\n"
                "enable_rp1_uart=1\n"
                "uart_2ndstage=1\n"
                "os_check=0\n"
                "pciex4_reset=0\n",
                encoding="utf-8",
            )
            cmdline = root / "cmdline.txt"
            cmdline.write_text("console=ttyAMA0\n", encoding="utf-8")
            provenance = product / "results" / "boot-modules.json"
            provenance.parent.mkdir(parents=True)
            product_bytes = PRODUCT_MANIFEST.read_bytes()
            product_document = json.loads(product_bytes.decode("utf-8"))
            provenance.write_text(
                json.dumps(
                    {
                        "bundle_sha256": bundle_digest(components),
                        "component_count": len(records),
                        "components": records,
                        "product_id": product_document["product_id"],
                        "product_manifest_sha256": hashlib.sha256(
                            product_bytes
                        ).hexdigest(),
                        "schema": "ribon-boot-module-bundle-provenance-v1",
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
            )
            package = root / "package"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(PACKAGER),
                    "--image",
                    str(image),
                    "--payload",
                    str(payload),
                    "--config",
                    str(config),
                    "--cmdline",
                    str(cmdline),
                    "--module-provenance",
                    str(provenance),
                    "--product-manifest",
                    str(PRODUCT_MANIFEST),
                    "--output",
                    str(package),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(completed.returncode, 0, completed.stdout)
            checked = self.run_checker(package)
            self.assertEqual(checked.returncode, 0, checked.stdout)

            manifest_path = package / "manifest.json"
            original = json.loads(manifest_path.read_text(encoding="utf-8"))
            mutations = []
            changed_name = json.loads(json.dumps(original))
            changed_name["boot_modules"][0]["name"] = "forged-name"
            mutations.append(changed_name)
            changed_role = json.loads(json.dumps(original))
            changed_role["boot_modules"][0]["role"] = "auxiliary"
            mutations.append(changed_role)
            changed_order = json.loads(json.dumps(original))
            changed_order["boot_modules"].reverse()
            mutations.append(changed_order)
            changed_offset = json.loads(json.dumps(original))
            changed_offset["boot_modules"][0]["image_offset"] = PAGE_SIZE
            changed_offset["boot_modules"][0]["physical_address"] = (
                0x80000 + PAGE_SIZE
            )
            mutations.append(changed_offset)
            for mutation in mutations:
                with self.subTest(mutation=mutation["boot_modules"]):
                    manifest_path.write_text(
                        json.dumps(mutation, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                    self.assertNotEqual(self.run_checker(package).returncode, 0)
            manifest_path.write_text(
                json.dumps(original, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )

            copied_provenance_path = package / PROVENANCE_FILE
            original_provenance = json.loads(
                copied_provenance_path.read_text(encoding="utf-8")
            )
            provenance_mutations = []
            changed_bundle = json.loads(json.dumps(original_provenance))
            changed_bundle["bundle_sha256"] = "0" * 64
            provenance_mutations.append(changed_bundle)
            changed_product = json.loads(json.dumps(original_provenance))
            changed_product["product_id"] = "bootmgr.forged"
            provenance_mutations.append(changed_product)
            changed_product_digest = json.loads(json.dumps(original_provenance))
            changed_product_digest["product_manifest_sha256"] = "0" * 64
            provenance_mutations.append(changed_product_digest)
            changed_index = json.loads(json.dumps(original_provenance))
            changed_index["components"][0]["index"] = 1
            provenance_mutations.append(changed_index)
            changed_shape = json.loads(json.dumps(original_provenance))
            changed_shape["unexpected"] = True
            provenance_mutations.append(changed_shape)

            for mutation in provenance_mutations:
                with self.subTest(provenance_mutation=mutation):
                    copied_provenance_path.write_text(
                        json.dumps(mutation, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8",
                    )
                    mutated_manifest = json.loads(json.dumps(original))
                    mutated_manifest["files"][PROVENANCE_FILE] = {
                        "sha256": hashlib.sha256(
                            copied_provenance_path.read_bytes()
                        ).hexdigest(),
                        "size": copied_provenance_path.stat().st_size,
                    }
                    if set(mutation) == set(original_provenance):
                        mutated_manifest["boot_module_provenance"] = {
                            "bundle_sha256": mutation["bundle_sha256"],
                            "component_count": mutation["component_count"],
                            "product_id": mutation["product_id"],
                            "product_manifest_sha256": mutation[
                                "product_manifest_sha256"
                            ],
                        }
                    manifest_path.write_text(
                        json.dumps(mutated_manifest, indent=2, sort_keys=True)
                        + "\n",
                        encoding="utf-8",
                    )
                    self.assertNotEqual(self.run_checker(package).returncode, 0)

            copied_provenance_path.write_text(
                json.dumps(original_provenance, indent=2, sort_keys=True)
                + "\n",
                encoding="utf-8",
            )
            manifest_path.write_text(
                json.dumps(original, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )


if __name__ == "__main__":
    unittest.main()
