#!/usr/bin/env python3
"""Generate a deterministic Ribon product registry from a source manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


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
    "platform": "RIBON_PLUGIN_KIND_PLATFORM",
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
        "RIBON_CAP_PLATFORM_FACTS",
        "RIBON_CAP_FIRMWARE_PERSONALITY",
        "RIBON_CAP_FIRMWARE_SERVICE_DIRECTORY",
        "RIBON_CAP_SDK_CONTRACT",
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


def load_manifest(path: Path, selected_architecture: str | None) -> dict[str, object]:
    """Load and validate a product graph, including its exact frontend tuple."""

    manifest = json.loads(path.read_text(encoding="utf-8"))
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
    platform = _string(manifest, "platform")
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
    required_prefixes = ["arch.", "platform."]
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
    if f"platform.{platform}" not in ids:
        raise ValueError("platform field and plugin ID disagree")
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
    return manifest


def _capability_expression(values: list[str]) -> str:
    return " |\n        ".join(CAPABILITIES[value] for value in values)


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
    return f"""/* Generated by tools/generate_plugin_registry.py; do not edit. */
#include <Ribon/plugin/registry.h>

{externs}
{service_externs}

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

const struct RibonServiceDirectory *ribon_generated_service_directory(void) {{
    return &generated_service_directory;
}}
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
            "platform": manifest["platform"],
            "services": manifest["services"],
            "service_selections": manifest["service_selections"],
            "plugin_selections": manifest["plugin_selections"],
            "mode": manifest["mode"],
            "plugins": [item["id"] for item in manifest["plugins"]],
            "packages": [item["package"] for item in manifest["plugins"]],
            "image": manifest["image"],
            "evidence": manifest["evidence"],
            "payload": manifest.get("payload"),
            "source_manifest": str(args.manifest),
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
