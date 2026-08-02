#!/usr/bin/env python3
"""Validate generated R4 target graphs against their linked object maps."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


BOOT_MODULE_BUNDLE_CONTRACT = {
    "component_manifest_schema": "ribon-boot-module-components-v1",
    "maximum_modules": 8,
    "provider": "generated-component-bundle-v1",
}

BOOT_MODULE_SERVICE_CONTRACT = {
    "id": "service.product.boot-module-bundle",
    "kind": "boot-module-bundle",
    "symbol": "ribon_generated_boot_module_bundle_service_descriptor",
}

RAW_FDT_LINKER_SCRIPTS = (
    "targets/qemu-aarch64-virt-raw-fdt/linker.ld",
    "targets/qemu-riscv64-virt-opensbi/linker.ld",
    "targets/rpi5-aarch64-raw-fdt/linker.ld",
)

RAW_FDT_LINKER_SYMBOLS = (
    "__bootloader_runtime_end",
    "__ribon_boot_modules_start",
    "__ribon_boot_modules_end",
    "__image_end",
)


EXPECTED = {
    "qemu-riscv64-virt-opensbi": {
        "architecture": "riscv64",
        "environment": "raw-fdt",
        "port": "qemu-virt-riscv64",
        "map": "ribon.map",
        "optional": True,
        "product_id": "bootmgr.qemu-riscv64-virt-parus-external",
        "payload_entry_abi": "riscv-rph1-v1",
        "boot_module_bundle": False,
        "needles": (
            "src/arch/riscv64/arch",
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-riscv64/port",
            "generated/embedded_payload",
        ),
        "forbidden": (
            "src/arch/aarch64/arch",
            "ports/qemu/virt-aarch64/port",
        ),
    },
    "qemu-riscv64-virt-rph1-fixture": {
        "architecture": "riscv64",
        "environment": "raw-fdt",
        "port": "qemu-virt-riscv64",
        "map": "ribon.map",
        "product_id": "bootmgr.qemu-riscv64-virt-rph1-fixture",
        "boot_module_bundle": False,
        "needles": (
            "src/arch/riscv64/arch",
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-riscv64/port",
            "generated/embedded_payload",
        ),
        "forbidden": (
            "src/arch/aarch64/arch",
            "ports/qemu/virt-aarch64/port",
        ),
    },
    "qemu-aarch64-virt-parus": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "qemu-virt-aarch64",
        "map": "ribon.map",
        "optional": True,
        "product_id": "bootmgr.qemu-aarch64-virt-parus-external",
        "payload_entry_abi": "arm64-rph1-v1",
        "boot_module_bundle": False,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-aarch64/port",
            "generated/embedded_payload",
        ),
    },
    "qemu-aarch64-virt-linux": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "qemu-virt-aarch64",
        "map": "ribon.map",
        "product_id": "bootmgr.qemu-aarch64-virt-linux",
        "payload_entry_abi": "arm64-linux-fdt-v1",
        "payload_format": "linux-aarch64-image",
        "boot_module_bundle": True,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "src/image-formats/linux_aarch64.o",
            "src/protocols/os/linux/protocol.o",
            "src/protocols/os/linux/fdt.o",
            "generated/external_payload.o",
            "generated/boot-modules/descriptor.o",
            "generated/boot-modules/bundle.o",
        ),
        "forbidden": (
            "src/image-formats/elf64.o",
            "src/protocols/os/parus/",
            "generated/embedded_payload.o",
        ),
    },
    "qemu-aarch64-virt-parus-modules": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "qemu-virt-aarch64",
        "map": "ribon.map",
        "optional": True,
        "product_id": "bootmgr.qemu-aarch64-virt-parus-modules",
        "payload_entry_abi": "arm64-rph1-v1",
        "boot_module_bundle": True,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-aarch64/port",
            "generated/boot-modules/descriptor.o",
            "generated/boot-modules/bundle.o",
        ),
    },
    "qemu-aarch64-virt-modules-fixture": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "qemu-virt-aarch64",
        "map": "ribon.map",
        "product_id": "bootmgr.qemu-aarch64-virt-modules-fixture",
        "boot_module_bundle": True,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-aarch64/port",
            "generated/boot-modules/descriptor.o",
            "generated/boot-modules/bundle.o",
        ),
    },
    "qemu-aarch64-virt-raw-fdt": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "qemu-virt-aarch64",
        "map": "ribon.map",
        "boot_module_bundle": False,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-aarch64/port",
        ),
    },
    "rpi5-aarch64-raw-fdt": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "raspberrypi-rpi5",
        "map": "ribon.map",
        "boot_module_bundle": False,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/raspberrypi/rpi5/port",
        ),
    },
    "rpi5-aarch64-modules-fixture": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "raspberrypi-rpi5",
        "map": "ribon.map",
        "product_id": "bootmgr.rpi5-aarch64-modules-fixture",
        "boot_module_bundle": True,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/raspberrypi/rpi5/port",
            "generated/boot-modules/descriptor.o",
            "generated/boot-modules/bundle.o",
        ),
    },
    "rpi5-aarch64-parus-modules": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "raspberrypi-rpi5",
        "map": "ribon.map",
        "optional": True,
        "product_id": "bootmgr.rpi5-aarch64-parus-modules",
        "payload_entry_abi": "arm64-rph1-v1",
        "boot_module_bundle": True,
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/raspberrypi/rpi5/port",
            "generated/boot-modules/descriptor.o",
            "generated/boot-modules/bundle.o",
        ),
    },
    "x86_64-uefi-parus-fixture": {
        "architecture": "x86_64",
        "environment": "uefi",
        "port": "qemu-pc-x86_64",
        "map": "ribon.map",
        "product_id": "bootmgr.x86_64-uefi-parus-fixture",
        "needles": (
            "uefi_app.o",
            "boot_config.o",
            "ribon_port_selected",
        ),
    },
    "x86_64-uefi-parus-external": {
        "architecture": "x86_64",
        "environment": "uefi",
        "port": "qemu-pc-x86_64",
        "map": "ribon.map",
        "optional": True,
        "product_id": "bootmgr.x86_64-uefi-parus-external",
        "payload_entry_abi": "amd64-rph1-v1",
        "needles": (
            "uefi_app.o",
            "boot_config.o",
            "ribon_port_selected",
        ),
    },
    "x86_64-uefi-freebsd": {
        "architecture": "x86_64",
        "environment": "uefi",
        "port": "qemu-pc-x86_64",
        "map": "ribon.map",
        "product_id": "bootmgr.x86_64-uefi-freebsd",
        "needles": (
            "uefi_app.o",
            "terminal_image.o",
            "pe_coff.o",
            "ribon_freebsd_protocol_plugin_descriptor",
            "ribon_port_selected",
        ),
        "forbidden": (
            "ribon_linux_efi_protocol_plugin_descriptor",
            "ribon_parus_protocol_plugin_descriptor",
        ),
    },
    "x86-bios-client": {
        "architecture": "x86_64",
        "environment": "bios",
        "port": None,
        "needles": (),
    },
}


def fail(message: str) -> None:
    print(f"RIBON-TARGET-OBJECT-GRAPH-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def check_raw_fdt_linker_contract() -> None:
    """All raw-FDT targets must publish the same separated image ranges."""

    root = Path(__file__).resolve().parents[2]
    tokens = (
        "__bootloader_runtime_end = .;",
        "__ribon_boot_modules_start = .;",
        "KEEP(*(SORT_BY_NAME(.ribon.boot_modules.*)))",
        "__ribon_boot_modules_end = .;",
        "__image_end = .;",
    )
    for relative in RAW_FDT_LINKER_SCRIPTS:
        linker = (root / relative).read_text(encoding="utf-8")
        positions = [linker.find(token) for token in tokens]
        if any(position < 0 for position in positions) or positions != sorted(positions):
            fail(f"{relative}: canonical boot-module section contract is absent")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target_root", type=Path)
    args = parser.parse_args()
    check_raw_fdt_linker_contract()
    for target, expected in EXPECTED.items():
        directory = args.target_root / target
        report_path = directory / "results" / "object-graph.json"
        if not report_path.is_file():
            if expected.get("optional") is True:
                continue
            fail(f"{target}: missing generated report")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        plugins = report.get("plugins")
        payload = report.get("payload")
        if (
            report.get("architecture") != expected["architecture"]
            or report.get("environment") != expected["environment"]
            or not isinstance(plugins, list)
            or report.get("port") != expected["port"]
            or sum(item.startswith("arch.") for item in plugins) != 1
            or sum(item.startswith("environment.") for item in plugins) != 1
            or any(item.startswith("platform.") for item in plugins)
        ):
            fail(f"{target}: generated tuple is not exact")
        if expected.get("product_id") is not None:
            if report.get("product_id") != expected["product_id"]:
                fail(f"{target}: product identity is not exact")
            payload_entry_abi = expected.get("payload_entry_abi")
            if payload_entry_abi is not None and (
                not isinstance(payload, dict)
                or payload.get("entry_abi") != payload_entry_abi
            ):
                fail(f"{target}: external payload product contract is not exact")
            payload_format = expected.get("payload_format")
            if payload_format is not None and (
                not isinstance(payload, dict)
                or payload.get("format") != payload_format
            ):
                fail(f"{target}: external payload format is not exact")
        expected_bundle = expected.get("boot_module_bundle")
        report_services = report.get("services")
        if not isinstance(report_services, list):
            fail(f"{target}: generated service report is absent")
        module_services = [
            service
            for service in report_services
            if isinstance(service, dict)
            and service.get("kind") == "boot-module-bundle"
        ]
        if expected_bundle is True:
            if (
                report.get("boot_module_bundle") != BOOT_MODULE_BUNDLE_CONTRACT
                or module_services != [BOOT_MODULE_SERVICE_CONTRACT]
            ):
                fail(f"{target}: boot-module bundle product contract is not exact")
        elif expected_bundle is False and (
            report.get("boot_module_bundle") is not None or module_services
        ):
            fail(f"{target}: module-free product selected a boot-module authority")
        map_name = expected.get("map")
        if map_name is None:
            continue
        map_path = directory / str(map_name)
        if not map_path.is_file():
            fail(f"{target}: link map is missing")
        link_map = map_path.read_text(encoding="utf-8", errors="replace")
        for needle in expected["needles"]:
            if needle not in link_map:
                fail(f"{target}: selected object is absent from link map: {needle}")
        for needle in expected.get("forbidden", ()):
            if needle in link_map:
                fail(f"{target}: forbidden object leaked into link map: {needle}")
        if expected["environment"] == "raw-fdt":
            symbol_positions = [link_map.find(symbol) for symbol in RAW_FDT_LINKER_SYMBOLS]
            if (
                any(position < 0 for position in symbol_positions)
                or symbol_positions != sorted(symbol_positions)
            ):
                fail(f"{target}: linked image range symbols are absent or unordered")
            has_module_objects = (
                "src/common/module_bundle.o" in link_map
                and "generated/boot-modules/descriptor.o" in link_map
                and "generated/boot-modules/bundle.o" in link_map
                and ".ribon.boot_modules.000" in link_map
            )
            if expected_bundle is True and not has_module_objects:
                fail(f"{target}: selected bundle objects are absent from the image")
            if expected_bundle is False and (
                "src/common/module_bundle.o" in link_map
                or "generated/boot-modules/" in link_map
                or ".ribon.boot_modules.000" in link_map
            ):
                fail(f"{target}: module object leaked into a module-free image")
        forbidden = (
            "raspberrypi/rpi5" if target.startswith("qemu-") else
            "qemu/virt-aarch64" if target.startswith("rpi5-") else
            "src/environments/raw-fdt" if target.startswith("x86_64-uefi-") else
            ""
        )
        if forbidden and forbidden in link_map:
            fail(f"{target}: forbidden object leaked into link map: {forbidden}")
        if target.startswith("x86_64-uefi-") and "embedded_payload" in link_map:
            fail(f"{target}: embedded payload object remains in the runtime object graph")
    print("RIBON-R4-TARGET-OBJECT-GRAPHS-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
