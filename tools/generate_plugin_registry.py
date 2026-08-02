#!/usr/bin/env python3
"""Generate a deterministic Ribon product registry from a source manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


ARCHITECTURE_MASKS = {
    "x86_64": "RIBON_ARCH_MASK_X86_64",
    "aarch64": "RIBON_ARCH_MASK_AARCH64",
    "riscv64": "RIBON_ARCH_MASK_RISCV64",
}
ENVIRONMENT_MASKS = {
    "host": "RIBON_ENV_MASK_HOST",
    "uefi": "RIBON_ENV_MASK_UEFI",
    "bios": "RIBON_ENV_MASK_BIOS",
    "raw-fdt": "RIBON_ENV_MASK_RAW_FDT",
    "sbi": "RIBON_ENV_MASK_SBI",
}
ENVIRONMENT_PLUGIN_IDS = {
    "host": "environment.host",
    "uefi": "environment.uefi-app",
    "bios": "environment.bios-client",
    "raw-fdt": "environment.raw-fdt",
    "sbi": "environment.sbi",
}
MODE_VALUES = {
    "normal": "RIBON_MODE_NORMAL",
    "recovery": "RIBON_MODE_RECOVERY",
    "provisioning": "RIBON_MODE_PROVISIONING",
    "diagnostic": "RIBON_MODE_DIAGNOSTIC",
}
PRODUCT_KIND_VALUES = {
    "library": "RIBON_PRODUCT_KIND_LIBRARY",
    "bootloader": "RIBON_PRODUCT_KIND_BOOTLOADER",
    "firmware": "RIBON_PRODUCT_KIND_FIRMWARE",
}
PLUGIN_KIND_VALUES = {
    "architecture": "RIBON_PLUGIN_KIND_ARCHITECTURE",
    "environment": "RIBON_PLUGIN_KIND_ENVIRONMENT",
    "image-format": "RIBON_PLUGIN_KIND_IMAGE_FORMAT",
    "boot-protocol": "RIBON_PLUGIN_KIND_BOOT_PROTOCOL",
    "policy": "RIBON_PLUGIN_KIND_POLICY",
    "firmware-personality": "RIBON_PLUGIN_KIND_FIRMWARE_PERSONALITY",
    "service": "RIBON_PLUGIN_KIND_SERVICE",
}
SERVICE_KIND_VALUES = {
    "boot-source": "RIBON_SERVICE_KIND_BOOT_SOURCE",
    "inactive-slot-storage": "RIBON_SERVICE_KIND_INACTIVE_SLOT_STORAGE",
    "storage-flush": "RIBON_SERVICE_KIND_STORAGE_FLUSH",
    "monotonic-timer": "RIBON_SERVICE_KIND_MONOTONIC_TIMER",
    "watchdog": "RIBON_SERVICE_KIND_WATCHDOG",
    "reset": "RIBON_SERVICE_KIND_RESET",
    "persistent-metadata": "RIBON_SERVICE_KIND_PERSISTENT_METADATA",
    "network-transport": "RIBON_SERVICE_KIND_NETWORK_TRANSPORT",
    "random-nonce": "RIBON_SERVICE_KIND_RANDOM_NONCE",
    "diagnostic-sink": "RIBON_SERVICE_KIND_DIAGNOSTIC_SINK",
    "environment-quiesce": "RIBON_SERVICE_KIND_ENVIRONMENT_QUIESCE",
    "machine-description": "RIBON_SERVICE_KIND_MACHINE_DESCRIPTION",
    "payload-placement": "RIBON_SERVICE_KIND_PAYLOAD_PLACEMENT",
    "boot-module-bundle": "RIBON_SERVICE_KIND_BOOT_MODULE_BUNDLE",
    "terminal-image-launch": "RIBON_SERVICE_KIND_TERMINAL_IMAGE_LAUNCH",
}
PERSONALITY_MASKS = {
    "uefi-compatible": "RIBON_PERSONALITY_MASK_UEFI_COMPATIBLE",
    "bios-compatible": "RIBON_PERSONALITY_MASK_BIOS_COMPATIBLE",
}
EVIDENCE_CLASSES = {
    "unit",
    "compile-only",
    "qemu-smoke",
    "qemu-runtime",
    "package",
}
CAPABILITIES = {
    name.removeprefix("RIBON_CAP_"): name
    for name in (
        "RIBON_CAP_BOOT_SOURCE_READ",
        "RIBON_CAP_INACTIVE_SLOT_WRITE",
        "RIBON_CAP_INACTIVE_SLOT_ERASE",
        "RIBON_CAP_STORAGE_FLUSH",
        "RIBON_CAP_MONOTONIC_TIMER",
        "RIBON_CAP_WATCHDOG",
        "RIBON_CAP_RESET",
        "RIBON_CAP_PERSISTENT_METADATA",
        "RIBON_CAP_NETWORK_TRANSPORT",
        "RIBON_CAP_RANDOM_NONCE",
        "RIBON_CAP_DIAGNOSTIC_SINK",
        "RIBON_CAP_ENVIRONMENT_QUIESCE",
        "RIBON_CAP_ARCHITECTURE",
        "RIBON_CAP_IMAGE_ELF64",
        "RIBON_CAP_BOOT_PROTOCOL",
        "RIBON_CAP_HANDOFF",
        "RIBON_CAP_ENTRY_CONTRACT",
        "RIBON_CAP_BOOT_CONFIRMATION",
        "RIBON_CAP_IMAGE_PE_COFF",
        "RIBON_CAP_MACHINE_DESCRIPTION",
        "RIBON_CAP_FIRMWARE_PERSONALITY",
        "RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY",
        "RIBON_CAP_SDK_CONTRACT",
        "RIBON_CAP_PAYLOAD_PLACEMENT",
        "RIBON_CAP_BOOT_MODULE_BUNDLE",
        "RIBON_CAP_IMAGE_LINUX_AARCH64",
        "RIBON_CAP_TERMINAL_IMAGE_LAUNCH",
        "RIBON_CAP_IMAGE_LINUX_RISCV64",
    )
}
LIMIT_KEYS = (
    "max_memory_regions",
    "max_load_segments",
    "max_components",
    "max_retries",
    "max_input_bytes",
    "max_handoff_bytes",
    "arena_bytes",
    "operation_deadline_ms",
)
IMAGE_KEYS = {"format", "recipe", "artifact"}
EVIDENCE_KEYS = {"class", "claim"}
PAYLOAD_KEYS = {
    "architecture",
    "class",
    "entry_abi",
    "format",
    "load_base",
    "load_size",
}
BOOT_MODULE_BUNDLE_KEYS = {
    "component_manifest_schema",
    "maximum_modules",
    "provider",
}
UPDATE_STORAGE_KEYS = {
    "schema",
    "provider_class",
    "layout_id",
    "layout_digest_sha256",
    "media_identity_digest_sha256",
    "read_service_id",
    "writer_service_id",
    "metadata_service_id",
    "flush_service_id",
}
UPDATE_STORAGE_PROVIDER_CLASSES = {
    "firmware": "RIBON_UPDATE_STORAGE_PROVIDER_CLASS_FIRMWARE",
    "native": "RIBON_UPDATE_STORAGE_PROVIDER_CLASS_NATIVE",
    "reference": "RIBON_UPDATE_STORAGE_PROVIDER_CLASS_REFERENCE",
}
RECOVERY_NETWORK_KEYS = {
    "schema",
    "transport",
    "service_id",
    "server_ipv4",
    "station_ipv4",
    "subnet_mask_ipv4",
    "block_size",
    "retry_count",
    "absolute_deadline_ms",
    "objects",
}
RECOVERY_NETWORK_OBJECT_KEYS = {"kind", "path", "maximum_bytes"}
RECOVERY_NETWORK_OBJECT_KINDS = (
    "manifest",
    "signature-envelope",
    "bundle",
)
SIGNATURE_PROVIDER_KEYS = {"algorithm", "class", "id", "symbol"}
PROTECTED_STATE_PROVIDER_KEYS = {"class", "id", "rollback_domains", "symbol"}
KEY_POLICY_KEYS = {"generation", "id", "keys"}
KEY_POLICY_RECORD_KEYS = {
    "id",
    "issuer",
    "maximum_sequence",
    "minimum_sequence",
    "modes",
    "public_key_hex",
    "rollback_domains",
    "status",
    "usages",
}
KEY_POLICY_MODES = {
    "normal": 1,
    "recovery": 2,
    "provisioning": 3,
    "diagnostic": 4,
}
KEY_POLICY_USAGES = {
    "policy-normal": 1,
    "policy-recovery": 2,
    "policy-provisioning": 3,
    "policy-diagnostic": 4,
    "update-manifest": 5,
    "boot-image": 6,
    "boot-confirmation": 7,
}
KEY_POLICY_LIFECYCLES = {
    "active": "RIBON_KEY_POLICY_LIFECYCLE_ACTIVE",
    "retiring": "RIBON_KEY_POLICY_LIFECYCLE_RETIRING",
    "revoked": "RIBON_KEY_POLICY_LIFECYCLE_REVOKED",
}
PROTECTED_STATE_PROVIDER_CLASSES = {
    "hardware": "RIBON_PROTECTED_STATE_PROVIDER_CLASS_HARDWARE",
    "reference": "RIBON_PROTECTED_STATE_PROVIDER_CLASS_REFERENCE",
    "fixture": "RIBON_PROTECTED_STATE_PROVIDER_CLASS_FIXTURE",
}
RIBOS_CAPABILITIES = {
    "INSPECT": 1 << 0,
    "DEVICE": 1 << 1,
    "STATE": 1 << 2,
    "NETWORK": 1 << 3,
    "FLASH": 1 << 4,
    "HANDOFF": 1 << 5,
    "BOOT": 1 << 6,
    "DIAGNOSTIC": 1 << 7,
}
RIBOS_EFFECTS = {
    "pure": ("RIBOS_VM_HELPER_EFFECT_PURE", 1),
    "ephemeral": ("RIBOS_VM_HELPER_EFFECT_EPHEMERAL", 2),
    "journaled": ("RIBOS_VM_HELPER_EFFECT_JOURNALED", 3),
    "terminal": ("RIBOS_VM_HELPER_EFFECT_TERMINAL", 4),
}
RIBOS_ROUTABLE_SERVICE_KINDS = set(SERVICE_KIND_VALUES) - {
    "boot-module-bundle"
}
RIBOS_DURABILITIES = {
    "none": ("RIBOS_VM_HELPER_DURABILITY_NONE", 0),
    "volatile": ("RIBOS_VM_HELPER_DURABILITY_VOLATILE", 1),
    "journal-receipt": ("RIBOS_VM_HELPER_DURABILITY_JOURNAL_RECEIPT", 2),
    "sealed-intent": ("RIBOS_VM_HELPER_DURABILITY_SEALED_INTENT", 3),
}
RIBOS_TRANSITIONS = {
    "none": ("RIBOS_VM_HANDLE_TRANSITION_NONE", 0),
    "create": ("RIBOS_VM_HANDLE_TRANSITION_CREATE", 1),
    "consume": ("RIBOS_VM_HANDLE_TRANSITION_CONSUME", 2),
    "replace": ("RIBOS_VM_HANDLE_TRANSITION_REPLACE", 3),
    "terminal-consume": ("RIBOS_VM_HANDLE_TRANSITION_TERMINAL_CONSUME", 4),
}
RIBOS_PHASES = {
    "early": 0,
    "foundation": 1,
    "driver": 2,
    "boot": 3,
    "quiesce": 4,
}
RIBOS_LIMIT_KEYS = {
    "maximum_instructions",
    "maximum_helper_calls",
    "maximum_stack_bytes",
    "maximum_arena_bytes",
    "maximum_input_bytes",
    "maximum_output_bytes",
    "maximum_operations",
    "maximum_polls",
    "maximum_execution_duration_ns",
    "maximum_helper_duration_ns",
    "maximum_call_depth",
    "maximum_handles",
    "maximum_trace_records",
}
RIBOS_HELPER_KEYS = {
    "stable_id",
    "callback_symbol",
    "service_kind",
    "service_id",
    "ribon_capabilities",
    "ribos_capabilities",
    "effect",
    "durability",
    "handle_transition",
    "transition_parameter",
    "allowed_modes",
    "allowed_phases",
    "maximum_input_bytes",
    "maximum_output_bytes",
    "maximum_operations",
    "maximum_polls",
    "maximum_duration_ns",
}
RIBOS_POLICY_KEYS = {
    "policy_id",
    "schema_symbol",
    "authorization",
    "factory_recovery_symbol",
    "validate_boot_action_symbol",
    "timer_service_id",
    "watchdog_service_id",
    "watchdog_required",
    "phase",
    "capabilities",
    "ribon_capabilities",
    "arena_budget",
    "limits",
    "helpers",
}
RIBOS_AUTHORIZATION_KEYS = {"class", "callback_symbol", "rollback_domain"}


def _string(manifest: dict[str, object], key: str) -> str:
    value = manifest.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{key} must be a non-empty string")
    return value


def _capabilities(manifest: dict[str, object], key: str) -> list[str]:
    values = manifest.get(key)
    if not isinstance(values, list) or not values:
        raise ValueError(f"{key} must be a non-empty list")
    if any(not isinstance(value, str) or value not in CAPABILITIES for value in values):
        raise ValueError(f"{key} contains an unknown capability")
    if values != sorted(set(values)):
        raise ValueError(f"{key} must be unique and sorted")
    return values


def _sorted_strings(
    manifest: dict[str, object],
    key: str,
    allow_empty: bool = False,
) -> list[str]:
    values = manifest.get(key)
    if (
        not isinstance(values, list)
        or (not values and not allow_empty)
        or any(not isinstance(value, str) or not value for value in values)
        or values != sorted(set(values))
    ):
        raise ValueError(f"{key} must be a sorted unique string list")
    return values


def _provider_entries(
    manifest: dict[str, object],
    key: str,
    kind_values: dict[str, str],
    symbol_required: bool,
) -> list[dict[str, str]]:
    """Validate deterministic typed service or plugin selection metadata."""

    values = manifest.get(key)
    if not isinstance(values, list):
        raise ValueError(f"{key} must be a list")
    entries: list[dict[str, str]] = []
    ids: list[str] = []
    symbols: set[str] = set()
    previous_kind = -1
    kind_order = list(kind_values)
    for value in values:
        if not isinstance(value, dict):
            raise ValueError(f"{key} entry must be an object")
        item_id = value.get("id")
        kind = value.get("kind")
        symbol = value.get("symbol")
        if (
            not isinstance(item_id, str)
            or not item_id
            or not isinstance(kind, str)
            or kind not in kind_values
            or (symbol_required and
                (not isinstance(symbol, str) or not symbol.startswith("ribon_")))
            or (not symbol_required and symbol is not None)
        ):
            raise ValueError(f"{key} entry has an invalid id, kind, or symbol")
        order = kind_order.index(kind)
        if not symbol_required and order <= previous_kind:
            raise ValueError(f"{key} must have one entry per kind in ABI order")
        previous_kind = order
        if item_id in ids:
            raise ValueError(f"{key} IDs must be unique")
        ids.append(item_id)
        if symbol_required:
            assert isinstance(symbol, str)
            if symbol in symbols:
                raise ValueError(f"duplicate {key} symbol: {symbol}")
            symbols.add(symbol)
            entries.append({"id": item_id, "kind": kind, "symbol": symbol})
        else:
            entries.append({"id": item_id, "kind": kind})
    if symbol_required and ids != sorted(ids):
        raise ValueError(f"{key} IDs must be sorted")
    return entries


def _signature_provider(
    manifest: dict[str, object],
) -> dict[str, str] | None:
    """Validate one optional product-selected verification-only provider."""

    value = manifest.get("signature_provider")
    if value is None:
        return None
    if (
        not isinstance(value, dict)
        or set(value) != SIGNATURE_PROVIDER_KEYS
        or value.get("algorithm") != "ed25519"
        or value.get("class") not in {"production", "fixture"}
        or not isinstance(value.get("id"), str)
        or not str(value["id"]).startswith("security.signature.")
        or not isinstance(value.get("symbol"), str)
        or not str(value["symbol"]).startswith("ribon_")
    ):
        raise ValueError("signature_provider must select one typed Ed25519 provider")
    return {key: str(value[key]) for key in sorted(SIGNATURE_PROVIDER_KEYS)}


def _protected_state_provider(
    manifest: dict[str, object],
) -> dict[str, object] | None:
    """Validate one provider and derive its source-independent domain table."""

    value = manifest.get("protected_state_provider")
    if value is None:
        return None
    if (
        not isinstance(value, dict)
        or set(value) != PROTECTED_STATE_PROVIDER_KEYS
        or value.get("class") not in {"hardware", "reference", "fixture"}
        or not isinstance(value.get("id"), str)
        or not str(value["id"]).startswith("security.protected-state.")
        or not isinstance(value.get("symbol"), str)
        or not str(value["symbol"]).startswith("ribon_")
    ):
        raise ValueError("protected_state_provider must select one typed provider")
    domains = _key_policy_string_list(
        value.get("rollback_domains"),
        "protected-state rollback domains",
        maximum=8,
    )
    if any(len(domain.encode("utf-8")) > 128 or "\0" in domain for domain in domains):
        raise ValueError("protected-state domain IDs must be 1..128 non-NUL UTF-8 bytes")
    manifest["_protected_state_domain_digests"] = sorted(
        hashlib.sha256(domain.encode("utf-8")).digest() for domain in domains
    )
    return {
        "class": str(value["class"]),
        "id": str(value["id"]),
        "rollback_domains": domains,
        "symbol": str(value["symbol"]),
    }


def _key_policy_ascii_id(value: object, field: str) -> str:
    """Validate one source-stable ASCII identity that is safe in generated C."""

    accepted = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"
    if (
        not isinstance(value, str)
        or not 1 <= len(value.encode("ascii", errors="ignore")) <= 64
        or any(character not in accepted for character in value)
    ):
        raise ValueError(f"{field} must be a 1..64 byte stable ASCII ID")
    return value


def _key_policy_string_list(
    value: object,
    field: str,
    accepted: set[str] | None = None,
    maximum: int = 32,
) -> list[str]:
    """Validate one bounded sorted unique manifest string list."""

    if (
        not isinstance(value, list)
        or not 1 <= len(value) <= maximum
        or any(not isinstance(item, str) or not item for item in value)
        or value != sorted(set(value))
        or (accepted is not None and any(item not in accepted for item in value))
    ):
        raise ValueError(f"{field} must be a bounded sorted unique list")
    return value


def _key_policy_mask(values: list[str], registry: dict[str, int]) -> int:
    """Convert stable one-based registry values to an explicit bit mask."""

    mask = 0
    for value in values:
        mask |= 1 << (registry[value] - 1)
    return mask


def _key_policy_contains(issuer: dict[str, object], child: dict[str, object]) -> bool:
    """Check that one issuer record cannot delegate authority it does not own."""

    return (
        set(child["usages"]).issubset(issuer["usages"])
        and set(child["modes"]).issubset(issuer["modes"])
        and set(child["rollback_domains"]).issubset(issuer["rollback_domains"])
        and child["minimum_sequence"] >= issuer["minimum_sequence"]
        and child["maximum_sequence"] <= issuer["maximum_sequence"]
    )


def _key_policy_canonical_digest(
    store_id: str,
    generation: int,
    records: list[dict[str, object]],
    product_digest: bytes,
) -> bytes:
    """Serialize the pointer-free key-store identity and return its SHA-256."""

    payload = bytearray(b"RIBON-KEY-STORE-V1".ljust(32, b"\0"))
    payload.extend(struct.pack("<HHIQ", 1, 0, len(records), generation))
    payload.extend(hashlib.sha256(store_id.encode("ascii")).digest())
    for record in records:
        key_id = str(record["id"]).encode("ascii")
        public_key = bytes.fromhex(str(record["public_key_hex"]))
        issuer = record["issuer"]
        domain_digests = record["domain_digests"]
        assert isinstance(domain_digests, list)
        payload.extend(hashlib.sha256(key_id).digest())
        payload.extend(public_key)
        payload.extend(hashlib.sha256(public_key).digest())
        payload.extend(product_digest)
        payload.extend(struct.pack("<Q", int(record["usage_mask"])))
        payload.extend(struct.pack("<I", int(record["mode_mask"])))
        payload.extend(struct.pack(
            "<I",
            {"active": 1, "retiring": 2, "revoked": 3}[str(record["status"])],
        ))
        payload.extend(struct.pack("<I", 1 if issuer is None else 0))
        payload.extend(struct.pack(
            "<QQ",
            int(record["minimum_sequence"]),
            int(record["maximum_sequence"]),
        ))
        payload.extend(
            bytes(32) if issuer is None
            else hashlib.sha256(str(issuer).encode("ascii")).digest()
        )
        payload.extend(struct.pack(
            "<II",
            int(record["delegation_depth"]),
            len(domain_digests),
        ))
        for digest in domain_digests:
            assert isinstance(digest, bytes)
            payload.extend(digest)
    return hashlib.sha256(payload).digest()


def _key_policy(
    manifest: dict[str, object],
    source_digest: bytes,
) -> dict[str, object] | None:
    """Validate and normalize one bounded immutable product trust store."""

    raw = manifest.get("key_policy")
    if raw is None:
        return None
    if not isinstance(raw, dict) or set(raw) != KEY_POLICY_KEYS:
        raise ValueError("key_policy must define the complete bounded v1 store")
    store_id = _key_policy_ascii_id(raw.get("id"), "key_policy.id")
    generation = raw.get("generation")
    if not isinstance(generation, int) or not 1 <= generation <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("key_policy.generation must be a positive u64")
    raw_records = raw.get("keys")
    if not isinstance(raw_records, list) or not 1 <= len(raw_records) <= 32:
        raise ValueError("key_policy.keys must contain 1..32 records")
    records: list[dict[str, object]] = []
    ids: list[str] = []
    for raw_record in raw_records:
        if not isinstance(raw_record, dict) or set(raw_record) != KEY_POLICY_RECORD_KEYS:
            raise ValueError("key_policy record must define the exact v1 fields")
        key_id = _key_policy_ascii_id(raw_record.get("id"), "key_policy key ID")
        issuer_value = raw_record.get("issuer")
        issuer = None if issuer_value is None else _key_policy_ascii_id(
            issuer_value,
            "key_policy issuer ID",
        )
        public_key_hex = raw_record.get("public_key_hex")
        if (
            not isinstance(public_key_hex, str)
            or len(public_key_hex) != 64
            or any(character not in "0123456789abcdef" for character in public_key_hex)
            or bytes.fromhex(public_key_hex) == bytes(32)
        ):
            raise ValueError("key_policy public key must be 32 lowercase hex bytes")
        modes = _key_policy_string_list(
            raw_record.get("modes"),
            "key_policy modes",
            set(KEY_POLICY_MODES),
            4,
        )
        usages = _key_policy_string_list(
            raw_record.get("usages"),
            "key_policy usages",
            set(KEY_POLICY_USAGES),
            6,
        )
        domains = _key_policy_string_list(
            raw_record.get("rollback_domains"),
            "key_policy rollback domains",
            maximum=4,
        )
        if any(len(domain.encode("utf-8")) > 128 or "\0" in domain for domain in domains):
            raise ValueError("rollback domain IDs must be 1..128 non-NUL UTF-8 bytes")
        minimum = raw_record.get("minimum_sequence")
        maximum = raw_record.get("maximum_sequence")
        if (
            not isinstance(minimum, int)
            or not isinstance(maximum, int)
            or minimum < 0
            or maximum > 0xFFFFFFFFFFFFFFFF
            or minimum > maximum
        ):
            raise ValueError("key policy sequence interval must be an inclusive u64 range")
        status = raw_record.get("status")
        if status not in KEY_POLICY_LIFECYCLES:
            raise ValueError("key policy lifecycle is invalid")
        ids.append(key_id)
        records.append({
            "id": key_id,
            "issuer": issuer,
            "maximum_sequence": maximum,
            "minimum_sequence": minimum,
            "modes": modes,
            "mode_mask": _key_policy_mask(modes, KEY_POLICY_MODES),
            "public_key_hex": public_key_hex,
            "rollback_domains": domains,
            "domain_digests": sorted(
                hashlib.sha256(domain.encode("utf-8")).digest()
                for domain in domains
            ),
            "status": status,
            "usages": usages,
            "usage_mask": _key_policy_mask(usages, KEY_POLICY_USAGES),
        })
    if ids != sorted(set(ids)):
        raise ValueError("key policy IDs must be unique and sorted")
    by_id = {str(record["id"]): record for record in records}
    visiting: set[str] = set()
    depths: dict[str, int] = {}

    def depth(key_id: str) -> int:
        if key_id in depths:
            return depths[key_id]
        if key_id in visiting:
            raise ValueError("key policy issuer graph contains a cycle")
        visiting.add(key_id)
        record = by_id[key_id]
        issuer = record["issuer"]
        if issuer is None:
            result = 0
        else:
            if issuer not in by_id:
                raise ValueError("key policy issuer is unknown")
            issuer_record = by_id[str(issuer)]
            if not _key_policy_contains(issuer_record, record):
                raise ValueError("key policy delegation expands issuer authority")
            result = depth(str(issuer)) + 1
        visiting.remove(key_id)
        if result > 2:
            raise ValueError("key policy delegation exceeds two edges")
        depths[key_id] = result
        return result

    for record in records:
        record["delegation_depth"] = depth(str(record["id"]))
    identities: set[str] = set()
    for record in records:
        identity = hashlib.sha256(
            bytes.fromhex(str(record["public_key_hex"]))
        ).hexdigest()
        record["key_identity_sha256"] = identity
        if identity in identities:
            raise ValueError("key records have duplicate public-key identity")
        identities.add(identity)
    if manifest.get("ribos_policy") is not None:
        selected_mode = str(manifest["mode"])
        selected_usage = f"policy-{selected_mode}"
        if any(
            record["modes"] != [selected_mode]
            or record["usages"] != [selected_usage]
            for record in records
        ):
            raise ValueError("Ribos key policy must be closed to its product mode and usage")
    canonical_digest = _key_policy_canonical_digest(
        store_id,
        generation,
        records,
        source_digest,
    )
    manifest["_key_policy_records"] = records
    manifest["_key_policy_digest"] = canonical_digest
    return {
        "id": store_id,
        "generation": generation,
        "keys": [
            {
                key: record[key]
                for key in (
                    "delegation_depth",
                    "id",
                    "issuer",
                    "key_identity_sha256",
                    "maximum_sequence",
                    "minimum_sequence",
                    "modes",
                    "public_key_hex",
                    "rollback_domains",
                    "status",
                    "usages",
                )
            }
            for record in records
        ],
    }


def _ribos_string_list(
    value: object,
    field: str,
    accepted: set[str],
    allow_empty: bool = False,
) -> list[str]:
    if (
        not isinstance(value, list)
        or (not value and not allow_empty)
        or any(not isinstance(item, str) or item not in accepted for item in value)
        or value != sorted(set(value))
    ):
        raise ValueError(f"{field} must be a sorted unique supported list")
    return value


def _ribos_policy(
    manifest: dict[str, object],
    plugin_ids: list[str],
    services: list[dict[str, str]],
    allowed_capabilities: list[str],
) -> dict[str, object] | None:
    """Validate the optional product-generated Ribos schema/helper binding."""

    raw = manifest.get("ribos_policy")
    if raw is None:
        if "policy.ribos" in plugin_ids:
            raise ValueError("policy.ribos plugin requires ribos_policy")
        return None
    if not isinstance(raw, dict) or set(raw) != RIBOS_POLICY_KEYS:
        raise ValueError("ribos_policy must define the complete v1 binding")
    if "policy.ribos" not in plugin_ids or "ribos" not in manifest["policies"]:
        raise ValueError("ribos_policy requires policy.ribos and ribos policy selection")
    selection = manifest["plugin_selections"]
    if not isinstance(selection, list) or not any(
        item == {"id": "policy.ribos", "kind": "policy"} for item in selection
    ):
        raise ValueError("ribos_policy requires the policy.ribos kind selector")
    strings = (
        "policy_id",
        "schema_symbol",
        "factory_recovery_symbol",
        "validate_boot_action_symbol",
        "timer_service_id",
    )
    for field in strings:
        if not isinstance(raw.get(field), str) or not raw[field]:
            raise ValueError(f"ribos_policy.{field} must be a stable string")
    for field in (
        "factory_recovery_symbol",
        "validate_boot_action_symbol",
    ):
        if not str(raw[field]).startswith("ribon_"):
            raise ValueError(f"ribos_policy.{field} must be a Ribon symbol")
    if not str(raw["schema_symbol"]).startswith("ribos_"):
        raise ValueError("ribos_policy.schema_symbol must be a Ribos schema provider")
    authorization = raw.get("authorization")
    if (
        not isinstance(authorization, dict)
        or set(authorization) != RIBOS_AUTHORIZATION_KEYS
        or authorization.get("class") not in {"fixture-callback", "signed-policy"}
    ):
        raise ValueError("ribos_policy.authorization must define one exact authority class")
    callback = authorization.get("callback_symbol")
    rollback_domain = authorization.get("rollback_domain")
    if authorization["class"] == "fixture-callback":
        if (
            not isinstance(callback, str)
            or not callback.startswith("ribon_")
            or rollback_domain is not None
        ):
            raise ValueError("fixture authorization requires one callback and no domain")
    elif (
        callback is not None
        or not isinstance(rollback_domain, str)
        or not rollback_domain
    ):
        raise ValueError("signed authorization requires one rollback domain and no callback")
    if raw.get("phase") != "boot":
        raise ValueError("Ribos policy v1 executes only in boot phase")
    mode = str(manifest["mode"])
    capabilities = _ribos_string_list(
        raw.get("capabilities"),
        "ribos_policy.capabilities",
        set(RIBOS_CAPABILITIES),
    )
    ribon_capabilities = _ribos_string_list(
        raw.get("ribon_capabilities"),
        "ribos_policy.ribon_capabilities",
        set(CAPABILITIES),
    )
    if not set(ribon_capabilities).issubset(allowed_capabilities):
        raise ValueError("Ribos policy Ribon capabilities must be product-allowed")
    if mode == "normal" and ({"NETWORK", "FLASH"} & set(capabilities)):
        raise ValueError("normal Ribos policy must not grant network or flash")
    arena_budget = raw.get("arena_budget")
    if not isinstance(arena_budget, int) or arena_budget <= 0:
        raise ValueError("ribos_policy.arena_budget must be positive")
    limits = raw.get("limits")
    if (
        not isinstance(limits, dict)
        or set(limits) != RIBOS_LIMIT_KEYS
        or any(not isinstance(limits[key], int) or limits[key] <= 0 for key in limits)
        or limits["maximum_arena_bytes"] > arena_budget
        or limits["maximum_helper_duration_ns"] >
            limits["maximum_execution_duration_ns"]
    ):
        raise ValueError("ribos_policy.limits must be a bounded complete v1 closure")
    service_by_id = {item["id"]: item for item in services}
    timer = service_by_id.get(str(raw["timer_service_id"]))
    if timer is None or timer["kind"] != "monotonic-timer":
        raise ValueError("ribos_policy timer must select a monotonic service")
    watchdog_required = raw.get("watchdog_required")
    watchdog_id = raw.get("watchdog_service_id")
    if not isinstance(watchdog_required, bool):
        raise ValueError("ribos_policy.watchdog_required must be boolean")
    if watchdog_required:
        watchdog = service_by_id.get(str(watchdog_id))
        if watchdog is None or watchdog["kind"] != "watchdog":
            raise ValueError("required Ribos watchdog must select a watchdog service")
    elif watchdog_id is not None:
        raise ValueError("optional Ribos watchdog ID must be null")
    helpers = raw.get("helpers")
    if not isinstance(helpers, list) or not helpers:
        raise ValueError("ribos_policy.helpers must be non-empty")
    normalized: list[dict[str, object]] = []
    stable_ids: list[int] = []
    for helper in helpers:
        if not isinstance(helper, dict) or set(helper) != RIBOS_HELPER_KEYS:
            raise ValueError("Ribos helper must define the complete execution route")
        stable_id = helper.get("stable_id")
        callback = helper.get("callback_symbol")
        if (
            not isinstance(stable_id, int)
            or stable_id < 0
            or stable_id >= 0xFFFFFFFF
            or not isinstance(callback, str)
            or not callback.startswith("ribon_")
        ):
            raise ValueError("Ribos helper stable ID or callback is invalid")
        stable_ids.append(stable_id)
        helper_ribos = _ribos_string_list(
            helper.get("ribos_capabilities"),
            "Ribos helper capabilities",
            set(RIBOS_CAPABILITIES),
        )
        helper_ribon = _ribos_string_list(
            helper.get("ribon_capabilities"),
            "Ribos helper Ribon capabilities",
            set(CAPABILITIES),
            allow_empty=True,
        )
        if not set(helper_ribos).issubset(capabilities):
            raise ValueError("helper capability exceeds Ribos policy grant")
        if not set(helper_ribon).issubset(ribon_capabilities):
            raise ValueError("helper Ribon capability exceeds policy grant")
        service_kind = helper.get("service_kind")
        service_id = helper.get("service_id")
        if service_kind is None or service_id is None:
            if service_kind is not None or service_id is not None or helper_ribon:
                raise ValueError("service-free helper must not require a Ribon service")
        elif (
            not isinstance(service_kind, str)
            or service_kind not in RIBOS_ROUTABLE_SERVICE_KINDS
            or not isinstance(service_id, str)
            or service_by_id.get(service_id, {}).get("kind") != service_kind
            or not helper_ribon
        ):
            raise ValueError("helper service route does not match the product graph")
        effect = helper.get("effect")
        durability = helper.get("durability")
        transition = helper.get("handle_transition")
        if (
            effect not in RIBOS_EFFECTS
            or durability not in RIBOS_DURABILITIES
            or transition not in RIBOS_TRANSITIONS
        ):
            raise ValueError("helper effect, durability, or transition is invalid")
        expected_durability = {
            "pure": "none",
            "ephemeral": "volatile",
            "journaled": "journal-receipt",
            "terminal": "sealed-intent",
        }[str(effect)]
        if durability != expected_durability:
            raise ValueError("helper durability must match its effect")
        transition_parameter = helper.get("transition_parameter")
        if (
            not isinstance(transition_parameter, int)
            or transition_parameter < 0
            or transition_parameter > 0xFFFFFFFF
            or (transition == "none" and transition_parameter != 0xFFFFFFFF)
        ):
            raise ValueError("helper transition parameter is invalid")
        allowed_modes = _ribos_string_list(
            helper.get("allowed_modes"),
            "Ribos helper modes",
            set(MODE_VALUES),
        )
        allowed_phases = _ribos_string_list(
            helper.get("allowed_phases"),
            "Ribos helper phases",
            set(RIBOS_PHASES),
        )
        if allowed_modes != [mode] or allowed_phases != ["boot"]:
            raise ValueError("Ribos helper must be closed to the selected mode and boot phase")
        for field in (
            "maximum_input_bytes",
            "maximum_output_bytes",
            "maximum_operations",
            "maximum_polls",
            "maximum_duration_ns",
        ):
            if not isinstance(helper.get(field), int) or helper[field] <= 0:
                raise ValueError(f"Ribos helper {field} must be positive")
        normalized.append(dict(helper))
    if stable_ids != sorted(set(stable_ids)):
        raise ValueError("Ribos helper stable IDs must be unique and sorted")
    if limits["maximum_helper_calls"] < 1 or len(helpers) > 256:
        raise ValueError("Ribos helper table exceeds v1 bounds")
    result = dict(raw)
    result["capabilities"] = capabilities
    result["ribon_capabilities"] = ribon_capabilities
    result["helpers"] = normalized
    return result


def load_manifest(path: Path, selected_architecture: str | None) -> dict[str, object]:
    """Load and validate a product graph, including its exact frontend tuple."""

    source_manifest = path.read_bytes()
    manifest = json.loads(source_manifest.decode("utf-8"))
    if not isinstance(manifest, dict):
        raise ValueError("product manifest must be an object")
    if manifest.get("schema_version") != 1:
        raise ValueError("schema_version must be 1")
    _string(manifest, "product_id")
    _string(manifest, "target_id")
    product_kind = _string(manifest, "product_kind")
    if product_kind not in PRODUCT_KIND_VALUES:
        raise ValueError("unsupported product_kind")
    architecture = _string(manifest, "architecture")
    if architecture == "selected":
        if selected_architecture is None:
            raise ValueError("selected architecture requires --architecture")
        architecture = selected_architecture
    elif selected_architecture is not None and selected_architecture != architecture:
        raise ValueError("--architecture does not match the product manifest")
    if architecture not in ARCHITECTURE_MASKS:
        raise ValueError(f"unsupported architecture: {architecture}")
    manifest["resolved_architecture"] = architecture
    manifest["_source_manifest_digest"] = hashlib.sha256(source_manifest).digest()
    environment = manifest.get("environment")
    personality = manifest.get("firmware_personality")
    if product_kind == "firmware":
        if environment is not None:
            raise ValueError("firmware product must not select an environment")
        if not isinstance(personality, str) or personality not in PERSONALITY_MASKS:
            raise ValueError("firmware product requires one supported personality")
    else:
        if personality is not None:
            raise ValueError("consumer/library product must not select a personality")
        if not isinstance(environment, str) or environment not in ENVIRONMENT_MASKS:
            raise ValueError("consumer/library product requires one environment")
    if _string(manifest, "mode") not in MODE_VALUES:
        raise ValueError("unsupported mode")
    port = manifest.get("port")
    if port is not None and (not isinstance(port, str) or not port):
        raise ValueError("port must be a non-empty string when present")
    protocols = _sorted_strings(
        manifest,
        "boot_protocols",
        allow_empty=product_kind != "bootloader",
    )
    if product_kind != "bootloader" and protocols:
        raise ValueError("only bootloader products select boot protocols")
    _sorted_strings(manifest, "policies")
    image = manifest.get("image")
    if (
        not isinstance(image, dict)
        or set(image) != IMAGE_KEYS
        or any(not isinstance(image[key], str) or not image[key] for key in IMAGE_KEYS)
    ):
        raise ValueError("image must define format, recipe, and artifact")
    evidence = manifest.get("evidence")
    if (
        not isinstance(evidence, dict)
        or set(evidence) != EVIDENCE_KEYS
        or evidence.get("class") not in EVIDENCE_CLASSES
        or not isinstance(evidence.get("claim"), str)
        or not evidence["claim"]
    ):
        raise ValueError("evidence must define a supported class and bounded claim")
    payload = manifest.get("payload")
    if payload is not None:
        if (
            product_kind != "bootloader"
            or not isinstance(payload, dict)
            or set(payload) != PAYLOAD_KEYS
            or payload.get("class") != "external-kernel"
            or payload.get("format") not in (
                "elf64", "linux-aarch64-image", "linux-riscv64-image"
            )
            or payload.get("architecture") != architecture
            or not isinstance(payload.get("entry_abi"), str)
            or not payload["entry_abi"]
            or not isinstance(payload.get("load_base"), int)
            or payload["load_base"] <= 0
            or not isinstance(payload.get("load_size"), int)
            or payload["load_size"] <= 0
        ):
            raise ValueError("payload must define one typed external kernel contract")
        if payload["format"] == "linux-aarch64-image" and (
            architecture != "aarch64" or payload["entry_abi"] != "arm64-linux-fdt-v1"
        ):
            raise ValueError("Linux raw Image requires the AArch64 Linux FDT entry ABI")
        if payload["format"] == "linux-riscv64-image" and (
            architecture != "riscv64" or
            payload["entry_abi"] != "riscv64-linux-fdt-v1"
        ):
            raise ValueError(
                "Linux raw Image requires the RISC-V64 Linux FDT entry ABI"
            )
    boot_module_bundle = manifest.get("boot_module_bundle")
    if boot_module_bundle is not None:
        if (
            product_kind != "bootloader"
            or environment != "raw-fdt"
            or not isinstance(boot_module_bundle, dict)
            or set(boot_module_bundle) != BOOT_MODULE_BUNDLE_KEYS
            or boot_module_bundle.get("provider") !=
                "generated-component-bundle-v1"
            or boot_module_bundle.get("component_manifest_schema") !=
                "ribon-boot-module-components-v1"
            or boot_module_bundle.get("maximum_modules") != 8
        ):
            raise ValueError(
                "boot_module_bundle requires the exact raw-FDT generated provider"
            )
    update_storage = manifest.get("update_storage")
    if update_storage is not None:
        if (
            product_kind != "bootloader"
            or manifest["mode"] not in {"recovery", "provisioning"}
            or not isinstance(update_storage, dict)
            or set(update_storage) != UPDATE_STORAGE_KEYS
            or update_storage.get("schema") != "ribon-update-storage-binding-v1"
            or update_storage.get("provider_class") not in
                {"firmware", "native", "reference"}
            or not isinstance(update_storage.get("layout_id"), str)
            or not update_storage["layout_id"]
            or any(
                not isinstance(update_storage.get(key), str)
                or not update_storage[key]
                for key in (
                    "read_service_id",
                    "writer_service_id",
                    "metadata_service_id",
                    "flush_service_id",
                )
            )
            or len(set(
                str(update_storage[key])
                for key in (
                    "read_service_id",
                    "writer_service_id",
                    "metadata_service_id",
                    "flush_service_id",
                )
            )) != 4
            or not isinstance(update_storage.get("layout_digest_sha256"), str)
            or len(str(update_storage["layout_digest_sha256"])) != 64
            or any(
                character not in "0123456789abcdef"
                for character in str(update_storage["layout_digest_sha256"])
            )
            or set(str(update_storage["layout_digest_sha256"])) == {"0"}
            or not isinstance(
                update_storage.get("media_identity_digest_sha256"), str
            )
            or len(str(update_storage["media_identity_digest_sha256"])) != 64
            or any(
                character not in "0123456789abcdef"
                for character in str(update_storage["media_identity_digest_sha256"])
            )
            or set(str(update_storage["media_identity_digest_sha256"])) == {"0"}
        ):
            raise ValueError(
                "update_storage must define one recovery/provisioning bounded provider"
            )
    recovery_network = manifest.get("recovery_network")
    if recovery_network is not None:
        if (
            product_kind != "bootloader"
            or manifest["mode"] not in {"recovery", "provisioning"}
            or environment != "uefi"
            or not isinstance(recovery_network, dict)
            or set(recovery_network) != RECOVERY_NETWORK_KEYS
            or recovery_network.get("schema") !=
                "ribon-recovery-network-binding-v1"
            or recovery_network.get("transport") != "uefi-bounded-tftp"
            or not isinstance(recovery_network.get("service_id"), str)
            or not str(recovery_network["service_id"])
            or any(
                not isinstance(recovery_network.get(field), list)
                or len(recovery_network[field]) != 4
                for field in (
                    "server_ipv4", "station_ipv4", "subnet_mask_ipv4"
                )
            )
            or any(
                not isinstance(octet, int) or octet < 0 or octet > 255
                for field in (
                    "server_ipv4", "station_ipv4", "subnet_mask_ipv4"
                )
                for octet in recovery_network[field]
            )
            or recovery_network["server_ipv4"][0] == 0
            or recovery_network["server_ipv4"][0] >= 224
            or recovery_network["station_ipv4"][0] == 0
            or recovery_network["station_ipv4"][0] >= 224
            or recovery_network["subnet_mask_ipv4"][0] == 0
            or not isinstance(recovery_network.get("block_size"), int)
            or not 512 <= recovery_network["block_size"] <= 1468
            or not isinstance(recovery_network.get("retry_count"), int)
            or not 0 <= recovery_network["retry_count"] <= 3
            or not isinstance(recovery_network.get("absolute_deadline_ms"), int)
            or not recovery_network["retry_count"] <
                recovery_network["absolute_deadline_ms"] <= 30000
            or not isinstance(recovery_network.get("objects"), list)
            or len(recovery_network["objects"]) !=
                len(RECOVERY_NETWORK_OBJECT_KINDS)
        ):
            raise ValueError(
                "recovery_network must define one bounded UEFI TFTP provider"
            )
        normalized_objects = []
        for expected_kind, item in zip(
            RECOVERY_NETWORK_OBJECT_KINDS,
            recovery_network["objects"],
            strict=True,
        ):
            if (
                not isinstance(item, dict)
                or set(item) != RECOVERY_NETWORK_OBJECT_KEYS
                or item.get("kind") != expected_kind
                or not isinstance(item.get("path"), str)
                or not 1 <= len(item["path"].encode("ascii", errors="ignore")) <= 96
                or item["path"].startswith("/")
                or item["path"].endswith("/")
                or ".." in item["path"]
                or any(
                    character not in
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-/"
                    for character in item["path"]
                )
                or not isinstance(item.get("maximum_bytes"), int)
                or not 1 <= item["maximum_bytes"] <= 64 * 1024 * 1024
                or item["maximum_bytes"] >=
                    (65535 - 1) * recovery_network["block_size"]
            ):
                raise ValueError(
                    "recovery network objects must be canonical bounded role rows"
                )
            normalized_objects.append(dict(item))
        recovery_network = dict(recovery_network)
        recovery_network["objects"] = normalized_objects

    plugins = manifest.get("plugins")
    if not isinstance(plugins, list) or not plugins:
        raise ValueError("plugins must be a non-empty list")
    ids: list[str] = []
    symbols: set[str] = set()
    for item in plugins:
        if not isinstance(item, dict):
            raise ValueError("plugin entry must be an object")
        plugin_id = item.get("id")
        symbol = item.get("symbol")
        package = item.get("package")
        if (
            not isinstance(plugin_id, str)
            or not plugin_id
            or not isinstance(symbol, str)
            or not symbol.startswith("ribon_")
            or not isinstance(package, str)
            or not package
        ):
            raise ValueError("plugin id, package, and symbol must be stable strings")
        ids.append(plugin_id)
        if symbol in symbols:
            raise ValueError(f"duplicate plugin symbol: {symbol}")
        symbols.add(symbol)
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise ValueError("plugin IDs must be unique and sorted")
    required_prefixes = ["arch."]
    required_prefixes.append(
        "personality." if product_kind == "firmware" else "environment."
    )
    for prefix in required_prefixes:
        if sum(plugin_id.startswith(prefix) for plugin_id in ids) != 1:
            raise ValueError(f"product must select exactly one {prefix[:-1]} plugin")
    forbidden_prefix = (
        "environment." if product_kind == "firmware" else "personality."
    )
    if any(plugin_id.startswith(forbidden_prefix) for plugin_id in ids):
        raise ValueError(f"product must not select {forbidden_prefix[:-1]} plugin")
    if product_kind == "firmware":
        if f"personality.{personality}" not in ids:
            raise ValueError("personality field and plugin ID disagree")
    elif (
        product_kind == "bootloader" and
        ENVIRONMENT_PLUGIN_IDS[str(environment)] not in ids
    ):
        raise ValueError("environment field and plugin ID disagree")
    for protocol in protocols:
        if f"protocol.{protocol}" not in ids:
            raise ValueError(f"boot protocol has no selected plugin: {protocol}")

    services = _provider_entries(
        manifest, "services", SERVICE_KIND_VALUES, symbol_required=True
    )
    if product_kind == "bootloader" and not services:
        raise ValueError("bootloader product requires typed services")
    manifest["services"] = services
    manifest["service_selections"] = _provider_entries(
        manifest, "service_selections", SERVICE_KIND_VALUES, symbol_required=False
    )
    manifest["plugin_selections"] = _provider_entries(
        manifest, "plugin_selections", PLUGIN_KIND_VALUES, symbol_required=False
    )

    required = _capabilities(manifest, "required_capabilities")
    allowed = _capabilities(manifest, "allowed_capabilities")
    if not set(required).issubset(allowed):
        raise ValueError("required capabilities must be allowed")
    module_services = [
        service
        for service in services
        if service["kind"] == "boot-module-bundle"
    ]
    module_service_is_exact = module_services == [{
        "id": "service.product.boot-module-bundle",
        "kind": "boot-module-bundle",
        "symbol": "ribon_generated_boot_module_bundle_service_descriptor",
    }]
    bundle_is_selected = boot_module_bundle is not None
    if (
        (bundle_is_selected and not module_service_is_exact)
        or (not bundle_is_selected and module_services)
        or ("BOOT_MODULE_BUNDLE" in required) != bundle_is_selected
        or ("BOOT_MODULE_BUNDLE" in allowed) != bundle_is_selected
    ):
        raise ValueError(
            "boot_module_bundle, exact service, and capability authority must agree"
        )
    writer_capabilities = {
        "INACTIVE_SLOT_ERASE",
        "INACTIVE_SLOT_WRITE",
    }
    writer_services = [
        service for service in services
        if service["kind"] == "inactive-slot-storage"
    ]
    if update_storage is None:
        if writer_services or writer_capabilities.intersection(required) or \
                writer_capabilities.intersection(allowed):
            raise ValueError(
                "inactive slot writer authority requires update_storage binding"
            )
    else:
        assert isinstance(update_storage, dict)
        service_by_id = {service["id"]: service for service in services}
        expected_services = {
            str(update_storage["read_service_id"]): "boot-source",
            str(update_storage["writer_service_id"]): "inactive-slot-storage",
            str(update_storage["metadata_service_id"]): "persistent-metadata",
            str(update_storage["flush_service_id"]): "storage-flush",
        }
        if any(
            service_by_id.get(service_id, {}).get("kind") != kind
            for service_id, kind in expected_services.items()
        ):
            raise ValueError(
                "update_storage service IDs must resolve to exact typed roles"
            )
        required_update_capabilities = {
            "BOOT_SOURCE_READ",
            "INACTIVE_SLOT_ERASE",
            "INACTIVE_SLOT_WRITE",
            "PERSISTENT_METADATA",
            "STORAGE_FLUSH",
        }
        if not required_update_capabilities.issubset(required) or \
                not required_update_capabilities.issubset(allowed):
            raise ValueError(
                "update_storage requires complete read, writer, metadata, and flush authority"
            )
        manifest["update_storage"] = {
            key: update_storage[key] for key in sorted(UPDATE_STORAGE_KEYS)
        }
    network_services = [
        service for service in services
        if service["kind"] == "network-transport"
    ]
    if recovery_network is None:
        if network_services or "NETWORK_TRANSPORT" in required or \
                "NETWORK_TRANSPORT" in allowed:
            raise ValueError(
                "network transport authority requires recovery_network binding"
            )
    else:
        assert isinstance(recovery_network, dict)
        if network_services != [{
            "id": recovery_network["service_id"],
            "kind": "network-transport",
            "symbol": "ribon_uefi_bounded_tftp_network_service_descriptor",
        }]:
            raise ValueError(
                "recovery_network requires the exact generated UEFI TFTP service"
            )
        if "NETWORK_TRANSPORT" not in required or \
                "NETWORK_TRANSPORT" not in allowed:
            raise ValueError(
                "recovery_network requires network transport capability"
            )
        manifest["recovery_network"] = recovery_network
    limits = manifest.get("limits")
    if (
        not isinstance(limits, dict)
        or set(limits) != set(LIMIT_KEYS)
        or any(not isinstance(limits[key], int) or limits[key] <= 0 for key in LIMIT_KEYS)
    ):
        raise ValueError("limits must contain positive values for the complete ABI")
    if isinstance(recovery_network, dict) and (
        recovery_network["retry_count"] > limits["max_retries"]
        or max(
            int(item["maximum_bytes"])
            for item in recovery_network["objects"]
        ) > limits["max_input_bytes"]
        or recovery_network["absolute_deadline_ms"] >
            limits["operation_deadline_ms"]
    ):
        raise ValueError("recovery_network exceeds product resource limits")
    max_plugins = manifest.get("max_plugins")
    if not isinstance(max_plugins, int) or max_plugins < len(plugins) or max_plugins > 64:
        raise ValueError("max_plugins must bound the graph and the core ABI")
    manifest["ribos_policy"] = _ribos_policy(
        manifest,
        ids,
        services,
        allowed,
    )
    manifest["signature_provider"] = _signature_provider(manifest)
    manifest["key_policy"] = _key_policy(
        manifest,
        manifest["_source_manifest_digest"],
    )
    manifest["protected_state_provider"] = _protected_state_provider(manifest)
    security_selections = (
        manifest["signature_provider"],
        manifest["key_policy"],
        manifest["protected_state_provider"],
    )
    if any(value is None for value in security_selections) and any(
        value is not None for value in security_selections
    ):
        raise ValueError(
            "signature, key-policy, and protected-state selections must be complete"
        )
    if manifest["key_policy"] is not None:
        protected = manifest["protected_state_provider"]
        signature = manifest["signature_provider"]
        assert isinstance(protected, dict) and isinstance(signature, dict)
        policy_domains = sorted({
            domain
            for record in manifest["key_policy"]["keys"]
            for domain in record["rollback_domains"]
        })
        if protected["rollback_domains"] != policy_domains:
            raise ValueError("protected-state domains must exactly cover key-policy domains")
        if (signature["class"] == "fixture") != (protected["class"] == "fixture"):
            raise ValueError("fixture signature and protected-state providers cannot mix")
    ribos_policy = manifest.get("ribos_policy")
    if isinstance(ribos_policy, dict):
        authorization = ribos_policy["authorization"]
        assert isinstance(authorization, dict)
        if authorization["class"] == "signed-policy":
            if any(value is None for value in security_selections):
                raise ValueError("signed Ribos policy requires complete security selections")
            protected = manifest["protected_state_provider"]
            signature = manifest["signature_provider"]
            assert isinstance(protected, dict) and isinstance(signature, dict)
            if signature["class"] != "production":
                raise ValueError("signed Ribos policy requires a production signature provider")
            if authorization["rollback_domain"] not in protected["rollback_domains"]:
                raise ValueError("signed Ribos rollback domain is not product-authorized")
        elif any(value is not None for value in security_selections):
            raise ValueError("fixture Ribos authorization cannot select production trust state")
    return manifest


def _capability_expression(values: list[str]) -> str:
    return " |\n        ".join(CAPABILITIES[value] for value in values)


def _ribos_mask_expression(values: list[str], mapping: dict[str, int]) -> int:
    mask = 0
    for value in values:
        mask |= 1 << mapping[value]
    return mask


def _ribos_capability_value(values: list[str]) -> int:
    result = 0
    for value in values:
        result |= RIBOS_CAPABILITIES[value]
    return result


def _ribos_helper_digest(policy: dict[str, object]) -> bytes:
    helpers = policy["helpers"]
    assert isinstance(helpers, list)
    payload = bytearray(b"RIBOS-HELPER-EXECUTION-V1".ljust(32, b"\0"))
    payload.extend(struct.pack("<HHI", 1, 0, len(helpers)))
    for helper in helpers:
        assert isinstance(helper, dict)
        ribos_caps = helper["ribos_capabilities"]
        modes = helper["allowed_modes"]
        phases = helper["allowed_phases"]
        assert isinstance(ribos_caps, list)
        assert isinstance(modes, list)
        assert isinstance(phases, list)
        payload.extend(
            struct.pack(
                "<IIIIIIII",
                helper["stable_id"],
                0,
                _ribos_capability_value(ribos_caps),
                RIBOS_EFFECTS[str(helper["effect"])][1],
                1,
                RIBOS_DURABILITIES[str(helper["durability"])][1],
                RIBOS_TRANSITIONS[str(helper["handle_transition"])][1],
                helper["transition_parameter"],
            )
        )
        payload.extend(
            struct.pack(
                "<QQQQQQQ",
                _ribos_mask_expression(modes, {
                    "normal": 0,
                    "recovery": 1,
                    "provisioning": 2,
                    "diagnostic": 3,
                }),
                _ribos_mask_expression(phases, RIBOS_PHASES),
                helper["maximum_input_bytes"],
                helper["maximum_output_bytes"],
                helper["maximum_operations"],
                helper["maximum_polls"],
                helper["maximum_duration_ns"],
            )
        )
    return hashlib.sha256(payload).digest()


def _c_bytes(data: bytes) -> str:
    return ", ".join(f"0x{value:02x}u" for value in data)


def _render_key_policy(manifest: dict[str, object]) -> str:
    """Render one immutable bounded trust store or an explicit null getter."""

    policy = manifest.get("key_policy")
    if policy is None:
        return """
