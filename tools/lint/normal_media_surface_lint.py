#!/usr/bin/env python3
"""Reject update-writer and network authority from normal boot-media products."""

from __future__ import annotations

import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN = {"INACTIVE_SLOT_WRITE", "INACTIVE_SLOT_ERASE", "NETWORK_TRANSPORT"}
FORBIDDEN_LINK_TOKENS = ("inactive_slot", "inactive-slot", "network_transport", "network-transport")


def fail(message: str) -> None:
    print(f"RIBON-NORMAL-MEDIA-SURFACE-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    if len(sys.argv) > 2:
        fail("usage: normal_media_surface_lint.py [normal-product-link-map]")
    link_map = Path(sys.argv[1]) if len(sys.argv) == 2 else None
    checked = 0
    for path in sorted((ROOT / "products").rglob("*.json")):
        manifest = json.loads(path.read_text(encoding="utf-8"))
        if manifest.get("product_kind") != "bootloader" or manifest.get("mode") != "normal":
            continue
        checked += 1
        capability_fields = (
            manifest.get("required_capabilities"),
            manifest.get("allowed_capabilities"),
        )
        if any(
            isinstance(values, list) and FORBIDDEN.intersection(values)
            for values in capability_fields
        ):
            fail(f"{path.relative_to(ROOT)} declares writer or network capability")
        services = manifest.get("services")
        if not isinstance(services, list):
            fail(f"{path.relative_to(ROOT)} has no services list")
        if any(
            isinstance(service, dict)
            and service.get("kind") in {"inactive-slot-storage", "network-transport"}
            for service in services
        ):
            fail(f"{path.relative_to(ROOT)} links writer or network service")
    if checked == 0:
        fail("no normal bootloader manifest was checked")
    if link_map is not None:
        if not link_map.is_file():
            fail(f"missing normal product link map: {link_map}")
        lowered = link_map.read_text(encoding="utf-8", errors="replace").lower()
        for token in FORBIDDEN_LINK_TOKENS:
            if token in lowered:
                fail(f"normal product link map contains forbidden authority token: {token}")
    print("RIBON-R8-NORMAL-MEDIA-SURFACE-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
