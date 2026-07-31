#!/usr/bin/env python3
"""Bounded hostile-input and fail-closed tests for Ribos host tooling."""

from __future__ import annotations

import argparse
import hashlib
import random
import struct
import subprocess
import tempfile
from pathlib import Path

from fixture_codec import (
    Artifact,
    CALLBACK_CONTRACT_FAULT,
    JOURNAL_PARTIAL,
    RESULT_NONE,
    SECTION_INSTRUCTIONS,
    TranscriptRow,
    compile_policy,
    context_fixture,
    parse_report,
    run_policy,
    transcript_fixture,
)

SANITIZER_MARKERS = (
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "runtime error:",
    "LeakSanitizer",
)


def bounded_run(command: list[str], *, timeout: float = 3) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        command,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )
    if result.returncode < 0:
        raise AssertionError(
            f"tool terminated by signal {-result.returncode}: {command[0]}"
        )
    combined = result.stdout + result.stderr
    marker = next(
        (item for item in SANITIZER_MARKERS if item in combined),
        None,
    )
    if marker is not None:
        raise AssertionError(f"sanitizer reported {marker}")
    return result


def rewrite_transcript_body_digest(transcript: bytearray) -> None:
    transcript[136:168] = hashlib.sha256(transcript[192:]).digest()


def require_runner_reject(
    runner: Path,
    artifact: Path,
    context: Path,
    transcript: Path,
) -> None:
    result = bounded_run(
        [
            str(runner),
            "--context",
            str(context),
            "--transcript",
            str(transcript),
            str(artifact),
        ]
    )
    if result.returncode == 0:
        raise AssertionError(
            "hostile fixture was accepted: "
            f"artifact={artifact.name} context={context.name} "
            f"transcript={transcript.name}"
        )


