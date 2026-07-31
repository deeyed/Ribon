#!/usr/bin/env python3
"""End-to-end deterministic replay tests for the production Ribos VM."""

from __future__ import annotations

import argparse
import hashlib
import tempfile
from pathlib import Path

from fixture_codec import (
    Artifact,
    JOURNAL_COMMITTED,
    RESULT_VALUE,
    TranscriptRow,
    compile_policy,
    context_fixture,
    parse_report,
    run_policy,
    transcript_fixture,
)


def write_bound_fixtures(
    artifact_path: Path,
    context_path: Path,
    transcript_path: Path,
    rows: tuple[TranscriptRow, ...],
) -> Artifact:
    artifact = Artifact.from_path(artifact_path)
    context = context_fixture(artifact)
    context_path.write_bytes(context)
    transcript_path.write_bytes(
        transcript_fixture(artifact, context, rows)
    )
    return artifact


def repeat_and_require(
    runner: Path,
    artifact: Path,
    context: Path,
    transcript: Path,
    expected_outcome: str,
) -> dict[str, str]:
    runs = [
        run_policy(
            runner,
            artifact,
            context,
            transcript,
            check=False,
        )
        for _ in range(4)
    ]
    failed = next((run for run in runs if run.returncode != 0), None)
    if failed is not None:
        raise AssertionError(
            f"ribos-run failed rc={failed.returncode}: {failed.stderr}"
        )
    outputs = [run.stdout for run in runs]
    if any(output != outputs[0] for output in outputs[1:]):
        raise AssertionError("replay output changed across identical runs")
    report = parse_report(outputs[0])
    if report["outcome"] != expected_outcome:
        raise AssertionError(
            f"expected outcome {expected_outcome}, got {report['outcome']}"
        )
    if int(report["instructions.actual"]) > int(
        report["instructions.upper"]
    ):
        raise AssertionError("runtime instruction count exceeded verifier bound")
    if int(report["helpers.actual"]) > int(report["helpers.upper"]):
        raise AssertionError("runtime helper count exceeded verifier bound")
    if report["transcript.rows"] != report["transcript.consumed"]:
        raise AssertionError("transcript was not consumed exactly")
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    ribos = Path(__file__).resolve().parents[2]
    policies = ribos / "vm" / "tests"

    with tempfile.TemporaryDirectory(prefix="ribos-replay-") as temporary:
        output = Path(temporary)

        error_artifact = output / "error.rba"
        compile_policy(
            args.compiler,
            policies / "terminal_policy_error.rbs",
            error_artifact,
        )
        error_context = output / "error.rbctx"
        error_transcript = output / "error.rbtr"
        write_bound_fixtures(
            error_artifact,
            error_context,
            error_transcript,
            (),
        )
        repeat_and_require(
            args.runner,
            error_artifact,
            error_context,
            error_transcript,
            "policy-error",
        )

        action_artifact = output / "action.rba"
        compile_policy(
            args.compiler,
            policies / "runtime_storage.rbs",
            action_artifact,
        )
        action = Artifact.from_path(action_artifact)
        _, action_size = action.type_by_name("BootAction")
        action_context = output / "action.rbctx"
        action_transcript = output / "action.rbtr"
        action_rows = (
            TranscriptRow(
                helper_id=22,
                result_kind=RESULT_VALUE,
                payload=bytes([0x5A]) * action_size,
            ),
        )
        write_bound_fixtures(
            action_artifact,
            action_context,
            action_transcript,
            action_rows,
        )
        action_report = repeat_and_require(
            args.runner,
            action_artifact,
            action_context,
            action_transcript,
            "boot-action",
        )
        if action_report["helpers.actual"] != "1":
            raise AssertionError("terminal action helper count is not one")

        journal_artifact = output / "journal.rba"
        compile_policy(
            args.compiler,
            policies / "terminal_journal.rbs",
            journal_artifact,
        )
        journal = Artifact.from_path(journal_artifact)
        _, journal_action_size = journal.type_by_name("BootAction")
        journal_context = output / "journal.rbctx"
        journal_transcript = output / "journal.rbtr"
        journal_digest = hashlib.sha256(b"r16-journal-1").digest()
        journal_rows = (
            TranscriptRow(
                helper_id=1,
                payload=b"",
                journal_state=JOURNAL_COMMITTED,
                journal_digest=journal_digest,
            ),
            TranscriptRow(
                helper_id=22,
                payload=bytes([0xA5]) * journal_action_size,
            ),
        )
        write_bound_fixtures(
            journal_artifact,
            journal_context,
            journal_transcript,
            journal_rows,
        )
        journal_report = repeat_and_require(
            args.runner,
            journal_artifact,
            journal_context,
            journal_transcript,
            "boot-action",
        )
        if journal_report["terminal.journal.count"] != "1":
            raise AssertionError("journal receipt was not sealed once")

    print(
        "RIBOS-HOST-REPLAY-OK compiler=ribosc verifier=ribos-verify "
        "runner=ribos-run repeats=4 fixtures=3 evidence=host-only"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
