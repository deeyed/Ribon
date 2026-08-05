#!/usr/bin/env python3
"""Resolve one LLVM tool without embedding a package-manager installation path."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


TOOLS = ("clang", "ld.lld", "lld-link", "llvm-objcopy", "llvm-ar")


def command_output(arguments: list[str]) -> str:
    """Return stripped stdout for one optional discovery command."""

    try:
        result = subprocess.run(
            arguments,
            text=True,
            capture_output=True,
            check=False,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def discovery_directories(explicit: list[Path]) -> list[Path]:
    """Return caller, LLVM and package-manager bin directories in priority order."""

    directories = [path.expanduser().resolve() for path in explicit]
    llvm_prefix = os.environ.get("LLVM_PREFIX")
    if llvm_prefix:
        directories.append(Path(llvm_prefix).expanduser().resolve() / "bin")
    llvm_config = shutil.which("llvm-config")
    if llvm_config:
        bindir = command_output([llvm_config, "--bindir"])
        if bindir:
            directories.append(Path(bindir).resolve())
    if platform.system() == "Darwin":
        brew = shutil.which("brew")
        if brew:
            formulae = command_output([brew, "list", "--formula"]).splitlines()
            llvm_formulae = sorted(
                (name for name in formulae if name == "llvm" or name.startswith("llvm@")),
                reverse=True,
            )
            for formula in llvm_formulae:
                prefix = command_output([brew, "--prefix", formula])
                if prefix:
                    directories.append(Path(prefix).resolve() / "bin")
    result: list[Path] = []
    seen: set[Path] = set()
    for directory in directories:
        if directory not in seen:
            seen.add(directory)
            result.append(directory)
    return result


def candidate_names(tool: str) -> list[str]:
    """Include common version-suffixed distro executables after the base name."""

    return [tool, *(f"{tool}-{version}" for version in range(22, 13, -1))]


def candidates(tool: str, search_dirs: list[Path]) -> list[Path]:
    """Return existing executable candidates with stable de-duplication."""

    result: list[Path] = []
    seen: set[Path] = set()
    for directory in discovery_directories(search_dirs):
        for name in candidate_names(tool):
            path = directory / name
            if path.is_file() and os.access(path, os.X_OK):
                executable = path.absolute()
                if executable not in seen:
                    seen.add(executable)
                    result.append(executable)
    for name in candidate_names(tool):
        resolved_name = shutil.which(name)
        if resolved_name:
            executable = Path(resolved_name).absolute()
            if executable not in seen:
                seen.add(executable)
                result.append(executable)
    return result


def supports_target(compiler: Path, architecture: str) -> bool:
    """Probe target-code generation without producing a persistent file."""

    try:
        result = subprocess.run(
            [
                str(compiler),
                f"--target={architecture}-none-elf",
                "-x",
                "c",
                "-c",
                "-",
                "-o",
                os.devnull,
            ],
            input="int ribon_toolchain_probe;\n",
            text=True,
            capture_output=True,
            check=False,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


def main() -> int:
    """Print the first suitable executable or fail only when required."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", choices=TOOLS, required=True)
    parser.add_argument("--search-dir", action="append", type=Path, default=[])
    parser.add_argument("--require-target")
    parser.add_argument("--required", action="store_true")
    args = parser.parse_args()
    for candidate in candidates(args.tool, args.search_dir):
        if args.require_target and not supports_target(candidate, args.require_target):
            continue
        print(candidate)
        return 0
    if args.required:
        target = f" target={args.require_target}" if args.require_target else ""
        print(
            f"RIBON-LLVM-TOOL-NOT-FOUND tool={args.tool}{target}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
