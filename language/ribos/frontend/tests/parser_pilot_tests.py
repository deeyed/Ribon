#!/usr/bin/env python3
"""Run the tracked Ribos parser snapshot against the syntax corpus."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


FRONTEND = Path(__file__).resolve().parents[1]
RIBOS = FRONTEND.parent
EXECUTABLE = RIBOS / "examples" / "executable"
FRAGMENTS = FRONTEND / "tests" / "fragments"
NEGATIVE = FRONTEND / "tests" / "negative"
EXPECTED_NEGATIVE = {
    "chained_comparison.rbs": ("syntax-error", "syntax"),
    "exception_try.rbs": ("syntax-error", "reserved-feature"),
    "invalid_decorator_placement.rbs": ("syntax-error", "syntax"),
    "invalid_number.rbs": ("lexical-error", "invalid-number"),
    "invalid_string_escape.rbs": ("lexical-error", "invalid-string"),
    "missing_parameter_type.rbs": ("syntax-error", "syntax"),
    "nested_function.rbs": ("syntax-error", "syntax"),
    "python_conditional.rbs": ("syntax-error", "syntax"),
    "reserved_while.rbs": ("syntax-error", "reserved-feature"),
    "top_level_reserved_while.rbs": ("syntax-error", "reserved-feature"),
    "top_level_statement.rbs": ("syntax-error", "syntax"),
    "unbalanced_delimiter.rbs": ("syntax-error", "syntax"),
}


def invoke(parser: Path, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Invoke the standalone parser once without a shell."""

    return subprocess.run(
        [str(parser), str(fixture)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def main(argv: list[str]) -> int:
    """Require every positive fixture to pass and every negative to fail."""

    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--parser", type=Path, required=True)
    args = argument_parser.parse_args(argv)

    failures: list[str] = []
    executable_count = 0
    fragment_count = 0
    negative_count = 0
    for fixture in sorted(EXECUTABLE.glob("*.rbs")):
        result = invoke(args.parser, fixture)
        executable_count += 1
        if result.returncode != 0 or "RIBOS-PARSER-PILOT-OK" not in result.stdout:
            failures.append(
                f"executable fixture failed: {fixture.name}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )

    for fixture in sorted(FRAGMENTS.glob("*.rbs")):
        result = invoke(args.parser, fixture)
        fragment_count += 1
        if result.returncode != 0 or "RIBOS-PARSER-PILOT-OK" not in result.stdout:
            failures.append(
                f"syntax fragment failed: {fixture.name}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )

    for fixture in sorted(NEGATIVE.glob("*.rbs")):
        result = invoke(args.parser, fixture)
        negative_count += 1
        if result.returncode == 0 or "RIBOS-PARSER-PILOT-FAIL" not in result.stderr:
            failures.append(
                f"negative fixture accepted: {fixture.name}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )
            continue
        expected_status, expected_kind = EXPECTED_NEGATIVE[fixture.name]
        if (
            f"status={expected_status}" not in result.stderr
            or f"kind={expected_kind}" not in result.stderr
        ):
            failures.append(
                f"negative fixture category mismatch: {fixture.name} "
                f"expected={expected_status}/{expected_kind}\n"
                f"stderr={result.stderr}"
            )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-PARSER-CORPUS-OK "
        f"executable={executable_count} fragments={fragment_count} "
        f"negative={negative_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
