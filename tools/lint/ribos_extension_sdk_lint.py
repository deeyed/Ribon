#!/usr/bin/env python3
"""Reject private-header, install, package-selection, and symbol ABI drift."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_INCLUDE = re.compile(r'#include\s+["<](?:\.\./|src/|products/|platforms/)')


def fail(message: str) -> None:
    print(f"RIBON-R01-RIBOS-EXTENSION-SDK-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def symbols(archive: Path) -> set[str]:
    result = subprocess.run(
        ["nm", "-g", str(archive)], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        fail(result.stderr.strip() or f"nm failed: {archive}")
    return {
        fields[-1].removeprefix("_")
        for line in result.stdout.splitlines()
        if len((fields := line.split())) >= 2 and fields[-2] not in {"U", "u"}
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--example-root", type=Path, required=True)
    args = parser.parse_args()

    for relative in (
        "include/Ribon/policy/ribos_extension.h",
        "include/ribos/schema/schema.h",
        "include/ribos/vm/runtime.h",
        "lib/libribon-policy-ribos.a",
        "lib/libribos-target-core.a",
    ):
        if not (args.install_root / relative).is_file():
            fail(f"installed SDK component missing: {relative}")

    expected = {
        line.strip()
        for line in (ROOT / "sdk/abi/libribon-policy-ribos-v1.symbols")
        .read_text(encoding="utf-8")
        .splitlines()
        if line.strip()
    }
    actual = symbols(args.install_root / "lib/libribon-policy-ribos.a")
    if actual != expected:
        fail(f"adapter symbol ABI mismatch expected={sorted(expected)} actual={sorted(actual)}")

    package = json.loads((args.example_root / "package.json").read_text())
    product = json.loads((args.example_root / "product.json").read_text())
    if (
        package.get("schema") != "ribon-ribos-extension-package-v1"
        or package.get("abi_version") != 1
        or package.get("helper_ids") != sorted(set(package.get("helper_ids", [])))
        or product.get("schema") != "ribon-ribos-extension-selection-v1"
        or product.get("ribos_extensions") != [package.get("package_id")]
    ):
        fail("external product does not select one canonical extension package")

    for source in args.example_root.glob("*.[ch]"):
        if PRIVATE_INCLUDE.search(source.read_text(encoding="utf-8")):
            fail(f"private include escaped installed SDK boundary: {source}")
    print("RIBON-R01-RIBOS-EXTENSION-SDK-LINT-OK package=selected symbols=closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
