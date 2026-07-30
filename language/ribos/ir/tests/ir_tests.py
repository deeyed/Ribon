#!/usr/bin/env python3
"""Exercise deterministic Typed AST to Policy IR v1 lowering."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


RIBOS = Path(__file__).resolve().parents[2]
CORPUS = RIBOS / "frontend" / "tests" / "semantic" / "positive"
EXPECTED_SCHEMA = (
    "da48c96b07390ecbadb6eef06ab6cdfbd"
    "07b9a6de1bb1aa8e876e19f24378f52"
)
REQUIRED_OPCODES = {
    "branch",
    "build-list",
    "build-map",
    "build-struct",
    "build-variant",
    "call-direct",
    "call-helper",
    "checked-binary",
    "jump",
    "move",
    "return",
    "variant-payload",
    "variant-tag",
}
TERMINATORS = {"branch", "jump", "return", "trap"}


def invoke(compiler: Path, fixture: Path) -> subprocess.CompletedProcess[str]:
    """Lower one source file without a shell."""

    return subprocess.run(
        [str(compiler), "--dump-ir", str(fixture)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def check_dump(fixture: Path, output: str, failures: list[str]) -> set[str]:
    """Check one deterministic dump's table links and explicit CFG."""

    lines = output.splitlines()
    module = next(
        (line for line in lines if line.startswith("IR-MODULE ")),
        "",
    )
    schema_match = re.search(r"\bschema=([0-9a-f]{64})\b", module)
    helper_count_match = re.search(r"\bhelper-calls=(\d+)\b", module)
    shape_count_match = re.search(r"\bshapes=(\d+)\b", module)
    shape_rows = [
        line for line in lines if line.startswith("IR-SHAPE ")
    ]
    source_ids = {
        int(match.group(1))
        for line in lines
        if (match := re.match(r"IR-SOURCE id=m(\d+) ", line))
    }
    instruction_rows: dict[int, tuple[str, int]] = {}
    block_last_opcode: dict[int, str] = {}
    opcodes: set[str] = set()

    if schema_match is None or schema_match.group(1) != EXPECTED_SCHEMA:
        failures.append(f"{fixture.name}: unexpected product schema identity")
    if shape_count_match is None or int(shape_count_match.group(1)) != len(
        shape_rows
    ):
        failures.append(f"{fixture.name}: aggregate shape table count mismatch")
    for line in lines:
        match = re.match(
            r"IR-INSTRUCTION id=i(\d+) block=b(\d+) op=([a-z-]+).*"
            r"source=m(\d+)$",
            line,
        )
        if match is None:
            continue
        instruction_id = int(match.group(1))
        block_id = int(match.group(2))
        opcode = match.group(3)
        source_id = int(match.group(4))
        instruction_rows[instruction_id] = (opcode, source_id)
        block_last_opcode[block_id] = opcode
        opcodes.add(opcode)
        if source_id not in source_ids:
            failures.append(
                f"{fixture.name}: instruction i{instruction_id} "
                f"references missing source map m{source_id}"
            )
    if not instruction_rows:
        failures.append(f"{fixture.name}: no IR instructions")
    for block_id, opcode in block_last_opcode.items():
        if opcode not in TERMINATORS:
            failures.append(
                f"{fixture.name}: block b{block_id} ends in {opcode}"
            )

    helper_rows = [
        match
        for line in lines
        if (
            match := re.match(
                r"IR-HELPER id=(\d+) instruction=i(\d+) helper=(\d+) "
                r"caps=0x([0-9a-f]{8}) result=(\d+) arguments=(\d+) "
                r"source=m(\d+)$",
                line,
            )
        )
    ]
    if helper_count_match is None or int(helper_count_match.group(1)) != len(
        helper_rows
    ):
        failures.append(f"{fixture.name}: helper table count mismatch")
    previous_instruction = -1
    for helper in helper_rows:
        instruction_id = int(helper.group(2))
        stable_id = int(helper.group(3))
        source_id = int(helper.group(7))
        if (
            instruction_id <= previous_instruction
            or instruction_rows.get(instruction_id) != ("call-helper", source_id)
            or stable_id == 0
        ):
            failures.append(f"{fixture.name}: malformed helper call-site table")
        previous_instruction = instruction_id
    return opcodes


def main(argv: list[str]) -> int:
    """Require a deterministic, schema-bound explicit-CFG IR corpus."""

    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--compiler", type=Path, required=True)
    args = argument_parser.parse_args(argv)
    failures: list[str] = []
    observed_opcodes: set[str] = set()
    fixture_count = 0
    dumps: dict[str, str] = {}

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
                f"{fixture.name}: lowering failed or was nondeterministic\n"
                f"first={first.stdout}{first.stderr}"
                f"second={second.stdout}{second.stderr}"
            )
            continue
        dumps[fixture.name] = first.stdout
        observed_opcodes |= check_dump(fixture, first.stdout, failures)

    missing = REQUIRED_OPCODES - observed_opcodes
    if missing:
        failures.append(f"IR corpus does not cover opcodes: {sorted(missing)}")
    pipeline_helpers = [
        int(value)
        for value in re.findall(
            r"^IR-HELPER .* helper=(\d+) ",
            dumps.get("policy_pipeline.rbs", ""),
            flags=re.MULTILINE,
        )
    ]
    if pipeline_helpers != [1, 2, 8, 11, 13, 21]:
        failures.append(
            "left-to-right helper evaluation order changed: "
            f"{pipeline_helpers}"
        )
    aggregate_dump = dumps.get("aggregate_lowering.rbs", "")
    if (
        "kind=0 owner=" not in aggregate_dump
        or "kind=1 owner=" not in aggregate_dump
        or "kind=2 owner=" not in aggregate_dump
    ):
        failures.append(
            "struct field, enum variant, or enum payload shape is missing"
        )
    legacy_sources = sorted(RIBOS.rglob("*.ribos"))
    if legacy_sources:
        failures.append(
            "legacy .ribos source extensions remain: "
            + ", ".join(str(path.relative_to(RIBOS)) for path in legacy_sources)
        )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-POLICY-IR-V1-OK "
        f"fixtures={fixture_count} schema={EXPECTED_SCHEMA} "
        f"opcodes={len(observed_opcodes)} deterministic=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
