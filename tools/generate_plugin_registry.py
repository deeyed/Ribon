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
}
PERSONALITY_MASKS = {
    "uefi-compatible": "RIBON_PERSONALITY_MASK_UEFI_COMPATIBLE",
    "bios-compatible": "RIBON_PERSONALITY_MASK_BIOS_COMPATIBLE",
}
EVIDENCE_CLASSES = {
    "unit",
    "compile-only",
    "qemu-smoke",
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
SIGNATURE_PROVIDER_KEYS = {"algorithm", "class", "id", "symbol"}
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
    "authorize_symbol",
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
        "authorize_symbol",
        "factory_recovery_symbol",
        "validate_boot_action_symbol",
        "timer_service_id",
    )
    for field in strings:
        if not isinstance(raw.get(field), str) or not raw[field]:
            raise ValueError(f"ribos_policy.{field} must be a stable string")
    for field in (
        "authorize_symbol",
        "factory_recovery_symbol",
        "validate_boot_action_symbol",
    ):
        if not str(raw[field]).startswith("ribon_"):
            raise ValueError(f"ribos_policy.{field} must be a Ribon symbol")
    if not str(raw["schema_symbol"]).startswith("ribos_"):
        raise ValueError("ribos_policy.schema_symbol must be a Ribos schema provider")
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
            or payload.get("format") != "elf64"
            or payload.get("architecture") != architecture
            or not isinstance(payload.get("entry_abi"), str)
            or not payload["entry_abi"]
            or not isinstance(payload.get("load_base"), int)
            or payload["load_base"] <= 0
            or not isinstance(payload.get("load_size"), int)
            or payload["load_size"] <= 0
        ):
            raise ValueError("payload must define one typed external kernel contract")
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
    limits = manifest.get("limits")
    if (
        not isinstance(limits, dict)
        or set(limits) != set(LIMIT_KEYS)
        or any(not isinstance(limits[key], int) or limits[key] <= 0 for key in LIMIT_KEYS)
    ):
        raise ValueError("limits must contain positive values for the complete ABI")
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
    authorizer = str(policy["authorize_symbol"])
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
    return f"""
extern const struct RibosProductSchema *{schema}(void);
extern uint32_t {authorizer}(
    void *, const struct RibosArtifactAuthorizationRequest *,
    struct RibosArtifactAuthorizationReceipt *);
extern void {recovery}(
    void *, const struct RibonRibosFailureReceipt *);
extern int {action_validator}(
    void *, const struct RibosVmBootAction *,
    const struct RibonBootTransaction *);
{callbacks}

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
    .authorize = {authorizer},
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
#include <Ribon/security/signature.h>
{ribos_includes}

{externs}
{service_externs}
{signature_extern}

static const uint8_t generated_product_source_digest[32] = {{
    {_c_bytes(source_manifest_digest)}
}};

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
            "plugins": [item["id"] for item in manifest["plugins"]],
            "packages": [item["package"] for item in manifest["plugins"]],
            "image": manifest["image"],
            "evidence": manifest["evidence"],
            "payload": manifest.get("payload"),
            "boot_module_bundle": manifest.get("boot_module_bundle"),
            "signature_provider": manifest.get("signature_provider"),
            "ribos_policy": manifest.get("ribos_policy"),
            "source_manifest": str(args.manifest),
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
