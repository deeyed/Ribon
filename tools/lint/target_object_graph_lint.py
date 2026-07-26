#!/usr/bin/env python3
"""Validate generated R4 target graphs against their linked object maps."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


EXPECTED = {
    "qemu-aarch64-virt-raw-fdt": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "platform": "platform.virt-aarch64",
        "map": "ribon.map",
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "platforms/qemu/virt-aarch64/platform",
        ),
    },
    "rpi5-aarch64-raw-fdt": {
        "architecture": "aarch64",
        "environment": "raw-fdt",
        "platform": "platform.raspberrypi-rpi5",
        "map": "ribon.map",
        "needles": (
            "src/environments/raw-fdt/raw_fdt",
            "platforms/raspberrypi/rpi5/platform",
        ),
    },
    "x86_64-uefi-app": {
        "architecture": "x86_64",
        "environment": "uefi",
        "platform": "platform.pc-uefi-x86_64",
        "map": "ribon.map",
        "needles": (
            "uefi_app.o",
            "ribon_platform_selected",
        ),
    },
    "x86-bios-client": {
        "architecture": "x86_64",
        "environment": "bios",
        "platform": "platform.pc-bios-x86",
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
            fail(f"{target}: missing generated report")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        plugins = report.get("plugins")
        if (
            report.get("architecture") != expected["architecture"]
            or report.get("environment") != expected["environment"]
            or not isinstance(plugins, list)
            or expected["platform"] not in plugins
            or sum(item.startswith("arch.") for item in plugins) != 1
            or sum(item.startswith("environment.") for item in plugins) != 1
            or sum(item.startswith("platform.") for item in plugins) != 1
        ):
            fail(f"{target}: generated tuple is not exact")
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
        forbidden = (
            "raspberrypi/rpi5" if target.startswith("qemu-") else
            "qemu/virt-aarch64" if target.startswith("rpi5-") else
            "src/environments/raw-fdt" if target == "x86_64-uefi-app" else
            ""
        )
        if forbidden and forbidden in link_map:
            fail(f"{target}: forbidden object leaked into link map: {forbidden}")
    print("RIBON-R4-TARGET-OBJECT-GRAPHS-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