const struct RibonKeyPolicyStore *ribon_generated_key_policy_store(void) {
    return 0;
}
"""
    assert isinstance(policy, dict)
    records = manifest["_key_policy_records"]
    source_digest = manifest["_source_manifest_digest"]
    canonical_digest = manifest["_key_policy_digest"]
    assert isinstance(records, list)
    assert isinstance(source_digest, bytes)
    assert isinstance(canonical_digest, bytes)
    id_indices = {
        str(record["id"]): index for index, record in enumerate(records)
    }
    declarations: list[str] = [
        "static const uint8_t generated_key_policy_store_id[] = "
        f'"{policy["id"]}";'
    ]
    rows: list[str] = []
    for index, record in enumerate(records):
        assert isinstance(record, dict)
        domains = record["domain_digests"]
        assert isinstance(domains, list)
        declarations.append(
            f"static const uint8_t generated_key_policy_key_id_{index}[] = "
            f'"{record["id"]}";'
        )
        domain_rows = ",\n".join(
            "    { " + _c_bytes(digest) + " }"
            for digest in domains
            if isinstance(digest, bytes)
        )
        declarations.append(
            "static const uint8_t "
            f"generated_key_policy_domains_{index}[]"
            f"[RIBON_KEY_POLICY_DIGEST_BYTES] = {{\n{domain_rows}\n}};"
        )
        issuer = record["issuer"]
        issuer_value = "0"
        issuer_size = "0u"
        flags = "RIBON_KEY_POLICY_RECORD_ROOT"
        if issuer is not None:
            issuer_index = id_indices[str(issuer)]
            issuer_value = f"generated_key_policy_key_id_{issuer_index}"
            issuer_size = (
                f"sizeof(generated_key_policy_key_id_{issuer_index}) - 1u"
            )
            flags = "0u"
        public_key = bytes.fromhex(str(record["public_key_hex"]))
        key_identity = hashlib.sha256(public_key).digest()
        rows.append(
            """    {
        .size = sizeof(struct RibonKeyPolicyRecord),
        .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
        .flags = %(flags)s,
        .algorithm = RIBON_SIGNATURE_ALGORITHM_ED25519,
        .lifecycle = %(lifecycle)s,
        .mode_mask = %(mode_mask)su,
        .usage_mask = UINT64_C(%(usage_mask)s),
        .key_id = generated_key_policy_key_id_%(index)s,
        .key_id_size = sizeof(generated_key_policy_key_id_%(index)s) - 1u,
        .public_key = { %(public_key)s },
        .key_identity_digest = { %(key_identity)s },
        .product_digest = { %(product_digest)s },
        .rollback_domain_digests = generated_key_policy_domains_%(index)s,
        .rollback_domain_count = %(domain_count)su,
        .delegation_depth = %(depth)su,
        .issuer_key_id = %(issuer)s,
        .issuer_key_id_size = %(issuer_size)s,
        .minimum_sequence = UINT64_C(%(minimum)s),
        .maximum_sequence = UINT64_C(%(maximum)s),
    },""" % {
                "flags": flags,
                "lifecycle": KEY_POLICY_LIFECYCLES[str(record["status"])],
                "mode_mask": record["mode_mask"],
                "usage_mask": record["usage_mask"],
                "index": index,
                "public_key": _c_bytes(public_key),
                "key_identity": _c_bytes(key_identity),
                "product_digest": _c_bytes(source_digest),
                "domain_count": len(domains),
                "depth": record["delegation_depth"],
                "issuer": issuer_value,
                "issuer_size": issuer_size,
                "minimum": record["minimum_sequence"],
                "maximum": record["maximum_sequence"],
            }
        )
    return f"""
{chr(10).join(declarations)}

