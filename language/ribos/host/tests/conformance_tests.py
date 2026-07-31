#!/usr/bin/env python3
"""Cross-layer compiler, verifier and production-VM conformance oracle."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

from fixture_codec import (
    Artifact,
    JOURNAL_COMMITTED,
    TranscriptRow,
    compile_policy,
    context_fixture,
    parse_report,
    run_policy,
    transcript_fixture,
)


def summary_line(output: str, prefix: str) -> dict[str, str]:
    line = next(
        (candidate for candidate in output.splitlines()
         if candidate.startswith(prefix)),
        None,
    )
    if line is None:
        raise AssertionError(f"missing summary {prefix!r}")
    fields: dict[str, str] = {}
    for token in line.split()[1:]:
        key, separator, value = token.partition("=")
        if separator:
            fields[key] = value
    return fields


def rows_for(artifact: Artifact, source_name: str) -> tuple[TranscriptRow, ...]:
    if source_name == "terminal_policy_error.rbs":
        return ()
    _, action_size = artifact.type_by_name("BootAction")
    action = TranscriptRow(
        helper_id=22,
        payload=bytes([0x5A]) * action_size,
    )
    if source_name == "terminal_journal.rbs":
        return (
            TranscriptRow(
                helper_id=1,
                payload=b"",
                journal_state=JOURNAL_COMMITTED,
                journal_digest=hashlib.sha256(
                    b"r16-conformance-journal"
                ).digest(),
            ),
            action,
        )
    return (action,)


def compile_verify_run(
    compiler: Path,
    verifier: Path,
    runner: Path,
    source: Path,
    directory: Path,
) -> tuple[Artifact, dict[str, str], dict[str, str]]:
    stem = source.stem
    artifact_path = directory / f"{stem}.rba"
    compiler_output = compile_policy(
        compiler,
        source,
        artifact_path,
    )
    compiler_summary = summary_line(
        compiler_output,
        "RIBOS-COMPILER-OK",
    )
    verifier_run = subprocess.run(
        [str(verifier), str(artifact_path)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
    )
    verifier_summary = summary_line(
        verifier_run.stdout,
        "RIBOS-VERIFIER-STAGE2-OK",
    )
    artifact = Artifact.from_path(artifact_path)
    context = context_fixture(artifact)
    context_path = directory / f"{stem}.rbctx"
    transcript_path = directory / f"{stem}.rbtr"
    context_path.write_bytes(context)
    transcript_path.write_bytes(
        transcript_fixture(
            artifact,
            context,
            rows_for(artifact, source.name),
        )
    )
    run = run_policy(
        runner,
        artifact_path,
        context_path,
        transcript_path,
    )
    report = parse_report(run.stdout)
    comparisons = (
        (
            compiler_summary["instruction-upper"],
            verifier_summary["instruction-upper"],
            report["instructions.upper"],
            "instruction upper bound",
        ),
        (
            compiler_summary["helper-upper"],
            verifier_summary["helper-upper"],
            report["helpers.upper"],
            "helper upper bound",
        ),
        (
            compiler_summary["stack-bytes"],
            verifier_summary["entry-stack"],
            report["stack.upper"],
            "stack upper bound",
        ),
        (
            compiler_summary["call-depth"],
            verifier_summary["call-depth"],
            report["call-depth.upper"],
            "call depth upper bound",
        ),
    )
    for compiler_value, verifier_value, runtime_value, label in comparisons:
        if not compiler_value == verifier_value == runtime_value:
            raise AssertionError(
                f"{source.name}: {label} differs: "
                f"compiler={compiler_value} verifier={verifier_value} "
                f"runtime={runtime_value}"
            )
    return artifact, compiler_summary, report


def semantic_report(report: dict[str, str]) -> dict[str, str]:
    ignored_prefixes = (
        "artifact.",
        "binding.",
        "transcript.",
        "source.",
        "outcome.receipt.",
        "terminal.trace.",
    )
    ignored = {"report.sha256"}
    return {
        key: value
        for key, value in report.items()
        if key not in ignored
        and not any(key.startswith(prefix) for prefix in ignored_prefixes)
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    ribos = Path(__file__).resolve().parents[2]
    vm_tests = ribos / "vm" / "tests"
    positive = (
        vm_tests / "scalar_interpreter.rbs",
        vm_tests / "calls_loops_interpreter.rbs",
        vm_tests / "aggregate_interpreter.rbs",
        vm_tests / "runtime_storage.rbs",
        vm_tests / "terminal_journal.rbs",
        vm_tests / "terminal_policy_error.rbs",
        vm_tests / "opcode_conformance.rbs",
    )
    negative = (
        ribos / "frontend" / "tests" / "negative" / "exception_try.rbs",
        ribos / "frontend" / "tests" / "negative" / "reserved_while.rbs",
        ribos
        / "frontend"
        / "tests"
        / "semantic"
        / "negative"
        / "capability_missing.rbs",
        ribos
        / "frontend"
        / "tests"
        / "semantic"
        / "negative"
        / "unbounded_iteration.rbs",
    )

    with tempfile.TemporaryDirectory(
        prefix="ribos-conformance-"
    ) as temporary:
        directory = Path(temporary)
        observed_opcodes: set[int] = set()
        for source in positive:
            artifact, _, _ = compile_verify_run(
                args.compiler,
                args.verifier,
                args.runner,
                source,
                directory,
            )
            observed_opcodes.update(artifact.opcodes())
        expected_opcodes = set(range(1, 0x19))
        missing = sorted(expected_opcodes - observed_opcodes)
        if missing:
            raise AssertionError(
                "positive corpus lacks opcode coverage: "
                + ", ".join(f"0x{opcode:02x}" for opcode in missing)
            )

        original_source = vm_tests / "runtime_storage.rbs"
        shifted_source = directory / "runtime_storage_shifted.rbs"
        shifted_source.write_text(
            "# diagnostic-only source position shift\n\n"
            + original_source.read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        mapped_artifact, _, mapped_report = compile_verify_run(
            args.compiler,
            args.verifier,
            args.runner,
            original_source,
            directory,
        )
        unmapped_artifact, _, unmapped_report = compile_verify_run(
            args.compiler,
            args.verifier,
            args.runner,
            shifted_source,
            directory,
        )
        if mapped_artifact.artifact_hash == unmapped_artifact.artifact_hash:
            raise AssertionError("source position shift did not alter artifact")
        if semantic_report(mapped_report) != semantic_report(unmapped_report):
            raise AssertionError("source map changed VM semantic report")

        for source in negative:
            artifact_path = directory / f"negative-{source.stem}.rba"
            result = subprocess.run(
                [
                    str(args.compiler),
                    "--emit-artifact",
                    str(artifact_path),
                    str(source),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=10,
            )
            if result.returncode != 2 or artifact_path.exists():
                raise AssertionError(
                    f"negative source was not fail-closed: {source.name}"
                )

    print(
        "RIBOS-HOST-CONFORMANCE-OK opcodes=24 positive=7 negative=4 "
        "resources=compiler-verifier-runtime source-map=diagnostic-only "
        "evidence=host-only"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
