#!/usr/bin/env python3
"""Verify deterministic D03 fixture generation and hostile disk inspection."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def run(command: list[str], succeed: bool) -> subprocess.CompletedProcess[bytes]:
    """Run one command and require the selected success class."""

    completed = subprocess.run(
        command, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    if (completed.returncode == 0) != succeed:
        raise RuntimeError(
            f"unexpected command status {completed.returncode}: {command!r}\n"
            + completed.stdout.decode("utf-8", errors="replace")
        )
    return completed


def tree(root: Path) -> dict[str, bytes]:
    """Read a generated fixture as a relative-name to exact-byte map."""

    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def layout_offsets(disk: bytes) -> tuple[int, int, int]:
    """Derive slot A, slot B, and metadata offsets from the embedded identity."""

    identity = disk[64 * 1024 + 128:64 * 1024 + 640]
    if len(identity) != 512:
        raise RuntimeError("installed disk lacks a complete layout identity")

    def region(index: int) -> int:
        return struct.unpack_from("<Q", identity, 128 + index * 24 + 8)[0]

    return region(4), region(6), region(8)


def inspect(
    python: str,
    inspector: Path,
    disk: Path,
    manifest: Path,
    active_sha256: str,
    output: Path,
    succeed: bool,
) -> None:
    """Invoke the independent inspector for one positive or hostile medium."""

    run(
        [
            python,
            str(inspector),
            "--disk",
            str(disk),
            "--manifest",
            str(manifest),
            "--expected-active-sha256",
            active_sha256,
            "--output",
            str(output),
        ],
        succeed,
    )


def main() -> int:
    """Generate twice and require corruption rejection after the real QEMU install."""

    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--product-manifest", type=Path, required=True)
    parser.add_argument("--layout-source", type=Path, required=True)
    parser.add_argument("--inspector", type=Path, required=True)
    parser.add_argument("--installed-disk", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    args = parser.parse_args()
    python = sys.executable

    try:
        provenance = json.loads(args.provenance.read_text(encoding="utf-8"))
        active_sha256 = provenance["active_slot_sha256"]
        if not isinstance(active_sha256, str) or len(active_sha256) != 64:
            raise RuntimeError("fixture provenance active-slot digest is malformed")
        with tempfile.TemporaryDirectory(prefix="ribon-d03-fixture-") as directory:
            root = Path(directory)
            first = root / "first"
            second = root / "second"
            for output in (first, second):
                run(
                    [
                        python,
                        str(args.generator),
                        "--product-manifest",
                        str(args.product_manifest),
                        "--layout-source",
                        str(args.layout_source),
                        "--output-root",
                        str(output),
                    ],
                    True,
                )
            if tree(first) != tree(second):
                raise RuntimeError("fixture generator is not byte deterministic")

            canonical = root / "installed.raw"
            shutil.copyfile(args.installed_disk, canonical)
            inspect(
                python, args.inspector, canonical, args.manifest,
                active_sha256, root / "positive.json", True,
            )
            installed = canonical.read_bytes()
            slot_a, slot_b, metadata = layout_offsets(installed)
            mutations = {
                "short": installed[:-512],
                "gpt": installed[:512 + 16]
                    + bytes([installed[512 + 16] ^ 1])
                    + installed[512 + 17:],
                "active": installed[:slot_a]
                    + bytes([installed[slot_a] ^ 1])
                    + installed[slot_a + 1:],
                "component": installed[:slot_b]
                    + bytes([installed[slot_b] ^ 1])
                    + installed[slot_b + 1:],
                "metadata": installed[:metadata]
                    + bytes([installed[metadata] ^ 1])
                    + installed[metadata + 1:],
            }
            for name, data in mutations.items():
                hostile = root / f"hostile-{name}.raw"
                hostile.write_bytes(data)
                inspect(
                    python, args.inspector, hostile, args.manifest,
                    active_sha256, root / f"hostile-{name}.json", False,
                )
        print(
            "RIBON-D03-QEMU-UPDATE-FIXTURE-TEST-OK "
            "deterministic=1 hostile=5 inspector=independent"
        )
        return 0
    except (OSError, KeyError, RuntimeError, json.JSONDecodeError) as error:
        print(f"qemu-update-fixture-test: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