static const struct RibonKeyPolicyRecord generated_key_policy_records[] = {{
{chr(10).join(rows)}
}};

static const struct RibonKeyPolicyStore generated_key_policy_store = {{
    .magic = RIBON_KEY_POLICY_STORE_MAGIC,
    .size = sizeof(generated_key_policy_store),
    .abi_version = RIBON_KEY_POLICY_ABI_VERSION,
    .id = generated_key_policy_store_id,
    .id_size = sizeof(generated_key_policy_store_id) - 1u,
    .generation = UINT64_C({policy['generation']}),
    .records = generated_key_policy_records,
    .record_count = (uint32_t)(
        sizeof(generated_key_policy_records) /
        sizeof(generated_key_policy_records[0])),
    .canonical_digest = {{ {_c_bytes(canonical_digest)} }},
}};

const struct RibonKeyPolicyStore *ribon_generated_key_policy_store(void) {{
    return &generated_key_policy_store;
}}
"""


def _render_protected_state(manifest: dict[str, object]) -> str:
    """Render one provider/domain binding or an explicit null getter."""

    provider = manifest.get("protected_state_provider")
    if provider is None:
        return """
const struct RibonProtectedStateProductBinding *
ribon_generated_protected_state_binding(void) {
    return 0;
}
"""
    assert isinstance(provider, dict)
    digests = manifest["_protected_state_domain_digests"]
    assert isinstance(digests, list)
    rows = ",\n".join(
        "    { " + _c_bytes(digest) + " }"
        for digest in digests
        if isinstance(digest, bytes)
    )
    symbol = str(provider["symbol"])
    provider_class = PROTECTED_STATE_PROVIDER_CLASSES[str(provider["class"])]
    return f"""
