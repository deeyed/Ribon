#!/usr/bin/env python3
"""Check Ribos generated-parser digests without invoking Pegen."""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GRAMMAR = ROOT / "language" / "grammar" / "ribos.gram"
TOKENS = ROOT / "language" / "grammar" / "Tokens"
SNAPSHOTS = (
    ROOT / "language" / "generated" / "ribos_parser.c",
    ROOT / "language" / "generated" / "ribos_tokens.h",
)
RECEIPT = ROOT / "language" / "generated" / "ribos_parser.receipt.json"
EXPECTED_PEGEN_REVISION = "9ccd5bb81edde823fb5fdbd51287f4a0ddfcd149"


def sha256(path: Path) -> str:
    """Return the SHA-256 digest for one authority source."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def marker(path: Path, name: str) -> str | None:
    """Read one generated provenance marker."""

    match = re.search(
        rf"{re.escape(name)}:\s*([0-9a-f]{{64}})",
        path.read_text(encoding="utf-8"),
    )
    return match.group(1) if match else None


def main() -> int:
    """Reject missing or stale snapshots without running the generator."""

    expected = {
        "RIBOS_GRAMMAR_SHA256": sha256(GRAMMAR),
        "RIBOS_TOKENS_SHA256": sha256(TOKENS),
    }
    failed = False
    for snapshot in SNAPSHOTS:
        if not snapshot.is_file():
            print(f"missing snapshot: {snapshot.relative_to(ROOT)}", file=sys.stderr)
            failed = True
            continue
        for name, digest in expected.items():
            actual = marker(snapshot, name)
            if actual != digest:
                print(
                    f"stale snapshot: {snapshot.relative_to(ROOT)} "
                    f"{name} expected={digest} actual={actual}",
                    file=sys.stderr,
                )
                failed = True
    try:
        receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"invalid snapshot receipt: {error}", file=sys.stderr)
        failed = True
        receipt = {}
    if receipt and all(snapshot.is_file() for snapshot in SNAPSHOTS):
        receipt_inputs = receipt.get("inputs", {})
        receipt_outputs = receipt.get("outputs", {})
        receipt_generator = receipt.get("generator", {})
        receipt_expectations = (
            (
                "grammar input",
                receipt_inputs.get("grammar", {}).get("sha256"),
                expected["RIBOS_GRAMMAR_SHA256"],
            ),
            (
                "token input",
                receipt_inputs.get("tokens", {}).get("sha256"),
                expected["RIBOS_TOKENS_SHA256"],
            ),
            (
                "parser output",
                receipt_outputs.get("parser", {}).get("sha256"),
                sha256(SNAPSHOTS[0]),
            ),
            (
                "token header output",
                receipt_outputs.get("tokens", {}).get("sha256"),
                sha256(SNAPSHOTS[1]),
            ),
            (
                "Pegen revision",
                receipt_generator.get("cpython_revision"),
                EXPECTED_PEGEN_REVISION,
            ),
        )
        for label, actual, expected_value in receipt_expectations:
            if actual != expected_value:
                print(
                    f"stale snapshot receipt: {label} "
                    f"expected={expected_value} actual={actual}",
                    file=sys.stderr,
                )
                failed = True
    if failed:
        return 1
    print(
        "RIBOS-PARSER-SNAPSHOT-OK "
        f"grammar={expected['RIBOS_GRAMMAR_SHA256']} "
        f"tokens={expected['RIBOS_TOKENS_SHA256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
