#!/usr/bin/env python3
"""Validate the Ribon RPi5 raw-FDT package and its recorded object facts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import sys


REQUIRED_FILES = (
    "kernel8.img",
    "boot/payload.elf",
    "config.txt",
    "cmdline.txt",
)
MODULE_PROVENANCE_FILE = "metadata/boot-modules.json"
PRODUCT_MANIFEST_FILE = "metadata/product.json"
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
PACKAGE_MODULE_KEYS = {
    "backing_size",
    "image_offset",
    "name",
    "physical_address",
    "role",
    "sha256",
    "size",
}
MODULE_BINDING_KEYS = {
    "bundle_sha256",
    "component_count",
    "product_id",
    "product_manifest_sha256",
}
MODULE_SERVICE = {
    "id": "service.product.boot-module-bundle",
    "kind": "boot-module-bundle",
    "symbol": "ribon_generated_boot_module_bundle_service_descriptor",
}


def fail(message: str) -> int:
    print(f"RIBON-RPI5-PACKAGE-FAIL: {message}", file=sys.stderr)
    return 1


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


def product_is_bound(
    raw: bytes,
    provenance: dict[str, object],
) -> bool:
    """Return whether copied product bytes authorize this exact provenance."""

    try:
        product = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError):
        return False
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
    return (
        isinstance(product, dict)
        and product.get("schema_version") == 1
        and product.get("product_kind") == "bootloader"
        and product.get("target_id") == "rpi5-aarch64-raw-fdt"
        and product.get("architecture") == "aarch64"
        and product.get("environment") == "raw-fdt"
        and product.get("port") == "raspberrypi-rpi5"
        and product.get("image") == {
            "format": "raw-binary",
            "recipe": "raspberrypi-firmware-image",
            "artifact": "ribon-rpi5.img",
        }
        and product.get("boot_module_bundle") == {
            "component_manifest_schema": "ribon-boot-module-components-v1",
            "maximum_modules": 8,
            "provider": "generated-component-bundle-v1",
        }
        and module_services == [MODULE_SERVICE]
        and "BOOT_MODULE_BUNDLE" in product.get("required_capabilities", [])
        and "BOOT_MODULE_BUNDLE" in product.get("allowed_capabilities", [])
        and product.get("product_id") == provenance.get("product_id")
        and hashlib.sha256(raw).hexdigest() ==
            provenance.get("product_manifest_sha256")
    )


def payload_load_ranges(path: Path) -> list[tuple[int, int]]:
    """Return the physical PT_LOAD ranges from one little-endian ELF64."""

    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        raise ValueError("payload is not ELF")
    if data[4] != 2 or data[5] != 1:
        raise ValueError("payload is not little-endian ELF64")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
    phoff = header[5]
    phentsize = header[9]
    phnum = header[10]
    if phentsize < 56:
        raise ValueError("payload program header size is invalid")
    ranges: list[tuple[int, int]] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        if offset + 56 > len(data):
            raise ValueError("payload program header extends past EOF")
        fields = struct.unpack_from("<IIQQQQQQ", data, offset)
        if fields[0] != 1:
            continue
        start = fields[4]
        size = fields[6]
        if size == 0 or start > (1 << 64) - 1 - size:
            raise ValueError("payload PT_LOAD range is invalid")
        ranges.append((start, start + size))
    if not ranges:
        raise ValueError("payload has no PT_LOAD range")
    return ranges


def loader_memory_range(path: Path) -> tuple[int, int]:
    """Read the RPi AArch64 image header's physical in-memory extent."""

    data = path.read_bytes()
    if len(data) < 64 or data[56:60] != b"ARM\x64":
        raise ValueError("kernel8.img has no AArch64 image header")
    start = struct.unpack_from("<Q", data, 8)[0]
    size = struct.unpack_from("<Q", data, 16)[0]
    if start == 0 or size == 0 or start > (1 << 64) - 1 - size:
        raise ValueError("kernel8.img memory range is invalid")
    return start, start + size


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()
    if not args.package.is_dir():
        return fail("package directory is missing")
    manifest_path = args.package / "manifest.json"
    if not manifest_path.is_file():
        return fail("manifest.json is missing")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    schema = manifest.get("schema")
    if (
        schema not in ("ribon-rpi5-package-v1", "ribon-rpi5-package-v2")
        or manifest.get("port") != "raspberrypi-rpi5"
        or manifest.get("environment") != "raw-fdt"
        or manifest.get("claim") != "package-only; no live RPi5 execution"
    ):
        return fail("manifest identity or evidence boundary is invalid")
    recorded = manifest.get("files")
    expected_files = set(REQUIRED_FILES)
    if schema == "ribon-rpi5-package-v2":
        expected_files.add(MODULE_PROVENANCE_FILE)
        expected_files.add(PRODUCT_MANIFEST_FILE)
    if not isinstance(recorded, dict) or set(recorded) != expected_files:
        return fail("manifest file set is not exact")
    for relative in sorted(expected_files):
        path = args.package / relative
        entry = recorded[relative]
        if (
            not path.is_file()
            or path.stat().st_size == 0
            or not isinstance(entry, dict)
            or entry.get("size") != path.stat().st_size
            or entry.get("sha256") != sha256(path)
        ):
            return fail(f"invalid or unbound file: {relative}")
    config = (args.package / "config.txt").read_text(encoding="utf-8")
    for required in (
        "device_tree=bcm2712-rpi-5-b.dtb",
        "arm_64bit=1",
        "kernel=kernel8.img",
        "enable_uart=1",
        "enable_rp1_uart=1",
        "uart_2ndstage=1",
        "os_check=0",
        "pciex4_reset=0",
    ):
        if required not in config:
            return fail(f"config.txt missing {required}")
    cmdline = (args.package / "cmdline.txt").read_text(encoding="utf-8").strip()
    if not cmdline or "\n" in cmdline or "\r" in cmdline:
        return fail("cmdline.txt must contain one non-empty line")
    try:
        loader_range = loader_memory_range(args.package / "kernel8.img")
        payload_ranges = payload_load_ranges(
            args.package / "boot" / "payload.elf"
        )
    except (OSError, ValueError, struct.error) as error:
        return fail(str(error))
    if any(
        loader_range[0] < payload_end
        and payload_start < loader_range[1]
        for payload_start, payload_end in payload_ranges
    ):
        return fail("Ribon in-memory image overlaps a payload PT_LOAD range")
    if schema == "ribon-rpi5-package-v2":
        provenance = json.loads(
            (args.package / MODULE_PROVENANCE_FILE).read_text(encoding="utf-8")
        )
        modules = manifest.get("boot_modules")
        binding = manifest.get("boot_module_provenance")
        provenance_components = provenance.get("components")
        if (
            set(manifest) != {
                "boot_module_provenance",
                "boot_modules",
                "claim",
                "environment",
                "files",
                "port",
                "schema",
            }
            or not isinstance(provenance, dict)
            or set(provenance) != PROVENANCE_KEYS
            or provenance.get("schema") != PROVENANCE_SCHEMA
            or not is_sha256(provenance.get("bundle_sha256"))
            or not is_sha256(provenance.get("product_manifest_sha256"))
            or not isinstance(provenance.get("product_id"), str)
            or not provenance.get("product_id")
            or not isinstance(modules, list)
            or not isinstance(binding, dict)
            or set(binding) != MODULE_BINDING_KEYS
            or not isinstance(provenance_components, list)
            or not 1 <= len(modules) <= 8
            or provenance.get("component_count") != len(modules)
            or len(provenance_components) != len(modules)
            or binding != {
                "bundle_sha256": provenance.get("bundle_sha256"),
                "component_count": len(modules),
                "product_id": provenance.get("product_id"),
                "product_manifest_sha256": provenance.get(
                    "product_manifest_sha256"
                ),
            }
            or not product_is_bound(
                (args.package / PRODUCT_MANIFEST_FILE).read_bytes(),
                provenance,
            )
        ):
            return fail("module provenance or package module count is invalid")
        image = (args.package / "kernel8.img").read_bytes()
        if any(
            not isinstance(module, dict)
            or not isinstance(module.get("backing_size"), int)
            or isinstance(module.get("backing_size"), bool)
            or module["backing_size"] <= 0
            for module in modules
        ):
            return fail("module backing sizes are invalid")
        total_backing = sum(module["backing_size"] for module in modules)
        previous_end = len(image) - total_backing
        if previous_end < 0 or previous_end % PAGE_SIZE != 0:
            return fail("module backing is not a page-aligned image suffix")
        initial_images = 0
        names: set[str] = set()
        bundle_digest = hashlib.sha256()
        bundle_digest.update(PROVENANCE_SCHEMA.encode("ascii") + b"\0")
        for index, module in enumerate(modules):
            component = provenance_components[index]
            if (
                not isinstance(module, dict)
                or set(module) != PACKAGE_MODULE_KEYS
                or not isinstance(component, dict)
                or set(component) != COMPONENT_KEYS
            ):
                return fail("module package entry is not an object")
            offset = module.get("image_offset")
            size = module.get("size")
            backing_size = module.get("backing_size")
            role = module.get("role")
            name = module.get("name")
            maximum_size = component.get("maximum_size")
            source_value = component.get("source")
            source = (
                PurePosixPath(source_value)
                if isinstance(source_value, str)
                else None
            )
            expected_snapshot = (
                "generated/boot-modules/boot-module-components/"
                f"{index:03d}.bin"
            )
            if (
                not isinstance(offset, int)
                or not isinstance(size, int)
                or not isinstance(backing_size, int)
                or size <= 0
                or backing_size < size
                or backing_size % PAGE_SIZE != 0
                or offset % PAGE_SIZE != 0
                or offset != previous_end
                or offset > len(image)
                or size > len(image) - offset
                or backing_size > len(image) - offset
                or module.get("physical_address") != 0x80000 + offset
                or role not in ("initial-image", "auxiliary")
                or not isinstance(name, str)
                or not 1 <= len(name) <= 63
                or any(
                    not (ch.isascii() and (ch.isalnum() or ch in "._-"))
                    for ch in name
                )
                or name in names
                or component.get("index") != index
                or not isinstance(maximum_size, int)
                or isinstance(maximum_size, bool)
                or maximum_size < size
                or not is_sha256(component.get("sha256"))
                or component.get("snapshot") != expected_snapshot
                or source is None
                or source.is_absolute()
                or any(part in ("", ".", "..") for part in source.parts)
                or module.get("name") != component.get("name")
                or role != component.get("role")
                or size != component.get("size")
                or module.get("sha256") != component.get("sha256")
                or hashlib.sha256(image[offset : offset + size]).hexdigest() !=
                    module.get("sha256")
            ):
                return fail(f"invalid embedded module entry {index}")
            data = image[offset : offset + size]
            previous_end = offset + backing_size
            names.add(name)
            bundle_digest.update(name.encode("ascii") + b"\0")
            bundle_digest.update(str(role).encode("ascii") + b"\0")
            bundle_digest.update(size.to_bytes(8, "little"))
            bundle_digest.update(data)
            if role == "initial-image":
                initial_images += 1
                if initial_images > 1:
                    return fail("duplicate initial image in package")
        if bundle_digest.hexdigest() != provenance.get("bundle_sha256"):
            return fail("module bundle digest does not match package bytes")
        if previous_end != len(image):
            return fail("module backing does not close at the image end")
    print("RIBON-R4-RPI5-PACKAGE-OK package-only disjoint-load-ranges")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