extern const struct RibonProtectedStateProvider {symbol};

static const uint8_t generated_protected_state_domains[]
    [RIBON_PROTECTED_STATE_DIGEST_BYTES] = {{
{rows}
}};

static const struct RibonProtectedStateProductBinding
generated_protected_state_binding = {{
    .size = sizeof(generated_protected_state_binding),
    .abi_version = RIBON_PROTECTED_STATE_ABI_VERSION,
    .provider_class = {provider_class},
    .provider = &{symbol},
    .domain_digests = generated_protected_state_domains,
    .domain_count = (uint32_t)(
        sizeof(generated_protected_state_domains) /
        sizeof(generated_protected_state_domains[0])),
}};

const struct RibonProtectedStateProductBinding *
ribon_generated_protected_state_binding(void) {{
    return &generated_protected_state_binding;
}}
"""


def _render_update_storage(manifest: dict[str, object]) -> str:
    """Render the exact product-selected update media binding or null getter."""

    binding = manifest.get("update_storage")
    if binding is None:
        return """
const struct RibonUpdateStorageProductBinding *
ribon_generated_update_storage_binding(void) {
    return 0;
}
"""
    assert isinstance(binding, dict)
    provider_class = UPDATE_STORAGE_PROVIDER_CLASSES[str(binding["provider_class"])]
    return f"""
