#!/usr/bin/env python3
"""Exercise deterministic Policy IR CFG and resource closure."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


RIBOS = Path(__file__).resolve().parents[2]
CORPUS = RIBOS / "frontend" / "tests" / "semantic" / "positive"


def invoke(compiler: Path, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Build and dump one resource closure without a shell."""

    return subprocess.run(
        [str(compiler), "--dump-resources", str(fixture)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def check_dump(fixture: Path, output: str, failures: list[str]) -> None:
    """Check stable table links, bounds, frames, and terminal closure."""

    lines = output.splitlines()
    header = next(
        (
            line
            for line in lines
            if line.startswith("IR-RESOURCE-CLOSURE ")
        ),
        "",
    )
    if "version=1.1" not in header:
        failures.append(f"{fixture.name}: missing Policy IR 1.1 closure")

    functions: dict[int, tuple[int, int]] = {}
    for line in lines:
        match = re.match(
            r"IR-RESOURCE-FUNCTION id=(\d+) reachable=(\d+) "
            r"terminal=0x([0-9a-f]+) closed=(\d+) frame=(\d+) "
            r"aggregate=(\d+) largest=(\d+) stack=(\d+) "
            r"call-depth=(\d+) instructions=(\d+) helpers=(\d+) "
            r"instruction-budget=(\d+) helper-budget=(\d+)$",
            line,
        )
        if match is None:
            continue
        function_id = int(match.group(1))
        frame_bytes = int(match.group(5))
        functions[function_id] = (frame_bytes, int(match.group(10)))
        if (
            int(match.group(2)) == 0
            or int(match.group(3), 16) == 0
            or int(match.group(4)) != 1
            or int(match.group(8)) < frame_bytes
            or int(match.group(9)) == 0
            or int(match.group(10)) == 0
            or int(match.group(12)) != 1
            or int(match.group(13)) != 1
        ):
            failures.append(
                f"{fixture.name}: function resource closure is incomplete"
            )

    if not functions:
        failures.append(f"{fixture.name}: no function resource rows")

    for line in lines:
        match = re.match(
            r"IR-RESOURCE-SLOT id=s(\d+) function=(\d+) type=(\d+) "
            r"offset=(\d+) bytes=(\d+) align=(\d+)$",
            line,
        )
        if match is None:
            continue
        function_id = int(match.group(2))
        offset = int(match.group(4))
        byte_size = int(match.group(5))
        alignment = int(match.group(6))
        frame = functions.get(function_id, (0, 0))[0]
        if (
            alignment == 0
            or offset % alignment != 0
            or offset + byte_size > frame
        ):
            failures.append(f"{fixture.name}: invalid slot frame layout")

    if fixture.name == "bounded_map.rbs" and not any(
        line.startswith("IR-RESOURCE-TYPE ")
        and "storage=4" in line
        and "capacity=2" in line
        for line in lines
    ):
        failures.append(
            "bounded_map.rbs: FrozenMap is not a capacity-2 sorted array"
        )
    if fixture.name == "bounded_map.rbs" and not any(
        line.startswith("IR-RESOURCE-FUNCTION id=1 ")
        and "stack=116 call-depth=2 instructions=18 helpers=1 " in line
        for line in lines
    ):
        failures.append(
            "bounded_map.rbs: direct-call stack/depth/instruction closure changed"
        )

    if fixture.name == "policy_pipeline.rbs":
        if not any(
            "IR-RESOURCE-LOOP " in line and "trips=2" in line
            for line in lines
        ):
            failures.append("policy_pipeline.rbs: loop trip bound is not 2")
        if not any(
            line
            == "IR-RESOURCE-HELPER function=1 helper=1 upper=2"
            for line in lines
        ):
            failures.append(
                "policy_pipeline.rbs: device.init upper bound is not 2"
            )

    if fixture.name == "nested_resources.rbs":
        trips = sorted(
            int(match.group(1))
            for line in lines
            if (
                match := re.match(
                    r"IR-RESOURCE-LOOP .* trips=(\d+) reachable=1$",
                    line,
                )
            )
        )
        if trips != [2, 3]:
            failures.append(
                f"nested_resources.rbs: unexpected loop bounds {trips}"
            )
        if not any(
            line
            == "IR-RESOURCE-HELPER function=0 helper=1 upper=6"
            for line in lines
        ):
            failures.append(
                "nested_resources.rbs: nested helper upper bound is not 6"
            )
        if not any(
            line.startswith("IR-RESOURCE-FUNCTION id=0 ")
            and "instructions=132 helpers=7 " in line
            for line in lines
        ):
            failures.append(
                "nested_resources.rbs: nested instruction/helper closure changed"
            )


def main(argv: list[str]) -> int:
    """Require deterministic, budget-closed resource analysis."""

    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--compiler", type=Path, required=True)
    args = argument_parser.parse_args(argv)
    failures: list[str] = []
    fixture_count = 0

    for fixture in sorted(CORPUS.glob("*.rbs")):
        first = invoke(args.compiler, fixture)
        second = invoke(args.compiler, fixture)
        fixture_count += 1
        if (
            first.returncode != 0
            or second.returncode != 0
            or first.stdout != second.stdout
        ):
            failures.append(
                f"{fixture.name}: resource dump failed or changed\n"
                f"first={first.stdout}{first.stderr}"
                f"second={second.stdout}{second.stderr}"
            )
            continue
        check_dump(fixture, first.stdout, failures)

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-RESOURCE-CLOSURE-OK "
        f"fixtures={fixture_count} deterministic=1 "
        "cfg=closed budgets=enforced dict=sorted-array"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
