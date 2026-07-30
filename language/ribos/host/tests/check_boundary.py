#!/usr/bin/env python3
"""Enforce the Ribos host compiler and target runtime object-graph boundary."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[4]
TARGET_ROOTS = (
    ROOT / "language/ribos/base/include",
    ROOT / "language/ribos/base/src",
    ROOT / "language/ribos/schema/include",
    ROOT / "language/ribos/schema/src",
    ROOT / "language/ribos/artifact/include",
    ROOT / "language/ribos/artifact/src",
    ROOT / "language/ribos/vm/include",
    ROOT / "language/ribos/vm/src",
)
LEGACY_HOST_PATHS = (
    ROOT / "language/ribos/artifact/src/emitter.c",
    ROOT / "language/ribos/artifact/include/ribos/artifact/emitter.h",
    ROOT / "language/ribos/frontend/tools/parse.c",
    ROOT / "language/ribos/frontend/tools/generate_parser.py",
    ROOT / "language/ribos/frontend/tools/check_parser_snapshot.py",
    ROOT / "language/ribos/vm/tools/verify.c",
)
SOURCE_BANS = (
    re.compile(r"#\s*include\s*<stdio\.h>"),
    re.compile(r"#\s*include\s*<stdlib\.h>"),
    re.compile(r"\bFILE\b"),
    re.compile(r"\b(?:malloc|calloc|realloc|free|qsort)\s*\("),
    re.compile(r'["<]ribos/(?:frontend|host|ir)/'),
)
UNDEFINED_SYMBOL_BANS = re.compile(
    r"(?:^|_)(?:malloc|calloc|realloc|free|qsort|"
    r"fopen|fclose|fprintf|vfprintf|snprintf|vsnprintf)$"
)


def command(*arguments: str) -> str:
    completed = subprocess.run(
        arguments,
        cwd=ROOT,
        check=True,
        text=True,
        capture_output=True,
    )
    return completed.stdout


def scan_target_sources() -> list[str]:
    failures: list[str] = []
    for root in TARGET_ROOTS:
        for path in sorted(root.rglob("*")):
            if path.suffix not in {".c", ".h"}:
                continue
            text = path.read_text(encoding="utf-8")
            for pattern in SOURCE_BANS:
                match = pattern.search(text)
                if match is not None:
                    relative = path.relative_to(ROOT)
                    failures.append(
                        f"{relative}:{text.count(chr(10), 0, match.start()) + 1}:"
                        f" forbidden={match.group(0)!r}"
                    )
    for path in LEGACY_HOST_PATHS:
        if path.exists():
            failures.append(
                f"{path.relative_to(ROOT)}: legacy host path still exists"
            )
    return failures


def inspect_archives(
    target_archive: Path,
    host_compiler_archive: Path,
) -> list[str]:
    failures: list[str] = []
    target_members = command("ar", "-t", str(target_archive)).splitlines()
    host_members = command("ar", "-t", str(host_compiler_archive)).splitlines()

    forbidden_target_members = {
        "artifact_emitter.o",
        "lexer.o",
        "runtime.o",
        "ast.o",
        "parser.o",
        "compiler.o",
        "semantic.o",
        "lower.o",
        "frontend_dump.o",
        "generated_parser.o",
        "ir_module.o",
        "ir_dump.o",
        "ir_analysis.o",
    }
    leaked = sorted(forbidden_target_members.intersection(target_members))
    if leaked:
        failures.append(
            "target archive contains host compiler members: " + ", ".join(leaked)
        )

    required_target_members = {
        "base_allocator.o",
        "base_writer.o",
        "schema.o",
        "artifact_wire.o",
        "artifact_sha256.o",
        "artifact_codec.o",
        "verifier.o",
        "prepared.o",
    }
    missing_target = sorted(required_target_members.difference(target_members))
    if missing_target:
        failures.append(
            "target archive is missing members: " + ", ".join(missing_target)
        )

    required_host_members = {
        "artifact_emitter.o",
        "lexer.o",
        "parser.o",
        "compiler.o",
        "semantic.o",
        "lower.o",
        "generated_parser.o",
        "ir_module.o",
        "ir_analysis.o",
    }
    missing_host = sorted(required_host_members.difference(host_members))
    if missing_host:
        failures.append(
            "host compiler archive is missing members: " + ", ".join(missing_host)
        )

    undefined = command("nm", "-u", str(target_archive))
    for line in undefined.splitlines():
        symbol = line.strip().split()[-1] if line.strip() else ""
        if UNDEFINED_SYMBOL_BANS.search(symbol):
            failures.append(f"target archive imports hosted symbol: {symbol}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target-archive", type=Path, required=True)
    parser.add_argument("--host-compiler-archive", type=Path, required=True)
    arguments = parser.parse_args()

    failures = scan_target_sources()
    failures.extend(
        inspect_archives(
            arguments.target_archive.resolve(),
            arguments.host_compiler_archive.resolve(),
        )
    )
    if failures:
        for failure in failures:
            print(f"RIBOS-HOST-BOUNDARY-FAIL {failure}")
        return 1
    print(
        "RIBOS-HOST-BOUNDARY-OK "
        "target=freestanding host-compiler=isolated legacy-paths=absent"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