static const struct RibonUpdateStorageProductBinding
generated_update_storage_binding = {{
    .size = sizeof(generated_update_storage_binding),
    .abi_version = RIBON_UPDATE_STORAGE_ABI_VERSION,
    .provider_class = {provider_class},
    .layout_id = "{binding['layout_id']}",
    .layout_digest = {{
        {_c_bytes(bytes.fromhex(str(binding['layout_digest_sha256'])))}
    }},
    .media_identity_digest = {{
        {_c_bytes(bytes.fromhex(str(binding['media_identity_digest_sha256'])))}
    }},
    .read_service_id = "{binding['read_service_id']}",
    .writer_service_id = "{binding['writer_service_id']}",
    .metadata_service_id = "{binding['metadata_service_id']}",
    .flush_service_id = "{binding['flush_service_id']}",
}};

const struct RibonUpdateStorageProductBinding *
ribon_generated_update_storage_binding(void) {{
    return &generated_update_storage_binding;
}}
"""


def _render_recovery_network(manifest: dict[str, object]) -> str:
    """Render one immutable recovery endpoint/budget binding or null getter."""

    binding = manifest.get("recovery_network")
    if binding is None:
        return """
const struct RibonRecoveryNetworkProductBinding *
ribon_generated_recovery_network_binding(void) {
    return 0;
}
"""
    assert isinstance(binding, dict)
    objects = binding["objects"]
    assert isinstance(objects, list)
    kinds = {
        "manifest": "RIBON_RECOVERY_NETWORK_OBJECT_MANIFEST",
        "signature-envelope":
            "RIBON_RECOVERY_NETWORK_OBJECT_SIGNATURE_ENVELOPE",
        "bundle": "RIBON_RECOVERY_NETWORK_OBJECT_BUNDLE",
    }
    rows = "\n".join(
        """        {
            .kind = %(kind)s,
            .path = \"%(path)s\",
            .maximum_bytes = UINT64_C(%(maximum)s),
        },""" % {
            "kind": kinds[str(item["kind"])],
            "path": item["path"],
            "maximum": item["maximum_bytes"],
        }
        for item in objects
        if isinstance(item, dict)
    )
    server = binding["server_ipv4"]
    station = binding["station_ipv4"]
    subnet = binding["subnet_mask_ipv4"]
    assert isinstance(server, list) and isinstance(station, list) and isinstance(subnet, list)
    return f"""
