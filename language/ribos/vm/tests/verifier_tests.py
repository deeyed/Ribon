#!/usr/bin/env python3
"""Exercise the independent Ribos verifier with positive and hostile artifacts."""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable


RIBOS = Path(__file__).resolve().parents[2]
CORPUS = RIBOS / "frontend" / "tests" / "semantic" / "positive"
HOSTILE_SOURCE = RIBOS / "vm" / "tests" / "hostile"
INVALID = 0xFFFFFFFF


def u16(data: bytes | bytearray, offset: int) -> int:
    """Read one little-endian u16."""

    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes | bytearray, offset: int) -> int:
    """Read one little-endian u32."""

    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes | bytearray, offset: int) -> int:
    """Read one little-endian u64."""

    return struct.unpack_from("<Q", data, offset)[0]


def p32(data: bytearray, offset: int, value: int) -> None:
    """Write one little-endian u32."""

    struct.pack_into("<I", data, offset, value)


def p64(data: bytearray, offset: int, value: int) -> None:
    """Write one little-endian u64."""

    struct.pack_into("<Q", data, offset, value)


class Artifact:
    """Small mutation view over the frozen unsigned `.rba` format."""

    def __init__(self, raw: bytes):
        self.data = bytearray(raw)
        self.payload = u64(self.data, 24)
        self.payload_length = u64(self.data, 32)
        self.sections: dict[int, tuple[int, int, int]] = {}
        count = u32(self.data, self.payload + 24)
        for index in range(count):
            descriptor = self.payload + 160 + index * 32
            kind = u16(self.data, descriptor)
            row_size = u32(self.data, descriptor + 4)
            offset = u64(self.data, descriptor + 8)
            rows = u32(self.data, descriptor + 24)
            self.sections[kind] = (
                self.payload + offset,
                row_size,
                rows,
            )

    def row(self, kind: int, index: int) -> int:
        """Return the absolute offset of one section row."""

        offset, row_size, rows = self.sections[kind]
        if index >= rows:
            raise IndexError(index)
        return offset + index * row_size

    def count(self, kind: int) -> int:
        """Return one section row count."""

        return self.sections[kind][2]

    def seal(self) -> bytes:
        """Recompute the envelope payload SHA-256 after hostile mutation."""

        payload = self.data[
            self.payload : self.payload + self.payload_length
        ]
        self.data[72:104] = hashlib.sha256(payload).digest()
        return bytes(self.data)


