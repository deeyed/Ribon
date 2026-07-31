#!/usr/bin/env python3
"""Separate firmware consumers from personality provider reference products."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    print(f"RIBON-FIRMWARE-OBJECT-GRAPH-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def make_variable(text: str, name: str) -> str:
    """Return one multiline Make source-list assignment."""

    match = re.search(
        rf"^{re.escape(name)}\s*:=\s*(.*?)(?=^[A-Z0-9_]+\s*:?=|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        fail(f"missing Make variable {name}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--makefile", type=Path, required=True)
    parser.add_argument("--provider-root", type=Path, required=True)
    args = parser.parse_args()
    makefile = args.makefile.read_text(encoding="utf-8")

    if "src/firmware/" in make_variable(makefile, "UEFI_SRCS"):
        fail("UEFI application consumer links a firmware provider object")
    for variable in ("UEFI_PROVIDER_SRCS", "BIOS_PROVIDER_SRCS"):
        sources = make_variable(makefile, variable)
        if "src/environments/" in sources:
            fail(f"{variable} links an environment consumer object")

    expected = {
        "uefi-compatible-reference": "uefi-compatible",
        "bios-compatible-reference": "bios-compatible",
    }
    for directory_name, personality in expected.items():
        directory = args.provider_root / directory_name
        report_path = directory / "results" / "object-graph.json"
        if not report_path.is_file():
            fail(f"{directory_name}: generated report is missing")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        plugins = report.get("plugins")
        if (
            report.get("product_kind") != "firmware"
            or report.get("environment") is not None
            or report.get("firmware_personality") != personality
            or not isinstance(plugins, list)
            or sum(item.startswith("arch.") for item in plugins) != 1
            or sum(item.startswith("personality.") for item in plugins) != 1
            or any(item.startswith("environment.") for item in plugins)
        ):
            fail(f"{directory_name}: provider tuple is not exact")
        objects = [
            path.relative_to(directory).as_posix()
            for path in directory.rglob("*.o")
        ]
        if not any("src/firmware/" in path for path in objects):
            fail(f"{directory_name}: personality object is missing")
        if any("src/environments/" in path for path in objects):
            fail(f"{directory_name}: consumer object leaked into provider")

    consumer = json.loads(
        (ROOT / "products" / "bootmgr" / "manifests" /
         "x86_64-uefi-parus-fixture.json").read_text(encoding="utf-8")
    )
    provider = json.loads(
        (ROOT / "products" / "firmware" / "manifests" /
         "uefi-compatible-reference.json").read_text(encoding="utf-8")
    )
    if (
        consumer.get("product_kind") != "bootloader"
        or consumer.get("environment") != "uefi"
        or provider.get("product_kind") != "firmware"
        or provider.get("firmware_personality") != "uefi-compatible"
        or consumer.get("target_id") == provider.get("target_id")
        or consumer["image"]["artifact"] == provider["image"]["artifact"]
    ):
        fail("UEFI consumer/provider naming or direction is ambiguous")
    print("RIBON-R5-FIRMWARE-CONSUMER-PROVIDER-GRAPHS-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
