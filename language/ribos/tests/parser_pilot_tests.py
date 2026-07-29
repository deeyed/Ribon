#!/usr/bin/env python3
"""Run the tracked Ribos parser snapshot against the syntax corpus."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
POSITIVE = PROJECT / "tests" / "positive"
NEGATIVE = PROJECT / "tests" / "negative"
EXPECTED_NEGATIVE_KIND = {
    "exception_try.ribos": "reserved-feature",
    "invalid_number.ribos": "invalid-number",
    "invalid_string_escape.ribos": "invalid-string",
    "reserved_while.ribos": "reserved-feature",
    "top_level_reserved_while.ribos": "reserved-feature",
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
    positive_count = 0
    negative_count = 0
    for fixture in sorted(POSITIVE.glob("*.ribos")):
        result = invoke(args.parser, fixture)
        positive_count += 1
        if result.returncode != 0 or "RIBOS-PARSER-PILOT-OK" not in result.stdout:
            failures.append(
                f"positive fixture failed: {fixture.name}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )

    for fixture in sorted(NEGATIVE.glob("*.ribos")):
        result = invoke(args.parser, fixture)
        negative_count += 1
        if result.returncode == 0 or "RIBOS-PARSER-PILOT-FAIL" not in result.stderr:
            failures.append(
                f"negative fixture accepted: {fixture.name}\n"
                f"stdout={result.stdout}stderr={result.stderr}"
            )
            continue
        expected_kind = EXPECTED_NEGATIVE_KIND.get(fixture.name)
        if expected_kind is not None and f"kind={expected_kind}" not in result.stderr:
            failures.append(
                f"negative fixture category mismatch: {fixture.name} "
                f"expected={expected_kind}\nstderr={result.stderr}"
            )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-PARSER-CORPUS-OK "
        f"positive={positive_count} negative={negative_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
