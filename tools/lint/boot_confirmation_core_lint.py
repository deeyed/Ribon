#!/usr/bin/env python3
"""Gate the D06 confirmation authority and OS-neutral core boundary."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


BANNED_CORE_TOKENS = (
    "parus",
    "linux",
    "freebsd",
    "zircon",
    "kernel_main",
    "userspace",
    "service-name",
    "getenv",
)


def fail(message: str) -> None:
    print(f"RIBON-D06-BOOT-CONFIRMATION-GRAPH-FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_json(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"{path}: cannot read generated graph: {error}")
    if not isinstance(value, dict):
        fail(f"{path}: graph root is not an object")
    return value


def usages(graph: dict[str, object]) -> set[str]:
    policy = graph.get("key_policy")
    if not isinstance(policy, dict):
        return set()
    keys = policy.get("keys")
    if not isinstance(keys, list):
        return set()
    result: set[str] = set()
    for key in keys:
        if not isinstance(key, dict) or not isinstance(key.get("usages"), list):
            fail("key-policy graph contains a malformed usage list")
        result.update(value for value in key["usages"] if isinstance(value, str))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--confirmation-graph", type=Path, required=True)
    parser.add_argument("--network-graph", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()

    core_paths = (
        root / "include/Ribon/update/confirmation.h",
        root / "src/update/confirmation.c",
    )
    core_text = "\n".join(path.read_text(encoding="utf-8").lower()
                          for path in core_paths)
    for token in BANNED_CORE_TOKENS:
        if token in core_text:
            fail(f"generic confirmation core contains OS/product token {token!r}")
    if "ribon_boot_protocol_validate_boot_health" not in core_text:
        fail("generic core does not delegate health semantics to Boot Protocol")
    if "ribon_key_policy_usage_boot_confirmation" not in core_text:
        fail("generic core does not require the dedicated confirmation key usage")
    if "ribon_protected_state_confirm_bound" not in core_text or \
            "ribon_update_transaction_confirm_pending" not in core_text:
        fail("confirmation does not close both durable authorities")

    confirmation = load_json(args.confirmation_graph)
    network = load_json(args.network_graph)
    if confirmation.get("product_id") != \
            "validation.x86_64-uefi-update-recovery":
        fail("confirmation graph has the wrong product identity")
    protected = confirmation.get("protected_state_provider")
    if not isinstance(protected, dict) or protected.get("class") != "reference":
        fail("confirmation fixture must declare reference protected state")
    if "boot-confirmation" not in usages(confirmation):
        fail("confirmation product lacks dedicated key usage")
    if "boot-confirmation" in usages(network):
        fail("network-only recovery product unexpectedly authorizes confirmation")

    protocols = (
        root / "src/protocols/os/parus/protocol.c",
        root / "src/protocols/os/linux/protocol.c",
        root / "src/protocols/os/freebsd/protocol.c",
        root / "src/protocols/os/zircon/protocol.c",
    )
    for path in protocols:
        text = path.read_text(encoding="utf-8")
        if "validate_boot_health" not in text or \
                "RIBON_PROTOCOL_STATUS_UNSUPPORTED" not in text:
            fail(f"{path}: companion health producer is not fail-closed")

    print("RIBON-D06-BOOT-CONFIRMATION-GRAPH-OK "
          "core=os-neutral authority=dedicated protocol=fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
