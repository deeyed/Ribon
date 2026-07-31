#!/usr/bin/env python3
"""Derive the diagnostic-only Ribos QEMU product from the host reference graph."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


SYMBOL_RENAMES = {
    "ribon_host_boot_source_service_descriptor":
        "ribon_validation_boot_source_service_descriptor",
    "ribon_host_environment_quiesce_service_descriptor":
        "ribon_validation_environment_quiesce_service_descriptor",
    "ribon_host_monotonic_timer_service_descriptor":
        "ribon_validation_monotonic_timer_service_descriptor",
    "ribon_host_persistent_metadata_service_descriptor":
        "ribon_validation_persistent_metadata_service_descriptor",
    "ribon_host_storage_flush_service_descriptor":
        "ribon_validation_storage_flush_service_descriptor",
    "ribon_host_watchdog_service_descriptor":
        "ribon_validation_watchdog_service_descriptor",
    "ribon_host_environment_plugin_descriptor":
        "ribon_validation_environment_plugin_descriptor",
    "ribon_host_ribos_authorize":
        "ribon_validation_ribos_authorize",
    "ribon_host_ribos_factory_recovery":
        "ribon_validation_ribos_factory_recovery",
    "ribon_host_ribos_validate_boot_action":
        "ribon_validation_ribos_validate_boot_action",
    "ribon_host_ribos_slot_selected":
        "ribon_validation_ribos_slot_selected",
    "ribon_host_ribos_slot_image":
        "ribon_validation_ribos_slot_image",
    "ribon_host_ribos_image_verify":
        "ribon_validation_ribos_image_verify",
    "ribon_host_ribos_boot_slot":
        "ribon_validation_ribos_boot_slot",
    "ribon_host_ribos_boot_recovery":
        "ribon_validation_ribos_boot_recovery",
}


def rewrite(value: object) -> object:
    """Recursively rewrite stable IDs and provider symbols."""

    if isinstance(value, dict):
        return {key: rewrite(item) for key, item in value.items()}
    if isinstance(value, list):
        return [rewrite(item) for item in value]
    if isinstance(value, str):
        if value in SYMBOL_RENAMES:
            return SYMBOL_RENAMES[value]
        if value.startswith("service.host."):
            return value.replace("service.host.", "service.validation.", 1)
        if value == "ribon.environment.host":
            return "ribon.environment.validation"
        if value == "policy.ribos.host-reference.v1":
            return "policy.ribos.qemu-validation.v1"
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = json.loads(args.input.read_text(encoding="utf-8"))
    manifest = rewrite(source)
    assert isinstance(manifest, dict)
    manifest["product_id"] = "ribos-qemu-validation"
    manifest["target_id"] = "ribos-qemu-cross-architecture"
    manifest["image"] = {
        "format": "diagnostic-executable",
        "recipe": "ribos-qemu-validation",
        "artifact": "ribon-ribos-validation",
    }
    manifest["evidence"] = {
        "class": "qemu-smoke",
        "claim": (
            "same signed fixture artifact through generated product binding, "
            "generic Ribon adapter and target-neutral VM"
        ),
    }
    policy = manifest["ribos_policy"]
    assert isinstance(policy, dict)
    policy["policy_id"] = "policy.ribos.qemu-validation.v1"

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print("RIBOS-R18-QEMU-MANIFEST-OK product=ribos-qemu-validation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