static const struct RibonRecoveryNetworkProductBinding
generated_recovery_network_binding = {{
    .size = sizeof(generated_recovery_network_binding),
    .abi_version = RIBON_RECOVERY_NETWORK_ABI_VERSION,
    .transport_class = RIBON_RECOVERY_NETWORK_TRANSPORT_UEFI_BOUNDED_TFTP,
    .service_id = "{binding['service_id']}",
    .server_ipv4 = {{ {', '.join(str(value) + 'u' for value in server)} }},
    .station_ipv4 = {{ {', '.join(str(value) + 'u' for value in station)} }},
    .subnet_mask_ipv4 = {{ {', '.join(str(value) + 'u' for value in subnet)} }},
    .block_size = {binding['block_size']}u,
    .retry_count = {binding['retry_count']}u,
    .absolute_deadline_ms = {binding['absolute_deadline_ms']}u,
    .objects = {{
{rows}
    }},
}};

const struct RibonRecoveryNetworkProductBinding *
ribon_generated_recovery_network_binding(void) {{
    return &generated_recovery_network_binding;
}}
"""
def _render_ribos_policy(manifest: dict[str, object]) -> str:
    policy = manifest.get("ribos_policy")
    if policy is None:
        return ""
    assert isinstance(policy, dict)
    helpers = policy["helpers"]
    limits = policy["limits"]
    assert isinstance(helpers, list)
    assert isinstance(limits, dict)
    callback_symbols = sorted(
        {str(helper["callback_symbol"]) for helper in helpers}
    )
    callbacks = "\n".join(
        "extern uint32_t " + symbol +
        "(void *, const struct RibonServiceDescriptor *, "
        "struct RibosVmHelperCall *);"
        for symbol in callback_symbols
    )
    authorization = policy["authorization"]
    assert isinstance(authorization, dict)
    authorization_class = str(authorization["class"])
    authorizer = authorization.get("callback_symbol")
    recovery = str(policy["factory_recovery_symbol"])
    action_validator = str(policy["validate_boot_action_symbol"])
    schema = str(policy["schema_symbol"])
    binding_rows: list[str] = []
    route_rows: list[str] = []
    for helper in helpers:
        assert isinstance(helper, dict)
        ribos_caps = helper["ribos_capabilities"]
        modes = helper["allowed_modes"]
        phases = helper["allowed_phases"]
        ribon_caps = helper["ribon_capabilities"]
        assert isinstance(ribos_caps, list)
        assert isinstance(modes, list)
        assert isinstance(phases, list)
        assert isinstance(ribon_caps, list)
        binding_rows.append(
            """    {
        .execution = {
            .size = sizeof(RibosVmHelperExecutionDescriptor),
            .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
            .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
            .stable_id = %(stable_id)su,
            .required_capabilities = %(ribos_caps)su,
            .effect = %(effect)s,
            .execution_mode = RIBOS_VM_HELPER_EXECUTION_SYNCHRONOUS,
            .durability = %(durability)s,
            .handle_transition = %(transition)s,
            .transition_parameter = %(transition_parameter)su,
            .allowed_mode_mask = UINT64_C(%(mode_mask)s),
            .allowed_phase_mask = UINT64_C(%(phase_mask)s),
            .maximum_input_bytes = UINT64_C(%(maximum_input_bytes)s),
            .maximum_output_bytes = UINT64_C(%(maximum_output_bytes)s),
            .maximum_operations = UINT64_C(%(maximum_operations)s),
            .maximum_polls = UINT64_C(%(maximum_polls)s),
            .maximum_duration_ns = UINT64_C(%(maximum_duration_ns)s),
        },
        .invoke = ribon_ribos_policy_helper_dispatch,
    },""" % {
                "stable_id": helper["stable_id"],
                "ribos_caps": _ribos_capability_value(ribos_caps),
                "effect": RIBOS_EFFECTS[str(helper["effect"])][0],
                "durability":
                    RIBOS_DURABILITIES[str(helper["durability"])][0],
                "transition":
                    RIBOS_TRANSITIONS[str(helper["handle_transition"])][0],
                "transition_parameter": helper["transition_parameter"],
                "mode_mask": _ribos_mask_expression(
                    modes,
                    {
                        "normal": 0,
                        "recovery": 1,
                        "provisioning": 2,
                        "diagnostic": 3,
                    },
                ),
                "phase_mask": _ribos_mask_expression(phases, RIBOS_PHASES),
                "maximum_input_bytes": helper["maximum_input_bytes"],
                "maximum_output_bytes": helper["maximum_output_bytes"],
                "maximum_operations": helper["maximum_operations"],
                "maximum_polls": helper["maximum_polls"],
                "maximum_duration_ns": helper["maximum_duration_ns"],
            }
        )
        service_kind = (
            "RIBON_RIBOS_NO_SERVICE_KIND"
            if helper["service_kind"] is None
            else SERVICE_KIND_VALUES[str(helper["service_kind"])]
        )
        service_id = (
            "0"
            if helper["service_id"] is None
            else f"\"{helper['service_id']}\""
        )
        ribon_capability = (
            "0u"
            if not ribon_caps
            else _capability_expression(ribon_caps)
        )
        route_rows.append(
            """    {
        .stable_id = %(stable_id)su,
        .service_kind = %(service_kind)s,
        .service_id = %(service_id)s,
        .required_ribon_capabilities = %(ribon_caps)s,
        .invoke = %(callback)s,
    },""" % {
                "stable_id": helper["stable_id"],
                "service_kind": service_kind,
                "service_id": service_id,
                "ribon_caps": ribon_capability,
                "callback": helper["callback_symbol"],
            }
        )
    helper_digest = _ribos_helper_digest(policy)
    mode = str(manifest["mode"])
    watchdog_id = policy["watchdog_service_id"]
    authorizer_extern = ""
    signed_binding = ""
    signed_pointer = "0"
    fixture_pointer = "0"
    if authorization_class == "fixture-callback":
        assert isinstance(authorizer, str)
        authorizer_extern = f"""extern uint32_t {authorizer}(
    void *, const struct RibosArtifactAuthorizationRequest *,
    struct RibosArtifactAuthorizationReceipt *);"""
        authorization_value = "RIBON_RIBOS_AUTHORIZATION_FIXTURE_CALLBACK"
        fixture_pointer = authorizer
    else:
        rollback_domain = str(authorization["rollback_domain"])
        source_digest = manifest["_source_manifest_digest"]
        signature = manifest["signature_provider"]
        assert isinstance(source_digest, bytes)
        assert isinstance(signature, dict)
        signed_binding = f"""
