#!/usr/bin/env python3
"""Validate the R3 public API directory and one-way include boundary."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PUBLIC = ROOT / "include" / "Ribon"
REQUIRED = {
    "core/context.h",
    "core/memory.h",
    "core/capability.h",
    "core/status.h",
    "boot/source.h",
    "boot/image.h",
    "boot/plan.h",
    "boot/transfer.h",
    "plugin/descriptor.h",
    "plugin/registry.h",
    "plugin/phases.h",
    "plugin/manifest.h",
    "protocol/protocol.h",
    "protocol/entry_contract.h",
    "protocol/confirmation.h",
    "firmware/services.h",
    "firmware/environment.h",
    "firmware/personality.h",
    "arch/ops.h",
    "arch/entry.h",
    "platform/facts.h",
    "sdk/abi.h",
    "sdk/package.h",
    "sdk/host.h",
}
FORBIDDEN_FLAT = {
    "arch.h",
    "core.h",
    "firmware.h",
    "loader.h",
    "memory.h",
    "platform.h",
    "profile.h",
    "ribon.h",
}
CORE_FORBIDDEN_INCLUDE = re.compile(
    r"#include <Ribon/(?:arch|boot|firmware|protocol)/"
)


def main() -> int:
    """Check required headers, removed flat ABI and Core include direction."""

    actual = {
        path.relative_to(PUBLIC).as_posix()
        for path in PUBLIC.rglob("*.h")
        if path.is_file()
    }
    failures: list[str] = []
    missing = sorted(REQUIRED - actual)
    if missing:
        failures.append(f"missing required headers: {missing}")
    retained = sorted(FORBIDDEN_FLAT & actual)
    if retained:
        failures.append(f"retired flat headers remain: {retained}")
    for relative in sorted(path for path in actual if path.startswith("core/")):
        text = (PUBLIC / relative).read_text(encoding="utf-8")
        if CORE_FORBIDDEN_INCLUDE.search(text):
            failures.append(f"Core header crosses architecture/boot/firmware/protocol: {relative}")
    if failures:
        for failure in failures:
            print(f"public_api_layout_lint: {failure}")
        return 1
    print("RIBON-R3-PUBLIC-API-LAYOUT-OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