def emit(compiler: Path, source: Path, output: Path) -> subprocess.CompletedProcess[str]:
    """Compile one source fixture into a canonical artifact."""

    return subprocess.run(
        [str(compiler), "--emit-artifact", str(output), str(source)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def verify(verifier: Path, artifact: Path) -> subprocess.CompletedProcess[str]:
    """Run the standalone verifier executable."""

    return subprocess.run(
        [str(verifier), str(artifact)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def find_opcode(artifact: Artifact, opcode: int) -> int:
    """Find the first instruction row with one opcode."""

    for index in range(artifact.count(9)):
        if artifact.data[artifact.row(9, index)] == opcode:
            return index
    raise AssertionError(f"opcode 0x{opcode:02x} absent")


def instruction_function(artifact: Artifact, instruction: int) -> int:
    """Resolve instruction -> block -> function."""

    block = u32(artifact.data, artifact.row(9, instruction) + 8)
    return u32(artifact.data, artifact.row(6, block) + 4)


def slot_type(artifact: Artifact, slot: int) -> int:
    """Read one slot type ID."""

    return u32(artifact.data, artifact.row(8, slot) + 8)


def type_id_named(artifact: Artifact, spelling: str) -> int:
    """Resolve a type-table row by its canonical name bytes."""

    expected = spelling.encode("utf-8")
    for index in range(artifact.count(1)):
        row = artifact.row(1, index)
        length = u32(artifact.data, row + 56)
        if bytes(artifact.data[row + 60 : row + 60 + length]) == expected:
            return index
    raise AssertionError(f"type {spelling} absent")


def mutate_invalid_opcode(artifact: Artifact) -> None:
    """Replace one valid opcode with an unknown ISA byte."""

    artifact.data[artifact.row(9, 0)] = 0xFF


def mutate_instruction_boundary(artifact: Artifact) -> None:
    """Make a block begin outside the instruction table."""

    p32(artifact.data, artifact.row(6, 0) + 8, artifact.count(9))


def mutate_branch_target(artifact: Artifact) -> None:
    """Replace a direct branch target with a non-block ID."""

    instruction = find_opcode(artifact, 0x16)
    p32(artifact.data, artifact.row(9, instruction) + 20, INVALID - 1)


def mutate_fallthrough_terminal(artifact: Artifact) -> None:
    """Turn a block terminal into a non-terminal MOVE."""

    instruction = find_opcode(artifact, 0x17)
    artifact.data[artifact.row(9, instruction)] = 0x07


def mutate_direct_call_target(artifact: Artifact) -> None:
    """Replace a direct-call target with a non-function ID."""

    instruction = find_opcode(artifact, 0x13)
    p32(artifact.data, artifact.row(9, instruction) + 20, artifact.count(5))


def mutate_uninitialized_operand(artifact: Artifact) -> None:
    """Use a same-typed slot whose definition occurs later."""

    instructions: list[tuple[int, int, int]] = []
    for index in range(artifact.count(9)):
        row = artifact.row(9, index)
        result = u32(artifact.data, row + 12)
        if result != INVALID:
            instructions.append((index, result, instruction_function(artifact, index)))
    for use in range(artifact.count(9)):
        row = artifact.row(9, use)
        operand_count = u16(artifact.data, row + 2)
        if operand_count == 0:
            continue
        operand_start = u32(artifact.data, row + 16)
        current = u32(artifact.data, artifact.row(10, operand_start))
        current_type = slot_type(artifact, current)
        function = instruction_function(artifact, use)
        for definition, candidate, owner in instructions:
            if (
                definition > use
                and owner == function
                and slot_type(artifact, candidate) == current_type
            ):
                p32(artifact.data, artifact.row(10, operand_start), candidate)
                return
    raise AssertionError("no same-typed later definition for hostile test")


def mutate_frame_offset(artifact: Artifact) -> None:
    """Move one slot away from its independently derived frame offset."""

    row = artifact.row(8, 0)
    p32(artifact.data, row + 12, u32(artifact.data, row + 12) + 1)


def mutate_operand_type(artifact: Artifact) -> None:
    """Feed a non-bool same-function slot to BRANCH."""

    instruction = find_opcode(artifact, 0x16)
    function = instruction_function(artifact, instruction)
    function_row = artifact.row(5, function)
    first_slot = u32(artifact.data, function_row + 24)
    slot_count = u32(artifact.data, function_row + 28)
    replacement = None
    for slot in range(first_slot, first_slot + slot_count):
        type_row = artifact.row(1, slot_type(artifact, slot))
        if u16(artifact.data, type_row + 4) != 3:
            replacement = slot
            break
    if replacement is None:
        raise AssertionError("branch function has no non-bool slot")
    operand_start = u32(artifact.data, artifact.row(9, instruction) + 16)
    p32(artifact.data, artifact.row(10, operand_start), replacement)


def mutate_constant_index(artifact: Artifact) -> None:
    """Make a constant-producing instruction index outside the pool."""

    instruction = find_opcode(artifact, 0x06)
    p32(artifact.data, artifact.row(9, instruction) + 20, artifact.count(3))


def mutate_result_slot_type(artifact: Artifact) -> None:
    """Redirect MOVE to a same-function slot with an incompatible type."""

    instruction = find_opcode(artifact, 0x07)
    row = artifact.row(9, instruction)
    function = instruction_function(artifact, instruction)
    operand_start = u32(artifact.data, row + 16)
    operand = u32(artifact.data, artifact.row(10, operand_start))
    expected = slot_type(artifact, operand)
    function_row = artifact.row(5, function)
    first_slot = u32(artifact.data, function_row + 24)
    slot_count = u32(artifact.data, function_row + 28)
    for slot in range(first_slot, first_slot + slot_count):
        if slot_type(artifact, slot) != expected:
            p32(artifact.data, row + 12, slot)
            return
    raise AssertionError("MOVE function has no differently typed slot")


def mutate_constant_hash(artifact: Artifact) -> None:
    """Corrupt compiler-provided constant metadata while retaining payload hash."""

    row = artifact.row(3, 0)
    p64(artifact.data, row + 16, u64(artifact.data, row + 16) ^ 1)


def mutate_stack_metadata(artifact: Artifact) -> None:
    """Lie about a function maximum stack size."""

    row = artifact.row(5, 0)
    p64(artifact.data, row + 80, u64(artifact.data, row + 80) + 1)


def mutate_schema_digest(artifact: Artifact) -> None:
    """Bind the artifact to a schema identity other than the selected product."""

    artifact.data[artifact.payload + 96] ^= 1


def mutate_section_bounds(artifact: Artifact) -> None:
    """Move the type section outside the payload while preserving its hash."""

    descriptor = artifact.payload + 160
    p64(
        artifact.data,
        descriptor + 8,
        artifact.payload_length + 8,
    )


def mutate_struct_member_ordinal(artifact: Artifact) -> None:
    """Invalidate the declaration-order ordinal of a user struct member."""

    for index in range(artifact.count(9)):
        row = artifact.row(9, index)
        if artifact.data[row] == 0x0E and u32(
            artifact.data, row + 24
        ) != INVALID:
            p32(artifact.data, row + 24, INVALID)
            return
    raise AssertionError("user struct MEMBER absent")


def mutate_enum_tag_width(artifact: Artifact) -> None:
    """Make one enum variant tag exceed the one-byte runtime encoding."""

    for index in range(artifact.count(2)):
        row = artifact.row(2, index)
        if u32(artifact.data, row + 4) == 1:
            p32(artifact.data, row + 12, 256)
            return
    raise AssertionError("enum variant shape absent")


def mutate_reachable_capability(artifact: Artifact) -> None:
    """Make header/function capability metadata omit one reachable helper."""

    entry = u32(artifact.data, artifact.payload + 28)
    function = artifact.row(5, entry)
    required = u32(artifact.data, function + 44)
    reduced = required & (required - 1)

    if required == 0 or reduced == required:
        raise AssertionError("entry has no removable capability")
    p32(artifact.data, function + 44, reduced)
    p32(artifact.data, artifact.payload + 44, reduced)


def mutate_instruction_upper_bound(artifact: Artifact) -> None:
    """Make compiler/header instruction closure agree on a false bound."""

    entry = u32(artifact.data, artifact.payload + 28)
    function = artifact.row(5, entry)
    claimed = u64(artifact.data, function + 56) + 1

    if claimed > u64(artifact.data, function + 48):
        raise AssertionError("no instruction budget headroom")
    p64(artifact.data, function + 56, claimed)
    p64(artifact.data, artifact.payload + 56, claimed)


def mutate_helper_upper_bound(artifact: Artifact) -> None:
    """Make compiler/header helper closure agree on a false bound."""

    entry = u32(artifact.data, artifact.payload + 28)
    function = artifact.row(5, entry)
    claimed = u64(artifact.data, function + 72) + 1

    if claimed > u64(artifact.data, function + 64):
        raise AssertionError("no helper budget headroom")
    p64(artifact.data, function + 72, claimed)
    p64(artifact.data, artifact.payload + 72, claimed)


def mutate_helper_specific_bound(artifact: Artifact) -> None:
    """Corrupt one helper-specific worst-path closure row."""

    if artifact.count(12) == 0:
        raise AssertionError("helper bound row absent")
    row = artifact.row(12, 0)
    p64(artifact.data, row + 8, u64(artifact.data, row + 8) + 1)


def mutate_opaque_forgery(artifact: Artifact) -> None:
    """Retype an unused ImageId symbol as an equally sized Image handle."""

    image_id = type_id_named(artifact, "ImageId")
    image = type_id_named(artifact, "Image")

    for index in range(artifact.count(9)):
        row = artifact.row(9, index)
        result = u32(artifact.data, row + 12)
        if (
            artifact.data[row] == 0x06
            and result != INVALID
            and slot_type(artifact, result) == image_id
        ):
            p32(artifact.data, artifact.row(8, result) + 8, image)
            for candidate in range(artifact.count(9)):
                move = artifact.row(9, candidate)
                if artifact.data[move] != 0x07:
                    continue
                operand_start = u32(artifact.data, move + 16)
                operand = u32(
                    artifact.data,
                    artifact.row(10, operand_start),
                )
                if operand == result:
                    moved = u32(artifact.data, move + 12)
                    p32(
                        artifact.data,
                        artifact.row(8, moved) + 8,
                        image,
                    )
            return
    raise AssertionError("unused ImageId symbol absent")


Mutation = tuple[str, Callable[[Artifact], None], str]


MUTATIONS: tuple[Mutation, ...] = (
    ("invalid-opcode", mutate_invalid_opcode, "structural-error"),
    ("instruction-boundary", mutate_instruction_boundary, "invalid-block"),
    ("branch-target", mutate_branch_target, "invalid-target"),
    ("fallthrough-terminal", mutate_fallthrough_terminal, "invalid-block"),
    ("direct-call-target", mutate_direct_call_target, "invalid-target"),
    ("uninitialized-slot", mutate_uninitialized_operand, "uninitialized-slot"),
    ("frame-offset", mutate_frame_offset, "frame-mismatch"),
    ("operand-type", mutate_operand_type, "type-mismatch"),
    ("constant-index", mutate_constant_index, "type-mismatch"),
    ("result-type", mutate_result_slot_type, "type-mismatch"),
    ("constant-hash", mutate_constant_hash, "invalid-constant"),
    ("stack-metadata", mutate_stack_metadata, "resource-mismatch"),
    ("schema-digest", mutate_schema_digest, "schema-mismatch"),
    ("section-bounds", mutate_section_bounds, "structural-error"),
    (
        "reachable-capability",
        mutate_reachable_capability,
        "capability-mismatch",
    ),
    (
        "instruction-upper",
        mutate_instruction_upper_bound,
        "resource-mismatch",
    ),
    ("helper-upper", mutate_helper_upper_bound, "resource-mismatch"),
    (
        "helper-specific-bound",
        mutate_helper_specific_bound,
        "resource-mismatch",
    ),
)


SOURCE_HOSTILES: tuple[tuple[str, str], ...] = (
    ("duplicate_verified_image.rbs", "ownership-violation"),
    ("fault_after_boot_action.rbs", "fail-closed-violation"),
    ("no_success_action.rbs", "terminal-action-violation"),
)


def main(argv: list[str]) -> int:
    """Verify the corpus and require fail-closed hostile mutations."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    args = parser.parse_args(argv)
    failures: list[str] = []
    positive = 0
    hostile = 0

    with tempfile.TemporaryDirectory(prefix="ribos-verifier-") as directory:
        root = Path(directory)
        emitted: dict[str, bytes] = {}
        for fixture in sorted(CORPUS.glob("*.rbs")):
            output = root / f"{fixture.stem}.rba"
            compiled = emit(args.compiler, fixture, output)
            if compiled.returncode != 0:
                failures.append(
                    f"{fixture.name}: emission failed\n"
                    f"{compiled.stdout}{compiled.stderr}"
                )
                continue
            checked = verify(args.verifier, output)
            if (
                checked.returncode != 0
                or "RIBOS-VERIFIER-STAGE2-OK" not in checked.stdout
            ):
                failures.append(
                    f"{fixture.name}: verifier rejected positive artifact\n"
                    f"{checked.stdout}{checked.stderr}"
                )
            else:
                positive += 1
            emitted[fixture.name] = output.read_bytes()

        seed = emitted.get("policy_pipeline.rbs")
        if seed is None:
            failures.append("policy_pipeline.rbs: hostile seed unavailable")
        else:
            seed_path = root / "policy_pipeline.rba"
            workspace = subprocess.run(
                [
                    str(args.verifier),
                    "--self-test-workspace",
                    str(seed_path),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            if (
                workspace.returncode != 0
                or "RIBOS-VERIFIER-WORKSPACE-BOUND-OK"
                not in workspace.stdout
            ):
                failures.append(
                    "workspace-bound: short or unaligned probe failed\n"
                    f"{workspace.stdout}{workspace.stderr}"
                )
            for name, mutate, expected in MUTATIONS:
                artifact = Artifact(seed)
                try:
                    mutate(artifact)
                except AssertionError as error:
                    failures.append(f"{name}: mutation unavailable: {error}")
                    continue
                output = root / f"hostile-{name}.rba"
                output.write_bytes(artifact.seal())
                checked = verify(args.verifier, output)
                combined = checked.stdout + checked.stderr
                if (
                    checked.returncode == 0
                    or f"status={expected}" not in combined
                ):
                    failures.append(
                        f"{name}: expected {expected}, got\n{combined}"
                    )
                else:
                    hostile += 1

            aggregate_seed = emitted.get("aggregate_lowering.rbs")
            if aggregate_seed is None:
                failures.append(
                    "aggregate_lowering.rbs: member hostile seed unavailable"
                )
            else:
                aggregate_mutations: tuple[Mutation, ...] = (
                    (
                        "struct-member-ordinal",
                        mutate_struct_member_ordinal,
                        "type-mismatch",
                    ),
                    (
                        "enum-tag-width",
                        mutate_enum_tag_width,
                        "invalid-type",
                    ),
                )
                for name, mutate, expected in aggregate_mutations:
                    artifact = Artifact(aggregate_seed)
                    try:
                        mutate(artifact)
                    except AssertionError as error:
                        failures.append(
                            f"{name}: mutation unavailable: {error}"
                        )
                        continue
                    output = root / f"hostile-{name}.rba"
                    output.write_bytes(artifact.seal())
                    checked = verify(args.verifier, output)
                    combined = checked.stdout + checked.stderr
                    if (
                        checked.returncode == 0
                        or f"status={expected}" not in combined
                    ):
                        failures.append(
                            f"{name}: expected {expected}, got\n{combined}"
                        )
                    else:
                        hostile += 1

        structural = bytearray(seed or b"")
        if structural:
            p64(structural, 32, u64(structural, 32) + 1)
            output = root / "hostile-header-bounds.rba"
            output.write_bytes(structural)
            checked = verify(args.verifier, output)
            combined = checked.stdout + checked.stderr
            if (
                checked.returncode == 0
                or "status=structural-error" not in combined
            ):
                failures.append(
                    "header-bounds: expected structural-error, got\n"
                    f"{combined}"
                )
            else:
                hostile += 1

        for source_name, expected in SOURCE_HOSTILES:
            source = HOSTILE_SOURCE / source_name
            output = root / f"source-hostile-{source.stem}.rba"
            compiled = emit(args.compiler, source, output)
            if compiled.returncode != 0:
                failures.append(
                    f"{source_name}: hostile source emission failed\n"
                    f"{compiled.stdout}{compiled.stderr}"
                )
                continue
            checked = verify(args.verifier, output)
            combined = checked.stdout + checked.stderr
            if (
                checked.returncode == 0
                or f"status={expected}" not in combined
            ):
                failures.append(
                    f"{source_name}: expected {expected}, got\n{combined}"
                )
            else:
                hostile += 1

        opaque_source = HOSTILE_SOURCE / "opaque_forgery_seed.rbs"
        opaque_output = root / "opaque-forgery-seed.rba"
        compiled = emit(args.compiler, opaque_source, opaque_output)
        if compiled.returncode != 0:
            failures.append(
                "opaque-forgery: seed emission failed\n"
                f"{compiled.stdout}{compiled.stderr}"
            )
        else:
            opaque = Artifact(opaque_output.read_bytes())
            try:
                mutate_opaque_forgery(opaque)
            except AssertionError as error:
                failures.append(
                    f"opaque-forgery: mutation unavailable: {error}"
                )
            else:
                output = root / "hostile-opaque-forgery.rba"
                output.write_bytes(opaque.seal())
                checked = verify(args.verifier, output)
                combined = checked.stdout + checked.stderr
                if (
                    checked.returncode == 0
                    or "status=opaque-forgery" not in combined
                ):
                    failures.append(
                        "opaque-forgery: expected opaque-forgery, "
                        f"got\n{combined}"
                    )
                else:
                    hostile += 1

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        "RIBOS-VERIFIER-CORPUS-OK "
        f"positive={positive} hostile={hostile} "
        "compiler-trusted=0 workspace=caller-owned stage=2"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
