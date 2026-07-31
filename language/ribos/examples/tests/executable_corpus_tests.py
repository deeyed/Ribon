#!/usr/bin/env python3
"""Close every public Ribos example through the independent host VM path."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


RIBOS = Path(__file__).resolve().parents[2]
EXAMPLES = RIBOS / "examples"
REPOSITORY = RIBOS.parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(RIBOS / "host" / "tests"))

from fixture_codec import (  # noqa: E402
    Artifact,
    RESULT_VALUE,
    TranscriptRow,
    context_fixture,
    parse_report,
    run_policy,
    transcript_fixture,
)


def run(argv: list[str], *, expected: int = 0) -> subprocess.CompletedProcess[str]:
    """Run one bounded host tool invocation without a shell."""

    result = subprocess.run(
        argv,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=15,
    )
    if result.returncode != expected:
        raise AssertionError(
            f"command failed rc={result.returncode} expected={expected}: {argv}\n"
            f"stdout={result.stdout}stderr={result.stderr}"
        )
    return result


def sha256(data: bytes) -> str:
    """Return one lower-case SHA-256 spelling."""

    return hashlib.sha256(data).hexdigest()


def summary(output: str, prefix: str) -> dict[str, str]:
    """Parse a single key=value summary line."""

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


def extract_documented_examples(document: Path) -> dict[str, bytes]:
    """Extract only explicitly tagged executable Ribos Markdown blocks."""

    text = document.read_text(encoding="utf-8")
    pattern = re.compile(
        r"<!-- ribos-executable: ([a-z0-9_]+) -->\n"
        r"```(?:text|ribos)\n(.*?)```",
        flags=re.DOTALL,
    )
    extracted: dict[str, bytes] = {}
    for match in pattern.finditer(text):
        identifier = match.group(1)
        if identifier in extracted:
            raise AssertionError(f"duplicate documented example {identifier}")
        body = match.group(2)
        if not body.endswith("\n"):
            raise AssertionError(f"documented example {identifier} lacks final LF")
        extracted[identifier] = body.encode("utf-8")
    return extracted


def expected_rows(
    artifact: Artifact,
    terminal_helper: int | None,
) -> tuple[TranscriptRow, ...]:
    """Build the only transcript shape allowed by the public v1 corpus."""

    if terminal_helper is None:
        return ()
    if terminal_helper != 22:
        raise AssertionError(f"unsupported corpus terminal helper {terminal_helper}")
    _, action_size = artifact.type_by_name("BootAction")
    return (
        TranscriptRow(
            helper_id=terminal_helper,
            result_kind=RESULT_VALUE,
            payload=bytes([0x5A]) * action_size,
        ),
    )


def require_resource_contract(artifact: Artifact, entry: dict[str, object]) -> None:
    """Compare the artifact header with the reviewed manifest bounds."""

    declared = struct.unpack_from("<I", artifact.payload, 40)[0]
    required = struct.unpack_from("<I", artifact.payload, 44)[0]
    actual = {
        "declared_capabilities": f"0x{declared:08x}",
        "required_capabilities": f"0x{required:08x}",
        "instruction_upper": artifact.instruction_upper,
        "helper_upper": artifact.helper_upper,
        "stack_upper": artifact.stack_upper,
        "call_depth_upper": artifact.call_depth_upper,
    }
    for key, value in actual.items():
        if entry[key] != value:
            raise AssertionError(
                f"{entry['id']}: {key} changed: expected={entry[key]} actual={value}"
            )


def require_replay(
    runner: Path,
    artifact_path: Path,
    artifact: Artifact,
    entry: dict[str, object],
    directory: Path,
) -> None:
    """Replay four times and bind the report to reviewed closure values."""

    identifier = str(entry["id"])
    context_bytes = context_fixture(artifact)
    context_path = directory / f"{identifier}.rbctx"
    transcript_path = directory / f"{identifier}.rbtr"
    context_path.write_bytes(context_bytes)
    transcript_path.write_bytes(
        transcript_fixture(
            artifact,
            context_bytes,
            expected_rows(artifact, entry["terminal_helper"]),
        )
    )
    results = [
        run_policy(
            runner,
            artifact_path,
            context_path,
            transcript_path,
            check=False,
        )
        for _ in range(4)
    ]
    if any(result.returncode != 0 for result in results):
        raise AssertionError(
            f"{identifier}: VM replay failed: "
            + "".join(result.stderr for result in results)
        )
    if any(result.stdout != results[0].stdout for result in results[1:]):
        raise AssertionError(f"{identifier}: VM replay report is nondeterministic")
    report = parse_report(results[0].stdout)
    if report["outcome"] != entry["expected_outcome"]:
        raise AssertionError(
            f"{identifier}: expected {entry['expected_outcome']}, "
            f"got {report['outcome']}"
        )
    report_bounds = {
        "instructions.upper": entry["instruction_upper"],
        "helpers.upper": entry["helper_upper"],
        "stack.upper": entry["stack_upper"],
        "call-depth.upper": entry["call_depth_upper"],
    }
    for key, expected in report_bounds.items():
        if int(report[key]) != expected:
            raise AssertionError(
                f"{identifier}: runtime {key} changed: "
                f"expected={expected} actual={report[key]}"
            )
    if int(report["instructions.actual"]) > int(report["instructions.upper"]):
        raise AssertionError(f"{identifier}: instruction closure was exceeded")
    if int(report["helpers.actual"]) > int(report["helpers.upper"]):
        raise AssertionError(f"{identifier}: helper closure was exceeded")
    if report["transcript.rows"] != report["transcript.consumed"]:
        raise AssertionError(f"{identifier}: transcript was not consumed exactly")


def main() -> int:
    """Require source, docs, compiler, verifier and VM agreement."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--verifier", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(
        (EXAMPLES / "manifest.json").read_text(encoding="utf-8")
    )
    if manifest.get("format") != "ribon.ribos.executable-corpus.v1":
        raise AssertionError("unexpected executable corpus manifest format")
    schema_sha256 = manifest["schema_sha256"]
    if not re.fullmatch(r"[0-9a-f]{64}", schema_sha256):
        raise AssertionError("manifest schema digest is not canonical SHA-256")
    entries = manifest["entries"]
    identifiers = [entry["id"] for entry in entries]
    if identifiers != sorted(identifiers) or len(set(identifiers)) != len(identifiers):
        raise AssertionError("manifest IDs must be unique and sorted")
    documented = extract_documented_examples(
        REPOSITORY / manifest["documentation"]
    )
    tracked = {
        str(path.relative_to(EXAMPLES))
        for path in (EXAMPLES / "executable").glob("*.rbs")
    }
    declared = {entry["source"] for entry in entries}
    if tracked != declared:
        raise AssertionError(
            f"manifest is not exhaustive: tracked={sorted(tracked)} "
            f"declared={sorted(declared)}"
        )
    if set(documented) != set(identifiers):
        raise AssertionError(
            f"documentation markers differ: documented={sorted(documented)} "
            f"manifest={identifiers}"
        )

    with tempfile.TemporaryDirectory(prefix="ribos-executable-corpus-") as temporary:
        directory = Path(temporary)
        for entry in entries:
            identifier = entry["id"]
            source = EXAMPLES / entry["source"]
            source_bytes = source.read_bytes()
            if sha256(source_bytes) != entry["source_sha256"]:
                raise AssertionError(f"{identifier}: source digest changed")
            if documented[identifier] != source_bytes:
                raise AssertionError(f"{identifier}: documentation block drifted")

            parser_result = run([str(args.compiler), str(source)])
            if "RIBOS-PARSER-PILOT-OK" not in parser_result.stdout:
                raise AssertionError(f"{identifier}: parser marker missing")
            semantic_result = run([str(args.compiler), "--check", str(source)])
            compiler_fields = summary(
                semantic_result.stdout,
                "RIBOS-COMPILER-OK",
            )
            for mode in ("--dump-ir", "--dump-resources"):
                first = run([str(args.compiler), mode, str(source)])
                second = run([str(args.compiler), mode, str(source)])
                if first.stdout != second.stdout:
                    raise AssertionError(
                        f"{identifier}: {mode} output is nondeterministic"
                    )

            first_path = directory / f"{identifier}-a.rba"
            second_path = directory / f"{identifier}-b.rba"
            first_emit = run(
                [str(args.compiler), "--emit-artifact", str(first_path), str(source)]
            )
            run(
                [str(args.compiler), "--emit-artifact", str(second_path), str(source)]
            )
            first_bytes = first_path.read_bytes()
            if first_bytes != second_path.read_bytes():
                raise AssertionError(
                    f"{identifier}: artifact bytes are nondeterministic"
                )
            artifact = Artifact(first_bytes)
            if artifact.payload[96:128].hex() != schema_sha256:
                raise AssertionError(
                    f"{identifier}: selected product schema digest changed"
                )
            if len(first_bytes) != entry["artifact_bytes"]:
                raise AssertionError(f"{identifier}: artifact length changed")
            if sha256(first_bytes) != entry["artifact_sha256"]:
                raise AssertionError(f"{identifier}: artifact digest changed")
            if artifact.artifact_hash.hex() != entry["payload_sha256"]:
                raise AssertionError(f"{identifier}: payload digest changed")
            emitted = summary(
                first_emit.stdout,
                "RIBOS-ARTIFACT-EMIT-OK",
            )
            if emitted["sha256"] != entry["payload_sha256"]:
                raise AssertionError(f"{identifier}: compiler payload digest disagrees")
            require_resource_contract(artifact, entry)

            verifier_result = run([str(args.verifier), str(first_path)])
            verifier_fields = summary(
                verifier_result.stdout,
                "RIBOS-VERIFIER-STAGE2-OK",
            )
            comparisons = (
                ("instruction-upper", "instruction_upper"),
                ("helper-upper", "helper_upper"),
                ("entry-stack", "stack_upper"),
                ("call-depth", "call_depth_upper"),
            )
            for field, manifest_field in comparisons:
                values = (
                    int(
                        compiler_fields[
                            field if field != "entry-stack" else "stack-bytes"
                        ]
                    ),
                    int(verifier_fields[field]),
                    entry[manifest_field],
                )
                if values[0] != values[1] or values[1] != values[2]:
                    raise AssertionError(
                        f"{identifier}: {manifest_field} differs across "
                        f"stages: {values}"
                    )
            require_replay(
                args.runner,
                first_path,
                artifact,
                entry,
                directory,
            )

    print(
        "RIBOS-EXECUTABLE-CORPUS-OK "
        f"examples={len(entries)} docs-drift=0 parse=ok semantic=ok "
        "ir=deterministic artifact=deterministic verifier=independent "
        "vm=terminal repeats=4 evidence=host-only"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
