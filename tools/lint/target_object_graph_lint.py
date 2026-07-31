#!/usr/bin/env python3
"""Validate generated R4 target graphs against their linked object maps."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


EXPECTED = {
    "qemu-riscv64-virt-opensbi": {
        "architecture": "riscv64",
        "environment": "raw-fdt",
        "port": "qemu-virt-riscv64",
        "map": "ribon.map",
        "optional": True,
        "product_id": "bootmgr.qemu-riscv64-virt-parus-external",
        "payload_entry_abi": "riscv-rph1-v1",
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
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/qemu/virt-aarch64/port",
            "generated/embedded_payload",
        ),
    },
    "qemu-aarch64-virt-raw-fdt": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "port": "qemu-virt-aarch64",
        "map": "ribon.map",
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
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "ports/raspberrypi/rpi5/port",
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target_root", type=Path)
    args = parser.parse_args()
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