static const struct RibonRibosSignedPolicyBinding generated_ribos_signed_policy = {{
    .size = sizeof(generated_ribos_signed_policy),
    .abi_version = RIBON_RIBOS_POLICY_ABI_VERSION,
    .trust_mode = {KEY_POLICY_MODES[mode]}u,
    .key_usage = {KEY_POLICY_USAGES[f'policy-{mode}']}u,
    .product_digest = {{ {_c_bytes(source_digest)} }},
    .rollback_domain_digest = {{
        {_c_bytes(hashlib.sha256(rollback_domain.encode('utf-8')).digest())}
    }},
    .policy_identity_digest = {{
        {_c_bytes(hashlib.sha256(str(policy['policy_id']).encode('utf-8')).digest())}
    }},
    .signature_provider = &{signature['symbol']},
    .key_policy = &generated_key_policy_store,
    .protected_state = &generated_protected_state_binding,
}};
"""
        authorization_value = "RIBON_RIBOS_AUTHORIZATION_SIGNED_POLICY"
        signed_pointer = "&generated_ribos_signed_policy"
    return f"""
extern const struct RibosProductSchema *{schema}(void);
{authorizer_extern}
extern void {recovery}(
    void *, const struct RibonRibosFailureReceipt *);
extern int {action_validator}(
    void *, const struct RibosVmBootAction *,
    const struct RibonBootTransaction *);
{callbacks}
{signed_binding}

static const RibosVmHelperBinding generated_ribos_helper_bindings[] = {{
{chr(10).join(binding_rows)}
}};

static const RibosVmHelperContract generated_ribos_helper_contract = {{
    .size = sizeof(generated_ribos_helper_contract),
    .contract_major = RIBOS_VM_HELPER_EXECUTION_V1_MAJOR,
    .contract_minor = RIBOS_VM_HELPER_EXECUTION_V1_MINOR,
    .binding_count = (uint32_t)(
        sizeof(generated_ribos_helper_bindings) /
        sizeof(generated_ribos_helper_bindings[0])),
    .bindings = generated_ribos_helper_bindings,
    .digest = {{ {_c_bytes(helper_digest)} }},
}};

static const struct RibonRibosHelperRoute generated_ribos_routes[] = {{
{chr(10).join(route_rows)}
}};

static const RibosVmLimits generated_ribos_limits = {{
    .size = sizeof(generated_ribos_limits),
    .runtime_abi_major = RIBOS_VM_RUNTIME_ABI_V1_MAJOR,
    .runtime_abi_minor = RIBOS_VM_RUNTIME_ABI_V1_MINOR,
    .maximum_instructions = UINT64_C({limits['maximum_instructions']}),
    .maximum_helper_calls = UINT64_C({limits['maximum_helper_calls']}),
    .maximum_stack_bytes = UINT64_C({limits['maximum_stack_bytes']}),
    .maximum_arena_bytes = UINT64_C({limits['maximum_arena_bytes']}),
    .maximum_input_bytes = UINT64_C({limits['maximum_input_bytes']}),
    .maximum_output_bytes = UINT64_C({limits['maximum_output_bytes']}),
    .maximum_operations = UINT64_C({limits['maximum_operations']}),
    .maximum_polls = UINT64_C({limits['maximum_polls']}),
    .maximum_execution_duration_ns =
        UINT64_C({limits['maximum_execution_duration_ns']}),
    .maximum_helper_duration_ns =
        UINT64_C({limits['maximum_helper_duration_ns']}),
    .maximum_call_depth = {limits['maximum_call_depth']}u,
    .maximum_handles = {limits['maximum_handles']}u,
    .maximum_trace_records = {limits['maximum_trace_records']}u,
}};

