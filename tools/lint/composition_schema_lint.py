#!/usr/bin/env python3
"""Validate Ribon product, target, image, and external package metadata."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_ROOT = ROOT / "qstar" / "schemas"
PACKAGE_ROOT = ROOT / "examples" / "plugins" / "diagnostic-sink"
PACKAGE_KEYS = {
    "schema_version",
    "package_id",
    "plugin_id",
    "plugin_kind",
    "sdk_abi",
    "public_headers",
    "sources",
    "tests",
    "documentation",
    "qstar_file",
}


def fail(message: str) -> None:
    print(f"RIBON-COMPOSITION-SCHEMA-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_generator():
    """Load the source-owned composer validator without importing a package."""

    path = ROOT / "tools" / "generate_plugin_registry.py"
    spec = importlib.util.spec_from_file_location("ribon_composer", path)
    if spec is None or spec.loader is None:
        fail("cannot load product composer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_package_files(package: dict[str, object], key: str) -> None:
    """Require sorted relative files declared by one package field."""

    values = package.get(key)
    if (
        not isinstance(values, list)
        or not values
        or any(not isinstance(value, str) or not value for value in values)
        or values != sorted(set(values))
    ):
        fail(f"package {key} must be a sorted unique non-empty list")
    for value in values:
        if not (PACKAGE_ROOT / value).is_file():
            fail(f"package declares missing {key} file: {value}")


def main() -> int:
    """Validate exact metadata and every source-owned product manifest."""

    schema_names = {
        "plugin-package.schema.json",
        "product.schema.json",
        "target.schema.json",
        "image.schema.json",
    }
    actual_schemas = {path.name for path in SCHEMA_ROOT.glob("*.json")}
    if actual_schemas != schema_names:
        fail(
            f"schema set mismatch expected={sorted(schema_names)} "
            f"actual={sorted(actual_schemas)}"
        )
    for path in sorted(SCHEMA_ROOT.glob("*.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            fail(f"{path.name}: unsupported schema dialect")

    composer = load_generator()
    manifests = [
        ROOT / "qstar" / "manifests" / "host-reference.json",
        *sorted((ROOT / "products").rglob("*.json")),
        PACKAGE_ROOT / "tests" / "product.json",
    ]
    for path in manifests:
        selected = "x86_64" if path.name == "host-reference.json" else None
        try:
            composer.load_manifest(path, selected)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            fail(f"{path.relative_to(ROOT)}: {error}")

    package_path = PACKAGE_ROOT / "package.json"
    package = json.loads(package_path.read_text(encoding="utf-8"))
    if set(package) != PACKAGE_KEYS:
        fail("external package keys do not match plugin-package schema")
    if (
        package.get("schema_version") != 1
        or package.get("sdk_abi") != 3
        or package.get("package_id") != "example.diagnostic-sink"
        or package.get("plugin_id") != "service.diagnostic-sink"
        or package.get("plugin_kind") != "service"
    ):
        fail("external package identity or ABI is invalid")
    for key in ("public_headers", "sources", "tests", "documentation"):
        require_package_files(package, key)
    qstar_file = package.get("qstar_file")
    if not isinstance(qstar_file, str) or not (PACKAGE_ROOT / qstar_file).is_file():
        fail("external package plugin.qst is missing")
    print(
        "RIBON-R5-COMPOSITION-SCHEMAS-OK "
        f"products={len(manifests)} schemas={len(schema_names)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
