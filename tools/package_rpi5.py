#!/usr/bin/env python3
"""Create a deterministic RPi5 raw-FDT boot-partition package."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil


PAGE_SIZE = 4096
PROVENANCE_SCHEMA = "ribon-boot-module-bundle-provenance-v1"
PROVENANCE_KEYS = {
    "bundle_sha256",
    "component_count",
    "components",
    "product_id",
    "product_manifest_sha256",
    "schema",
}
COMPONENT_KEYS = {
    "index",
    "maximum_size",
    "name",
    "role",
    "sha256",
    "size",
    "snapshot",
    "source",
}
MODULE_SERVICE = {
    "id": "service.product.boot-module-bundle",
    "kind": "boot-module-bundle",
    "symbol": "ribon_generated_boot_module_bundle_service_descriptor",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def is_sha256(value: object) -> bool:
    """Return whether value is one canonical lowercase SHA-256 string."""

    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def validate_product_manifest(
    path: Path,
    provenance: dict[str, object],
) -> bytes:
    """Bind provenance to the exact module-bearing raw-FDT product."""

    raw = path.read_bytes()
    product = json.loads(raw.decode("utf-8"))
    services = product.get("services") if isinstance(product, dict) else None
    module_services = (
        [
            service
            for service in services
            if isinstance(service, dict)
            and service.get("kind") == "boot-module-bundle"
        ]
        if isinstance(services, list)
        else []
    )
    if (
        not isinstance(product, dict)
        or product.get("schema_version") != 1
        or product.get("product_kind") != "bootloader"
        or product.get("target_id") != "rpi5-aarch64-raw-fdt"
        or product.get("architecture") != "aarch64"
        or product.get("environment") != "raw-fdt"
        or product.get("port") != "raspberrypi-rpi5"
        or product.get("image") != {
            "format": "raw-binary",
            "recipe": "raspberrypi-firmware-image",
            "artifact": "ribon-rpi5.img",
        }
        or product.get("boot_module_bundle") != {
            "component_manifest_schema": "ribon-boot-module-components-v1",
            "maximum_modules": 8,
            "provider": "generated-component-bundle-v1",
        }
        or module_services != [MODULE_SERVICE]
        or "BOOT_MODULE_BUNDLE" not in product.get("required_capabilities", [])
        or "BOOT_MODULE_BUNDLE" not in product.get("allowed_capabilities", [])
        or product.get("product_id") != provenance.get("product_id")
        or hashlib.sha256(raw).hexdigest() !=
            provenance.get("product_manifest_sha256")
    ):
        raise ValueError("module provenance is not bound to the product manifest")
    return raw


def validate_provenance(
    provenance: object,
    product_root: Path,
) -> tuple[list[dict[str, object]], list[bytes]]:
    """Validate one exact generated provenance envelope and its snapshots."""

    if (
        not isinstance(provenance, dict)
        or set(provenance) != PROVENANCE_KEYS
        or provenance.get("schema") != PROVENANCE_SCHEMA
        or not is_sha256(provenance.get("bundle_sha256"))
        or not is_sha256(provenance.get("product_manifest_sha256"))
        or not isinstance(provenance.get("product_id"), str)
        or not provenance.get("product_id")
    ):
        raise ValueError("invalid boot-module provenance envelope")
    components = provenance.get("components")
    if (
        not isinstance(components, list)
        or not 1 <= len(components) <= 8
        or provenance.get("component_count") != len(components)
    ):
        raise ValueError("invalid boot-module provenance count")

    names: set[str] = set()
    initial_images = 0
    data_items: list[bytes] = []
    digest = hashlib.sha256()
    digest.update(PROVENANCE_SCHEMA.encode("ascii") + b"\0")
    for index, component in enumerate(components):
        if not isinstance(component, dict) or set(component) != COMPONENT_KEYS:
            raise ValueError("invalid boot-module component record")
        name = component.get("name")
        role = component.get("role")
        size = component.get("size")
        maximum_size = component.get("maximum_size")
        snapshot_value = component.get("snapshot")
        source_value = component.get("source")
        source = PurePosixPath(source_value) if isinstance(source_value, str) else None
        expected_snapshot = (
            "generated/boot-modules/boot-module-components/"
            f"{index:03d}.bin"
        )
        if (
            component.get("index") != index
            or not isinstance(name, str)
            or not 1 <= len(name) <= 63
            or any(not (ch.isascii() and (ch.isalnum() or ch in "._-")) for ch in name)
            or name in names
            or role not in ("initial-image", "auxiliary")
            or not isinstance(size, int)
            or isinstance(size, bool)
            or size <= 0
            or not isinstance(maximum_size, int)
            or isinstance(maximum_size, bool)
            or maximum_size < size
            or not is_sha256(component.get("sha256"))
            or snapshot_value != expected_snapshot
            or source is None
            or source.is_absolute()
            or any(part in ("", ".", "..") for part in source.parts)
        ):
            raise ValueError("invalid boot-module component record")
        if role == "initial-image":
            initial_images += 1
            if initial_images > 1:
                raise ValueError("duplicate initial image in provenance")
        snapshot = product_root / expected_snapshot
        data = snapshot.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != component["sha256"]:
            raise ValueError("boot-module snapshot does not match provenance")
        names.add(name)
        data_items.append(data)
        digest.update(name.encode("ascii") + b"\0")
        digest.update(str(role).encode("ascii") + b"\0")
        digest.update(len(data).to_bytes(8, "little"))
        digest.update(data)
    if digest.hexdigest() != provenance.get("bundle_sha256"):
        raise ValueError("boot-module bundle digest does not match provenance")
    return components, data_items


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--cmdline", type=Path, required=True)
    parser.add_argument("--module-provenance", type=Path)
    parser.add_argument("--product-manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if (args.module_provenance is None) != (args.product_manifest is None):
        raise ValueError(
            "module provenance and product manifest must be provided together"
        )
    if args.output.exists():
        shutil.rmtree(args.output)
    (args.output / "boot").mkdir(parents=True)
    files = {
        "kernel8.img": args.image,
        "boot/payload.elf": args.payload,
        "config.txt": args.config,
        "cmdline.txt": args.cmdline,
    }
    module_records: list[dict[str, object]] = []
    module_binding: dict[str, object] | None = None
    if args.module_provenance is not None:
        provenance = json.loads(
            args.module_provenance.read_text(encoding="utf-8")
        )
        product_root = args.module_provenance.parent.parent
        components, component_data = validate_provenance(
            provenance, product_root
        )
        assert args.product_manifest is not None
        validate_product_manifest(args.product_manifest, provenance)
        image_bytes = args.image.read_bytes()
        backing_sizes = [
            (len(data) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)
            for data in component_data
        ]
        module_bytes = sum(backing_sizes)
        offset = len(image_bytes) - module_bytes
        if offset < 0 or offset % PAGE_SIZE != 0:
            raise ValueError("module section is not a page-aligned image suffix")
        for component, data, backing_size in zip(
            components, component_data, backing_sizes
        ):
            digest = hashlib.sha256(data).hexdigest()
            if (
                not data
                or digest != component.get("sha256")
                or len(data) != component.get("size")
                or image_bytes[offset : offset + len(data)] != data
            ):
                raise ValueError("module bytes do not match the linked image suffix")
            module_records.append(
                {
                    "backing_size": backing_size,
                    "image_offset": offset,
                    "name": component.get("name"),
                    "physical_address": 0x80000 + offset,
                    "role": component.get("role"),
                    "sha256": digest,
                    "size": len(data),
                }
            )
            offset += backing_size
        if offset != len(image_bytes):
            raise ValueError("module backing does not close at the image end")
        files["metadata/boot-modules.json"] = args.module_provenance
        files["metadata/product.json"] = args.product_manifest
        module_binding = {
            "bundle_sha256": provenance["bundle_sha256"],
            "component_count": len(components),
            "product_id": provenance["product_id"],
            "product_manifest_sha256": provenance[
                "product_manifest_sha256"
            ],
        }
    for relative, source in files.items():
        destination = args.output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    manifest = {
        "claim": "package-only; no live RPi5 execution",
        "environment": "raw-fdt",
        "files": {
            relative: {
                "sha256": sha256(args.output / relative),
                "size": (args.output / relative).stat().st_size,
            }
            for relative in sorted(files)
        },
        "port": "raspberrypi-rpi5",
        "schema": (
            "ribon-rpi5-package-v2"
            if module_records
            else "ribon-rpi5-package-v1"
        ),
    }
    if module_records:
        manifest["boot_modules"] = module_records
        manifest["boot_module_provenance"] = module_binding
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("RIBON-R4-RPI5-PACKAGE-CREATED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
