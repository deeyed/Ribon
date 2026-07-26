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
MODE_VALUES = {
    "normal": "RIBON_MODE_NORMAL",
    "recovery": "RIBON_MODE_RECOVERY",
    "provisioning": "RIBON_MODE_PROVISIONING",
    "diagnostic": "RIBON_MODE_DIAGNOSTIC",
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
        "RIBON_CAP_ARCHITECTURE",
        "RIBON_CAP_IMAGE_ELF64",
        "RIBON_CAP_BOOT_PROTOCOL",
        "RIBON_CAP_HANDOFF",
        "RIBON_CAP_ENTRY_CONTRACT",
        "RIBON_CAP_BOOT_CONFIRMATION",
        "RIBON_CAP_IMAGE_PE_COFF",
        "RIBON_CAP_PLATFORM_FACTS",
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


def load_manifest(path: Path, selected_architecture: str | None) -> dict[str, object]:
    """Load and validate a product graph, including its exact frontend tuple."""

    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise ValueError("product manifest must be an object")
    _string(manifest, "product_id")
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
    if _string(manifest, "environment") not in ENVIRONMENT_MASKS:
        raise ValueError("unsupported environment")
    if _string(manifest, "mode") not in MODE_VALUES:
        raise ValueError("unsupported mode")

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
        if (
            not isinstance(plugin_id, str)
            or not plugin_id
            or not isinstance(symbol, str)
            or not symbol.startswith("ribon_")
        ):
            raise ValueError("plugin id and symbol must be stable strings")
        ids.append(plugin_id)
        if symbol in symbols:
            raise ValueError(f"duplicate plugin symbol: {symbol}")
        symbols.add(symbol)
    if ids != sorted(ids) or len(ids) != len(set(ids)):
        raise ValueError("plugin IDs must be unique and sorted")
    for prefix in ("arch.", "environment.", "platform."):
        if sum(plugin_id.startswith(prefix) for plugin_id in ids) != 1:
            raise ValueError(f"product must select exactly one {prefix[:-1]} plugin")

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
    architecture = str(manifest["resolved_architecture"])
    environment = str(manifest["environment"])
    mode = str(manifest["mode"])
    required = _capability_expression(manifest["required_capabilities"])  # type: ignore[arg-type]
    allowed = _capability_expression(manifest["allowed_capabilities"])  # type: ignore[arg-type]
    return f"""/* Generated by tools/generate_plugin_registry.py; do not edit. */
#include <Ribon/plugin/registry.h>

{externs}

static const struct RibonPluginDescriptor *const generated_plugins[] = {{
{pointers}
}};

static const struct RibonPluginRegistry generated_registry = {{
    .size = sizeof(generated_registry),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .plugins = generated_plugins,
    .plugin_count = (uint32_t)(sizeof(generated_plugins) / sizeof(generated_plugins[0])),
}};

static const struct RibonProductDescriptor generated_product = {{
    .magic = RIBON_PRODUCT_DESCRIPTOR_MAGIC,
    .size = sizeof(generated_product),
    .abi_version = RIBON_CORE_ABI_VERSION,
    .id = "{manifest['product_id']}",
    .architecture_mask = {ARCHITECTURE_MASKS[architecture]},
    .environment_mask = {ENVIRONMENT_MASKS[environment]},
    .mode_mask = RIBON_MODE_MASK({MODE_VALUES[mode]}),
    .max_plugins = {manifest['max_plugins']}u,
    .required_capabilities =
        {required},
    .allowed_capabilities =
        {allowed},
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
            "architecture": manifest["resolved_architecture"],
            "environment": manifest["environment"],
            "mode": manifest["mode"],
            "plugins": [item["id"] for item in manifest["plugins"]],
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