def require_runner_safe(
    runner: Path,
    artifact: Path,
    context: Path,
    transcript: Path,
) -> bool:
    result = bounded_run(
        [
            str(runner),
            "--context",
            str(context),
            "--transcript",
            str(transcript),
            str(artifact),
        ]
    )
    if result.returncode == 0:
        report = parse_report(result.stdout)
        if report["transcript.rows"] != report["transcript.consumed"]:
            raise AssertionError("accepted fixture was not fully consumed")
        return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    ribos = Path(__file__).resolve().parents[2]
    policy = ribos / "vm" / "tests" / "terminal_journal.rbs"
    random_source = random.Random(0x5249424F53)

    with tempfile.TemporaryDirectory(prefix="ribos-hostile-") as temporary:
        directory = Path(temporary)
        artifact_path = directory / "policy.rba"
        context_path = directory / "context.rbctx"
        transcript_path = directory / "helpers.rbtr"
        compile_policy(args.compiler, policy, artifact_path)
        artifact = Artifact.from_path(artifact_path)
        context = context_fixture(artifact)
        context_path.write_bytes(context)
        _, action_size = artifact.type_by_name("BootAction")
        normal_rows = (
            TranscriptRow(
                helper_id=1,
                payload=b"",
                journal_state=1,
                journal_digest=hashlib.sha256(b"hostile-normal").digest(),
            ),
            TranscriptRow(
                helper_id=22,
                payload=bytes([0x5A]) * action_size,
            ),
        )
        transcript = transcript_fixture(artifact, context, normal_rows)
        transcript_path.write_bytes(transcript)

        fault_transcript_path = directory / "fault.rbtr"
        fault_transcript_path.write_bytes(
            transcript_fixture(
                artifact,
                context,
                (
                    TranscriptRow(
                        helper_id=1,
                        callback_status=CALLBACK_CONTRACT_FAULT,
                        result_kind=RESULT_NONE,
                        payload=b"",
                        journal_state=JOURNAL_PARTIAL,
                        journal_digest=hashlib.sha256(
                            b"hostile-partial"
                        ).digest(),
                    ),
                ),
            )
        )
        fault_outputs = [
            run_policy(
                args.runner,
                artifact_path,
                context_path,
                fault_transcript_path,
            ).stdout
            for _ in range(4)
        ]
        if any(item != fault_outputs[0] for item in fault_outputs[1:]):
            raise AssertionError("fault replay output is nondeterministic")
        fault_report = parse_report(fault_outputs[0])
        if (
            fault_report["outcome"] != "vm-fault"
            or fault_report["recovery.calls"] != "1"
            or fault_report["terminal.journal.state"] != str(JOURNAL_PARTIAL)
        ):
            raise AssertionError("partial journal fault was not fail-closed")

        inputs = (
            ("artifact", artifact_path, context_path, transcript_path),
            ("context", context_path, artifact_path, transcript_path),
            ("transcript", transcript_path, artifact_path, context_path),
        )
        mutation_count = 0
        valid_mutation_count = 0
        for kind, target, first_other, second_other in inputs:
            original = target.read_bytes()
            for index in range(24):
                mutated = bytearray(original)
                offset = random_source.randrange(len(mutated))
                mutated[offset] ^= 1 << random_source.randrange(8)
                mutated_path = directory / f"{kind}-mutation-{index}.bin"
                mutated_path.write_bytes(mutated)
                if kind == "artifact":
                    verifier_result = bounded_run(
                        [str(args.verifier), str(mutated_path)]
                    )
                    if verifier_result.returncode == 0:
                        raise AssertionError(
                            "single-byte artifact mutation was verified"
                        )
                    require_runner_reject(
                        args.runner,
                        mutated_path,
                        first_other,
                        second_other,
                    )
                elif kind == "context":
                    valid_mutation_count += require_runner_safe(
                        args.runner,
                        first_other,
                        mutated_path,
                        second_other,
                    )
                else:
                    valid_mutation_count += require_runner_safe(
                        args.runner,
                        first_other,
                        second_other,
                        mutated_path,
                    )
                mutation_count += 1

        for kind, original_path in (
            ("artifact", artifact_path),
            ("context", context_path),
            ("transcript", transcript_path),
        ):
            original = original_path.read_bytes()
            for length in (0, 1, 7, len(original) // 2, len(original) - 1):
                truncated = directory / f"{kind}-truncated-{length}.bin"
                truncated.write_bytes(original[:length])
                if kind == "artifact":
                    bounded_run([str(args.verifier), str(truncated)])
                    require_runner_reject(
                        args.runner,
                        truncated,
                        context_path,
                        transcript_path,
                    )
                elif kind == "context":
                    require_runner_reject(
                        args.runner,
                        artifact_path,
                        truncated,
                        transcript_path,
                    )
                else:
                    require_runner_reject(
                        args.runner,
                        artifact_path,
                        context_path,
                        truncated,
                    )

        coherent_artifact = bytearray(artifact_path.read_bytes())
        instruction_section = artifact.section(SECTION_INSTRUCTIONS)
        opcode_offset = (
            artifact.payload_offset + instruction_section.offset
        )
        coherent_artifact[opcode_offset] = 0
        payload = coherent_artifact[
            artifact.payload_offset :
            artifact.payload_offset + artifact.payload_length
        ]
        coherent_hash = hashlib.sha256(payload).digest()
        coherent_artifact[72:104] = coherent_hash
        coherent_artifact_path = directory / "coherent-invalid-opcode.rba"
        coherent_artifact_path.write_bytes(coherent_artifact)
        coherent_transcript = bytearray(transcript)
        coherent_transcript[72:104] = coherent_hash
        coherent_transcript_path = directory / "coherent-invalid-opcode.rbtr"
        coherent_transcript_path.write_bytes(coherent_transcript)
        if bounded_run(
            [str(args.verifier), str(coherent_artifact_path)]
        ).returncode == 0:
            raise AssertionError("invalid opcode with valid hash was verified")
        require_runner_reject(
            args.runner,
            coherent_artifact_path,
            context_path,
            coherent_transcript_path,
        )

        wrong_helper = bytearray(transcript)
        struct.pack_into("<I", wrong_helper, 192 + 8, 0x7FFFFFFE)
        rewrite_transcript_body_digest(wrong_helper)
        wrong_helper_path = directory / "wrong-helper.rbtr"
        wrong_helper_path.write_bytes(wrong_helper)
        require_runner_reject(
            args.runner,
            artifact_path,
            context_path,
            wrong_helper_path,
        )

    print(
        "RIBOS-HOST-HOSTILE-OK deterministic-fault-repeats=4 "
        f"mutations={mutation_count} truncations=15 coherent=2 "
        f"well-formed-mutations={valid_mutation_count} "
        "timeouts=bounded sanitizer-markers=clean evidence=host-only"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
