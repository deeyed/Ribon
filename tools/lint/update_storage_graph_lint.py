#!/usr/bin/env python3
"""Validate D02 update-writer graph isolation and layout identity binding."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
WRITER_CAPABILITIES = {"INACTIVE_SLOT_ERASE", "INACTIVE_SLOT_WRITE"}


def fail(message: str) -> None:
    """Terminate with one stable negative graph diagnostic."""

    print(f"RIBON-UPDATE-STORAGE-GRAPH-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    """Check one positive recovery report and every source normal product."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    layout = json.loads(args.layout.read_text(encoding="utf-8"))
    binding = report.get("update_storage")
    if (
        report.get("mode") not in {"recovery", "provisioning"}
        or not isinstance(binding, dict)
        or binding.get("schema") != "ribon-update-storage-binding-v1"
        or binding.get("layout_digest_sha256")
        != layout.get("layout_identity_sha256")
    ):
        fail("positive product does not bind the generated layout identity")
    service_by_id = {
        service.get("id"): service.get("kind")
        for service in report.get("services", [])
        if isinstance(service, dict)
    }
    expected = {
        binding["read_service_id"]: "boot-source",
        binding["writer_service_id"]: "inactive-slot-storage",
        binding["metadata_service_id"]: "persistent-metadata",
        binding["flush_service_id"]: "storage-flush",
    }
    if any(service_by_id.get(service_id) != kind for service_id, kind in expected.items()):
        fail("positive provider service roles are incomplete")
    for field in ("required_capabilities", "allowed_capabilities"):
        values = report.get(field)
        if not isinstance(values, list) or not WRITER_CAPABILITIES.issubset(values):
            fail(f"positive graph lacks writer authority in {field}")

    checked = 0
    roots = (ROOT / "products", ROOT / "qstar" / "manifests")
    for manifest_root in roots:
        for path in sorted(manifest_root.rglob("*.json")):
            document = json.loads(path.read_text(encoding="utf-8"))
            if document.get("product_kind") != "bootloader" or document.get("mode") != "normal":
                continue
            checked += 1
            if document.get("update_storage") is not None:
                fail(f"normal product selects update_storage: {path.relative_to(ROOT)}")
            if any(
                WRITER_CAPABILITIES.intersection(document.get(field, []))
                for field in ("required_capabilities", "allowed_capabilities")
            ):
                fail(f"normal product carries writer capability: {path.relative_to(ROOT)}")
            if any(
                isinstance(service, dict)
                and service.get("kind") == "inactive-slot-storage"
                for service in document.get("services", [])
            ):
                fail(f"normal product links writer service: {path.relative_to(ROOT)}")
    if checked == 0:
        fail("no normal bootloader product was inspected")
    print(
        f"RIBON-UPDATE-STORAGE-GRAPH-OK normal_products={checked} "
        "writer_modes=recovery,provisioning"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
