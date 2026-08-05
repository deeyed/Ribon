#!/usr/bin/env python3
"""Exercise portable firmware discovery and build-doctor failure semantics."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def run(arguments: list[str], expected: int) -> subprocess.CompletedProcess[str]:
    """Run one tool and require its exact exit status."""

    result = subprocess.run(arguments, text=True, capture_output=True, check=False)
    if result.returncode != expected:
        raise AssertionError(
            f"status {result.returncode} != {expected}: {arguments}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result


def main() -> int:
    """Validate success, missing-dependency and prefix-discovery paths."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--doctor", type=Path, required=True)
    parser.add_argument("--firmware-resolver", type=Path, required=True)
    parser.add_argument("--llvm-tool-resolver", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="ribon-build-tools-") as directory:
        prefix = Path(directory)
        opensbi = prefix / "share/qemu/opensbi-riscv64-generic-fw_dynamic.bin"
        ovmf = prefix / "share/qemu/edk2-x86_64-code.fd"
        opensbi.parent.mkdir(parents=True)
        opensbi.write_bytes(b"opensbi")
        ovmf.write_bytes(b"ovmf")
        fake_bin = prefix / "bin"
        fake_bin.mkdir()
        fake_clang = fake_bin / "clang"
        fake_clang.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        fake_clang.chmod(0o755)
        for kind, expected in (
            ("opensbi-riscv64", opensbi),
            ("uefi-x86_64", ovmf),
        ):
            result = run(
                [
                    sys.executable,
                    str(args.firmware_resolver),
                    "--kind",
                    kind,
                    "--search-root",
                    str(prefix),
                    "--required",
                ],
                0,
            )
            if Path(result.stdout.strip()).resolve() != expected.resolve():
                raise AssertionError(f"{kind}: unexpected resolved path")
        llvm = run(
            [
                sys.executable,
                str(args.llvm_tool_resolver),
                "--tool",
                "clang",
                "--search-dir",
                str(fake_bin),
                "--require-target",
                "riscv64",
                "--required",
            ],
            0,
        )
        if Path(llvm.stdout.strip()).resolve() != fake_clang.resolve():
            raise AssertionError("LLVM resolver ignored the explicit search directory")
        run(
            [
                sys.executable,
                str(args.doctor),
                "--scope",
                "test",
                "--tool",
                f"PYTHON={sys.executable}",
                "--file",
                f"FIRMWARE={ovmf}",
            ],
            0,
        )
        missing = run(
            [
                sys.executable,
                str(args.doctor),
                "--scope",
                "negative",
                "--tool",
                "MISSING=ribon-tool-that-does-not-exist",
            ],
            1,
        )
        if "RIBON-BUILD-MISSING" not in missing.stderr:
            raise AssertionError("missing dependency did not fail closed")
    print("RIBON-BUILD-TOOL-TESTS-OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError) as error:
        print(f"RIBON-BUILD-TOOL-TESTS-FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