static const struct RibonRibosProductBinding generated_ribos_binding = {{
    .size = sizeof(generated_ribos_binding),
    .abi_version = RIBON_RIBOS_POLICY_ABI_VERSION,
    .product_id = "{manifest['product_id']}",
    .policy_id = "{policy['policy_id']}",
    .schema = {schema},
    .helper_contract = &generated_ribos_helper_contract,
    .routes = generated_ribos_routes,
    .route_count = (uint32_t)(
        sizeof(generated_ribos_routes) / sizeof(generated_ribos_routes[0])),
    .selected_phase = RIBON_PLUGIN_PHASE_BOOT,
    .mode_mask = RIBON_MODE_MASK({MODE_VALUES[mode]}),
    .granted_ribos_capabilities =
        {_ribos_capability_value(policy['capabilities'])}u,
    .watchdog_required = {1 if policy['watchdog_required'] else 0}u,
    .required_ribon_capabilities =
        {_capability_expression(policy['ribon_capabilities'])},
    .arena_budget = UINT64_C({policy['arena_budget']}),
    .timer_service_id = "{policy['timer_service_id']}",
    .watchdog_service_id =
        {f'"{watchdog_id}"' if watchdog_id is not None else "0"},
    .limits = &generated_ribos_limits,
    .authorization_class = {authorization_value},
    .signed_policy = {signed_pointer},
    .fixture_authorize = {fixture_pointer},
    .factory_recovery = {recovery},
    .validate_boot_action = {action_validator},
}};

const struct RibonRibosProductBinding *
ribon_generated_ribos_policy_binding(void)
{{
    return &generated_ribos_binding;
}}
"""


def render(manifest: dict[str, object]) -> str:
    """Render a C registry without constructors, weak discovery, or policy probes."""

    plugins = manifest["plugins"]
    limits = manifest["limits"]
    assert isinstance(plugins, list)
    assert isinstance(limits, dict)
    externs = "\n".join(
        f"extern const struct RibonPluginDescriptor {item['symbol']};"
        for item in plugins
    )
    pointers = "\n".join(f"    &{item['symbol']}," for item in plugins)
    services = manifest["services"]
    service_selections = manifest["service_selections"]
    plugin_selections = manifest["plugin_selections"]
    assert isinstance(services, list)
    assert isinstance(service_selections, list)
    assert isinstance(plugin_selections, list)
    source_manifest_digest = manifest["_source_manifest_digest"]
    signature_provider = manifest.get("signature_provider")
    assert isinstance(source_manifest_digest, bytes)
    assert signature_provider is None or isinstance(signature_provider, dict)
    service_externs = "\n".join(
        f"extern const struct RibonServiceDescriptor {item['symbol']};"
        for item in services
    )
    service_pointers = "\n".join(f"    &{item['symbol']}," for item in services)
    service_selection_entries = "\n".join(
        "    { .kind = " + SERVICE_KIND_VALUES[item["kind"]] +
        ", .id = \"" + item["id"] + "\" },"
        for item in service_selections
    )
    plugin_selection_entries = "\n".join(
        "    { .kind = " + PLUGIN_KIND_VALUES[item["kind"]] +
        ", .id = \"" + item["id"] + "\" },"
        for item in plugin_selections
    )
    architecture = str(manifest["resolved_architecture"])
    product_kind = str(manifest["product_kind"])
    environment = manifest.get("environment")
    personality = manifest.get("firmware_personality")
    mode = str(manifest["mode"])
    environment_mask = (
        ENVIRONMENT_MASKS[str(environment)] if environment is not None else "0u"
    )
    personality_mask = (
        PERSONALITY_MASKS[str(personality)] if personality is not None else "0u"
    )
    required = _capability_expression(manifest["required_capabilities"])  # type: ignore[arg-type]
    allowed = _capability_expression(manifest["allowed_capabilities"])  # type: ignore[arg-type]
    ribos_policy = _render_ribos_policy(manifest)
    key_policy = _render_key_policy(manifest)
    protected_state = _render_protected_state(manifest)
    update_storage_binding = _render_update_storage(manifest)
    recovery_network_binding = _render_recovery_network(manifest)
    signature_extern = (
        "extern const struct RibonSignatureProvider " +
        str(signature_provider["symbol"]) + ";"
        if signature_provider is not None else ""
    )
    signature_value = (
        "&" + str(signature_provider["symbol"])
        if signature_provider is not None else "0"
    )
    ribos_includes = """#include <Ribon/policy/ribos.h>
#include <ribos/schema/schema.h>
#include <ribos/vm/prepared.h>
#include <ribos/vm/runtime.h>
""" if manifest.get("ribos_policy") is not None else ""
    return f"""/* Generated by tools/generate_plugin_registry.py; do not edit. */
#include <Ribon/plugin/registry.h>
#include <Ribon/security/key_policy.h>
#include <Ribon/security/protected_state.h>
#include <Ribon/security/signature.h>
#include <Ribon/network/recovery.h>
#include <Ribon/update/storage.h>
{ribos_includes}

{externs}
{service_externs}
{signature_extern}

static const uint8_t generated_product_source_digest[32] = {{
    {_c_bytes(source_manifest_digest)}
}};

{key_policy}
{protected_state}
{update_storage_binding}
{recovery_network_binding}

static const struct RibonPluginDescriptor *const generated_plugins[] = {{
{pointers}
}};

static const struct RibonPluginRegistry generated_registry = {{
    .size = sizeof(generated_registry),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .plugins = generated_plugins,
    .plugin_count = (uint32_t)(sizeof(generated_plugins) / sizeof(generated_plugins[0])),
}};

{("static const struct RibonServiceDescriptor *const generated_services[] = {\n" + service_pointers + "\n};") if services else ""}

{("static const struct RibonServiceSelection generated_service_selections[] = {\n" + service_selection_entries + "\n};") if service_selections else ""}

{("static const struct RibonPluginSelection generated_plugin_selections[] = {\n" + plugin_selection_entries + "\n};") if plugin_selections else ""}

static const struct RibonServiceDirectory generated_service_directory = {{
    .size = sizeof(generated_service_directory),
    .abi_version = RIBON_SERVICE_DIRECTORY_ABI_VERSION,
    .services = {"generated_services" if services else "0"},
    .service_count = {len(services)}u,
}};

static const struct RibonProductDescriptor generated_product = {{
    .magic = RIBON_PRODUCT_DESCRIPTOR_MAGIC,
    .size = sizeof(generated_product),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .id = "{manifest['product_id']}",
    .kind = {PRODUCT_KIND_VALUES[product_kind]},
    .architecture_mask = {ARCHITECTURE_MASKS[architecture]},
    .environment_mask = {environment_mask},
    .personality_mask = {personality_mask},
    .mode_mask = RIBON_MODE_MASK({MODE_VALUES[mode]}),
    .max_plugins = {manifest['max_plugins']}u,
    .required_capabilities =
        {required},
    .allowed_capabilities =
        {allowed},
    .service_selections = {"generated_service_selections" if service_selections else "0"},
    .service_selection_count = {len(service_selections)}u,
    .plugin_selections = {"generated_plugin_selections" if plugin_selections else "0"},
    .plugin_selection_count = {len(plugin_selections)}u,
    .limits = {{
        .max_memory_regions = {limits['max_memory_regions']}u,
        .max_load_segments = {limits['max_load_segments']}u,
        .max_components = {limits['max_components']}u,
        .max_retries = {limits['max_retries']}u,
        .max_input_bytes = {limits['max_input_bytes']}ull,
        .max_handoff_bytes = {limits['max_handoff_bytes']}ull,
        .arena_bytes = {limits['arena_bytes']}ull,
        .operation_deadline_ms = {limits['operation_deadline_ms']}u,
    }},
}};

const struct RibonPluginRegistry *ribon_generated_plugin_registry(void) {{
    return &generated_registry;
}}

const struct RibonProductDescriptor *ribon_generated_product_descriptor(void) {{
    return &generated_product;
}}

const uint8_t *ribon_generated_product_source_digest(void) {{
    return generated_product_source_digest;
}}

const struct RibonSignatureProvider *ribon_generated_signature_provider(void) {{
    return {signature_value};
}}

const struct RibonServiceDirectory *ribon_generated_service_directory(void) {{
    return &generated_service_directory;
}}
{ribos_policy}
"""


def main() -> int:
    """Generate the selected product source and optional object-graph report."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--architecture", choices=sorted(ARCHITECTURE_MASKS))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    manifest = load_manifest(args.manifest, args.architecture)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(manifest), encoding="utf-8")
    if args.report is not None:
        report = {
            "product_id": manifest["product_id"],
            "product_kind": manifest["product_kind"],
            "target_id": manifest["target_id"],
            "architecture": manifest["resolved_architecture"],
            "environment": manifest.get("environment"),
            "firmware_personality": manifest.get("firmware_personality"),
            "port": manifest.get("port"),
            "services": manifest["services"],
            "service_selections": manifest["service_selections"],
            "plugin_selections": manifest["plugin_selections"],
            "mode": manifest["mode"],
            "required_capabilities": manifest["required_capabilities"],
            "allowed_capabilities": manifest["allowed_capabilities"],
            "plugins": [item["id"] for item in manifest["plugins"]],
            "packages": [item["package"] for item in manifest["plugins"]],
            "image": manifest["image"],
            "evidence": manifest["evidence"],
            "payload": manifest.get("payload"),
            "boot_module_bundle": manifest.get("boot_module_bundle"),
            "update_storage": manifest.get("update_storage"),
            "recovery_network": manifest.get("recovery_network"),
            "signature_provider": manifest.get("signature_provider"),
            "key_policy": manifest.get("key_policy"),
            "key_policy_digest_sha256": (
                manifest["_key_policy_digest"].hex()
                if manifest.get("key_policy") is not None else None
            ),
            "protected_state_provider": manifest.get("protected_state_provider"),
            "protected_state_domain_digests_sha256": (
                [digest.hex() for digest in manifest["_protected_state_domain_digests"]]
                if manifest.get("protected_state_provider") is not None else None
            ),
            "ribos_policy": manifest.get("ribos_policy"),
            "source_manifest": args.manifest.name,
            "source_manifest_sha256": hashlib.sha256(
                args.manifest.read_bytes()
            ).hexdigest(),
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
