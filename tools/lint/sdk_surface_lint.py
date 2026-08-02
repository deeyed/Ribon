#!/usr/bin/env python3
"""Audit installed Ribon SDK headers, archives, symbols, and example includes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_INCLUDE = re.compile(r'#include\s+["<](?:\.\./|src/|platforms/|products/)')


def fail(message: str) -> None:
    print(f"RIBON-SDK-SURFACE-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def defined_symbols(archive: Path) -> set[str]:
    """Return normalized global defined symbols from one static archive."""

    result = subprocess.run(
        ["nm", "-g", str(archive)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(result.stderr.strip() or f"nm failed for {archive}")
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[-2] not in {"U", "u"}:
            symbols.add(fields[-1].removeprefix("_"))
    return symbols


def unsigned_macro(header: Path, name: str) -> int:
    """Read one public unsigned integer macro from an installed header."""

    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+([0-9]+)[uU]?\s*$",
        header.read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if match is None:
        fail(f"installed public ABI macro is missing: {name}")
    return int(match.group(1), 10)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", type=Path, required=True)
    args = parser.parse_args()
    root = args.install_root

    required_archives = {
        "libribon-core.a",
        "libribon-boot.a",
        "libribon-sdk.a",
        "libribon-update.a",
    }
    archives = {path.name for path in (root / "lib").glob("*.a")}
    if archives != required_archives:
        fail(
            f"archive set mismatch expected={sorted(required_archives)} "
            f"actual={sorted(archives)}"
        )
    if (root / "include" / "Ribon").is_dir() is False:
        fail("installed public include/Ribon tree is missing")
    if any((root / "include").rglob("reference.h")):
        fail("source-private reference header leaked into SDK")

    sdk_abi = unsigned_macro(
        root / "include" / "Ribon" / "sdk" / "abi.h",
        "RIBON_SDK_ABI_VERSION",
    )
    core_abi = unsigned_macro(
        root / "include" / "Ribon" / "core" / "capability.h",
        "RIBON_CORE_ABI_VERSION",
    )
    plugin_abi_major = unsigned_macro(
        root / "include" / "Ribon" / "plugin" / "descriptor.h",
        "RIBON_PLUGIN_ABI_MAJOR",
    )
    plugin_abi_minor = unsigned_macro(
        root / "include" / "Ribon" / "plugin" / "descriptor.h",
        "RIBON_PLUGIN_ABI_MINOR",
    )
    expected_symbols = {
        line.strip()
        for line in (ROOT / "sdk" / "abi" / f"libribon-sdk-v{sdk_abi}.symbols")
        .read_text(encoding="utf-8")
        .splitlines()
        if line.strip()
    }
    actual_symbols = defined_symbols(root / "lib" / "libribon-sdk.a")
    if actual_symbols != expected_symbols:
        fail(
            f"SDK symbol set mismatch expected={sorted(expected_symbols)} "
            f"actual={sorted(actual_symbols)}"
        )

    manifest_path = root / "share" / "ribon" / "sdk-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    required_tools = {
        "ribosc": "compiler",
        "ribos-verify": "independent-verifier",
        "ribos-run": "host-runner",
        "ribon-compose-product": "product-composer",
        "ribon-update-manifest": "update-assembler-inspector",
        "ribon-update-layout": "layout-composer-inspector",
        "ribon-sign-policy": "offline-private-key-capable-signer",
    }
    host_tools = manifest.get("host_tools")
    boundaries = manifest.get("boundaries")
    if (
        manifest.get("schema") != "ribon-sdk-install-v2"
        or manifest.get("schema_version") != 2
        or manifest.get("sdk_abi") != sdk_abi
        or manifest.get("core_abi") != core_abi
        or manifest.get("plugin_abi")
            != {"major": plugin_abi_major, "minor": plugin_abi_minor}
        or manifest.get("source_version") != "0.4.0"
        or not re.fullmatch(r"[0-9a-f]{40}", manifest.get("source_revision", ""))
        or not isinstance(host_tools, dict)
        or {name: value.get("class") for name, value in host_tools.items()}
            != required_tools
        or any(value.get("target_linkable") is not False for value in host_tools.values())
        or boundaries != {
            "host_tools_target_linkable": False,
            "private_key_material_included": False,
            "target_libraries": sorted(required_archives),
        }
    ):
        fail("installed SDK ABI manifest is inconsistent")
    for name, metadata in host_tools.items():
        tool = root / "bin" / name
        if not tool.is_file() or metadata.get("sha256") != hashlib.sha256(
            tool.read_bytes()
        ).hexdigest():
            fail(f"installed host tool identity mismatch: {name}")
    signer_symbols = {
        symbol
        for archive in required_archives
        for symbol in defined_symbols(root / "lib" / archive)
        if re.search(
            r"(?:^|_)(?:sign|private|secret|seed|keypair)(?:$|_)",
            symbol.lower().lstrip("_"),
        )
    }
    if signer_symbols:
        fail(f"private signing surface leaked into target libraries: {sorted(signer_symbols)}")

    for path in sorted((ROOT / "examples").rglob("*")):
        if path.suffix not in {".c", ".h"}:
            continue
        text = path.read_text(encoding="utf-8")
        if PRIVATE_INCLUDE.search(text):
            fail(f"example crosses source-private include boundary: {path.relative_to(ROOT)}")
    print("RIBON-D08-SDK-INSTALL-SURFACE-OK tools=7 target-libraries=4 signer=host-only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
