#!/usr/bin/env python3
"""Create a deterministic signed update set from one RPi5 package fixture."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
from types import ModuleType


PAGE_SIZE = 4096
KEY_ID = "ribon-rpi5-prehardware-fixture-key"


def sha256_bytes(data: bytes) -> str:
    """Return one canonical byte digest."""

    return hashlib.sha256(data).hexdigest()


def sha256(path: Path) -> str:
    """Return one canonical file digest."""

    return sha256_bytes(path.read_bytes())


def write_json(path: Path, value: object) -> None:
    """Write deterministic presentation JSON."""

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_tool(path: Path, name: str) -> ModuleType:
    """Load one repository host tool without creating source-tree bytecode."""

    sys.dont_write_bytecode = True
    specification = importlib.util.spec_from_file_location(name, path)
    if specification is None or specification.loader is None:
        raise ValueError(f"cannot load host tool: {path}")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def bounded_bytes(path: Path, expected: dict[str, object]) -> bytes:
    """Read one package file and require the package manifest identity."""

    data = path.read_bytes()
    if (
        not data
        or expected.get("size") != len(data)
        or expected.get("sha256") != sha256_bytes(data)
    ):
        raise ValueError(f"package file identity mismatch: {path.name}")
    return data


def aligned(value: int) -> int:
    """Round one bounded size up to the canonical update alignment."""

    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--private-seed", type=Path, required=True)
    parser.add_argument("--source-revision", required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()

    package = args.package.resolve()
    output = args.output_root.resolve()
    if not package.is_dir() or package == output or package.is_relative_to(output):
        raise ValueError("package or output root is invalid")
    package_manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8"))
    files = package_manifest.get("files")
    modules = package_manifest.get("boot_modules")
    if (
        package_manifest.get("schema") != "ribon-rpi5-package-v2"
        or package_manifest.get("claim") != "package-only; no live RPi5 execution"
        or not isinstance(files, dict)
        or not isinstance(modules, list)
        or not 1 <= len(modules) <= 8
        or len(args.source_revision) != 40
        or any(character not in "0123456789abcdef" for character in args.source_revision)
    ):
        raise ValueError("RPi5 package is not the module-bearing prehardware fixture")

    image = bounded_bytes(package / "kernel8.img", files["kernel8.img"])
    payload = bounded_bytes(package / "boot/payload.elf", files["boot/payload.elf"])
    product = bounded_bytes(package / "metadata/product.json", files["metadata/product.json"])
    provenance = bounded_bytes(
        package / "metadata/boot-modules.json", files["metadata/boot-modules.json"]
    )
    product_value = json.loads(product.decode("utf-8"))
    if (
        product_value.get("architecture") != "aarch64"
        or product_value.get("environment") != "raw-fdt"
        or product_value.get("port") != "raspberrypi-rpi5"
        or product_value.get("boot_protocols") != ["parus"]
    ):
        raise ValueError("RPi5 product binding is not the expected raw-FDT product")

    component_inputs: list[tuple[str, str, str, str, str, bytes]] = [
        (
            "firmware.rpi5.boot-image",
            "firmware",
            "firmware-slot",
            "slot.inactive.rpi5-firmware",
            "raw",
            image,
        ),
        (
            "system.kernel",
            "kernel",
            "kernel-slot",
            "slot.inactive.kernel",
            "elf64",
            payload,
        ),
    ]
    for index, module in enumerate(modules):
        if not isinstance(module, dict):
            raise ValueError("RPi5 boot-module record is not an object")
        start = module.get("image_offset")
        size = module.get("size")
        name = module.get("name")
        if (
            not isinstance(start, int)
            or isinstance(start, bool)
            or not isinstance(size, int)
            or isinstance(size, bool)
            or size <= 0
            or start < 0
            or start > len(image) - size
            or not isinstance(name, str)
        ):
            raise ValueError("RPi5 boot-module range is invalid")
        data = image[start:start + size]
        if module.get("sha256") != sha256_bytes(data):
            raise ValueError("RPi5 boot-module bytes do not match package metadata")
        component_inputs.append(
            (
                f"boot-module.{index:02d}.{name}",
                "boot-module",
                "module-slot",
                f"slot.inactive.module.{index:02d}",
                "opaque",
                data,
            )
        )

    if output.exists():
        shutil.rmtree(output)
    components_root = output / "components"
    components_root.mkdir(parents=True)
    bundle = bytearray()
    source_components: list[dict[str, object]] = []
    release_components: list[dict[str, object]] = []
    for index, (logical_id, role, destination, destination_id, image_format, data) in enumerate(
        component_inputs
    ):
        offset = aligned(len(bundle))
        bundle.extend(bytes(offset - len(bundle)))
        bundle.extend(data)
        relative = Path("components") / f"{index:02d}.bin"
        snapshot = output / relative
        snapshot.write_bytes(data)
        source_components.append(
            {
                "bundle_offset": offset,
                "destination_class": destination,
                "destination_id": destination_id,
                "entry_contract_id": "entry.aarch64.raw-fdt-v1",
                "expected_sha256": sha256_bytes(data),
                "image_format": image_format,
                "logical_id": logical_id,
                "maximum_size": aligned(len(data)),
                "required": True,
                "role": role,
                "source": relative.as_posix(),
            }
        )
        release_components.append(
            {
                "bundle_offset": offset,
                "logical_id": logical_id,
                "role": role,
                "sha256": sha256_bytes(data),
                "size": len(data),
            }
        )
    bundle_path = output / "update.bin"
    bundle_path.write_bytes(bundle)

    source = {
        "architecture_id": "architecture.aarch64",
        "bundle_generation": 1,
        "components": source_components,
        "creation_policy_version": 1,
        "environment_id": "environment.raw-fdt",
        "hardware_revision": {"maximum": 0xFFFFFFFF, "minimum": 0},
        "manifest_schema_id": "ribon.update.manifest.v1",
        "mode": "recovery",
        "platform_id": "platform.raspberrypi-rpi5",
        "predecessor_generation": 0,
        "product_digest_sha256": sha256_bytes(product),
        "product_id": product_value["product_id"],
        "protocol": {"id": "protocol.luca", "major": 1, "minor": 0},
        "rollback_domain": "ribon.update.rpi5-prehardware.v1",
        "rollback_sequence": 1,
        "schema": "ribon-update-manifest-source-v1",
    }
    source_path = output / "update-source.json"
    write_json(source_path, source)

    root = Path(__file__).resolve().parent
    update_tool = load_tool(root / "update_manifest.py", "ribon_update_manifest_tool")
    signer_tool = load_tool(root / "sign_ribos_policy.py", "ribon_signing_tool")
    manifest_data = update_tool.encode_manifest(update_tool.parse_source(source_path))
    manifest_path = output / "update.man"
    manifest_path.write_bytes(manifest_data)
    key_id = KEY_ID.encode("ascii")
    message = update_tool.signed_message(manifest_data, key_id)
    seed = signer_tool.load_seed(args.private_seed)
    public_key, signature = signer_tool.openssl_sign("openssl", seed, message)
    envelope_data = update_tool.encode_envelope(manifest_data, key_id, signature)
    envelope_path = output / "update.sig"
    envelope_path.write_bytes(envelope_data)
    manifest_view = update_tool.validate_manifest(manifest_data)
    envelope_view = update_tool.validate_envelope(envelope_data)
    if (
        manifest_view["component_count"] != len(component_inputs)
        or envelope_view["manifest_sha256"] != sha256_bytes(manifest_data)
        or envelope_view["key_id_utf8"] != KEY_ID
    ):
        raise ValueError("signed RPi5 update self-inspection failed")

    report = {
        "schema": "ribon-rpi5-prehardware-v1",
        "source_revision": args.source_revision,
        "evidence_class": "package/prehardware",
        "hardware_execution": "not-run",
        "signing_key": {
            "class": "RFC8032 fixture; non-production",
            "key_id": KEY_ID,
            "public_key_sha256": sha256_bytes(public_key),
        },
        "claims": [
            "deterministic RPi5 raw-FDT package assembly",
            "typed module bytes are bound into a canonical update manifest",
            "update manifest has an independently verified Ed25519 fixture signature",
        ],
        "nonclaims": [
            "physical RPi5 boot or update execution",
            "production key custody, HSM, TPM, or RPMB integration",
            "Parus user-process execution",
        ],
        "package": {
            "kernel8_sha256": sha256_bytes(image),
            "manifest_sha256": sha256(package / "manifest.json"),
            "module_provenance_sha256": sha256_bytes(provenance),
            "payload_sha256": sha256_bytes(payload),
            "product_sha256": sha256_bytes(product),
        },
        "update": {
            "bundle_sha256": sha256(bundle_path),
            "component_count": len(component_inputs),
            "components": release_components,
            "envelope_sha256": sha256(envelope_path),
            "manifest_sha256": sha256(manifest_path),
            "source_sha256": sha256(source_path),
        },
    }
    write_json(output / "prehardware.json", report)
    print(
        "RIBON-D08-RPI5-PREHARDWARE-OK "
        f"components={len(component_inputs)} signature=ed25519-fixture hardware=not-run"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"RIBON-D08-RPI5-PREHARDWARE-FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
