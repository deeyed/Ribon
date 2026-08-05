#!/usr/bin/env python3
"""Keep the public Make frontend modular, complete and host-path neutral."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


REQUIRED_MODULES = (
    "make/config.mk",
    "make/model.mk",
    "make/rules/tooling.mk",
    "make/rules/core.mk",
    "make/rules/ribos.mk",
    "make/rules/security-update.mk",
    "make/rules/raw-fdt.mk",
    "make/rules/uefi-bios.mk",
    "make/rules/host-sdk.mk",
    "make/rules/aggregate.mk",
)
REQUIRED_TARGETS = (
    "all:",
    "lib:",
    "sdk-install:",
    "host-reference:",
    "check:",
    "check-target-builds:",
    "qstar-check:",
    "docs:",
    "qemu-aarch64-virt-modules-fixture-smoke:",
    "qemu-riscv64-virt-rph1-fixture-smoke:",
    "x86_64-uefi-parus-fixture-smoke:",
)
FORBIDDEN_PATHS = (
    "/Users/",
    "/opt/homebrew/Cellar/",
    "/usr/local/Cellar/",
    "/usr/bin/clang",
)


def fail(message: str) -> None:
    """Emit one stable build-graph failure."""

    print(f"RIBON-MAKE-MODULE-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    """Validate root ownership, module inventory and public target coverage."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    root_makefile = root / "Makefile"
    root_text = root_makefile.read_text(encoding="utf-8")
    if len(root_text.splitlines()) > 80:
        fail("root Makefile owns implementation rules")
    texts = [root_text]
    for relative in REQUIRED_MODULES:
        path = root / relative
        if not path.is_file():
            fail(f"required module is missing: {relative}")
        if relative not in root_text.replace("$(RIBON_MAKE_DIR)/", "make/"):
            fail(f"root Makefile does not include: {relative}")
        texts.append(path.read_text(encoding="utf-8"))
    combined = "\n".join(texts)
    for forbidden in FORBIDDEN_PATHS:
        if forbidden in combined:
            fail(f"host-specific path leaked into Make graph: {forbidden}")
    for target in REQUIRED_TARGETS:
        if target not in combined:
            fail(f"public target is missing: {target[:-1]}")
    if not (root / "qstar.lua").is_file() or "qstar-check:" not in combined:
        fail("QStar graph or Make-owned QStar gate is missing")
    print(
        "RIBON-MAKE-MODULES-OK "
        f"modules={len(REQUIRED_MODULES)} targets={len(REQUIRED_TARGETS)}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as error:
        fail(str(error))
