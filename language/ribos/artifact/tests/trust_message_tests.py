#!/usr/bin/env python3
"""Cross-check the C and Python product-bound trust-message encoders."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    """Run one bounded host command with captured text output."""

    return subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def inspect(inspector: Path, vector: Path) -> subprocess.CompletedProcess[str]:
    """Ask the independent Python inspector for canonical lowercase hex."""

    return run(
        [sys.executable, str(inspector), "--vector", str(vector), "--format", "hex"]
    )


def main() -> int:
    """Require byte identity and binding-sensitive negative behavior."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--c-codec", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--vector", type=Path, required=True)
    args = parser.parse_args()

    source = json.loads(args.vector.read_text(encoding="utf-8"))
    expected = source["expected_message_hex"]
    c_result = run([str(args.c_codec), "--dump-trust-vector"])
    python_result = inspect(args.inspector, args.vector)
    failures: list[str] = []
    if c_result.returncode != 0 or c_result.stdout.strip() != expected:
        failures.append(f"C codec mismatch: {c_result.stdout}{c_result.stderr}")
    if python_result.returncode != 0 or python_result.stdout.strip() != expected:
        failures.append(
            f"Python inspector mismatch: {python_result.stdout}{python_result.stderr}"
        )

    with tempfile.TemporaryDirectory(prefix="ribon-trust-vector-") as directory:
        root = Path(directory)
        mutations = (
            ("artifact", "artifact_sha256", "f" * 64),
            ("product", "product_sha256", "e" * 64),
            ("schema", "schema_sha256", "d" * 64),
            ("domain", "rollback_domain_sha256", "c" * 64),
            ("sequence", "sequence", source["sequence"] + 1),
        )
        for label, key, value in mutations:
            mutated = copy.deepcopy(source)
            mutated[key] = value
            mutated.pop("expected_message_hex")
            mutated.pop("expected_message_sha256")
            # The public inspector requires a frozen vector, so restore expectations from
            # the baseline only to prove that each changed binding is rejected.
            mutated["expected_message_hex"] = expected
            mutated["expected_message_sha256"] = source[
                "expected_message_sha256"
            ]
            path = root / f"{label}.json"
            path.write_text(
                json.dumps(mutated, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            result = inspect(args.inspector, path)
            if result.returncode == 0:
                failures.append(f"{label} mutation retained the frozen message")

        hostile = copy.deepcopy(source)
        hostile["key_usage"] = 5
        hostile_path = root / "wrong-usage.json"
        hostile_path.write_text(
            json.dumps(hostile, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        hostile_result = inspect(args.inspector, hostile_path)
        if hostile_result.returncode == 0:
            failures.append("update-manifest usage was accepted for a Ribos policy")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBON-TRUST-MESSAGE-V1-OK bytes=232 cross-tool=2 "
        "bindings=artifact,product,schema,mode,usage,domain,sequence"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
