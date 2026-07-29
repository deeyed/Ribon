#!/usr/bin/env python3
"""Exercise Ribos typed-AST, type, bound, and capability diagnostics."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
POSITIVE = PROJECT / "tests" / "semantic" / "positive"
NEGATIVE = PROJECT / "tests" / "semantic" / "negative"
EXPECTED = {
    "argument_type.ribos": "E_ARGUMENT_TYPE_MISMATCH",
    "capability_missing.ribos": "E_CAPABILITY_NOT_DECLARED",
    "duplicate_binding.ribos": "E_DUPLICATE_BINDING",
    "empty_collection.ribos": "E_CANNOT_INFER_EMPTY_COLLECTION",
    "helper_budget.ribos": "E_HELPER_BUDGET_EXCEEDED",
    "handoff_value_type.ribos": "E_ARGUMENT_TYPE_MISMATCH",
    "heterogeneous_list.ribos": "E_COLLECTION_ELEMENT_TYPE_MISMATCH",
    "immutable_assignment.ribos": "E_MUTATE_IMMUTABLE_BINDING",
    "missing_return.ribos": "E_MISSING_RETURN",
    "non_exhaustive_match.ribos": "E_NON_EXHAUSTIVE_MATCH",
    "pure_effect.ribos": "E_PURE_FUNCTION_HAS_EFFECT",
    "recursive_call.ribos": "E_RECURSIVE_CALL_GRAPH",
    "result_unused.ribos": "E_RESULT_MUST_BE_USED",
    "type_mismatch.ribos": "E_TYPE_MISMATCH",
    "unbounded_iteration.ribos": "E_UNBOUNDED_ITERATION",
    "unknown_name.ribos": "E_UNKNOWN_NAME",
}


def invoke(parser: Path, fixture: Path, mode: str = "--check") -> subprocess.CompletedProcess[str]:
    """Invoke one bounded compiler operation without a shell."""

    return subprocess.run(
        [str(parser), mode, str(fixture)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def main(argv: list[str]) -> int:
    """Require semantic positives and stable primary negative diagnostics."""

    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--parser", type=Path, required=True)
    args = argument_parser.parse_args(argv)
    failures: list[str] = []

    positive_count = 0
    for fixture in sorted(POSITIVE.glob("*.ribos")):
        result = invoke(args.parser, fixture)
        positive_count += 1
        if result.returncode != 0 or "RIBOS-COMPILER-OK" not in result.stdout:
            failures.append(
                f"semantic positive failed: {fixture.name}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )

    negative_count = 0
    for fixture in sorted(NEGATIVE.glob("*.ribos")):
        result = invoke(args.parser, fixture)
        negative_count += 1
        expected = EXPECTED[fixture.name]
        if result.returncode == 0 or f"code={expected}" not in result.stderr:
            failures.append(
                f"semantic negative mismatch: {fixture.name} expected={expected}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )

    pilot = POSITIVE / "policy_pipeline.ribos"
    first_dump = invoke(args.parser, pilot, "--dump-semantics")
    second_dump = invoke(args.parser, pilot, "--dump-semantics")
    if first_dump.returncode != 0 or first_dump.stdout != second_dump.stdout:
        failures.append(
            "semantic dump is not deterministic\n"
            f"first={first_dump.stdout}{first_dump.stderr}"
            f"second={second_dump.stdout}{second_dump.stderr}"
        )
    if "TRIVIA " not in first_dump.stdout or "AST " not in first_dump.stdout:
        failures.append("semantic dump omitted token trivia or typed AST records")
    ast_lines = [
        line for line in first_dump.stdout.splitlines() if line.startswith("AST ")
    ]
    summary_match = re.search(r"\bast=(\d+)\b", first_dump.stdout)
    if summary_match is None or int(summary_match.group(1)) != len(ast_lines):
        failures.append(
            "reachable AST count does not match the deterministic dump "
            f"summary={summary_match.group(1) if summary_match else 'missing'} "
            f"records={len(ast_lines)}"
        )
    if not any(
        line.startswith("TRIVIA ")
        and "kind=comment" in line
        and "lossless token/trivia model" in line
        for line in first_dump.stdout.splitlines()
    ):
        failures.append("comment trivia was not preserved in the token model")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-SEMANTIC-CORPUS-OK "
        f"positive={positive_count} negative={negative_count} "
        "deterministic-dump=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
